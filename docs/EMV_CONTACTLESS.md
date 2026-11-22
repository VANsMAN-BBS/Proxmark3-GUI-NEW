# EMV Contactless — GUI integration

Status: implemented on branch `upgrade/v0.4.1-polish`. Read-only card access
(no writes, no authentication bypass, no cloning). Not committed.

This page exposes the existing
[`emv_log_reader`](../../emv_log_reader/README.md) integration as **More Features
→ EMV Contactless**. The APDU sequence and record parser are **not** reimplemented
in C++: the exact `pm3_emv_log.py` module is bundled and executed by Python.

## What is verified, and what is not

| Item | State | Evidence |
|---|---|---|
| `emv reader -v` command chain on a real Proxmark3 | verified by the earlier `emv_log_reader` work | device transcript in `emv_log_reader/sample_output_nocard.txt` |
| Real device **no-card** branch | verified (user, real device) | `stop_reason=no_card` |
| Real device **cancel / serial release** | verified (user, real device) | `stop_reason=cancelled`, next read works |
| 4-record parsing (BCD amounts, merchant 9F4E) | verified with the bundled mock only | `tests/emv` |
| CSV / JSON export | verified with mock records | `tests/emv` |
| Layout-mismatch warning | verified with a synthetic 45-byte record that is not BCD | `MOCK_PM3_BAD_LAYOUT=1` |
| **Real bank card record field layout** | **NOT verified** | no bank card was available; offsets are the hard-coded ones from `emv_log_reader` |
| Bank / issuer name decoding | not implemented | the page does not invent a value |

The page shows only the controls and status fields that are backed by the
reader: Read, Cancel, status, transaction table, details/warnings, CSV and JSON.

## Navigation

```
funcTab
├─ … LF / NTAG-UL / eMRTD / FeliCa
└─ More Features                 (index 5, same position as the old HID SEOS tab)
   ├─ Tmoney                     (the existing HidSeosPage, unchanged)
   └─ EMV Contactless            (new EmvPage)
```

The old top-level `HID SEOS` tab is gone; `HidSeosPage` itself was not modified
and is not duplicated. The container is inserted at index 5, immediately after
FeliCa, so existing `rawTabIndex` assumptions remain valid.

Translations: `More Features` / `更多功能`, `EMV Contactless` / `闪付`,
`Tmoney` stays `Tmoney`.

## Architecture

```
EmvPage (src/module/emv/emvpage.*)
  │  Read / Cancel / Export CSV / Export JSON
  │  signals: aboutToRead(), readFinished(), readingChanged(bool)
  ▼
EmvWorker (src/module/emv/emvworker.*)   — lives in its own QThread
  │  QProcess: python -W ignore -u emv_worker.py …
  │  JSON-lines events → Qt signals (recordReady / warningAdded / finished)
  ▼
emv_worker.py                           — thin JSON bridge
  │  imports the unchanged pm3_emv_log.EmvLogReader
  ▼
proxmark3.exe -p COMxx -c "emv reader -v"   (cwd = client directory)
```

Files:

| File | Purpose |
|---|---|
| `src/module/emv/emvtypes.h` | `Txn`, `ReadResult`, documented stop-reason check |
| `src/module/emv/emvruntime.h/.cpp` | extract bundled `.py` files, find Python 3, resolve runtime directory |
| `src/module/emv/emvworker.h/.cpp` | QThread worker, QProcess lifecycle, JSON event parsing, cancel/timeout |
| `src/module/emv/emvexport.h/.cpp` | CSV (UTF-8 BOM) and JSON export |
| `src/module/emv/emvpage.h/.cpp` | More Features → EMV Contactless page |
| `src/module/emv/python/pm3_emv_log.py` | parser copied from `emv_log_reader`; only the machine-specific default `proxmark3.exe` candidate was replaced with generic fallbacks |
| `src/module/emv/python/mock_pm3.py` | synthetic mock client used by the automated tests only; there is no GUI toggle |
| `src/module/emv/python/emv_worker.py` | JSON-lines bridge, stdin `cancel` handling, mock launcher shim |
| `resources/emv/emv.qrc` | embeds the three Python files under `:/emv/…` |
| `tests/emv/emv_tests.pro`, `tst_emv.cpp` | 12 QtTest cases |
| `scripts/test.ps1` | runs the `emv` Qt suite and the 11 Python parser tests |

## Runtime packaging

No third-party Python packages are required. The parser files are embedded in
the executable as Qt resources and extracted before the first read:

1. `PM3GUI_EMV_RUNTIME_DIR` (explicit override, also used by CI)
2. `%LOCALAPPDATA%/<app>/emv-runtime`
3. `%TEMP%/Proxmark3GUI-emv-runtime`
4. `<exe dir>/emv-runtime`
5. `<exe dir>/emv` if a package already contains the three files

Existing files are overwritten only when their content differs. A read-only
installation therefore still works via the Temp / AppData fallback.

Python lookup order:
`PM3GUI_EMV_PYTHON` → bundled `<exe dir>/emv/python.exe` → `python3` → `python`
→ per-user `%LOCALAPPDATA%\Programs\Python\Python3*\python.exe` → `py -3`.
Each candidate except the explicit override is probed with `-c print('emv-ok')`
so a broken `WindowsApps` alias is skipped.

The mock launcher (`mock_pm3_runtime.bat` / `.sh`) is generated at runtime in the
extracted directory. The repository does **not** contain `mock_pm3.bat` or any
machine-specific path from `emv_log_reader`.

## Worker protocol

`python -W ignore -u emv_worker.py` prints one UTF-8 JSON object per line:

```json
{"event":"started","port":"COM14","exe":"…","mock":false}
{"event":"record","txn":{"date":"2026-06-15","time":"09:32:41",
  "merchant":"PSBC-ATM","amount":"500.00","record_no":1,"raw":"…"}}
{"event":"result","stop_reason":"done","card_detected":true,
 "returncode":0,"count":4,"warnings":[]}
{"event":"fatal","message":"…"}
```

stderr carries diagnostics only and is collected for the Details panel.

`stop_reason` values are exactly the documented ones from
`emv_log_reader/README.md §5.2`:

| Value | UI text |
|---|---|
| `done` | Done — card read finished |
| `no_card` | No card detected — hold the card against the antenna and try again |
| `cancelled` | Cancelled — records read so far were kept |
| `timeout` | Timed out — partial records were kept |
| `error` | Failed — see Details |

Cancel writes `cancel\n` to the bridge, which calls `EmvLogReader.cancel()`.
If the process does not exit within 1.5 s, `QProcess::terminate()` is called; a
later fallback kills it. An overall hard deadline (`reader timeout + 20 s`)
also kills a stuck process and reports `timeout`.

## Serial exclusivity

Only one process may own the COM port:

1. `EmvPage::readCard()` emits `aboutToRead()` **before** the worker starts.
2. `MainWindow` saves `pm3->isConnected()`, calls `pm3->stopClient()` (which
   waits for the process to exit), then calls `setEmvReadActive(true)`.
3. While active, every other page dock is disabled (the pages live in
   `QDockWidget`s after `dockInit()`), the **Tmoney** sub-tab is disabled, and
   Connect / Disconnect / Stop / Raw Send are disabled and guarded in their
   slots.
4. The EMV page itself stays enabled so **Cancel** is clickable.
5. `readFinished()` re-enables everything and calls `pm3->restartClient()` only
   if the client was connected before the read.

Only user-triggered reads write anything to disk (the explicit CSV/JSON export).
No card data is logged or uploaded automatically.

## Using the page

1. Connect the Proxmark3 normally (optional; the reader can auto-detect a port).
2. Open **More Features → EMV Contactless**.
3. Hold the card against the antenna, press **Read Transactions**.
4. Records appear incrementally in the table.
5. **Cancel** keeps rows read so far.
6. **Export CSV** / **Export JSON** open a save dialog; nothing is exported
   without that dialog.

CSV columns: `date,time,merchant,amount,record_no` (UTF-8 with BOM, Excel-ready).
JSON contains metadata (`experimental`, `read_only`, `read_status`) and a
`transactions` array with `date/time/merchant/amount/record_no` (plus `raw` only
if exported through `EmvExport::toJson(result, true)`; the UI uses the default,
which omits raw record bytes).

## Test-only mock (not exposed in the UI)

The synthetic `mock_pm3.py` client and the `EmvWorker::setMockMode()` entry point
are kept for the parser/worker unit tests. The GUI has no Mock checkbox, mock
state, mock data, or mock-only labels; a user can only run the real reader.

The test-only environment switches understood by the copied mock are:

| Variable | Effect |
|---|---|
| `MOCK_PM3_NO_CARD=1` | only the no-card failure branch |
| `MOCK_PM3_SLOW=1` | 0.15 s per line, for cancel tests |
| `MOCK_PM3_BAD_LAYOUT=1` | one length-valid record with non-BCD fields → layout warning |
| `MOCK_PM3_STDERR=1` | writes the whole second record to stderr (proves stderr merging) |

## Tests

QtTest (`tests/emv`, simulated client, no hardware):

- 4 records / incremental callbacks / started port
- BCD amounts and 9F4E merchant field
- no-card branch
- layout-mismatch warning
- CSV export
- JSON export
- stderr mixed with stdout
- cancel keeps partial rows, then a second reader restarts successfully
- no leftover process (`isProcessRunning() == false`, PID 0)
- runtime extraction fallback without an override

```
powershell -File scripts\test.ps1
```

The suite also runs the original 11 Python parser tests from the sibling
`emv_log_reader/test_parse.py` when that folder exists (skipped with a message
otherwise, because the fixture contains a local `.bat` and machine-specific
sample logs that must not be copied into the app).

## Real-card checklist (still required)

1. Hold the card on the antenna centre, press Read, expect 2–5 s and
   `stop_reason=done`.
2. Compare date / time / amount with the POS receipt or bank app. Merchant may
   be truncated.
3. If the card is detected but no rows appear, open **Details**:
   `9F4F` in a warning means the fixed layout does not match this card.
4. Re-read once to confirm the COM port was released.
5. Save the raw client transcript and the warning text before changing offsets.
   Do not “fix” the hard-coded layout from a single card.

## Troubleshooting

| Symptom | Meaning / action |
|---|---|
| `Python 3 was not found` | install Python 3 or set `PM3GUI_EMV_PYTHON` |
| runtime extraction warning | AppData/Temp blocked; the app tries `<exe dir>` next |
| `no_card` | card not on the antenna, or not an EMV card |
| `done` with 0 rows + `9F4F` warning | different record layout; needs 9F4F work, not verified here |
| port busy | another client/reader still owns COM; close it, then retry |
| Chinese text in Details | the copied parser’s warnings are Chinese; UI labels are localized |

## Privacy / safety

- Read-only APDUs (`SELECT`, `GPO`, `READ RECORD`); no writes, no
  authentication bypass, no keys, no cloning, no MIFARE Classic path.
- Tests use synthetic dates/amounts/merchants only (fictional `PSBC-ATM`,
  `12345678`, `ATM`). No real card or bank data is stored in the repository.
- Export happens only after the user chooses a destination file.
