# emrtd_pm3

Read **ICAO 9303 eMRTD** travel documents (e-passports, some national ID/permit
chips) with a **Proxmark3** used as a plain ISO 14443-4 reader.

The Proxmark3 only moves bytes.  BAC (Basic Access Control), 3DES, secure
messaging and all data-group decoding happen locally in Python, so the flow is
easy to audit, test and embed in other tools.

```
MRZ (doc no / DOB / expiry)
        |
        v
   Kseed -> Kenc, Kmac  --(BAC)-->  session keys + SSC
        |                                   |
        v                                   v
  PM3  `hf 14a apdu`  <--- 3DES-CBC + ISO 9797-1 MAC --->  card
        |
        v
  EF.COM / DG1 / DG2 / DG11 / DG12 ...  -> decoders -> JSON + files
```

## Requirements

* Python 3.10+
* `pycryptodome` (DES/3DES only)
* `pillow` (optional, converts JPEG 2000 portraits to PNG)
* Proxmark3 with iceman firmware and a working `proxmark3` client
* The document must be an **ISO 14443-A** eMRTD that supports **BAC**
  (256 KB iceman builds may not include ISO 14443-B support)

## Install

```bat
cd emrtd_pm3
py -3 -m pip install -e .
```

or run in place without installing:

```bat
py -3 -m emrtd_pm3 --help
```

## CLI

Find the port first (Windows: Device Manager, or `python -c "from emrtd_pm3 import list_proxmark_ports; print(list_proxmark_ports())"`
with `pyserial` installed).

```bat
:: 1. quick probe: select the eMRTD application and request a challenge
py -3 -m emrtd_pm3 selftest --port <PORT>

:: 2. read the document (keep the card still on the antenna)
py -3 -m emrtd_pm3 read --port <PORT> ^
    --doc C12345678 --dob 900101 --exp 300101 ^
    --out dump
```

`read` writes `EF_COM`, the requested `DG*.bin` files, `face.*`, and
`summary.json` into `--out`.  Missing data groups are reported and skipped.

Useful flags:

| flag | meaning |
|---|---|
| `--dgs DG1,DG2,DG11` | data groups to read (default `DG1,DG2,DG11,DG12`) |
| `--chunk 224` | READ BINARY size; 224 keeps responses inside one short APDU |
| `--no-photo-png` | keep only the original JPEG/JPEG 2000 portrait |
| `--json` | also print the summary JSON |
| `--pm3-exe PATH` | proxmark3 client path (or `PM3_EXE`) |
| `--keep-logs` | let the PM3 client persist history/log files (off by default) |

## Library

```python
from emrtd_pm3 import EMRTD, Proxmark3, dg, mrz

with Proxmark3("<PORT>") as pm3:
    card = EMRTD(pm3)
    card.select_aid()
    card.authenticate("C12345678", "900101", "300101")

    com = dg.parse_ef_com(card.read_ef_com())
    document = mrz.parse_mrz(dg.parse_dg1(card.read_dg("DG1")))
    face = dg.extract_face(card.read_dg("DG2"))
    print(document["surname"], com["data_groups"], face["format"])
```

Any transport with `apdu(raw_hex, select=False) -> bytes` works, so the same
code can be driven by a PC/SC reader, a TCP bridge, or the software chip in
`tests/fake_chip.py`.

## Layout

| module | role |
|---|---|
| `crypto.py` | MRZ check digits, Kseed/KDF, padding, retail MAC, 3DES |
| `tlv.py` | BER-TLV parse/encode |
| `sm.py` | secure messaging (command encode, response decode, chip-side helpers) |
| `pm3.py` | long-lived `proxmark3` client process, `hf 14a apdu` bridge |
| `card.py` | SELECT AID, GET CHALLENGE, BAC, READ BINARY/file reading |
| `mrz.py` | TD1/TD2/TD3 parsing + lenient national fallback |
| `dg.py` | EF.COM, DG1, DG2 portrait, DG11, DG12 decoders |
| `cli.py` | `selftest` / `read` commands |

## Protocol notes (learned the hard way)

1. **MAC padding**: ICAO uses ISO/IEC 9797-1 padding method 2, which *always*
   appends at least one byte.  The two BAC MACs must be padded even though
   `E.IFD`/`E.ICC` are already 32 bytes.  Skipping that yields `63CF`.
2. **Response layout**: `E.ICC` decrypts to `RND.ICC || RND.IFD || K.ICC`
   (ICC first).  Swapping the two randoms looks like a key error.
3. **Session keys** are derived again from `K.IFD XOR K.ICC`, and
   `SSC = RND.ICC[4:8] || RND.IFD[4:8]` (8 bytes, big-endian).
4. **One process, field on**: every PM3 APDU uses `-k`; dropping the RF field
   resets the card and destroys the BAC session.
5. **Chunk size 224**: with SM overhead the protected response is
   `3 + 232 + 4 + 10 = 249` bytes, which still fits a short APDU.
   Larger reads trigger 61xx chaining (handled in `card.transceive`).
6. **Prompt detection**: the client is started with `-f`; its prompt is not
   newline-terminated, so commands are considered complete after the output
   stays quiet for ~350 ms.
7. **DG2 wrappers**: portraits may be plain JPEG, JP2, a raw JPEG 2000
   codestream, or an ISO/IEC 19794-5 `FAC` container around one of those.
8. **National DG1**: some chips store DG1 as one flat string with `<`
   separators instead of TD1 lines.  `mrz.parse_mrz` falls back to a lenient
   parser; DG11 `5F0F` is the authoritative name on such documents.
9. **Retry counters**: `63CX` means authentication failed with X attempts
   left.  `AuthenticationError.retries` exposes it.  Do not loop on guesses.
10. **Missing files**: `6A82` is normal for data groups the chip does not
    have (e.g. DG12) and is treated as "not present".

## Tests

No hardware needed:

```bat
py -3 -m pip install pytest
py -3 -m pytest -q
```

The suite covers published ISO 9797-1/ICAO KDF vectors, TLV, MRZ layouts,
secure-messaging round trips, and a full BAC read against `tests/fake_chip.py`.
All fixtures use the public ICAO specimen data (`L898902C3`), never real data.

## Privacy and scope

* Output contains personal data.  Keep it local, encrypt it, or delete it.
* The PM3 client is started with `--incognito`, so it does not write APDU
  history/logs by default.  Use `--keep-logs` only when debugging.
* This project is read-only.  It intentionally has no write/clone/simulate
  paths for identity documents.
* Only read documents you are authorised to read.
