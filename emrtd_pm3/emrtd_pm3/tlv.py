"""Minimal BER-TLV helpers used by eMRTD data groups.

Only single-byte tags (plus the multi-byte tag form, which a few DGs use) and
definite lengths are supported.  That is everything ICAO 9303 LDS needs.
"""

from __future__ import annotations


def parse(data: bytes, pos: int = 0) -> tuple[int, bytes, int]:
    """Parse one TLV and return ``(tag, value, next_position)``."""
    if pos >= len(data):
        raise ValueError("truncated TLV tag")
    tag = data[pos]
    pos += 1

    if tag & 0x1F == 0x1F:  # multi-byte tag
        while pos < len(data) and data[pos] & 0x80:
            tag = (tag << 8) | data[pos]
            pos += 1
        if pos >= len(data):
            raise ValueError("truncated multi-byte tag")
        tag = (tag << 8) | data[pos]
        pos += 1

    if pos >= len(data):
        raise ValueError("truncated TLV length")
    length = data[pos]
    pos += 1

    if length & 0x80:
        count = length & 0x7F
        if count == 0 or pos + count > len(data):
            raise ValueError("unsupported or truncated BER length")
        length = int.from_bytes(data[pos:pos + count], "big")
        pos += count

    if pos + length > len(data):
        raise ValueError("TLV value exceeds buffer")

    return tag, data[pos:pos + length], pos + length


def iter_tlv(data: bytes):
    """Yield ``(tag, value)`` pairs until the buffer is exhausted."""
    pos = 0
    while pos < len(data):
        tag, value, pos = parse(data, pos)
        yield tag, value


def encode_length(length: int) -> bytes:
    if length < 0:
        raise ValueError("negative length")
    if length < 0x80:
        return bytes([length])
    if length < 0x100:
        return b"\x81" + bytes([length])
    if length < 0x10000:
        return b"\x82" + length.to_bytes(2, "big")
    return b"\x83" + length.to_bytes(3, "big")


def encode_tag(tag: int) -> bytes:
    if tag <= 0xFF:
        if tag & 0x1F == 0x1F:
            raise ValueError("tag 0x%02X is a multi-byte tag prefix" % tag)
        return bytes([tag])
    return tag.to_bytes((tag.bit_length() + 7) // 8, "big")


def encode(tag: int, value: bytes) -> bytes:
    return encode_tag(tag) + encode_length(len(value)) + value


def find_first(data: bytes, wanted: int, max_depth: int = 8):
    """Depth-first search for a tag; returns its value or ``None``.

    Non-TLV payloads (images, MRZ text) are skipped instead of raising, which
    matters when walking DG2.
    """
    if max_depth <= 0:
        return None
    try:
        items = list(iter_tlv(data))
    except ValueError:
        return None
    for tag, value in items:
        if tag == wanted:
            return value
        nested = find_first(value, wanted, max_depth - 1)
        if nested is not None:
            return nested
    return None
