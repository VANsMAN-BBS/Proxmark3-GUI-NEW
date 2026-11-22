#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""ISO 14443A parser sidecar.

One JSON object per line on stdin, one JSON object per line on stdout.
Diagnostics go to stderr only.

Requests
--------
{"request_id": 1, "action": "initialize"}
{"request_id": 2, "action": "parse_trace", "path": "C:/traces/a.trace"}
{"request_id": 3, "action": "parse_capture", "path": "C:/captures/a.txt"}
{"request_id": 4, "action": "parse_capture", "lines": ["3E...", "3C..."]}
{"request_id": 5, "action": "reset"}
{"request_id": 6, "action": "shutdown"}

Every response contains schema, request_id, status ("ok" / "error"),
message and data.  A new ParserState is created for each parse request, so two
consecutive traces cannot share T=CL / MIFARE / native-card state.

The vendored parser is the Stage-1 fixed source.  This bridge only projects
its text result into structured fields; it does not re-implement protocol
decoding and never invents UID / ATQA / SAK / ATS values.
"""

import argparse
import json
import os
import struct
import sys
import traceback
from collections import Counter

_HERE = os.path.dirname(os.path.abspath(__file__))
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)

from parser.iso14443_parser import ISO14443AParser          # noqa: E402
from parser.parser_state import ParserState                # noqa: E402
from parser.raw_data_reconstructor import RawDataReconstructor  # noqa: E402

SCHEMA_VERSION = 1
SIDECAR_VERSION = "2.0"

# Hard limits protect the GUI process from a corrupt file or a runaway line.
LIMITS = {
    "max_line_bytes": 1024 * 1024,        # one JSON request line
    "max_input_bytes": 32 * 1024 * 1024,  # trace / capture file
    "max_frames": 20000,                  # frames per parse request
    "max_frame_bytes": 4096,              # raw payload of one frame
    "max_text_line_chars": 16384,         # one capture line
    "max_path_chars": 4096,
}

PM3_CARRIER_HZ = 13560000.0


def log(message):
    """Write a diagnostic line to stderr (never stdout)."""
    try:
        sys.stderr.write("[iso14443_sidecar] %s\n" % message)
        sys.stderr.flush()
    except Exception:
        pass


def _hex(data):
    return bytes(data).hex().upper()


def _fmt_hex(value, width):
    return None if value is None else ("0x%0*X" % (width, value))


# ---------------------------------------------------------------------------
# Input readers
# ---------------------------------------------------------------------------

def read_trace(path, limits):
    """Read a PM3 .trace file.

    Layout (doc/trace_notes.md):
      32-bit timestamp LE, 16-bit duration LE,
      15-bit data_len + 1-bit isResponse LE, data, parity bits (MSB first).
    """
    size = os.path.getsize(path)
    if size > limits["max_input_bytes"]:
        raise ValueError("input_too_large: %d bytes (limit %d)"
                         % (size, limits["max_input_bytes"]))

    with open(path, "rb") as handle:
        raw = handle.read(limits["max_input_bytes"] + 1)
    if len(raw) > limits["max_input_bytes"]:
        raise ValueError("input_too_large: limit %d bytes" % limits["max_input_bytes"])

    frames = []
    errors = []
    offset = 0
    total = len(raw)
    truncated = False

    while offset + 8 <= total:
        ts, duration, word = struct.unpack_from("<IHH", raw, offset)
        offset += 8
        data_len = word & 0x7FFF
        is_response = bool(word & 0x8000)

        if data_len == 0:
            # Field/end marker: header consumed, no data follows.
            continue
        if data_len > limits["max_frame_bytes"]:
            errors.append({
                "index": len(frames) + 1,
                "code": "frame_too_large",
                "message": "trace frame length %d exceeds limit %d"
                           % (data_len, limits["max_frame_bytes"]),
            })
            truncated = True
            break
        if offset + data_len > total:
            errors.append({
                "index": len(frames) + 1,
                "code": "truncated_frame",
                "message": "trace ended inside frame data at offset %d" % offset,
            })
            truncated = True
            break

        data = raw[offset:offset + data_len]
        offset += data_len
        parity_len = (data_len - 1) // 8 + 1
        if offset + parity_len > total:
            errors.append({
                "index": len(frames) + 1,
                "code": "truncated_parity",
                "message": "trace ended inside parity bits at offset %d" % offset,
            })
            truncated = True
            break

        parity_raw = raw[offset:offset + parity_len]
        offset += parity_len
        parity = []
        for i in range(data_len):
            byte_index = i // 8
            bit_index = 7 - (i % 8)
            parity.append((parity_raw[byte_index] >> bit_index) & 1)

        frames.append({
            "timestamp": ts & 0xFFFFFFFF,
            "duration": duration,
            "is_response": is_response,
            "data": data,
            "parity": parity,
        })
        if len(frames) >= limits["max_frames"]:
            errors.append({
                "index": len(frames),
                "code": "frame_limit",
                "message": "stopped at max_frames=%d" % limits["max_frames"],
            })
            truncated = True
            break

    if offset < total and not truncated:
        errors.append({
            "index": len(frames) + 1,
            "code": "trailing_bytes",
            "message": "%d trailing byte(s) ignored" % (total - offset),
        })

    return frames, errors


def data_parity_to_wire(data, parity):
    """data + odd-parity bits -> (raw sniffer payload, valid_bits)."""
    bits = []
    for i, value in enumerate(data):
        for k in range(8):
            bits.append((value >> k) & 1)
        bits.append(parity[i] & 1)

    packed = bytearray()
    for i in range(0, len(bits), 8):
        chunk = bits[i:i + 8]
        value = 0
        for j, bit in enumerate(chunk):
            value |= (bit & 1) << (7 - j)
        packed.append(value)
    valid = len(bits) % 8
    if valid == 0 and bits:
        valid = 8
    if valid != 8 and packed:
        # The sniffer stores the last partial byte right-aligned; the
        # reconstructor left-shifts it back before MSB-first expansion.
        packed[-1] >>= (8 - valid)
    return bytes(packed), valid


def trace_frame_to_packet(frame, frame_number):
    payload, valid_bits = data_parity_to_wire(frame["data"], frame["parity"])
    if len(payload) > LIMITS["max_frame_bytes"]:
        raise ValueError("frame_too_large: frame %d payload %d bytes"
                         % (frame_number, len(payload)))
    direction = ord("<") if frame["is_response"] else ord(">")
    ts24 = frame["timestamp"] & 0x00FFFFFF
    return (bytes([direction]) + struct.pack(">I", ts24)
            + struct.pack(">H", len(payload)) + bytes([valid_bits]) + payload)


def _clean_hex_line(line):
    text = line.strip()
    if not text or text.startswith("#") or text.startswith("//"):
        return None
    if "#" in text:
        text = text.split("#", 1)[0].strip()
    if not text:
        return None
    if text[0] in "<>":
        # Some notes prefix the direction for readability. The packet itself
        # still carries the direction byte, so drop the decoration.
        text = text[1:].strip()
    text = text.replace(" ", "").replace("\t", "")
    if text.lower().startswith("0x"):
        text = text[2:]
    return text


def parse_capture_lines(lines, limits):
    """Parse text capture lines into packet bytes + per-line errors."""
    packets = []
    timestamps = []
    errors = []

    for index, line in enumerate(lines, 1):
        if len(line) > limits["max_text_line_chars"]:
            errors.append({
                "index": index,
                "code": "line_too_long",
                "message": "line %d is %d chars (limit %d)"
                           % (index, len(line), limits["max_text_line_chars"]),
            })
            continue
        text = _clean_hex_line(line)
        if text is None:
            continue
        if len(text) % 2:
            errors.append({
                "index": index,
                "code": "invalid_hex",
                "message": "line %d has an odd number of hex digits" % index,
            })
            continue
        try:
            packet = bytes.fromhex(text)
        except ValueError:
            errors.append({
                "index": index,
                "code": "invalid_hex",
                "message": "line %d is not valid hexadecimal" % index,
            })
            continue

        if len(packet) < 8:
            errors.append({
                "index": index,
                "code": "packet_too_short",
                "message": "line %d: %d bytes, need at least 8" % (index, len(packet)),
            })
            continue
        data_len = struct.unpack(">H", packet[5:7])[0]
        if data_len > limits["max_frame_bytes"]:
            errors.append({
                "index": index,
                "code": "frame_too_large",
                "message": "line %d: payload %d exceeds limit %d"
                           % (index, data_len, limits["max_frame_bytes"]),
            })
            continue
        expected = 8 + data_len
        if len(packet) < expected:
            errors.append({
                "index": index,
                "code": "truncated_packet",
                "message": "line %d: %d bytes, declared length needs %d"
                           % (index, len(packet), expected),
            })
            continue
        if len(packet) > expected:
            errors.append({
                "index": index,
                "code": "trailing_bytes",
                "message": "line %d: %d trailing byte(s) ignored"
                           % (index, len(packet) - expected),
            })
            packet = packet[:expected]

        packets.append(packet)
        timestamps.append(struct.unpack(">I", packet[1:5])[0] & 0x00FFFFFF)
        if len(packets) >= limits["max_frames"]:
            errors.append({
                "index": index,
                "code": "frame_limit",
                "message": "stopped at max_frames=%d" % limits["max_frames"],
            })
            break

    return packets, timestamps, errors


# ---------------------------------------------------------------------------
# Result projection
# ---------------------------------------------------------------------------

_TYPE_RULES = (
    ("REQA (7-bit", "reqa"),
    ("WUPA (7-bit", "wupa"),
    ("7-bit 命令", "command_7bit"),
    ("HLTA", "hlta"),
    ("RATS", "rats"),
    ("ATS (Answer To Select", "ats"),
    ("PPS", "pps"),
    ("I-block", "i_block"),
    ("R-block", "r_block"),
    ("S-block", "s_block"),
    ("ATQA", "atqa"),
    ("SAK (", "sak"),
    ("SELECT (SEL=", "select"),
    ("ANTICOLLISION", "anticollision"),
    ("防冲突响应", "anticollision"),
    ("Mifare Auth A", "mifare_auth_a"),
    ("Mifare Auth B", "mifare_auth_b"),
    ("Mifare Nonce", "mifare_nonce"),
    ("加密响应", "mifare_auth_cipher"),
    ("Mifare 认证挑战", "mifare_auth_challenge"),
    ("Mifare ACK", "mifare_ack"),
    ("Mifare NAK", "mifare_nak"),
    ("Mifare Read", "mifare_read"),
    ("Mifare READ", "mifare_read"),
    ("Mifare", "mifare_data"),
    ("NTAG/Ultralight GET_VERSION", "ntag_get_version"),
    ("NTAG/Ultralight READ", "ntag_read"),
    ("NTAG/Ultralight WRITE", "ntag_write"),
    ("NTAG/Ultralight PWD_AUTH", "ntag_pwd_auth"),
    ("NTAG", "ntag"),
    ("DESFire", "desfire"),
    ("FM1208", "fm1208"),
    ("APDU", "apdu"),
)


def frame_type(commands, extra):
    # Command lists are specific; extra text also contains cross-references
    # such as "卡片类型 (ATQA+SAK)" that must not override a SAK frame.
    for source in (commands, extra):
        joined = "\n".join(source)
        for needle, code in _TYPE_RULES:
            if needle in joined:
                return code
    if commands:
        return "other"
    return "unknown"


def frame_direction(direction_byte):
    if direction_byte == ord(">"):
        return "pcd_to_picc"
    if direction_byte == ord("<"):
        return "picc_to_pcd"
    return "unknown"


def phase_name(state):
    if state._tcl_active:
        return "14443-4"
    if state._mifare_active:
        return "mifare"
    if state._native_active:
        return "native"
    return "14443-3"


def _is_warning_line(line):
    markers = ("警告", "失败", "不成立", "超出", "截断", "失败(")
    return any(marker in line for marker in markers)


def project_frame(result, packet, seq, source_timestamp, duration,
                  previous_timestamp, timestamp_basis, state):
    """Turn one Stage-1 result dict into a structured frame."""
    commands = list(result.get("commands") or [])
    extra = list(result.get("extra_info") or [])

    parity_ok = result.get("reconstruct_parity_ok")
    parity_bits = result.get("reconstruct_parity_bits") or []
    parity_checked = bool(parity_bits) and any(bit is not None for bit in parity_bits)

    crc_ok = result.get("reconstruct_crc_ok")
    crc_checked = crc_ok is not None

    raw_hex = result.get("raw_payload_hex") or ""
    data_hex = result.get("payload_hex") or ""
    raw_bytes = bytes.fromhex(raw_hex) if raw_hex else b""
    data_bytes = bytes.fromhex(data_hex) if data_hex else b""
    valid_bits = int(result.get("valid_bits") or 0)
    bit_count = 0
    if raw_bytes:
        bit_count = (len(raw_bytes) - 1) * 8 + (valid_bits if valid_bits else 8)

    delta = None
    delta_us = None
    if previous_timestamp is not None and source_timestamp is not None:
        if timestamp_basis == "pm3_trace":
            delta = (source_timestamp - previous_timestamp) & 0xFFFFFFFF
            delta_us = int(round(delta * 1000000.0 / PM3_CARRIER_HZ))
        else:
            delta = (source_timestamp - previous_timestamp) & 0x00FFFFFF

    mifare_kind = result.get("reconstruct_mifare_kind")
    ciphertext = (mifare_kind == "mifare_ciphertext")
    ciphertext_reason = result.get("reconstruct_mifare_reason") if ciphertext else None
    if not ciphertext:
        joined = "\n".join(commands + extra)
        if "Crypto1 密文" in joined or "块号: 未知" in joined:
            ciphertext = True
            ciphertext_reason = ("MIFARE AUTH 会话中的命令/块号由 Crypto1 加密"
                                 " (parser 已标记为未知/密文)")

    warnings = []
    if crc_checked and crc_ok is False:
        warnings.append("CRC16-A 校验失败")
    if parity_checked and parity_ok is False:
        warnings.append("校验位错误")
    for line in extra:
        if _is_warning_line(line) and line not in warnings:
            warnings.append(line)

    decode = {
        "commands": commands,
        "extra": extra,
        "pwd_auth_hex": result.get("native_pwd_hex"),
        "pwd_auth_value": result.get("native_pwd_value"),
        "ats": result.get("ats"),
        "card_type": result.get("card_type"),
    }

    error = None
    if result.get("error"):
        error = {
            "code": result.get("error_type") or "parse_error",
            "message": result.get("error") or "parse error",
        }
    elif result.get("error_type"):
        error = {
            "code": result.get("error_type"),
            "message": result.get("error_detail") or result.get("error_type"),
        }

    return {
        "seq": seq,
        "timestamp": source_timestamp,
        "timestamp_hex": _fmt_hex(source_timestamp, 8 if timestamp_basis == "pm3_trace" else 6),
        "delta": delta,
        "delta_us": delta_us,
        "duration": duration,
        "direction": frame_direction(packet[0] if packet else 0),
        "phase": phase_name(state),
        "valid_bits": valid_bits,
        "bit_count": bit_count,
        "raw": raw_hex,
        "data": data_hex,
        "data_len": len(data_bytes),
        "raw_len": len(raw_bytes),
        "type": frame_type(commands, extra),
        "crc": {"checked": crc_checked, "ok": crc_ok if crc_checked else None},
        "parity": {
            "checked": parity_checked,
            "ok": parity_ok if parity_checked else None,
            "bits": parity_bits,
        },
        "ciphertext": bool(ciphertext),
        "ciphertext_reason": ciphertext_reason,
        "decode": decode,
        "warnings": warnings,
        "error": error,
        "summary": commands[0] if commands else "",
    }


def build_card_info(state, frames, tracked=None):
    tracked = tracked or {}
    uid = tracked.get("uid")
    atqa = tracked.get("atqa", state._last_atqa)
    sak = tracked.get("sak", state._last_sak)
    card_type = tracked.get("card_type")
    if card_type is None and (atqa is not None or sak is not None):
        # identify_card_type is the Stage-1 parser's own lookup, not a guess
        # made by this bridge.
        card_type, _hint = ISO14443AParser.identify_card_type(atqa=atqa, sak=sak)

    ats = tracked.get("ats", state._last_ats)
    uid_hex = _hex(uid) if uid else None
    return {
        "uid": uid_hex,
        "uid_len": len(uid) if uid else None,
        "uid_complete": bool(uid),
        "atqa": atqa,
        "atqa_hex": ("%04X" % atqa) if atqa is not None else None,
        "sak": sak,
        "sak_hex": ("%02X" % sak) if sak is not None else None,
        "card_type": card_type,
        "ats": dict(ats) if ats else None,
        "phase": frames[-1]["phase"] if frames else phase_name(state),
        "protocol": {
            "tcl": bool(state._tcl_active),
            "mifare": bool(state._mifare_active),
            "native": bool(state._native_active),
            "waiting_for_ats": bool(state._waiting_for_ats),
        },
    }


def build_validation(frames, read_errors):
    crc_checked = crc_ok = crc_bad = 0
    crc_bad_cipher = 0
    parity_checked = parity_ok = parity_bad = 0
    parity_bad_cipher = 0
    parse_errors = 0
    ciphertext = 0
    unknown = 0
    warnings = 0

    for frame in frames:
        if frame["crc"]["checked"]:
            crc_checked += 1
            if frame["crc"]["ok"]:
                crc_ok += 1
            else:
                crc_bad += 1
                if frame["ciphertext"]:
                    crc_bad_cipher += 1
        if frame["parity"]["checked"]:
            parity_checked += 1
            if frame["parity"]["ok"]:
                parity_ok += 1
            else:
                parity_bad += 1
                if frame["ciphertext"]:
                    parity_bad_cipher += 1
        if frame["error"]:
            parse_errors += 1
        if frame["ciphertext"]:
            ciphertext += 1
        if frame["type"] in ("unknown", "other"):
            unknown += 1
        warnings += len(frame["warnings"])

    return {
        "frames_total": len(frames),
        "parse_errors": parse_errors,
        "read_errors": len(read_errors),
        "crc_checked": crc_checked,
        "crc_ok": crc_ok,
        "crc_failed": crc_bad,
        "crc_failed_plaintext": crc_bad - crc_bad_cipher,
        "crc_failed_ciphertext": crc_bad_cipher,
        "parity_checked": parity_checked,
        "parity_ok": parity_ok,
        "parity_failed": parity_bad,
        "parity_failed_plaintext": parity_bad - parity_bad_cipher,
        "parity_failed_ciphertext": parity_bad_cipher,
        "ciphertext_frames": ciphertext,
        "unclassified_frames": unknown,
        "warnings": warnings,
    }


def build_events(frames, read_errors):
    events = []

    for error in read_errors:
        events.append({
            "seq": error.get("index"),
            "code": error.get("code"),
            "severity": "error",
            "message": error.get("message"),
        })

    interesting = {
        "ats": ("ats", "info"),
        "rats": ("rats", "info"),
        "mifare_auth_a": ("mifare_auth", "info"),
        "mifare_auth_b": ("mifare_auth", "info"),
        "sak": ("sak", "info"),
    }
    for frame in frames:
        if frame["error"]:
            events.append({
                "seq": frame["seq"],
                "code": frame["error"]["code"],
                "severity": "error",
                "message": frame["error"]["message"],
            })
        if frame["ciphertext"]:
            events.append({
                "seq": frame["seq"],
                "code": "mifare_ciphertext",
                "severity": "warning",
                "message": frame["ciphertext_reason"] or "Crypto1 ciphertext",
            })
        for warning in frame["warnings"]:
            events.append({
                "seq": frame["seq"],
                "code": "frame_warning",
                "severity": "warning",
                "message": warning,
            })
        if frame["type"] in interesting:
            code, severity = interesting[frame["type"]]
            events.append({
                "seq": frame["seq"],
                "code": code,
                "severity": severity,
                "message": frame["summary"] or frame["type"],
            })
        if len(events) >= 1000:
            events.append({
                "seq": frame["seq"],
                "code": "event_limit",
                "severity": "warning",
                "message": "event list truncated at 1000 entries",
            })
            break

    return events


def build_statistics(frames, trace_frames):
    by_direction = Counter(frame["direction"] for frame in frames)
    by_type = Counter(frame["type"] for frame in frames)
    raw_bytes = sum(frame["raw_len"] for frame in frames)
    data_bytes = sum(frame["data_len"] for frame in frames)

    duration_cycles = None
    duration_us = None
    if trace_frames and len(trace_frames) > 1:
        duration_cycles = ((trace_frames[-1]["timestamp"]
                            - trace_frames[0]["timestamp"]) & 0xFFFFFFFF)
        duration_us = int(round(duration_cycles * 1000000.0 / PM3_CARRIER_HZ))

    return {
        "frames": len(frames),
        "raw_bytes": raw_bytes,
        "data_bytes": data_bytes,
        "by_direction": dict(by_direction),
        "by_type": dict(by_type),
        "duration_cycles": duration_cycles,
        "duration_us": duration_us,
    }


# ---------------------------------------------------------------------------
# Parse request handlers
# ---------------------------------------------------------------------------

def _track_card(tracked, frame, state):
    """Remember the most recent card identity across REQA/HLTA resets."""
    if frame["type"] in ("reqa", "wupa"):
        tracked.update({"uid": None, "atqa": None, "sak": None,
                        "card_type": None, "ats": None})
    if state._uid_full:
        tracked["uid"] = bytes(state._uid_full)
    if state._last_atqa is not None:
        tracked["atqa"] = state._last_atqa
    if state._last_sak is not None:
        tracked["sak"] = state._last_sak
    decode = frame.get("decode") or {}
    if frame["type"] == "sak" and decode.get("card_type"):
        tracked["card_type"] = decode["card_type"]
    if decode.get("ats"):
        tracked["ats"] = decode["ats"]


def _parse_packet_list(packets, source_timestamps, durations, timestamp_basis,
                       read_errors, source):
    """Run one independent ParserState over packets already converted."""
    state = ParserState()
    frames = []
    previous_timestamp = None
    tracked = {"uid": None, "atqa": None, "sak": None,
               "card_type": None, "ats": None}

    with ISO14443AParser.parser_state(state):
        for index, packet in enumerate(packets):
            seq = index + 1
            source_ts = source_timestamps[index] if index < len(source_timestamps) else None
            duration = durations[index] if durations and index < len(durations) else None
            try:
                result = ISO14443AParser.parse_packet(packet)
                frame = project_frame(result, packet, seq, source_ts, duration,
                                      previous_timestamp, timestamp_basis, state)
            except Exception as exc:  # never let one frame kill the request
                log("frame %d projection failed: %r" % (seq, exc))
                frame = {
                    "seq": seq,
                    "timestamp": source_ts,
                    "timestamp_hex": None,
                    "delta": None,
                    "delta_us": None,
                    "duration": duration,
                    "direction": frame_direction(packet[0] if packet else 0),
                    "phase": phase_name(state),
                    "valid_bits": 0,
                    "bit_count": 0,
                    "raw": _hex(packet[8:]) if len(packet) > 8 else "",
                    "data": "",
                    "data_len": 0,
                    "raw_len": max(0, len(packet) - 8),
                    "type": "unknown",
                    "crc": {"checked": False, "ok": None},
                    "parity": {"checked": False, "ok": None, "bits": []},
                    "ciphertext": False,
                    "ciphertext_reason": None,
                    "decode": {"commands": [], "extra": [str(exc)],
                               "pwd_auth_hex": None, "pwd_auth_value": None,
                               "ats": None, "card_type": None},
                    "warnings": [],
                    "error": {"code": type(exc).__name__, "message": str(exc)},
                    "summary": "",
                }
            frames.append(frame)
            _track_card(tracked, frame, state)
            if source_ts is not None:
                previous_timestamp = source_ts

        card_info = build_card_info(state, frames, tracked)
        state_snapshot = state.snapshot()

    return {
        "source": source,
        "card_info": card_info,
        "state": state_snapshot,
        "frames": frames,
        "events": build_events(frames, read_errors),
        "validation": build_validation(frames, read_errors),
        "errors": list(read_errors),
        "statistics": build_statistics(frames, None),
    }


def handle_parse_trace(request, limits):
    path = request.get("path")
    if not isinstance(path, str) or not path:
        raise ValueError("missing_path: parse_trace requires a string path")
    if len(path) > limits["max_path_chars"]:
        raise ValueError("path_too_long")
    if not os.path.isfile(path):
        raise ValueError("file_not_found: %s" % path)

    trace_frames, read_errors = read_trace(path, limits)
    packets = []
    timestamps = []
    durations = []
    for index, frame in enumerate(trace_frames, 1):
        try:
            packets.append(trace_frame_to_packet(frame, index))
            timestamps.append(frame["timestamp"])
            durations.append(frame["duration"])
        except ValueError as exc:
            read_errors.append({
                "index": index,
                "code": "frame_too_large",
                "message": str(exc),
            })

    source = {
        "kind": "pm3_trace",
        "path": os.path.abspath(path),
        "bytes": os.path.getsize(path),
        "trace_frames": len(trace_frames),
    }
    data = _parse_packet_list(packets, timestamps, durations, "pm3_trace",
                              read_errors, source)
    data["statistics"] = build_statistics(data["frames"], trace_frames)
    return data


def handle_parse_capture(request, limits):
    path = request.get("path")
    lines = request.get("lines")

    if isinstance(path, str) and path:
        if len(path) > limits["max_path_chars"]:
            raise ValueError("path_too_long")
        if not os.path.isfile(path):
            raise ValueError("file_not_found: %s" % path)
        size = os.path.getsize(path)
        if size > limits["max_input_bytes"]:
            raise ValueError("input_too_large: %d bytes (limit %d)"
                             % (size, limits["max_input_bytes"]))
        with open(path, "r", encoding="utf-8-sig", errors="replace") as handle:
            lines = handle.readlines()
        source = {
            "kind": "capture_text",
            "path": os.path.abspath(path),
            "bytes": size,
        }
    elif isinstance(lines, list):
        if len(lines) > limits["max_frames"] * 4:
            raise ValueError("too_many_lines: %d" % len(lines))
        source = {"kind": "capture_text", "path": None, "bytes": None}
    elif isinstance(request.get("text"), str):
        lines = request["text"].splitlines()
        source = {"kind": "capture_text", "path": None, "bytes": len(request["text"])}
    else:
        raise ValueError("missing_input: parse_capture requires path, lines or text")

    packets, timestamps, read_errors = parse_capture_lines(lines, limits)
    source["capture_lines"] = len(lines)
    return _parse_packet_list(packets, timestamps, [], "capture", read_errors, source)


# ---------------------------------------------------------------------------
# JSON-lines loop
# ---------------------------------------------------------------------------

def response_ok(request_id, data, message=""):
    return {
        "schema": SCHEMA_VERSION,
        "request_id": request_id,
        "status": "ok",
        "message": message,
        "data": data,
    }


def response_error(request_id, code, message, detail=None):
    data = {"code": code, "detail": detail}
    return {
        "schema": SCHEMA_VERSION,
        "request_id": request_id,
        "status": "error",
        "message": message,
        "data": data,
    }


def write_response(payload):
    text = json.dumps(payload, ensure_ascii=False, separators=(",", ":"))
    sys.stdout.buffer.write(text.encode("utf-8") + b"\n")
    sys.stdout.buffer.flush()


def initialize_data(limits):
    return {
        "sidecar_version": SIDECAR_VERSION,
        "schema": SCHEMA_VERSION,
        "python": sys.version.split()[0],
        "parser": "ISO14443AParser (Stage 1 fixed)",
        "state_isolation": "ParserState per parse request",
        "capabilities": [
            "initialize", "parse_trace", "parse_capture",
            "reset", "shutdown",
        ],
        "limits": dict(limits),
    }


def handle_request(request, limits):
    """Return (response_payload, should_exit)."""
    request_id = request.get("request_id")
    action = request.get("action")
    if not isinstance(action, str) or not action:
        return response_error(request_id, "missing_action",
                              "request has no action"), False

    if action == "initialize":
        return response_ok(request_id, initialize_data(limits),
                           "ISO 14443A parser ready"), False

    if action == "reset":
        ISO14443AParser.use_state(ParserState())
        return response_ok(request_id, {"reset": True, "state": "clean"},
                           "parser state cleared"), False

    if action == "parse_trace":
        try:
            data = handle_parse_trace(request, limits)
        except ValueError as exc:
            return response_error(request_id, str(exc).split(":", 1)[0],
                                  str(exc)), False
        except Exception as exc:
            log("parse_trace failed: %r\n%s" % (exc, traceback.format_exc(limit=8)))
            return response_error(request_id, "internal_error", str(exc),
                                  traceback.format_exc(limit=8)), False
        return response_ok(request_id, data,
                           "parsed %d frame(s)" % len(data["frames"])), False

    if action == "parse_capture":
        try:
            data = handle_parse_capture(request, limits)
        except ValueError as exc:
            return response_error(request_id, str(exc).split(":", 1)[0],
                                  str(exc)), False
        except Exception as exc:
            log("parse_capture failed: %r\n%s" % (exc, traceback.format_exc(limit=8)))
            return response_error(request_id, "internal_error", str(exc),
                                  traceback.format_exc(limit=8)), False
        return response_ok(request_id, data,
                           "parsed %d frame(s)" % len(data["frames"])), False

    if action == "shutdown":
        return response_ok(request_id, {"shutdown": True}, "shutting down"), True

    return response_error(request_id, "unknown_action",
                          "unsupported action: %s" % action), False


def main(argv=None):
    parser = argparse.ArgumentParser(description="ISO 14443A JSON-lines sidecar")
    parser.add_argument("--version", action="store_true",
                        help="print the sidecar version and exit")
    args = parser.parse_args(argv)
    if args.version:
        sys.stdout.write(SIDECAR_VERSION + "\n")
        return 0

    limits = dict(LIMITS)
    stream = sys.stdin.buffer
    log("started (pid %d, python %s)" % (os.getpid(), sys.version.split()[0]))

    while True:
        chunk = stream.readline(limits["max_line_bytes"] + 1)
        if not chunk:
            break

        too_long = len(chunk) > limits["max_line_bytes"]
        if too_long and not chunk.endswith(b"\n"):
            # Drain the remainder so the next read starts at a line boundary.
            while True:
                more = stream.readline(limits["max_line_bytes"] + 1)
                if not more or more.endswith(b"\n"):
                    break
        if too_long:
            write_response(response_error(
                None, "line_too_long",
                "request line exceeds %d bytes" % limits["max_line_bytes"]))
            log("rejected oversized request line")
            continue

        try:
            text = chunk.decode("utf-8").strip()
        except UnicodeDecodeError as exc:
            write_response(response_error(None, "invalid_utf8", str(exc)))
            continue
        if not text:
            continue

        try:
            request = json.loads(text)
        except Exception as exc:
            write_response(response_error(None, "invalid_json",
                                          "invalid JSON: %s" % exc))
            log("invalid JSON request: %s" % exc)
            continue

        if not isinstance(request, dict):
            write_response(response_error(None, "invalid_request",
                                          "request must be a JSON object"))
            continue

        try:
            response, should_exit = handle_request(request, limits)
        except Exception as exc:
            log("unhandled request error: %r\n%s"
                % (exc, traceback.format_exc(limit=8)))
            response = response_error(request.get("request_id"), "internal_error",
                                      str(exc), traceback.format_exc(limit=8))
            should_exit = False

        write_response(response)
        if should_exit:
            log("shutdown requested")
            break

    log("exiting")
    return 0


if __name__ == "__main__":
    sys.exit(main())
