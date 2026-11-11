"""Cryptographic primitives for ICAO 9303 BAC (Basic Access Control).

Only the pieces an eMRTD reader needs:

* MRZ check digits and Kseed derivation
* ICAO key derivation (SHA-1 based, 3DES key size 112)
* 3DES-CBC with single/double length keys
* ISO/IEC 9797-1 MAC algorithm 3 (retail MAC), padding method 2

``pycryptodome`` is used only for the raw DES block cipher; everything else
is plain Python so the code is easy to audit and port.
"""

from __future__ import annotations

import hashlib

from Crypto.Cipher import DES, DES3

MRZ_WEIGHTS = (7, 3, 1)


def check_digit(field: str) -> str:
    """ICAO 9303 check digit: weights 7,3,1; ``<`` and other fillers count 0."""
    total = 0
    for i, ch in enumerate(field):
        if "0" <= ch <= "9":
            value = ord(ch) - ord("0")
        elif "A" <= ch <= "Z":
            value = ord(ch) - ord("A") + 10
        else:
            value = 0
        total += value * MRZ_WEIGHTS[i % 3]
    return str(total % 10)


def normalize_document_number(doc: str) -> str:
    """Upper-case, strip separators, pad with ``<`` to the 9-char MRZ field."""
    doc = "".join(ch for ch in doc.upper() if ch.isalnum() or ch == "<")
    return doc[:9].ljust(9, "<")


def normalize_date(value: str) -> str:
    """Return the last six digits (YYMMDD) of a date string."""
    digits = "".join(ch for ch in value if ch.isdigit())
    if len(digits) < 6:
        raise ValueError("date needs at least 6 digits (YYMMDD): %r" % value)
    return digits[-6:]


def mrz_information(doc: str, dob: str, exp: str) -> str:
    """Concatenate the fields that seed BAC: number+cd, dob+cd, expiry+cd."""
    doc = normalize_document_number(doc)
    dob = normalize_date(dob)
    exp = normalize_date(exp)
    return (
        doc + check_digit(doc)
        + dob + check_digit(dob)
        + exp + check_digit(exp)
    )


def kdf(seed: bytes, counter: int) -> bytes:
    """ICAO KDF: SHA-1(seed || counter_be32)[0:16] with odd DES parity."""
    digest = hashlib.sha1(seed + counter.to_bytes(4, "big")).digest()[:16]
    return set_odd_parity(digest)


def derive_keys(doc: str, dob: str, exp: str) -> tuple[bytes, bytes, str]:
    """Return ``(k_enc, k_mac, mrz_information)`` for BAC."""
    info = mrz_information(doc, dob, exp)
    seed = hashlib.sha1(info.encode("ascii")).digest()[:16]
    return kdf(seed, 1), kdf(seed, 2), info


def set_odd_parity(data: bytes) -> bytes:
    """Set odd parity on every DES key byte (parity bits are ignored by DES)."""
    out = bytearray(data)
    for i, byte in enumerate(out):
        byte &= 0xFE
        if bin(byte).count("1") % 2 == 0:
            byte |= 1
        out[i] = byte
    return bytes(out)


def pad(data: bytes, block_size: int = 8) -> bytes:
    """ISO/IEC 9797-1 padding method 2 (always appends at least one byte)."""
    n = block_size - (len(data) % block_size)
    return data + b"\x80" + b"\x00" * (n - 1)


def unpad(data: bytes) -> bytes:
    """Reverse :func:`pad`."""
    stripped = data.rstrip(b"\x00")
    if not stripped or stripped[-1] != 0x80:
        raise ValueError("invalid ISO/IEC 9797-1 padding")
    return stripped[:-1]


def tdes_encrypt(key: bytes, data: bytes, iv: bytes = b"\x00" * 8) -> bytes:
    return DES3.new(key, DES3.MODE_CBC, iv).encrypt(data)


def tdes_decrypt(key: bytes, data: bytes, iv: bytes = b"\x00" * 8) -> bytes:
    return DES3.new(key, DES3.MODE_CBC, iv).decrypt(data)


def retail_mac(key: bytes, data: bytes) -> bytes:
    """ISO 9797-1 MAC algorithm 3 over block-aligned ``data``.

    ``key`` must be a 16-byte double-length 3DES key.  Data is CBC-MACed with
    K1, then the last block is decrypted with K2 and encrypted with K1.
    """
    if len(key) != 16:
        raise ValueError("retail MAC key must be 16 bytes, got %d" % len(key))
    if not data or len(data) % 8:
        raise ValueError("MAC input must be 1..n aligned blocks")

    k1, k2 = key[:8], key[8:16]
    cipher1 = DES.new(k1, DES.MODE_ECB)
    cipher2 = DES.new(k2, DES.MODE_ECB)

    block = b"\x00" * 8
    for offset in range(0, len(data), 8):
        chunk = bytes(a ^ b for a, b in zip(block, data[offset:offset + 8]))
        block = cipher1.encrypt(chunk)

    block = cipher2.decrypt(block)
    return cipher1.encrypt(block)


def retail_mac_padded(key: bytes, data: bytes) -> bytes:
    """Convenience wrapper that applies padding method 2 before the MAC."""
    return retail_mac(key, pad(data))
