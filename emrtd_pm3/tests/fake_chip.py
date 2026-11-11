"""A minimal software eMRTD used by the test-suite (no hardware required).

It implements just enough of ISO 7816-4 + ICAO BAC for
:class:`emrtd_pm3.card.EMRTD` to authenticate and read files.
"""

from __future__ import annotations

from emrtd_pm3 import crypto, sm


class FakeChip:
    def __init__(self, doc: str, dob: str, exp: str, files: dict[int, bytes] | None = None,
                 rnd_icc: bytes = bytes(range(1, 9)),
                 k_icc: bytes = bytes([0x10] * 16)):
        self.k_enc, self.k_mac, _ = crypto.derive_keys(doc, dob, exp)
        self.files = dict(files or {})
        self.rnd_icc = rnd_icc
        self.k_icc = k_icc
        self.sm: sm.SecureMessaging | None = None
        self.current_fid: int | None = None

    # -- transport API -------------------------------------------------

    def apdu(self, raw_hex: str, select: bool = False) -> bytes:
        raw = bytes.fromhex(raw_hex)
        cla, ins, p1, p2 = raw[0], raw[1], raw[2], raw[3]

        if cla == 0x00 and ins == 0xA4 and p1 == 0x04:
            self.sm = None
            self.current_fid = None
            return b"\x90\x00"
        if cla == 0x00 and ins == 0x84:
            return self.rnd_icc[:raw[4]] + b"\x90\x00"
        if cla == 0x00 and ins == 0x82:
            return self._mutual_auth(raw)
        if cla == 0x0C:
            return self._protected(raw)
        return b"\x6d\x00"

    # -- BAC -----------------------------------------------------------

    def _mutual_auth(self, raw: bytes) -> bytes:
        if self.sm is not None:
            return b"\x69\x85"

        body = raw[5:5 + raw[4]]
        e_ifd, m_ifd = body[:32], body[32:40]
        if crypto.retail_mac_padded(self.k_mac, e_ifd) != m_ifd:
            return b"\x63\xcf"

        plain = crypto.tdes_decrypt(self.k_enc, e_ifd)
        rnd_ifd, rnd_icc, k_ifd = plain[:8], plain[8:16], plain[16:32]
        if rnd_icc != self.rnd_icc:
            return b"\x63\xcf"

        e_icc = crypto.tdes_encrypt(self.k_enc, self.rnd_icc + rnd_ifd + self.k_icc)
        m_icc = crypto.retail_mac_padded(self.k_mac, e_icc)

        seed = bytes(a ^ b for a, b in zip(k_ifd, self.k_icc))
        self.sm = sm.SecureMessaging(
            crypto.kdf(seed, 1),
            crypto.kdf(seed, 2),
            int.from_bytes(self.rnd_icc[4:8] + rnd_ifd[4:8], "big"),
        )
        return e_icc + m_icc + b"\x90\x00"

    # -- protected commands -------------------------------------------

    def _protected(self, raw: bytes) -> bytes:
        ins, p1, p2 = raw[1], raw[2], raw[3]
        body = raw[5:5 + raw[4]]
        try:
            data, le = self.sm.decode_command(ins, p1, p2, body)
        except sm.SecureMessagingError:
            return b"\x6a\x80"

        if ins == 0xA4:
            fid = int.from_bytes(data, "big")
            if fid not in self.files:
                return self.sm.encode_response(status=0x6A82) + b"\x90\x00"
            self.current_fid = fid
            return self.sm.encode_response() + b"\x90\x00"

        if ins == 0xB0:
            if self.current_fid is None:
                return self.sm.encode_response(status=0x6985) + b"\x90\x00"
            offset = (p1 << 8) | p2
            blob = self.files[self.current_fid]
            chunk = blob[offset:offset + (le or 256)]
            if not chunk and offset:
                return self.sm.encode_response(status=0x6B00) + b"\x90\x00"
            return self.sm.encode_response(chunk) + b"\x90\x00"

        return self.sm.encode_response(status=0x6D00) + b"\x90\x00"
