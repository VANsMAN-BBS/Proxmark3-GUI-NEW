"""Decoders for the data groups commonly needed from a travel document."""

from __future__ import annotations

from . import tlv

DG_BY_TAG = {0x61: 1, 0x75: 2, 0x6B: 11, 0x6C: 12, 0x6D: 13,
             0x6E: 14, 0x6F: 15, 0x77: 16}

DG11_TAGS = {
    0x5F0E: "name_of_holder",
    0x5F0F: "name_of_holder_mrz",
    0x5F10: "full_date_of_birth",
    0x5F11: "place_of_birth",
    0x5F2B: "address",
    0x5F42: "telephone",
    0x5F12: "profession",
    0x5F13: "title",
    0x5F14: "personal_number",
}

DG12_TAGS = {
    0x5F19: "issuing_authority",
    0x5F26: "date_of_issue",
    0x5F28: "name_of_other_person",
}


def parse_ef_com(data: bytes) -> dict:
    """Decode EF.COM: LDS version, unicode version and the DG list."""
    out = {"lds_version": "", "unicode_version": "", "data_groups": []}
    for tag, value in tlv.iter_tlv(data):
        if tag != 0x60:
            continue
        for inner_tag, inner in tlv.iter_tlv(value):
            if inner_tag == 0x5F01 and len(inner) >= 2:
                out["lds_version"] = "%d.%d" % (inner[0], inner[1])
            elif inner_tag == 0x5F36 and len(inner) >= 3:
                out["unicode_version"] = "%d.%d.%d" % tuple(inner[:3])
            elif inner_tag == 0x5C:
                out["data_groups"] = sorted(DG_BY_TAG.get(b, b) for b in inner)
    return out


def parse_dg1(data: bytes) -> str:
    """Return the MRZ string stored in DG1 (may contain newlines or be flat)."""
    mrz = tlv.find_first(data, 0x5F1F)
    if mrz is None:
        raise ValueError("DG1 has no MRZ (5F1F)")
    return mrz.decode("ascii", "replace")


def detect_image(payload: bytes) -> dict:
    """Identify an embedded portrait and return ``{format, data, container}``.

    Handles plain JPEG, JPEG 2000 (JP2 file or raw codestream), and the
    ISO/IEC 19794-5 "FAC" wrapper used by some national implementations.
    """
    if payload.startswith(b"\xff\xd8\xff"):
        return {"format": "jpeg", "data": payload, "container": "raw"}
    if payload.startswith(b"\xff\x4f\xff\x51"):
        return {"format": "jpeg2000", "data": payload, "container": "codestream"}
    if payload[4:8] == b"jP  ":
        return {"format": "jp2", "data": payload, "container": "jp2"}

    if payload.startswith(b"FAC\x00"):
        codestream = payload.find(b"\xff\x4f\xff\x51")
        eoc = payload.rfind(b"\xff\xd9")
        if codestream >= 0 and eoc > codestream:
            return {
                "format": "jpeg2000",
                "data": payload[codestream:eoc + 2],
                "container": "iso19794-5",
            }
        jpeg = payload.find(b"\xff\xd8\xff")
        if jpeg >= 0 and eoc > jpeg:
            return {
                "format": "jpeg",
                "data": payload[jpeg:eoc + 2],
                "container": "iso19794-5",
            }

    return {"format": "bin", "data": payload, "container": "unknown"}


def extract_face(dg2: bytes) -> dict | None:
    """Extract the portrait from DG2 (tag 5F2E)."""
    payload = tlv.find_first(dg2, 0x5F2E)
    if payload is None:
        return None
    return detect_image(payload)


def _decode_text(value: bytes) -> str:
    try:
        return value.decode("utf-8").rstrip("\x00")
    except UnicodeDecodeError:
        return value.hex()


def _parse_dg(data: bytes, outer_tag: int, mapping: dict[int, str]) -> dict:
    out: dict = {}
    unknown: dict[str, str] = {}
    for tag, value in tlv.iter_tlv(data):
        if tag != outer_tag:
            continue
        for inner_tag, inner in tlv.iter_tlv(value):
            name = mapping.get(inner_tag)
            if name:
                text = _decode_text(inner)
                if name != "name_of_holder_mrz":
                    text = text.replace("<", " ").strip()
                out[name] = text
            else:
                unknown["tag_%02X" % inner_tag] = _decode_text(inner)
    if unknown:
        out["unknown"] = unknown
    return out


def parse_dg11(data: bytes) -> dict:
    return _parse_dg(data, 0x6B, DG11_TAGS)


def parse_dg12(data: bytes) -> dict:
    return _parse_dg(data, 0x6C, DG12_TAGS)
