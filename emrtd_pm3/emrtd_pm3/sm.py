"""ISO/IEC 7816-4 secure messaging as used by ICAO 9303 eMRTDs.

A :class:`SecureMessaging` instance holds the session keys and the send
sequence counter (SSC).  ``encode`` protects a command APDU, ``decode``
unwraps a protected response.  ``encode_response`` is the mirror operation,
provided so simulators and tests can implement the chip side with the same
code.

Notes
-----
* The SSC is incremented once before protecting a command and once before
  verifying/encoding the matching response.
* Command MAC input: ``SSC || pad(CLA INS P1 P2) || DO87/DO85 || DO97``.
* Response MAC input: ``SSC || DO87/DO85 || DO99``.
* Padding is ISO/IEC 9797-1 method 2 (always at least one byte).
"""

from __future__ import annotations

from . import crypto, tlv

MAX_SHORT_APDU_DATA = 255


class SecureMessagingError(Exception):
    pass


class SecureMessaging:
    def __init__(self, ks_enc: bytes, ks_mac: bytes, ssc: int = 0):
        if len(ks_enc) != 16 or len(ks_mac) != 16:
            raise ValueError("BAC session keys must be 16 bytes")
        self.ks_enc = ks_enc
        self.ks_mac = ks_mac
        self.ssc = ssc & 0xFFFFFFFFFFFFFFFF

    # -- SSC -----------------------------------------------------------

    @property
    def ssc_bytes(self) -> bytes:
        return self.ssc.to_bytes(8, "big")

    def increment(self) -> None:
        self.ssc = (self.ssc + 1) & 0xFFFFFFFFFFFFFFFF

    def decrement(self) -> None:
        """Undo an increment, e.g. after the card did not process a command."""
        self.ssc = (self.ssc - 1) & 0xFFFFFFFFFFFFFFFF

    # -- command side --------------------------------------------------

    def encode(self, ins: int, p1: int, p2: int, data: bytes = b"",
               le: int | None = None) -> bytes:
        """Return a protected command APDU (short length, Le=256)."""
        self.increment()

        nodes = b""
        if data:
            tag = 0x87 if ins % 2 == 0 else 0x85
            encrypted = crypto.tdes_encrypt(self.ks_enc, crypto.pad(data))
            nodes += tlv.encode(tag, b"\x01" + encrypted)
        if le is not None:
            nodes += b"\x97\x01" + bytes([le & 0xFF])

        header = crypto.pad(bytes([0x0C, ins, p1, p2]))
        mac = crypto.retail_mac_padded(self.ks_mac, self.ssc_bytes + header + nodes)

        body = nodes + b"\x8e\x08" + mac
        if len(body) > MAX_SHORT_APDU_DATA:
            raise SecureMessagingError(
                "protected APDU data too long (%d bytes); reduce the read chunk"
                % len(body)
            )
        return bytes([0x0C, ins, p1, p2, len(body)]) + body + b"\x00"

    def decode(self, blob: bytes) -> tuple[int, bytes]:
        """Verify and unwrap a protected response; returns ``(status, data)``."""
        self.increment()

        values: dict[int, bytes] = {}
        raw: dict[int, bytes] = {}
        pos = 0
        while pos < len(blob):
            start = pos
            tag, value, pos = tlv.parse(blob, pos)
            values[tag] = value
            raw[tag] = blob[start:pos]

        if 0x8E not in values:
            raise SecureMessagingError("protected response is missing DO8E")
        if 0x99 not in values:
            raise SecureMessagingError("protected response is missing DO99")

        enc_tag = 0x87 if 0x87 in values else 0x85
        mac_data = self.ssc_bytes
        if enc_tag in values:
            mac_data += raw[enc_tag]
        mac_data += raw[0x99]

        expected = crypto.retail_mac_padded(self.ks_mac, mac_data)
        if expected != values[0x8E]:
            raise SecureMessagingError("protected response MAC mismatch")

        status = int.from_bytes(values[0x99], "big")
        data = b""
        if enc_tag in values:
            payload = values[enc_tag]
            if not payload or payload[0] != 0x01:
                raise SecureMessagingError("unexpected DO87/DO85 version byte")
            data = crypto.unpad(crypto.tdes_decrypt(self.ks_enc, payload[1:]))
        return status, data

    # -- chip side (simulators / tests) --------------------------------

    def decode_command(self, ins: int, p1: int, p2: int, body: bytes) -> tuple[bytes, int | None]:
        """Verify and unwrap a protected command body (test/simulator use).

        Returns ``(data, le)`` where ``le`` is ``None`` when DO97 is absent.
        """
        self.increment()

        values: dict[int, bytes] = {}
        raw: dict[int, bytes] = {}
        pos = 0
        while pos < len(body):
            start = pos
            tag, value, pos = tlv.parse(body, pos)
            values[tag] = value
            raw[tag] = body[start:pos]

        if 0x8E not in values:
            raise SecureMessagingError("protected command is missing DO8E")
        enc_tag = 0x87 if 0x87 in values else 0x85

        mac_data = self.ssc_bytes + crypto.pad(bytes([0x0C, ins, p1, p2]))
        if enc_tag in values:
            mac_data += raw[enc_tag]
        if 0x97 in values:
            mac_data += raw[0x97]

        expected = crypto.retail_mac_padded(self.ks_mac, mac_data)
        if expected != values[0x8E]:
            raise SecureMessagingError("protected command MAC mismatch")

        data = b""
        if enc_tag in values:
            payload = values[enc_tag]
            if not payload or payload[0] != 0x01:
                raise SecureMessagingError("unexpected DO87/DO85 version byte")
            data = crypto.unpad(crypto.tdes_decrypt(self.ks_enc, payload[1:]))

        le = values[0x97][0] if 0x97 in values else None
        return data, (256 if le == 0 else le)

    def encode_response(self, data: bytes = b"", status: int = 0x9000) -> bytes:
        """Build a protected response for the current SSC (test/simulator use)."""
        self.increment()

        nodes = b""
        if data:
            encrypted = crypto.tdes_encrypt(self.ks_enc, crypto.pad(data))
            nodes += tlv.encode(0x87, b"\x01" + encrypted)
        nodes += tlv.encode(0x99, status.to_bytes(2, "big"))

        mac = crypto.retail_mac_padded(self.ks_mac, self.ssc_bytes + nodes)
        return nodes + b"\x8e\x08" + mac
