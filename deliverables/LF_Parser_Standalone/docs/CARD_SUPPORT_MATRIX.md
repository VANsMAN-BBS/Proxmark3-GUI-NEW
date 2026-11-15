# Card Support Matrix

Status legend:

- **Implemented** — parser exists and is covered by tests.
- **Partial** — some fields are parsed, but the protocol is not fully modelled.
- **Not Applicable** — no identity fields are available in client text, so a
  card parser would be meaningless.
- **Unsupported** — no sufficient client output evidence.
- **Pending Evidence** — plausible, but needs samples before implementation.

Evidence legend: **[S]** Iceman source, **[B]** v4.23346 client binary strings,
**[A]** LF-Audit historical sample, **[C]** synthetic fixture.

---

## 1. Audited card types

| Protocol | Stage 1 | Client output | Parser required | Parser class | Fields | Evidence | Test status | Limitation |
|---|---|---|---|---|---|---|---|---|
| **EM4100** | Implemented | `EM 410x ID`, XL, Electra, 128b, DEZ, CDVI | yes | `EM4100Parser` | hexId, decimalId, RF, frame, de-scramble extras | [S] `cmdlfem410x.c`, [B], [A] | 11 tests | decimal only if client prints it |
| **HID Prox / ProxII** | Implemented | `[NAME] … N-bit … FC: … CN: … parity` | yes | `HIDProxParser` | FC, CN, bit length, raw, Issue, OEM, parity | [S] `wiegand_formats.c`, [B], [A] | 14 tests | ProxII is not a distinct client format |
| **HID Corporate 1000** | Implemented | description contains `Corporate 1000` | yes (same class) | `HIDProxParser` | FC, CN, 35/48-bit format, raw | [S], [B] | 14 tests (shared) | depends on description text |
| **AWID** | Implemented | `AWID - len: N FC: … Card: … Raw:` | yes | `AWIDParser` | bit length, FC, CN, raw, Cansec/HCC40/RBH | [S] `cmdlfawid.c`, [B], [A] | 7 tests | `-unknown-` values are client-derived |
| **Indala** | Implemented | `Indala (len N) Raw:`, `Fmt 26 FC/ Card/ Parity` | yes | `IndalaParser` | raw, frame length, FC, CN, parity, Internal/Heden/Keyscan | [S] `cmdlfindala.c`, [B], [A] | 8 tests | Raw is optional by design |
| **IO Prox** | — | `IO Prox - XSF(vn)fc:cn, Raw:`, `IO Prox SSF` | yes | `IOProxParser` | version, FC (hex→dec), CN, raw | [S] `cmdlfio.c`, [A] | 4 tests | SSF has no raw |
| **FDX-B** | — | `Animal ID: cccc-nnnn`, `Animal code`, `CountryCode` | yes | `FDXBParser` | country, national ID, raw, animal bit, CRC, RUDI | [S] `cmdlffdxb.c`, [A] | 3 tests | country is **not** a facility code |
| **Paradox** | — | `Paradox - ID: … FC: … Card: …` | yes | `ParadoxParser` | hex ID, FC, CN, checksum, raw | [S] `cmdlfparadox.c`, [A] | 2 tests | checksum not verified locally |
| **Keri** | — | `KERI - Internal ID`, `MS - FC/Card`, `KERI NXT - Raw` | yes | `KeriParser` | type, CN/decimal ID, FC, raw | [S] `cmdlfkeri.c`, [A] | 3 tests | three variants modelled separately |
| **Viking** | — | `Viking - Card %08X, Raw:` | yes | `VikingParser` | hex card ID, raw | [S] `cmdlfviking.c`, [A] | 3 tests | no decimal card number printed |
| **Pyramid** | — | `Pyramid - len: N, FC/Card`, unknown variant | yes | `PyramidParser` | bit length, FC, CN, raw | [S] `cmdlfpyramid.c`, [A] | 3 tests | unknown length keeps only client CN |
| **NexWatch** | — | `raw id`, `88bit id`, `fingerprint`, `Raw` | yes | `NexWatchParser` | CN, 88-bit hex, fingerprint, mode, raw | [S] `cmdlfnexwatch.c`, [A] | 2 tests | raw id alone yields Partial |
| **Presco** | — | `Site code / User code / Full code / Raw` | yes | `PrescoParser` | site, user, full code, raw | [S] `cmdlfpresco.c`, [A] | 2 tests | full code only → Partial |
| **NEDAP** | — | `NEDAP (64b/128b) - ID … subtype … customer code` | yes | `NedapParser` | ID, variant, subtype, customer code, raw | [S] `cmdlfnedap.c`, [A] | 2 tests | raw optional |
| **G-Prox-II** | — | `G-Prox-II - Len … FC … Card … xor … Raw` | yes | `GProxIIParser` | bit length, FC, CN, xor, raw | [S] `cmdlfguard.c`, [A] | 3 tests | unknown length variant has no FC |
| **Gallagher** | — | `GALLAGHER - Region/Facility/Card No./Issue Level` | yes | `GallagherParser` | FC, CN, region, issue, displayed, raw, CRC | [S] `cmdlfgallagher.c`, [A] | 3 tests | region is not a facility code |
| **PAC / Stanley** | — | `PAC/Stanley - Card: %s, Raw:` | yes | `PACParser` | hex card value, raw | [S] `cmdlfpac.c`, [A] | 3 tests | no decimal card number |
| **Noralsy** | — | `Noralsy - Card: … Year: … Raw:` | yes | `NoralsyParser` | CN, year, raw | [S] `cmdlfnoralsy.c`, [A] | 3 tests | year optional |
| **Jablotron** | — | `Jablotron - Card: %x, Raw:`, `Printed` | yes | `JablotronParser` | hex card ID, printed code, raw | [S] `cmdlfjablotron.c`, [A] | 3 tests | card is hex, not decimal |
| **SecuraKey** | — | `Securakey - len … FC 0x… Card`, `Securakey Short` | yes | `SecuraKeyParser` | bit length, FC (hex), CN, raw, parity | [S] `cmdlfsecurakey.c`, [A] | 3 tests | short variant has no FC |
| **Visa2000** | — | `Visa2000 - Card %u, Raw:` | yes | `Visa2000Parser` | decimal CN, raw | [S] `cmdlfvisa2000.c`, [A] | 2 tests | |
| **Motorola** | — | `Motorola - fmt: 26 FC/Card, Raw:` | yes | `MotorolaParser` | bit length, FC, CN, raw | [S] `cmdlfmotorola.c` | 2 tests | **not printed by `lf search`**; reader only |
| **IDTECK** | — | `IDTECK Tag Found: Card ID … Raw … chksum` | yes | `IDTECKParser` | CN, hex ID, raw, checksum state | [S] `cmdlfidteck.c`, [A] | 3 tests | checksum only reported, not verified |
| **COTAG** | — | `[+] COTAG` marker only | **no** | — | none | [B], [A] | negative tests | Not Applicable: no identity fields |
| **EM4x50** | — | detection marker only | **no** | — | none | [B] | negative tests | Not Applicable: needs native reader command |
| **Hitag** | — | detection marker only | **no** | — | none | [B] | negative tests | Not Applicable: `lf hitag info` is a different command family |
| **T55x7 / T5577** | — | `Chip type`, `Password set`, `Block0`, `Modulation`, `Bit rate`, `Offset`, `Inverted` | separate module | `LfT55xx` | chip type, password flag, block0 bits, modulation, bit rate | [S] `cmdlft55xx.c`, [B], main `t55xx.cpp` layout | 8 tests | read-only; bits 0–4/8–11 unverified |

## 2. Disposition summary

| Status | Count | Types |
|---|---|---|
| Implemented | 22 | the parsers above |
| Not Applicable | 3 | COTAG, EM4x50, Hitag |
| Implemented as separate module | 1 | T55xx / T5577 |
| Unsupported / Pending Evidence | 0 within the audit list | — |

Audit list coverage: **22 / 22 parseable types implemented**, **3 / 3
non-parseable types explicitly dispositioned**, **1 / 1 chip type handled by a
separate read-only module**. No audited type is silently ignored.

## 3. Client protocols outside the audit list

`lf search` also prints Destron, Trovan, Watchnet, ICT, TI and Fermax.
They are **not** part of the LF-Audit card list and are **not implemented** here.

| Protocol | Status | Reason |
|---|---|---|
| Destron | Pending Evidence | formats not in audit matrix; needs samples |
| Trovan | Pending Evidence | same |
| Watchnet | Pending Evidence | same |
| ICT | Pending Evidence | same |
| TI | Pending Evidence | same |
| Fermax | Pending Evidence | same |

They are listed so future work can pick them up with the same pattern.

## 4. Shared parsers

| Shared class | Protocols |
|---|---|
| `HIDProxParser` | HID Prox, HID ProxII (client only reports Wiegand formats), HID Corporate 1000 |
| `PACParser` | PAC, Stanley |

Everything else has a dedicated class.
