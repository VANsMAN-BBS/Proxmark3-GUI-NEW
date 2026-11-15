# License and Reuse Notes

**Project license:** LGPL-2.1-or-later (inherited from Proxmark3GUI).
**Reference projects:** Phosphor (GPL-3.0-only), Iceman proxmark3 (GPL-3.0).

> Engineering record, not legal advice. Review with counsel before commercial
> distribution or any change of license.

## 1. Summary

| Question | Answer |
|---|---|
| Phosphor source copied? | **No** |
| Phosphor regex set copied? | **No** |
| Phosphor functions renamed and reused? | **No** |
| GPL-3.0 code linked into this module? | **No** |
| Third-party dependency added? | **No** (Qt5 Core only) |
| Project license changed? | **No** |

All files under `include/`, `src/`, `tests/`, `examples/` are independently
written for Proxmark3GUI. The module is distributed under LGPL-2.1-or-later.

## 2. What was referenced

### 2.1 Phosphor (read only, for behaviour and known defects)

| File | Used for |
|---|---|
| `src-tauri/src/pm3/output_parser.rs` | field inventory, dispatch order, failure behaviour, known defects |
| `src-tauri/src/pm3/command_builder.rs` | card-type ↔ command relationship (not used here) |
| `src-tauri/src/cards/types.rs` | data shape inspiration only |
| `docs/LF_AUDIT/LF_CARD_SUPPORT_MATRIX.md` (our own audit) | which types exist |

Nothing was copied. The Stage 2 dispatch (evidence score, single commit, no
cascade) and the HID candidate model are deliberate redesigns of the behaviour
described in the audit.

### 2.2 Iceman client source (read only, for output formats)

| File | Formats used |
|---|---|
| `client/src/cmdlfem410x.c` | `EM 410x ID`, `DEZ`, `Pattern CDVI` |
| `client/src/cmdlfhid.c`, `wiegand_formats.c` | `[NAME] … FC/CN`, `raw:`, `found N matching` |
| `client/src/cmdlfawid.c` | `AWID - len:` |
| `client/src/cmdlfindala.c` | `Indala (len N) Raw`, `Fmt` |
| `client/src/cmdlfio.c` | `IO Prox - XSF`, `IO Prox SSF` |
| `client/src/cmdlffdxb.c` | `Animal ID`, `CountryCode`, `Raw ID Hex` |
| `client/src/cmdlfparadox.c` | `Paradox - ID` |
| `client/src/cmdlfkeri.c` | `KERI - Internal ID`, `MS`, `KERI NXT` |
| `client/src/cmdlfviking.c` | `Viking - Card` |
| `client/src/cmdlfpyramid.c` | `Pyramid - len` |
| `client/src/cmdlfnexwatch.c` | `NexWatch raw id`, `88bit id`, `Raw` |
| `client/src/cmdlfpresco.c` | `Presco Site code` |
| `client/src/cmdlfnedap.c` | `NEDAP (64b/128b) - ID` |
| `client/src/cmdlfguard.c` | `G-Prox-II - Len` |
| `client/src/cmdlfgallagher.c` | `GALLAGHER - Region` |
| `client/src/cmdlfpac.c` | `PAC/Stanley - Card` |
| `client/src/cmdlfnoralsy.c` | `Noralsy - Card` |
| `client/src/cmdlfjablotron.c` | `Jablotron - Card`, `Printed` |
| `client/src/cmdlfsecurakey.c` | `Securakey - len`, `Securakey Short` |
| `client/src/cmdlfvisa2000.c` | `Visa2000 - Card` |
| `client/src/cmdlfmotorola.c` | `Motorola - fmt` |
| `client/src/cmdlfidteck.c` | `IDTECK Tag Found` |
| `client/local` client binary strings | label confirmation for v4.23346 |
| `client/src/cmdlft55xx.c` + local binary | T55xx labels (`Chip type`, `Block0`, …) |

Only `PrintAndLogEx` **format strings** were extracted. No C code, algorithm or
data structure was copied. These strings describe an interoperable text
interface.

### 2.3 Main project (read only, not modified)

- `src/module/t55xx.cpp` was read to cross-check the block0 bit layout used by
  this module. No code was copied; the layout is expressed independently in
  `src/lf/t55xxparser.cpp`.

## 3. Independent implementation evidence

| Area | Independent design |
|---|---|
| Data model | `LFParseResult` + `Optional<T>`; Phosphor used `HashMap` + synthetic `uid` strings |
| Dispatch | per-parser `detect()` score + single commit; no `contains` chain |
| HID | explicit Wiegand-candidate model with ambiguity handling; no default format |
| Indala | independent field regexes; Raw optional |
| All other parsers | one class per protocol, own detection thresholds and field mapping |
| T55xx | separate namespace, read-only, no command generation |

## 4. Per-parser provenance

| Parser | Client evidence | Phosphor-derived code | Notes |
|---|---|---|---|
| EM4100 | [S]/[B] | No | Stage 1 |
| HID Prox | [S]/[B] | No | Stage 1; redesigned |
| AWID | [S]/[B] | No | Stage 1 |
| Indala | [S]/[B] | No | Stage 1; redesigned |
| IO Prox | [S]/[B] | No | hex→dec done locally, documented |
| FDX-B | [S] | No | country kept as extra, not FC |
| Paradox | [S] | No | |
| Keri | [S] | No | three variants |
| Viking | [S] | No | hex ID only |
| Pyramid | [S] | No | known/unknown lengths |
| NexWatch | [S] | No | 88bit + raw |
| Presco | [S] | No | full/site/user |
| NEDAP | [S] | No | 64b/128b |
| G-Prox-II | [S] | No | known/unknown |
| Gallagher | [S] | No | multi-line tolerant |
| PAC/Stanley | [S] | No | hex card value |
| Noralsy | [S] | No | |
| Jablotron | [S] | No | |
| SecuraKey | [S] | No | long/short, hex FC |
| Visa2000 | [S] | No | |
| Motorola | [S] | No | reader output only |
| IDTECK | [S] | No | checksum warning |
| T55xx | [S]/[B]/main project layout | No | read-only, unverified bit semantics flagged |

Legend: [S] Iceman source, [B] local client binary strings.

## 5. Residual risk

| Risk | Level | Mitigation |
|---|---|---|
| Regex similarity due to fixed input grammar | Low | structure/naming/behaviour independently designed; this record retained |
| Client output text copyright | Very low | factual device output; fixtures synthetic |
| Future copying of Phosphor code | Medium | would force GPL-3.0 on the project; avoid |
| Redistributing `proxmark3.exe` | Medium | not done here; would add GPL-3.0 obligations |
| T55xx bit semantics | Technical, not legal | flagged unverified in API and docs |

## 6. Open questions

1. Any future port of Phosphor's wizard should follow the same independent
   implementation rule.
2. If the host application relicenses, re-check LGPL-2.1/Qt obligations.
3. No third-party code means no additional notices are required beyond the
   project license.
