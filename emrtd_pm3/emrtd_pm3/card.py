"""eMRTD reader: BAC authentication, secure messaging and file access."""

from __future__ import annotations

import secrets

from . import crypto, sm as sm_module, tlv

EMRTD_AID = "A0000002471001"

FID_BY_NAME = {
    "EF_COM": 0x011E,
    "DG1": 0x0101,
    "DG2": 0x0102,
    "DG3": 0x0103,
    "DG4": 0x0104,
    "DG5": 0x0105,
    "DG6": 0x0106,
    "DG7": 0x0107,
    "DG8": 0x0108,
    "DG9": 0x0109,
    "DG10": 0x010A,
    "DG11": 0x010B,
    "DG12": 0x010C,
    "DG13": 0x010D,
    "DG14": 0x010E,
    "DG15": 0x010F,
    "DG16": 0x0110,
    "SOD": 0x011D,
}
NAME_BY_FID = {value: key for key, value in FID_BY_NAME.items()}

#: 224 keeps a protected 256-byte-read response inside one short APDU:
#: 3 (DO87 hdr+version) + 232 (padded 224) + 4 (DO99) + 10 (DO8E) = 249.
DEFAULT_CHUNK = 224


class EMRTDError(Exception):
    pass


class AuthenticationError(EMRTDError):
    def __init__(self, message: str, retries: int | None = None):
        super().__init__(message)
        self.retries = retries


class EMRTD:
    """Drive an ICAO 9303 chip through any transport with ``apdu(hex)``."""

    def __init__(self, transport, chunk: int = DEFAULT_CHUNK):
        self.transport = transport
        self.chunk = chunk
        self.sm: sm_module.SecureMessaging | None = None
        self.mrz_information: str | None = None

    # -- helpers -------------------------------------------------------

    @staticmethod
    def _check(raw: bytes, what: str) -> None:
        if raw[-2:] != b"\x90\x00":
            raise EMRTDError("%s failed: SW=%s" % (what, raw[-2:].hex()))

    # -- BAC -----------------------------------------------------------

    def select_aid(self) -> None:
        apdu = "00A40400%02X%s00" % (len(EMRTD_AID) // 2, EMRTD_AID)
        self._check(self.transport.apdu(apdu, select=True), "SELECT eMRTD AID")

    def get_challenge(self, length: int = 8) -> bytes:
        raw = self.transport.apdu("00840000%02X" % length)
        self._check(raw, "GET CHALLENGE")
        if len(raw) - 2 != length:
            raise EMRTDError("GET CHALLENGE returned %d bytes" % (len(raw) - 2))
        return raw[:-2]

    def authenticate(self, doc: str, dob: str, exp: str) -> None:
        """Run BAC; afterwards :attr:`sm` is ready for protected commands."""
        k_enc, k_mac, info = crypto.derive_keys(doc, dob, exp)
        self.mrz_information = info

        rnd_icc = self.get_challenge(8)
        rnd_ifd = secrets.token_bytes(8)
        k_ifd = secrets.token_bytes(16)

        # S = RND.IFD || RND.ICC || K.IFD, encrypted with the initial Kenc
        e_ifd = crypto.tdes_encrypt(k_enc, rnd_ifd + rnd_icc + k_ifd)
        m_ifd = crypto.retail_mac_padded(k_mac, e_ifd)

        raw = self.transport.apdu("0082000028" + (e_ifd + m_ifd).hex() + "28")
        status = raw[-2:]
        if status[0] == 0x63:
            # 63CX: authentication failed, X attempts left
            raise AuthenticationError(
                "BAC failed (check document number / DOB / expiry)",
                retries=status[1] & 0x0F,
            )
        self._check(raw, "MUTUAL AUTHENTICATE")

        body = raw[:-2]
        if len(body) != 40:
            raise EMRTDError("unexpected mutual-auth response length %d" % len(body))
        if crypto.retail_mac_padded(k_mac, body[:32]) != body[32:]:
            raise AuthenticationError("mutual-auth response MAC mismatch")

        plain = crypto.tdes_decrypt(k_enc, body[:32])
        if plain[:8] != rnd_icc or plain[8:16] != rnd_ifd:
            raise AuthenticationError("mutual-auth random mismatch")

        session_seed = bytes(a ^ b for a, b in zip(k_ifd, plain[16:32]))
        ks_enc = crypto.kdf(session_seed, 1)
        ks_mac = crypto.kdf(session_seed, 2)
        ssc = int.from_bytes(rnd_icc[4:8] + rnd_ifd[4:8], "big")
        self.sm = sm_module.SecureMessaging(ks_enc, ks_mac, ssc)

    # -- protected APDUs ----------------------------------------------

    def transceive(self, ins: int, p1: int, p2: int, data: bytes = b"",
                   le: int | None = None) -> bytes:
        if self.sm is None:
            raise EMRTDError("authenticate() first")

        for _attempt in range(2):
            raw = self.transport.apdu(self.sm.encode(ins, p1, p2, data, le).hex())
            blob, status = raw[:-2], raw[-2:]

            # long responses are fetched with GET RESPONSE (not protected)
            guard = 0
            while status[0] == 0x61:
                guard += 1
                if guard > 64:
                    raise EMRTDError("response chaining did not terminate")
                more = self.transport.apdu("00C00000%02X" % status[1])
                blob += more[:-2]
                status = more[-2:]

            if status[0] == 0x6C:  # wrong Le: undo SSC, retry with suggested Le
                self.sm.decrement()
                le = status[1] or 256
                continue

            if status != b"\x90\x00":
                raise EMRTDError(
                    "protected command INS=%02X failed: SW=%s" % (ins, status.hex())
                )

            protected_status, payload = self.sm.decode(blob)
            if protected_status != 0x9000:
                raise EMRTDError("protected status %04X" % protected_status)
            return payload

        raise EMRTDError("protected command INS=%02X kept failing" % ins)

    # -- files ---------------------------------------------------------

    def select_ef(self, fid: int) -> None:
        self.transceive(0xA4, 0x02, 0x0C, fid.to_bytes(2, "big"), None)

    def read_binary(self, offset: int, length: int) -> bytes:
        if length <= 0 or length > 256:
            raise ValueError("length must be 1..256")
        if offset > 0x7FFF:
            raise EMRTDError(
                "READ BINARY offset %d exceeds the 15-bit limit; "
                "an odd-INS reader would be required" % offset
            )
        return self.transceive(0xB0, (offset >> 8) & 0xFF, offset & 0xFF, b"", length)

    @staticmethod
    def _total_length(header: bytes) -> int:
        if len(header) < 2:
            raise EMRTDError("short file header")
        pos = 1
        if header[0] & 0x1F == 0x1F:
            while pos < len(header) and header[pos] & 0x80:
                pos += 1
            pos += 1
        if pos >= len(header):
            raise EMRTDError("truncated file header")
        length = header[pos]
        pos += 1
        if length & 0x80:
            count = length & 0x7F
            if pos + count > len(header):
                raise EMRTDError("truncated BER length")
            length = int.from_bytes(header[pos:pos + count], "big")
            pos += count
        return pos + length

    def read_file(self, fid: int, chunk: int | None = None, progress=None) -> bytes:
        chunk = chunk or self.chunk
        self.select_ef(fid)

        header = self.read_binary(0, 4)
        total = self._total_length(header)
        # READ BINARY (ISO 7816-4, short EF addressing) only has a 15-bit
        # offset.  Fail with the actual size instead of hanging near the limit.
        if total > 0x8000:
            raise EMRTDError(
                "file length %d exceeds the 15-bit READ BINARY limit (32768); "
                "this document needs an extended-length reader" % total)
        data = bytearray(header)
        if progress is not None:
            progress(len(data), total)
        while len(data) < total:
            want = min(chunk, total - len(data))
            part = self.read_binary(len(data), want)
            if not part:
                raise EMRTDError("empty read at offset %d" % len(data))
            data += part
            if progress is not None:
                progress(len(data), total)
        return bytes(data[:total])

    def read_ef_com(self, progress=None) -> bytes:
        return self.read_file(FID_BY_NAME["EF_COM"], progress=progress)

    def read_dg(self, name: str | int, chunk: int | None = None, progress=None) -> bytes:
        if isinstance(name, int):
            fid = FID_BY_NAME.get("DG%d" % name)
            if fid is None:
                raise ValueError("unknown data group DG%d" % name)
        else:
            key = name.upper()
            if key.isdigit():
                key = "DG" + key
            if key not in FID_BY_NAME:
                raise ValueError("unknown file %r" % name)
            fid = FID_BY_NAME[key]
        return self.read_file(fid, chunk=chunk, progress=progress)
