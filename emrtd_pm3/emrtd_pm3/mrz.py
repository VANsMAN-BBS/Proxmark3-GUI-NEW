"""MRZ parsing (ICAO 9303 TD1/TD2/TD3) plus a lenient fallback.

Some national chips (e.g. the Chinese HK/Macau permit) store DG1 as one long
string with ``<`` separators instead of three newline-separated lines.  The
fallback extracts what is unambiguous (document number, dates) and leaves the
name to DG11, which is authoritative on those documents.
"""

from __future__ import annotations

import re

from .crypto import check_digit

_NAME = re.compile(r"([A-Z]{2,})<<([A-Z]{2,})")
_NATIONAL_FIELDS = re.compile(
    r"([A-Z]{1,2}[0-9]{6,8})([0-9])<?([0-9]{6})([0-9])<?([0-9]{6})([0-9])"
)


def _split_name(field: str) -> tuple[str, str]:
    field = field.rstrip("<")
    surname, _, given = field.partition("<<")
    return (
        surname.replace("<", " ").strip(),
        given.replace("<", " ").strip(),
    )


def _sex(code: str) -> str:
    return {"M": "M", "F": "F"}.get(code, "")


def _checks(**pairs: tuple[str, str]) -> dict[str, bool]:
    return {name: check_digit(value) == digit for name, (value, digit) in pairs.items()}


def parse_mrz(text: str) -> dict:
    """Parse MRZ text into a dict.  Raises ``ValueError`` if nothing matches."""
    raw = text.replace("\r", "")
    lines = [line for line in raw.split("\n") if line.strip()]

    if len(lines) == 3 and all(len(line) == 30 for line in lines):
        return _parse_td1(lines, raw)
    if len(lines) == 2 and all(len(line) == 36 for line in lines):
        return _parse_td2(lines, raw)
    if len(lines) == 2 and all(len(line) == 44 for line in lines):
        return _parse_td3(lines, raw)
    return _parse_national(raw)


def _parse_td3(lines: list[str], raw: str) -> dict:
    l1, l2 = lines
    surname, given = _split_name(l1[5:])
    number, number_cd = l2[0:9], l2[9]
    dob, dob_cd = l2[13:19], l2[19]
    expiry, expiry_cd = l2[21:27], l2[27]
    return {
        "layout": "TD3",
        "raw": raw,
        "document_code": l1[0:2].replace("<", ""),
        "issuing_state": l1[2:5].replace("<", ""),
        "document_number": number.replace("<", ""),
        "nationality": l2[10:13].replace("<", ""),
        "date_of_birth": dob,
        "sex": _sex(l2[20]),
        "date_of_expiry": expiry,
        "personal_number": l2[28:42].replace("<", ""),
        "surname": surname,
        "given_names": given,
        "checks": _checks(
            document_number=(number, number_cd),
            date_of_birth=(dob, dob_cd),
            date_of_expiry=(expiry, expiry_cd),
        ),
    }


def _parse_td2(lines: list[str], raw: str) -> dict:
    l1, l2 = lines
    surname, given = _split_name(l1[5:])
    number, number_cd = l2[0:9], l2[9]
    dob, dob_cd = l2[13:19], l2[19]
    expiry, expiry_cd = l2[21:27], l2[27]
    return {
        "layout": "TD2",
        "raw": raw,
        "document_code": l1[0:2].replace("<", ""),
        "issuing_state": l1[2:5].replace("<", ""),
        "document_number": number.replace("<", ""),
        "nationality": l2[10:13].replace("<", ""),
        "date_of_birth": dob,
        "sex": _sex(l2[20]),
        "date_of_expiry": expiry,
        "surname": surname,
        "given_names": given,
        "checks": _checks(
            document_number=(number, number_cd),
            date_of_birth=(dob, dob_cd),
            date_of_expiry=(expiry, expiry_cd),
        ),
    }


def _parse_td1(lines: list[str], raw: str) -> dict:
    l1, l2, l3 = lines
    surname, given = _split_name(l3)
    number, number_cd = l1[5:14], l1[14]
    dob, dob_cd = l2[0:6], l2[6]
    expiry, expiry_cd = l2[8:14], l2[14]
    return {
        "layout": "TD1",
        "raw": raw,
        "document_code": l1[0:2].replace("<", ""),
        "issuing_state": l1[2:5].replace("<", ""),
        "document_number": number.replace("<", ""),
        "nationality": l2[15:18].replace("<", ""),
        "date_of_birth": dob,
        "sex": _sex(l2[7]),
        "date_of_expiry": expiry,
        "surname": surname,
        "given_names": given,
        "checks": _checks(
            document_number=(number, number_cd),
            date_of_birth=(dob, dob_cd),
            date_of_expiry=(expiry, expiry_cd),
        ),
    }


def _parse_national(raw: str) -> dict:
    text = "".join(line for line in raw.split("\n") if line.strip())
    match = _NATIONAL_FIELDS.search(text)
    if not match:
        raise ValueError("unsupported MRZ layout")
    number, _, expiry, _, dob, _ = match.groups()

    name_match = _NAME.search(text)
    surname = given = ""
    if name_match:
        surname, given = name_match.group(1), name_match.group(2)

    return {
        "layout": "national",
        "raw": raw,
        "document_number": number,
        "date_of_birth": dob,
        "date_of_expiry": expiry,
        "surname": surname,
        "given_names": given,
        "sex": "",
        "nationality": "",
        "document_code": "",
        "issuing_state": "",
        "checks": {},
        "note": "non-ICAO layout; name may be contaminated, prefer DG11",
    }
