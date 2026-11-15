# Client Output Formats

Ground truth for the parsers. Evidence codes: **[S]** Iceman source
(`RfidResearchGroup/proxmark3`, master, 2026-11), **[B]** local
`proxmark3.exe` (v4.23346-suspect) strings, **[A]** LF-Audit historical sample.

> The exact commit of the local client (`9ce8a4b6c`) cannot be resolved
> upstream, so [S] is used for format semantics, not as a byte-for-byte claim
> about the binary. Where [A] and [S] differ, both variants are supported.

---

## 1. Common framing

Every line is prefixed by a status marker and may contain ANSI colors:

```
[=] informational
[+] success
[-] failure
[!!] error
```

The normalizer removes colors and normalizes line endings; parsers ignore the
marker.

Hex conventions:

| Style | Example | Used by |
|---|---|---|
| Concatenated, zero padded | `00785960` | IO Prox, AWID, Paradox, Viking |
| Concatenated, unpadded first word | `a0000000a0002021` | Indala |
| Space separated | `FF 82 0C A5` | FDX-B raw, NEDAP, G-Prox-II variant |
| Upper/lower mixed | `0f555556` | client specific |

---

## 2. EM4100

```
[+] EM 410x ID 0F00112233
[+] EM 410x XL ID 00112233445566778899AABB
[+] EM 410x ID 0F00112233 Electra 123
[+] EM 410x ID 0F00112233 on 128b frame with data 00000000000
[+] EM410x ( RF/64 )
[=] Unique TAG ID      : 0F00112233
[=]     DEZ 8          : 42768033
[=]     DEZ 10         : 4276803383
[=]     DEZ 14/IK2     : 00042768033833
[+] Pattern CDVI       : FC: 15 CN: 305
```

| Field | Meaning |
|---|---|
| `EM 410x ID` | 40-bit ID, 10 hex |
| `XL ID` | extended ID, 6 + 16 hex |
| `RF/n` | data rate divisor |
| `DEZ *` | client de-scramble values (not protocol fields) |
| `Pattern CDVI` | derived FC/CN pattern, **not** EM4100 fields |

Formats: [S] `cmdlfem410x.c`, [B], [A].

---

## 3. HID Prox

```
[+] [H10301  ] HID H10301 26-bit                FC: 65  CN: 29334  parity ( ok )
[+] [H10306  ] HID H10306 34-bit                FC: 200  CN: 60000  parity ( ok )
[+] [H10304  ] HID H10304 37-bit                FC: 118  CN: 123456  parity ( ok )
[+] [C1k35s   ] HID Corporate 1000 35-bit std   FC: 123  CN: 45678  parity ( ok )
[+] raw: 200078be5e1e
[+] found 1 matching 26-bit format
```

Layout from [S] `hid_print_card()`:

```
[%-8s] %-32s [FC: %u]  [CN: %u]  [Issue: %u]  [OEM: %u]  [parity ( ok|fail )]
```

| Field | Meaning |
|---|---|
| first bracket | Wiegand format name (`H10301`, `H10306`, `C1k35s`, `ind26`, …) |
| description | human name, contains `N-bit` |
| `FC:` | facility code |
| `CN:` | card number (may be absent for FC-only formats) |
| `Issue:` / `OEM:` | optional fields |
| `parity` | client validation result |
| `raw:` | 24 hex, HID transport frame |

Multiple format lines can appear for one card. The parser keeps all candidates
and only reports FC/CN if they agree.

[B] also contains older labels `HID Prox ID`, `HID-26........`.

---

## 4. AWID

```
[+] AWID - len: 26 FC: 50 Card: 1234 - Wiegand: 26409a4, Raw: 011db2881474411111111111
[+] AWID - len: 34 FC: 1 Card: 2 - Wiegand: 1a2b3c4d5e, Raw: ...
[+] AWID - len: 37 FC: 1 Card: 2 - Wiegand: ..., Raw: ...
[+] AWID - len: 50 FC: 4000 Card: 2248975 - Wiegand: ..., Raw: ...
[+] AWID - len: 33 -unknown- (1234) - Wiegand: ..., Raw: ...
[+] Farpointe/Cansec 37-bit - FC: 1 CN: 2
[+] Farpointe HCC40 - ID without parity: 1a2b3c4d
[+] RBH - CN: 2248975
```

- `len` = format bit length.
- `Card:` decimal.
- `Raw:` 24 hex (`%08x%08x%08x`).
- `-unknown-` variant has no FC; the parenthesised value is the client's card value.

[S] `cmdlfawid.c`, [B], [A].

---

## 5. Indala

```
[+] Indala (len 64)  Raw: a0000000a0002021
[+] Fmt 26 FC: 123 Card: 1337 Parity: 11
[+]   Printed....... __0000__  ( 0x0 )
[+]   Internal ID... 123456
[+]   Heden-2L...... 888
[+]   Keyscan 40010. FC: 12 CN: 1337
[+] Indala (len 224)  Raw: 80000001b23523a6c2e31eba3cbee4afb3c6ad1fcf649393928c14e5
```

- `len` is the frame length (64/224).
- `Fmt` is the Wiegand format (26).
- `Raw` uses `%x%08x`, so the first word is **not** zero padded.
- `Raw` is optional in practice; field order is not guaranteed.

[S] `cmdlfindala.c`, [B], [A].

---

## 6. IO Prox

```
[+] IO Prox - XSF(01)65:01337, Raw: 007859603059cdaf
[+] IO Prox SSF - 0165:01337
```

| Field | Meaning |
|---|---|
| XSF version | decimal |
| XSF facility | **hex** (`65` → 101) |
| XSF card | decimal, zero padded |
| SSF | version byte + facility byte + card |

[B] also has `IO PROX....... %08X` (raw-only fallback).

---

## 7. FDX-B

```
[+] Raw ID Hex: 28 58 99 7D 3B 9F 00 00 C0 CC 00 00 00
[+] Animal ID:     999-123456789012
[+] National Code: 123456789012
[+] CountryCode:   0999
[+] Animal Tag:    True
[+] CRC:           0x1A2B - [1A2B] - ok

[+] Animal code......... 999 123456789012
[+] National ID code.... 123456789012 ( 0x1CBE991A14 )
[+] Country code........ 999 - Test range
[+] Starting digit...... 1 ( 0x01 )
[+] Reserved field...... 0 ( 0x00 )
[+] Animal bit.......... True
[+] Data block flag..... False
[+] Trailer............. 0x000000
[+] RUDI-bit............ False
[+] CRC-16.............. 0x1A2B ( ok )
[+] Raw................. 28 58 99 7D 3B 9F 00 00 C0 CC 00 00 00
```

Country width differs (3 or 4 digits) between the two print blocks.

---

## 8. Paradox

```
[+] Paradox - ID: 0f555556 FC: 96 Card: 40426, Checksum: 6a, Raw: 0f55555695596a6a9999a59a
[+]  FSK/MAN raw : 98c6cc...
[+]          raw : 0f55555695596a6a9999a59a = (maxim crc8) 6a == 6a
```

---

## 9. Keri

```
[+] KERI - Internal ID: 12345, Raw: E000000080003039
[+] Descrambled MS - FC: 1 Card: 12544
[+] Scrambled MS - FC: 1 Card: 12544, Raw: E000000012345678
[+] KERI NXT - Raw: 00112233445566778899AABBCCDDEEFF
```

Different identity models; the parser reports an extra `keriType`
(`internal` / `ms` / `ms-scrambled` / `nxt`).

---

## 10. Viking

```
[+] Viking - Card 1A2B3C4D, Raw: F200001A2B3C4D1A
```

Card is a 32-bit hex ID, not a decimal card number.

---

## 11. Pyramid

```
[+] Pyramid - len: 26, FC: 123 Card: 4567 - Wiegand: 3e9d, Raw: 00112233445566778899aabbccddeeff
[+] Pyramid - len: 33 -unknown- Card: 4567, Raw: 00112233445566778899aabbccddeeff
```

---

## 12. NexWatch

```
[+]  NexWatch raw id : 0x40c00080
[+]      fingerprint : Quadrakey
[+]         88bit id : 521512301 (0x1f15a56d)
[+]             mode : 1
[=]  Raw : 5600000000213C9F8F150C00
```

---

## 13. Presco

```
[+] Presco Site code: 0 User code: 57470 Full code: 0031E07E Raw: 10D0000000000000000000000031E07E
```

---

## 14. NEDAP

```
[+] NEDAP (64b) - ID: 12345 subtype: 1 customer code: 101 / 0x065 Raw: FF 82 0C A5 89 60 F8 F3
[+] NEDAP (128b) - ID: 54321 subtype: 5 customer code: 123 / 0x7B
[+] Second Card: 54321
```

Raw is space separated.

---

## 15. G-Prox-II

```
[+] G-Prox-II - Len: 26 FC: 123 Card: 1234 xor: 141, Raw: fac2a38c2b081af008eb0ac2
[+] G-Prox-II - Unknown len: 33 xor: 141, Raw: 00 11 22 33 44 55 66 77 88 99 aa bb
```

Two client print paths exist; both concatenated and space-separated raw are
accepted.

---

## 16. Gallagher

```
[+] GALLAGHER - Region: 1 Facility: 22 Card No.: 3333 Issue Level: 1
[+]    Displayed: B22
[+]    Raw: 7FEAA35854B86B0D1A8CB120
[+]    CRC: 20 - 20 (ok)
```

Multi-line and single-line variants are supported.

---

## 17. PAC / Stanley

```
[+] PAC/Stanley - Card: CD4F5552, Raw: FF2049906D8511C593155B56D5B2649F
```

Card is a hex string (4–16 hex).

---

## 18. Noralsy

```
[+] Noralsy - Card: 112233, Year: 2000, Raw: BB0214FF0110002233070000
```

---

## 19. Jablotron

```
[+] Jablotron - Card: 1b669, Raw: FFFF00001122335C
[+] Printed: 1410-00-0011-2233
```

---

## 20. SecuraKey

```
[+] Securakey - len: 26 FC: 0x35 Card: 64169, Raw: 7FCB400001ADEA5344300000
[+] Wiegand: 006BF553 parity ( ok )
[+] Securakey Short - CN: 1234, Raw: 7FCB400001ADEA53
```

`FC` is printed in hex with `0x`.

---

## 21. Visa2000

```
[+] Visa2000 - Card 112233, Raw: 564953320001B66900000183
```

---

## 22. Motorola

```
[+] Motorola - fmt: 26 FC: 12 Card: 3456, Raw: A0000000A0002021
```

**Not called by `lf search`**; produced by `lf motorola reader`. The parser
accepts it wherever the text comes from.

---

## 23. IDTECK

```
[+] IDTECK Tag Found: Card ID 4963871 ( 0x4BBE1F ) Raw: 4944544B351FBE4B  chksum 0x35 ( fail )
```

`lf search` may also print a HID line afterwards; IDTECK wins by dispatch
priority.

---

## 24. T55xx (separate API)

```
[+] Chip type..... T55x7
[+] Password set...... No
[+] Block0........ 00148040
[+] Modulation........ ASK
[+] Bit rate......... RF/64
[+] Offset............ 0
[+] Inverted.......... No
```

Labels confirmed by [B]; block0 semantics cross-checked with the main project's
`t55xx.cpp` (see `PARSER_REFERENCE.md`).

---

## 25. Version differences

| Area | v4.20728 (audit samples) | v4.23346 family |
|---|---|---|
| HID | `HID Prox … FC/CN`, `raw:` | same plus `[NAME] … N-bit` format lines |
| EM4100 | `EM 410x ID`, `DEZ` | same |
| Indala | `Indala … Raw:` | `Indala (len N)  Raw:` |
| AWID | `AWID - len:` | same |
| IO Prox | `XSF(...)` | same, plus raw-only generic label in binary |
| T55xx | `Chip type.....` | same labels |

The parsers accept both old and current shapes where evidence exists; no
version detection is performed.
