"""Helpers that build synthetic (public ICAO specimen) data groups for tests."""

from __future__ import annotations

from emrtd_pm3 import tlv

ICAO_TD3_MRZ = (
    "P<UTOERIKSSON<<ANNA<MARIA<<<<<<<<<<<<<<<<<<<\n"
    "L898902C36UTO7408122F1204159ZE184226B<<<<<10"
)
ICAO_TD3_DOC = "L898902C3"
ICAO_TD3_DOB = "740812"
ICAO_TD3_EXP = "120415"

#: Smallest possible JPEG-ish payload; the decoder only needs the signature.
TINY_JPEG = b"\xff\xd8\xff\xe0\x00\x10JFIF\x00\x01\x01\x00\x00\x01\x00\x01\x00\x00\xff\xd9"


def ef_com(*dg_tags: int) -> bytes:
    body = (
        tlv.encode(0x5F01, b"\x01\x07")
        + tlv.encode(0x5F36, b"\x04\x00\x00")
        + tlv.encode(0x5C, bytes(dg_tags))
    )
    return tlv.encode(0x60, body)


def dg1(mrz: str = ICAO_TD3_MRZ) -> bytes:
    return tlv.encode(0x61, tlv.encode(0x5F1F, mrz.encode("ascii")))


def dg2(jpeg: bytes = TINY_JPEG) -> bytes:
    record = tlv.encode(0x02, b"\x01\x01") + tlv.encode(0x5F2E, jpeg)
    return tlv.encode(0x75, tlv.encode(0x7F61, record))


def dg11(native_name: str = "TEST PERSON", mrz_name: str = "TEST<<PERSON") -> bytes:
    body = (
        tlv.encode(0x5C, b"\x5F\x0E\x5F\x0F")
        + tlv.encode(0x5F0E, native_name.encode("utf-8"))
        + tlv.encode(0x5F0F, mrz_name.encode("ascii").ljust(72, b"\x00"))
    )
    return tlv.encode(0x6B, body)


def sample_files() -> dict[int, bytes]:
    return {
        0x011E: ef_com(0x61, 0x75, 0x6B),
        0x0101: dg1(),
        0x0102: dg2(),
        0x010B: dg11(),
    }
