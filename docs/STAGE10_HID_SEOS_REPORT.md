# Stage 10 — HID SEOS (T-money / Climate Card) read-only page

Date: 2026-11-16  
Branch: `upgrade/v0.4.1-polish`  
Status: implemented, built, tested. **Not committed, not pushed.**  
Stage 8.1/8.2 working-tree changes were left intact.

## 1. Scope

A new read-only page for KS X 6924 JavaCard transit cards read over
**ISO14443-A → ISO-DEP**:

- T-money prepaid cards (AID `D4100000030001`);
- T-money **Climate Card** period passes (`cardType 0xB5` + SFI 7).

No MIFARE Classic path, no write, no authentication, no key, no clone, no
restore. Every command is an ISO 7816-4 read (`SELECT`, `GET BALANCE`,
`READ RECORD`, `READ BINARY`).

## 2. Architecture

```
HidSeosPage (src/module/hidseospage.*)
  │  read / cancel / export / tables / detail dialogs
  ▼
SeosReader (src/module/hidseos/seos_reader.*)
  │  async state machine on ClientSession::executeCommand / commandFinished
  │  → SeosReadSnapshot → SeosReader::buildResult()
  ▼
Parser / Codes / Export (src/module/hidseos/)
  │  pure functions; also used directly by QtTest
  ▼
HidSeos::ReadResult (src/module/hidseos/seos_types.h)
```

Files:

| File | Purpose |
|---|---|
| `src/module/hidseos/seos_types.h` | Card model: purse, balance, pass, trips, financial records, grouped trips, calibration, raw records, read result |
| `src/module/hidseos/seos_codes.h/.cpp` | Documented card type / issuer / user / transport / marker / SFI / status-word tables |
| `src/module/hidseos/seos_parser.h/.cpp` | APDU output parsing, PurseInfo, SFI 3/4/7/11, timestamp decode, calibration, grouping |
| `src/module/hidseos/seos_reader.h/.cpp` | Read-only asynchronous sequence; `buildResult()` assembles a full result from raw APDU bodies |
| `src/module/hidseos/seos_exporter.h/.cpp` | JSON and UTF-8-BOM CSV export |
| `src/module/hidseospage.h/.cpp` | "HID SEOS" page UI |
| `tests/hidseos/hidseos_tests.pro`, `tst_hidseos.cpp` | 12 QtTest cases (14 including init/cleanup) |

The page is inserted at tab index 5, immediately after **FeliCa**, and receives
connection state through the existing `ClientSession::stateChanged` signal.
No new process, serial port, framework, or command queue was introduced.

## 3. APDU flow

The reader sends one command at a time through the shared FIFO queue:

| Step | Command | Handling |
|---|---|---|
| 1 | `hf 14a info` | UID / ATQA / SAK / ATS detection; 2 retries when no UID |
| 2 | `hf 14a apdu -s -k -d 00A4040007D4100000030001` | ISO-DEP activation + SELECT T-money; FCI `6F 31 B0 2F` + 47-byte PurseInfo |
| 3 | `hf 14a apdu -k -d 904C000004` | GET BALANCE; `6D00` = unsupported (N/A), other errors = unknown (`—`), never a fake 0 |
| 4 | `hf 14a apdu -k -d 00B087001B` | SFI 7 period pass (27 bytes); one `6Cxx` Le retry |
| 5 | `hf 14a apdu -k -d 00B2<rec>1C00` | SFI 3, records 1..20; `6A83`/`6A82` stop the series |
| 6 | `hf 14a apdu -k -d 00B2<rec>2400` | SFI 4, records 1..20; `6A82`/`6A83` stop |
| 7 | `hf 14a apdu -k -d 00B2<rec>2C00` | SFI 5, records 1..20; `6A82`/`6A83` stop |
| 8 | `hf 14a apdu -k -d 00B2<rec>5C00` | SFI 11 read events (only `cardType 0xB5`), records 1..2 |
| 9 | `hf 14a apdu -k -d 00B08D0010` | SFI 13 auxiliary file (only `cardType 0xB5`) |

Notes:

- `6C xx` is retried once with `xx` as Le; a second `6C` stops that item.
- A lost response during a record series stops the read immediately, keeps all
  data already collected, and marks the result partial.
- Cancelling emits a partial result; the in-flight command result is ignored.
- Detection is based on the AID select, not on the `hf 14a info` "possible
  types" hint.

## 4. Decoding rules

### 4.1 PurseInfo (FCI tag `B0`, 47 bytes)

Card type `[0]`, algorithm `[1]`, key version `[2]`, issuer `[3]`,
CSN BCD `[4..11]`, idtr BCD `[12..16]`, issue/expiry BCD `[17..20]/[21..24]`,
user code `[25]`, discount `[26]`, max balance `[27..30]`, branch `[31..32]`,
transaction limit `[33..36]`, carrier/financial institution `[37]/[38]`,
reserved `[39..46]`, raw 47 bytes preserved.

`0xB5` is treated as **Climate Card only when a complete 27-byte SFI 7 pass
parses successfully**. A `0xB5` card without a valid pass is shown as
`Unknown` with a warning instead of guessing.

### 4.2 SFI 3 trip record (52 bytes)

Type `[0..1]`, event `[2]`, transport `[3]`, journey counter `[4]`,
device `[6..8]`, packed timestamp `[11..14]`, distance `[19..20]`,
field-22 fare `[22..23]`, station/terminal `[24..27]`, base fare `[40..41]`,
journey cumulative fare `[46..47]`, distance surcharge `[50..51]`.

The display amount is always `[46..47]` (`fare_total` / journey cumulative),
never the sum of taps. `[40..41]` is kept as the single-leg base fare.

### 4.3 SFI 4 / 5 / 11 record

Type `[0]`, balance `[2..5]`, sequence `[8..9]`, fare `[12..13]`,
marker `[14..15]`, terminal `[18..25]`, BCD date/time `[26..32]`.

`marker 0x0720` is labelled as a card-read / initialization event, not a
purchase. The T-money empty fixture therefore has two read events and zero
purchases.

### 4.4 Grouping (same logic as `parse_ksx6924.py`)

- Group taps by `counter` (journey counter).
- For each transport in a group, take the first boarding and first alighting.
- One journey row: date (newest record of the group), start/end, tap count,
  `max(fare_total)` as the recorded journey fare, and legs.
- Backfill `journey_counter` / `journey_fare` on every tap.
- Sort journeys by date, then end time, descending.
- Exact duplicate raw records are ignored with a warning so a re-read cannot
  double-count.

## 5. Time calibration

- Global/community epoch stays **1989-06-14**.
- Each result carries `TimeCalibration { community_epoch, observed_epoch,
  offset_days, verified, source }`.
- When an SFI 7 pass exists, the newest SFI 3 date decoded with the community
  epoch is compared with `pass.valid_to`. If it lies after the pass end date by
  at most 31 days, the difference becomes a negative card-level offset:
  the sample Climate Card produces **1989-06-08, −6 days, verified**.
- Raw values are preserved: `day_counter`, `raw_timestamp`,
  `community_datetime` stay in the model and in the JSON. Only `datetime`,
  `date`, `time` are re-decoded with the observed epoch.
- Without a pass anchor the offset is `0` and `verified=false`; the source
  string explains that the community epoch was used.

## 6. UI

- Actions: **Read Card / Cancel / Export JSON / Export CSV / Clear View**.
- Card panel: balance (large), balance reason, card type + code, card kind,
  UID, CSN, issuer, user type, issue/expiry, protocol with ATQA/SAK/ATS tooltip.
- Period-pass panel: validity, days, product code, status, date calibration.
- Tabs:
  - **Transactions** — SFI 3 taps (date, time, event, transport, counter,
    journey, recorded fare, distance);
  - **Grouped trips** — 7 mergeable journeys with legs, taps, recorded fare;
  - **Financial records** — SFI 4/5 and SFI 11 rows;
  - **Raw data** — every raw record: SFI, file name, slot, length, hex.
- Selecting a row fills the detail pane; **double-click opens a details
  dialog** with field offsets, raw timestamp, raw bytes, and explanatory notes.
- Labels explicitly say **Recorded fare** and the detail text notes that it is
  a gate-written nominal amount, which the period pass may cover.
- Climate balance is shown as **N/A** (or `—` for other errors) with the reason;
  it is never shown as 0.
- All user-facing strings use `tr()`. New contexts were added manually:
  `HidSeosPage` and `HidSeos::SeosReader` in `i18n/zh_CN.ts` and
  `i18n/en_US.ts`, and `HID SEOS` in `MainWindow`.

## 7. Export

- `QFileDialog` pattern matches the FeliCa page (Documents folder, timestamped
  suggested name).
- JSON schema (always includes the required sections):

```
source, card_info, balance, pass, time_calibration,
raw_transactions, grouped_trips, records, read_status, warnings
```

- `balance.amount_krw` is `null` when unsupported/unknown, `0` only when the
  card really returned a 0 balance. `read_only` is fixed to `true`.
- CSV export writes two UTF-8-BOM files:
  `<stem>_transactions.csv` (taps + financial + read events) and
  `<stem>_trips.csv` (7 grouped journeys, with the nominal-fare note).
- No sample data is embedded; a failed/empty read cannot produce a fabricated
  card.

## 8. Tests

`tests/hidseos/tst_hidseos.cpp` uses synthetic fixtures only:

- T-money empty: balance `0`, 2 SFI 4 records, both marker `0x0720`,
  `purchaseCount() == 0`, no trips;
- Climate: card type `0xB5`, synthetic CSN, issue/expiry, pass
  `2026-07-13 → 2026-07-15 (3 days)`, 20 taps, 7 journeys,
  recorded total **15,250 KRW**, calibration **−6**, observed epoch
  **1989-06-08**, calibrated date **2026-07-15** while community date stays
  **2026-07-21**;
- Climate balance: `6D00`, unsupported, JSON `amount_krw` is `null`;
- unknown card type `0x7E` preserved with a warning;
- short/truncated purse, pass, trip and financial records are safe and raw
  bytes are preserved;
- duplicate taps are ignored and do not change 20/7/15,250;
- unknown SFI / status words are safe;
- JSON schema keys, CSV BOM + row count, raw byte preservation.

Result: **14 passed, 0 failed** (12 test functions + init/cleanup).

Full suite (`scripts/test.ps1`):

| Suite | Result |
|---|---|
| FeliCa | 22 passed |
| **hidseos** | **14 passed** |
| ntagmodel | 67 passed |
| amiibodata | 22 passed |
| lfparser | 59 passed |
| lfparser_stage2 | 126 passed |
| eMRTD (pytest) | 47 passed |
| git-audit | 17 passed, 0 failures |

## 9. Build

Commands used:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Configuration Release -Clean
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\test.ps1
```

Result:

```
Qt 5.15.19 / MinGW GCC 16.2.0
build/qt515-mingw64/release/release/Proxmark3GUI.exe
2,627,072 bytes, warnings 0, errors 0
```

Translation regeneration:

```powershell
C:\msys64\ucrt64\bin\lrelease-qt5.exe i18n\zh_CN.ts -qm i18n\zh_CN.qm
# 1325 translations, 0 unfinished
```

`lupdate` was not run. Both `.ts` files were edited directly; the generated
`.qm` was verified with a small `QTranslator` check (page, reader and `N/A`
lookups all resolve).

## 10. Privacy / license audit

- **No real card identity is in the repository.** Tests use UID `A1B2C3D4`
  and synthetic CSNs `9999000011112222` / `8888000022223333`, synthetic
  terminal/station bytes, and synthetic times. The fixture builder writes
  bytes programmatically; no real APDU log was copied.
- The only bundle sample retained is the 27-byte SFI 7 pass record, which the
  task explicitly allows (dates/product bytes, no identity data).
- Real UID/CSN/records were not added to source, tests, docs, or `.ts` files.
  (The original bundle remains outside this repository.)
- Reference facts: K-Transit (MIT), Metrodroid (GPL-3.0, facts only, no code
  copied), happybono balance reader (MIT/public-domain header), KS X 6924
  standard (not distributed). This implementation is new C++ code based on the
  documented offsets.
- No keys, no authentication, no write APDU, no MIFARE Classic, no cloning.

## 11. Limitations / unresolved

1. **No real card was read in this session.** There was no Proxmark3 device or
   client session available. The APDU flow and parsers are validated against
   the bundle logs and synthetic fixtures; hardware behaviour is untested.
2. **SFI 7 product-field discrepancy:** the prose table in `docs/02` places the
   product field at `[12..13]` with an extra zero byte. The raw APDU log,
   `climate_card.json`, and `parse_ksx6924.py` all place it at `[12..13]` after
   five zero bytes (sample result `0x0306`, unknown bytes `0E0A4300`). The
   implementation follows the raw log and the reference parser. The bundle was
   not modified.
3. Calibration is a per-card heuristic anchored to the SFI 7 pass end date. It
   cannot recover an epoch for a card without a pass record.
4. Grouping keeps the first boarding/alighting per transport per counter, the
   same as the reference Python parser. Multiple same-transport legs inside one
   counter are not split further.
5. SFI 11 and SFI 13 are displayed as raw bytes only; their semantics are not
   publicly documented.
6. Station/terminal IDs are shown as hex. The bundle has no station-name table.
7. SFI 3/4/5 are read up to 20 records (ring-buffer assumption). SFI 11 is
   read up to the documented 2 records.
8. Exports contain personal data (UID, CSN, travel times/amounts). The GUI does
   not redact exports; the user is responsible for handling them.
9. `i18n/en_US.ts` had pre-existing CRLF line endings from the Stage 8.x
   working tree; content changes are limited to the new contexts and the
   `HID SEOS` label.
10. No commit/push was performed; all changes are in the working tree.
