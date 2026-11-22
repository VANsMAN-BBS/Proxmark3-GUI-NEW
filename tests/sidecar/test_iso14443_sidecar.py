# -*- coding: utf-8 -*-
"""Protocol-level tests for the ISO 14443A JSON-lines sidecar.

All captures are synthetic: the UID is 11 22 33 44 and no real card data is
stored in the repository.  The suite covers initialize, trace/capture parsing,
state isolation, reset, CJK paths, malformed input, size limits and shutdown.
"""

import json
import os
import queue
import struct
import subprocess
import sys
import tempfile
import threading
import time
import unittest


HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.dirname(os.path.dirname(HERE))
SIDECAR = os.path.join(PROJECT, 'src', 'module', 'iso14443a', 'python',
                       'iso14443_sidecar.py')

UID = bytes.fromhex('11223344')
BCC = 0
for _b in UID:
    BCC ^= _b
SAK_M1 = 0x08
SAK_TCL = 0x20
ATS = bytes.fromhex('097877C1020E513101')


def crc16_a(data):
    crc = 0x6363
    for b in data:
        bt = b & 0xFF
        bt ^= crc & 0xFF
        bt ^= (bt << 4) & 0xFF
        crc = ((crc >> 8) ^ ((bt << 8) & 0xFFFF)
               ^ ((bt << 3) & 0xFFFF) ^ (bt >> 4))
        crc &= 0xFFFF
    return ((crc & 0x00FF) << 8) | ((crc >> 8) & 0x00FF)


def append_crc(data):
    value = crc16_a(data)
    return data + bytes([(value >> 8) & 0xFF, value & 0xFF])


def odd_parity(value):
    return 0 if bin(value & 0xFF).count('1') % 2 == 1 else 1


def bits_to_wire(bits):
    packed = bytearray()
    for i in range(0, len(bits), 8):
        chunk = bits[i:i + 8]
        value = 0
        for j, bit in enumerate(chunk):
            value |= (bit & 1) << (7 - j)
        packed.append(value)
    valid = len(bits) % 8 or 8
    if valid != 8 and packed:
        packed[-1] >>= (8 - valid)
    return bytes(packed), valid


def data_to_bits(data):
    bits = []
    for value in data:
        for k in range(8):
            bits.append((value >> k) & 1)
        bits.append(odd_parity(value))
    return bits


def build_packet(direction, timestamp, data, crc=True, partial=None):
    wire = append_crc(data) if crc else data
    if partial is not None:
        bits = [(wire[0] >> k) & 1 for k in range(partial)]
    else:
        bits = data_to_bits(wire)
    payload, valid = bits_to_wire(bits)
    return (bytes([ord(direction)]) + struct.pack('>I', timestamp & 0xFFFFFF)
            + struct.pack('>H', len(payload)) + bytes([valid]) + payload)


def m1_packets():
    return [
        build_packet('>', 1000, b'\x26', crc=False, partial=7),
        build_packet('<', 1100, b'\x04\x00', crc=False),
        build_packet('>', 1200, b'\x93\x20', crc=False),
        build_packet('<', 1300, UID + bytes([BCC]), crc=False),
        build_packet('>', 1400, b'\x93\x70' + UID + bytes([BCC])),
        build_packet('<', 1500, bytes([SAK_M1])),
        build_packet('>', 1600, b'\x60\x00'),
        build_packet('<', 1700, bytes.fromhex('AABBCCDD')),
        build_packet('>', 1800, bytes.fromhex('01020304'), crc=False),
        build_packet('<', 1900, bytes.fromhex('05060708')),
        build_packet('>', 2000, bytes.fromhex('8F2A7C19'), crc=False),
        build_packet('<', 2100, bytes(range(16)) + b'\xDE\xAD', crc=False),
        build_packet('>', 2200, b'\x50\x00'),
    ]


def atqa_only_packets():
    return [
        build_packet('<', 5000, b'\x04\x00', crc=False),
        build_packet('<', 5100, bytes([SAK_M1])),
    ]


def tcl_packets():
    return [
        build_packet('>', 3000, b'\x26', crc=False, partial=7),
        build_packet('<', 3100, b'\x04\x00', crc=False),
        build_packet('>', 3200, b'\x93\x20', crc=False),
        build_packet('<', 3300, UID + bytes([BCC]), crc=False),
        build_packet('>', 3400, b'\x93\x70' + UID + bytes([BCC])),
        build_packet('<', 3500, bytes([SAK_TCL])),
        build_packet('>', 3600, b'\xE0\x80'),
        build_packet('<', 3700, ATS),
        build_packet('>', 3800, bytes.fromhex('0200B0000008')),
    ]


def write_capture(path, packets):
    with open(path, 'w', encoding='utf-8') as handle:
        handle.write('\n'.join(packet.hex().upper() for packet in packets))
        handle.write('\n')


def write_trace(path, packets):
    """Write a PM3 trace from packets (direction/data, recomputed parity)."""
    with open(path, 'wb') as handle:
        for index, packet in enumerate(packets):
            direction = packet[0]
            data_len = struct.unpack('>H', packet[5:7])[0]
            raw = packet[8:8 + data_len]
            # Recover the logical bytes by reconstructing parity-packed wire.
            bits = []
            for value in raw:
                for k in range(7, -1, -1):
                    bits.append((value >> k) & 1)
            valid_bits = packet[7]
            effective = (len(raw) - 1) * 8 + valid_bits
            data = bytearray()
            parity = []
            at = 0
            while at + 9 <= effective:
                value = 0
                for i in range(8):
                    value |= bits[at + i] << i
                at += 8
                data.append(value)
                parity.append(bits[at])
                at += 1
            frame_ts = 100000 + index * 2000
            word = len(data) | (0x8000 if direction == ord('<') else 0)
            handle.write(struct.pack('<IHH', frame_ts, 1000, word))
            handle.write(bytes(data))
            parity_raw = bytearray((len(data) - 1) // 8 + 1)
            for i, bit in enumerate(parity):
                parity_raw[i // 8] |= (bit & 1) << (7 - (i % 8))
            handle.write(bytes(parity_raw))


class Sidecar(object):
    def __init__(self):
        self.proc = subprocess.Popen(
            [sys.executable, '-u', SIDECAR],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE)
        self.lines = queue.Queue()
        self.errors = []
        self._reader = threading.Thread(target=self._read_stdout, daemon=True)
        self._reader.start()
        self._err_reader = threading.Thread(target=self._read_stderr, daemon=True)
        self._err_reader.start()

    def _read_stdout(self):
        for line in self.proc.stdout:
            self.lines.put(line)
        self.lines.put(None)

    def _read_stderr(self):
        for line in self.proc.stderr:
            self.errors.append(line.decode('utf-8', 'replace'))

    def send_raw(self, payload):
        if isinstance(payload, str):
            payload = payload.encode('utf-8')
        self.proc.stdin.write(payload + b'\n')
        self.proc.stdin.flush()

    def read(self, timeout=25):
        item = self.lines.get(timeout=timeout)
        if item is None:
            raise AssertionError('sidecar stdout closed')
        return json.loads(item.decode('utf-8'))

    def ask(self, request, timeout=25):
        self.send_raw(json.dumps(request))
        return self.read(timeout)

    def close(self):
        if self.proc.poll() is None:
            try:
                self.send_raw(json.dumps({'request_id': 999, 'action': 'shutdown'}))
                self.read(5)
            except Exception:
                pass
            self.proc.kill()
        self.proc.wait(timeout=10)
        for stream in (self.proc.stdin, self.proc.stdout, self.proc.stderr):
            try:
                stream.close()
            except Exception:
                pass


class SidecarTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.sidecar = Sidecar()
        self.addCleanup(self.sidecar.close)

    def path(self, name):
        return os.path.join(self.tmp.name, name)

    def parse_capture(self, packets, name='capture.txt'):
        path = self.path(name)
        write_capture(path, packets)
        response = self.sidecar.ask({'request_id': 1, 'action': 'parse_capture',
                                     'path': path})
        self.assertEqual(response['status'], 'ok', response.get('message'))
        return response['data']

    def test_initialize(self):
        data = self.sidecar.ask({'request_id': 7, 'action': 'initialize'})
        self.assertEqual(data['status'], 'ok')
        self.assertEqual(data['request_id'], 7)
        self.assertEqual(data['schema'], 1)
        self.assertIn('parse_trace', data['data']['capabilities'])
        self.assertGreater(data['data']['limits']['max_frame_bytes'], 0)

    def test_parse_capture_m1(self):
        data = self.parse_capture(m1_packets())
        card = data['card_info']
        self.assertEqual(card['uid'], '11223344')
        self.assertEqual(card['uid_len'], 4)
        self.assertEqual(card['atqa_hex'], '0004')
        self.assertEqual(card['sak_hex'], '08')
        self.assertIn('Mifare Classic 1K', card['card_type'])
        self.assertGreaterEqual(data['validation']['ciphertext_frames'], 2)
        self.assertEqual(data['validation']['parity_failed_plaintext'], 0)
        types = data['statistics']['by_type']
        for name in ('reqa', 'atqa', 'sak', 'mifare_auth_a'):
            self.assertIn(name, types)
        cipher_frames = [f for f in data['frames'] if f['ciphertext']]
        self.assertTrue(cipher_frames)
        self.assertTrue(any(f['ciphertext_reason'] for f in cipher_frames))

    def test_parse_trace_with_ats(self):
        # No 7-bit REQA in a trace fixture; start at ATQA.
        packets = tcl_packets()[1:]
        path = self.path('synthetic.trace')
        write_trace(path, packets)
        response = self.sidecar.ask({'request_id': 2, 'action': 'parse_trace',
                                     'path': path})
        self.assertEqual(response['status'], 'ok', response.get('message'))
        data = response['data']
        self.assertEqual(data['source']['kind'], 'pm3_trace')
        self.assertEqual(data['card_info']['uid'], '11223344')
        self.assertIsNotNone(data['card_info']['ats'])
        self.assertEqual(data['card_info']['ats']['hex'], ATS.hex().upper())
        self.assertEqual(data['card_info']['sak_hex'], '20')
        self.assertIn('ats', data['statistics']['by_type'])
        self.assertIn('i_block', data['statistics']['by_type'])
        self.assertGreater(data['statistics']['duration_us'], 0)

    def test_consecutive_traces_do_not_share_state(self):
        first = self.parse_capture(tcl_packets(), 'first.txt')
        self.assertTrue(first['card_info']['protocol']['tcl'])

        # A fresh capture starts with ATQA/SAK only.  If T=CL leaked from the
        # first parse, the 2-byte ATQA would be treated as a T=CL frame.
        second = self.parse_capture(atqa_only_packets(), 'second.txt')
        self.assertEqual(second['frames'][0]['type'], 'atqa')
        self.assertFalse(second['frames'][0]['ciphertext'])
        self.assertEqual(second['card_info']['atqa_hex'], '0004')
        self.assertEqual(second['card_info']['sak_hex'], '08')

        third = self.parse_capture(tcl_packets(), 'third.txt')
        self.assertIn('ats', third['statistics']['by_type'])

    def test_reset_clears_state(self):
        self.parse_capture(tcl_packets(), 'before_reset.txt')
        response = self.sidecar.ask({'request_id': 3, 'action': 'reset'})
        self.assertEqual(response['status'], 'ok')
        self.assertTrue(response['data']['reset'])

        after = self.parse_capture(atqa_only_packets(), 'after_reset.txt')
        self.assertEqual(after['frames'][0]['type'], 'atqa')
        self.assertFalse(after['frames'][0]['phase'] == '14443-4')

    def test_cjk_path(self):
        directory = self.path('抓包_测试_é')
        os.makedirs(directory)
        path = os.path.join(directory, '协议_trace.txt')
        write_capture(path, m1_packets())
        response = self.sidecar.ask({'request_id': 4, 'action': 'parse_capture',
                                     'path': path})
        self.assertEqual(response['status'], 'ok', response.get('message'))
        self.assertEqual(response['data']['card_info']['uid'], '11223344')

    def test_empty_capture(self):
        data = self.parse_capture([], 'empty.txt')
        self.assertEqual(data['validation']['frames_total'], 0)
        self.assertEqual(data['validation']['read_errors'], 0)
        self.assertEqual(data['frames'], [])

    def test_corrupt_capture(self):
        path = self.path('corrupt.txt')
        with open(path, 'w', encoding='utf-8') as handle:
            handle.write('ZZ\n123\n0011223344\n')
        response = self.sidecar.ask({'request_id': 5, 'action': 'parse_capture',
                                     'path': path})
        self.assertEqual(response['status'], 'ok')
        data = response['data']
        self.assertEqual(data['validation']['frames_total'], 0)
        self.assertGreaterEqual(data['validation']['read_errors'], 3)
        self.assertEqual([e['code'] for e in data['errors']][:3],
                         ['invalid_hex', 'invalid_hex', 'packet_too_short'])

    def test_oversized_frame_is_rejected(self):
        path = self.path('oversized.txt')
        # Header declares 5000 payload bytes, above max_frame_bytes=4096.
        packet = (b'<' + struct.pack('>I', 1) + struct.pack('>H', 5000)
                  + b'\x08' + b'\x00' * 16)
        with open(path, 'w', encoding='utf-8') as handle:
            handle.write(packet.hex().upper() + '\n')
        response = self.sidecar.ask({'request_id': 6, 'action': 'parse_capture',
                                     'path': path})
        self.assertEqual(response['status'], 'ok')
        self.assertEqual(response['data']['validation']['frames_total'], 0)
        self.assertEqual(response['data']['errors'][0]['code'], 'frame_too_large')

    def test_invalid_json_and_unknown_action(self):
        self.sidecar.send_raw('this is not json')
        response = self.sidecar.read()
        self.assertEqual(response['status'], 'error')
        self.assertEqual(response['data']['code'], 'invalid_json')

        self.sidecar.send_raw('[]')
        response = self.sidecar.read()
        self.assertEqual(response['status'], 'error')
        self.assertEqual(response['data']['code'], 'invalid_request')

        response = self.sidecar.ask({'request_id': 8, 'action': 'nope'})
        self.assertEqual(response['status'], 'error')
        self.assertEqual(response['data']['code'], 'unknown_action')

        # Blank lines are ignored, and the next valid request still works.
        self.sidecar.send_raw('')
        response = self.sidecar.ask({'request_id': 9, 'action': 'initialize'})
        self.assertEqual(response['status'], 'ok')
        self.assertEqual(response['request_id'], 9)

    def test_oversized_line_and_resync(self):
        self.sidecar.send_raw('x' * (1024 * 1024 + 32))
        response = self.sidecar.read()
        self.assertEqual(response['status'], 'error')
        self.assertEqual(response['data']['code'], 'line_too_long')

        # The bridge must drain the rest of the oversized line and recover.
        response = self.sidecar.ask({'request_id': 10, 'action': 'initialize'})
        self.assertEqual(response['status'], 'ok')

    def test_shutdown(self):
        response = self.sidecar.ask({'request_id': 11, 'action': 'shutdown'})
        self.assertEqual(response['status'], 'ok')
        self.assertTrue(response['data']['shutdown'])
        self.sidecar.proc.wait(timeout=10)
        self.assertEqual(self.sidecar.proc.returncode, 0)


if __name__ == '__main__':
    unittest.main(verbosity=2)
