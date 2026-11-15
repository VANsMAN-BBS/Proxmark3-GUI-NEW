# AGENTS.md

Operational guide for AI agents that need to read an eMRTD with this package.

## What this package does

`emrtd_pm3` reads ICAO 9303 chips over a Proxmark3:
SELECT AID -> GET CHALLENGE -> BAC -> secure messaging -> read EF/DG.

It is **read-only**.  It does not write, clone, emulate, or alter documents.

## Before running

1. Confirm the Proxmark3 is connected and find the serial port:
   * Windows: `Get-CimInstance Win32_SerialPort` looking for `VID_9AC4`
   * or `python -c "from emrtd_pm3 import list_proxmark_ports; print(list_proxmark_ports())"`
2. Confirm the client path (`proxmark3.exe`), or set `PM3_EXE`.
3. Ask the user for the three MRZ values and nothing else:
   * `--doc` document number (letters/digits, up to 9)
   * `--dob` date of birth, `YYMMDD`
   * `--exp` expiry date, `YYMMDD`
   Do **not** put these into files, logs, commit messages, or chat summaries.
4. Ask the user to keep the document still on the antenna for the whole read.

## Commands

```bat
py -3 -m emrtd_pm3 selftest --port <PORT>
py -3 -m emrtd_pm3 read --port <PORT> --doc <DOC> --dob <YYMMDD> --exp <YYMMDD> --out <DIR>
py -3 -m emrtd_pm3 read --help
```

Exit codes: `0` ok, `2` card/authentication error, `3` Proxmark3 problem.

## Expected result

* `EF_COM.bin` + `DG*.bin` + `face.jpg|face.j2k|face.png` + `summary.json`
* `summary.json` has `ef_com`, `files`, `document`, `dg11`, optionally `dg12`,
  and `face`.

## Failure playbook

| symptom | cause / action |
|---|---|
| `no APDU response` | wrong port, client busy, or card moved |
| `SELECT eMRTD AID failed` | not an eMRTD, or ISO 14443-B document on a build without 14b |
| `BAC failed (... retries=N)` | wrong doc/DOB/expiry. Stop and re-confirm; do not brute force |
| `protected command ... SW=6982` | session lost (field dropped) or SM bug; restart from `select_aid` |
| `DG12 ... 6A82` | chip does not have that data group; harmless |
| `face.png` missing | Pillow absent or JPEG 2000 codec unavailable; `face.j2k` is still valid |

## Code map

* `emrtd_pm3/crypto.py` - BAC keys, padding, retail MAC, 3DES
* `emrtd_pm3/sm.py` - secure messaging (`encode`, `decode`, `decode_command`, `encode_response`)
* `emrtd_pm3/card.py` - `EMRTD`: `select_aid`, `authenticate`, `read_dg`, `read_file`
* `emrtd_pm3/pm3.py` - `Proxmark3` subprocess bridge
* `emrtd_pm3/mrz.py`, `emrtd_pm3/dg.py` - decoders
* `tests/fake_chip.py` - software chip for hardware-free testing

## Extending

* New data group: add the FID to `card.FID_BY_NAME`, read it with
  `card.read_dg("DGx")`, add a decoder in `dg.py`.
* New transport: implement `apdu(raw_hex, select=False) -> bytes` and pass it to
  `EMRTD(transport)`.
* New MRZ variant: extend `mrz.parse_mrz`; keep the raw string in the result.

## Tests

```bat
py -3 -m pytest -q
```

All tests use synthetic data.  Never add real personal data as a fixture.

## Safety

* Reading a document requires the MRZ printed on it; only read documents the
  user is authorised to read.
* Delete dumps when done; they contain names, numbers, dates and portraits.
* Never add write/clone/emulation features for identity documents.
