# Test Report

**Project:** LF Parser Standalone — Stage 2
**Framework:** QtTest (Qt 5.15.19)
**Compiler:** MinGW-w64 GCC 16.2.0 (MSYS2 UCRT64)
**Build:** qmake, clean rebuild with `-Wall -Wextra`
**Date:** 2026-11-14

---

## 1. Result

```
Totals: 126 passed, 0 failed, 0 skipped, 0 blacklisted
```

| Metric | Value |
|---|---|
| Test results | 126 |
| Passed | 126 |
| Failed | 0 |
| Skipped | 0 |
| Compiler warnings | **0** |
| Runtime | ~100 ms |

---

## 2. Group breakdown

| Group | Count |
|---|---|
| Normalizer | 5 |
| EM4100 | 11 |
| HID Prox | 14 |
| AWID | 7 |
| Indala | 8 |
| New protocols (data-driven: 25 standard + 9 missing-field + 6 invalid + 5 brand-only + 2 init) | 47 |
| IO Prox specific | 1 |
| FDX-B specific | 1 |
| Keri specific | 1 |
| SecuraKey specific | 1 |
| Pyramid specific | 1 |
| G-Prox-II specific | 1 |
| IDTECK specific | 2 |
| NexWatch specific | 1 |
| Motorola specific | 1 |
| Registry / dispatch | 12 |
| T55xx | 8 |
| Model semantics | 2 |
| init/cleanup | 2 |

Stage 1 contribution: **59** (all preserved, all passing).
Stage 2 contribution: **67**.

---

## 3. Coverage of required scenarios

The task requires 12 scenarios per parser. They are covered as follows:

| # | Scenario | How covered |
|---|---|---|
| 1 | Standard format | `newProtocols` rows (25 fixtures) |
| 2 | Field order variation | `em4100_fieldOrderIndependent`, `indala_fieldOrderReversed`, Gallagher multi-line |
| 3 | Optional field missing | `newProtocols_missingFields` (9 rows) |
| 4 | Invalid fields | `newProtocols_invalidValues` (6 rows) |
| 5 | Empty input | `registry_emptyInput`, `registry_whitespaceInput` |
| 6 | Unrelated text | `registry_unrelatedText`, `registry_noCard`, `registry_firmwareUnsupported` |
| 7 | Case variation | `em4100_lowercaseId`, `hid_lowercaseRaw`, `indala_lowercaseRaw`, `newProtocols_lowercaseHex` |
| 8 | CRLF / LF | `em4100_crlfInput`, `hid_h10301_crlf`, `newProtocols_crlf` |
| 9 | Unknown format | `hid_unknownFormatDoesNotDefault`, `hid_conflictingCandidates`, `pyramid_unknownLength`, `gproxii_unknownLength`, `awid_unknownLength` |
| 10 | Multiple protocols mixed | `registry_prefersStrongerSignature`, `hid_indalaKeywordDoesNotStealHid`, `idteck_winsOverHidLine`, `registry_multiProtocolTextIsDeterministic` |
| 11 | Abnormal raw data | `awid_invalidRawDoesNotCrash`, `newProtocols_invalidValues`, `t55xx_block0Invalid` |
| 12 | Bit-length mismatch | `pyramid_unknownLength`, `gproxii_unknownLength`, `awid_unknownLength`, `t55xx_block0ExtendedUnmapped` |

### 3.1 Negative tests

| Test | Expectation |
|---|---|
| `newProtocols_brandOnlyIsNotIdentified` (5 rows) | brand name alone → not Identified, no identity |
| `registry_unrelatedText` | Unidentified |
| `registry_firmwareUnsupported` | Unidentified |
| `newProtocols_invalidValues` (Partial rows) | no fabricated identity |
| `t55xx_notPartOfCardRegistry` | T55xx text does not become a card |

### 3.2 Dispatch tests

| Test | Expectation |
|---|---|
| `registry_allProtocolsRegistered` | exactly 22 protocols |
| `hid_conflictingCandidates` | `Ambiguous`, no FC/CN |
| `idteck_winsOverHidLine` | IDTECK beats incidental HID line |
| `registry_multiProtocolTextIsDeterministic` | same result twice |

---

## 4. Fixtures and provenance

Location: `tests/fixtures/<protocol>/`.

- 42 files total (15 Stage 1 flat fixtures + 27 Stage 2 protocol fixtures), plus `FIXTURES.md`.
- **All fixtures are synthetic**; none is a real device capture.
- Layout follows Iceman source; values are synthetic or taken from the LF-Audit
  historical samples where noted.

| Source class | Used for |
|---|---|
| Iceman client source [S] | all line layouts |
| v4.23346 binary strings [B] | label confirmation |
| LF-Audit v4.20728 samples [A] | HID FC/CN/raw, AWID, IO Prox, Gallagher, NEDAP, IDTECK values |
| Synthetic [C] | all other values and edge cases |

Detailed per-file provenance is in `tests/fixtures/FIXTURES.md` and the
Stage 2 section of that file.

---

## 5. Build verification

### 5.1 qmake (verified)

```powershell
$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"
cd build
qmake-qt5 ..\tests\lfparser_tests.pro
mingw32-make clean
mingw32-make -j4
release\tst_lfparser.exe
```

- clean rebuild succeeded
- `-Wall -Wextra`: 0 warnings
- exit code 0

### 5.2 CMake (provided, not executed here)

CMake files exist for library, tests and example. The authoring environment
only has the MSYS POSIX CMake build, which does not provide the
`MinGW Makefiles` generator; therefore the CMake path is unverified here.
See `README.md` §3.

### 5.3 Test binary size

`tst_lfparser.exe` ≈ 250 KB (static library linked in).

---

## 6. Not covered

| Item | Reason |
|---|---|
| Real device output | no LF-capable firmware |
| Client version drift (future) | needs future samples |
| Other client protocols (Destron/Trovan/Watchnet/ICT/TI/Fermax) | outside audit card list; Pending Evidence |
| Thread-safety stress | stateless design; not stress-tested |
| Memory tooling (ASAN/Valgrind) | not available in this environment |
| CMake build | toolchain limitation (see above) |

---

## 7. Reproduce

```powershell
cd deliverables\LF_Parser_Standalone\build
release\tst_lfparser.exe -o results.txt,txt
Get-Content results.txt | Select-String "Totals"
```

Expected:

```
Totals: 126 passed, 0 failed, 0 skipped, 0 blacklisted
```
