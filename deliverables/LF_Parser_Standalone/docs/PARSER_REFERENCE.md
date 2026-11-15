# Parser Reference

All card parsers implement `ILfParser` and are registered in
`LFParserRegistry`. T55xx is a separate read-only API.

Common rules:

- Input is client text; output is `LFParseResult`.
- Optional fields use `Lf::Optional<T>`; absent means absent, never 0.
- `status` is `Identified` when a core identity exists, `Partial` otherwise.
- Missing or inconsistent values produce `warnings`, not fabricated data.

---

## 1. EM4100Parser

| Item | Value |
|---|---|
| Protocol | `Lf::Protocol::EM4100` |
| Detects | `EM 410x ID …`, `EM 410x XL ID`, `EM410x`, `EM 410x` |
| Fields | `hexId`, `rawData`, `decimalId` (client `DEZ`), extras: RF, DEZ8/14/15, Electra, frame, CDVI |
| Example in | `EM 410x ID 0F00112233` |
| Example out | EM4100 / Identified / hex `0F00112233` / decimal `4276803383` (if `DEZ 10` present) |
| Limits | no local decimal conversion; `Pattern CDVI` values are extras only |

## 2. HIDProxParser

| Item | Value |
|---|---|
| Protocol | `Lf::Protocol::HIDProx` |
| Detects | `[NAME] … N-bit … FC:`, `HID Prox` + `FC:`, `found N matching M-bit` |
| Fields | FC, CN, bit length, raw, Issue, OEM, parity, all format candidates |
| Example in | `[H10301  ] HID H10301 26-bit  FC: 65  CN: 29334  parity ( ok )` |
| Example out | `H10301 (26-bit)`, FC 65, CN 29334, 26-bit, raw if present |
| Limits | multiple candidates must agree, otherwise `Ambiguous`; never defaults to H10301 |

## 3. AWIDParser

| Item | Value |
|---|---|
| Protocol | `Lf::Protocol::AWID` |
| Detects | `AWID - len:`, `AWID … Raw:`, `AWID ID found` |
| Fields | bit length, FC, CN, raw, Cansec/HCC40/RBH extras |
| Example out | `AWID 26-bit`, FC 50, CN 1234 |
| Limits | `-unknown-` length yields client-derived CN with warning |

## 4. IndalaParser

| Item | Value |
|---|---|
| Protocol | `Lf::Protocol::Indala` |
| Detects | `Indala (len N) Raw:`, `Indala (len N)`, `Fmt 26 FC/ Card`, `Indala … Raw` |
| Fields | raw, frame length, FC, CN, parity, Wiegand format, Internal/Heden/Keyscan |
| Example out | Indala 64-bit, raw `A0000000A0002021`, FC 123, CN 1337 |
| Limits | Raw is optional; Keyscan values are extras, not FC/CN |

## 5. IOProxParser

| Item | Value |
|---|---|
| Protocol | `Lf::Protocol::IOProx` |
| Detects | `IO Prox - XSF(`, `IO Prox SSF -`, `IO Prox … Raw`, `IO Prox` |
| Fields | XSF version, FC (hex→decimal), CN, raw; SSF version/FC/CN |
| Example out | `IO Prox XSF`, FC 101, CN 1337, raw `007859603059CDAF` |
| Limits | SSF has no raw; hex→dec conversion is documented in `facilityCodeHex` |

## 6. FDXBParser

| Item | Value |
|---|---|
| Protocol | `Lf::Protocol::FDXB` |
| Detects | `Animal ID:` or `Animal code` with `National Code`; `Raw ID Hex`; `FDX-B` |
| Fields | country (extra), national ID (CN + decimalId), raw, animal bit, data block, CRC, RUDI, reserved |
| Example out | country 999, CN 123456789012, raw 26 hex |
| Limits | country is **not** a facility code |

## 7. ParadoxParser

| Item | Value |
|---|---|
| Protocol | `Lf::Protocol::Paradox` |
| Detects | `Paradox - ID:` |
| Fields | hex ID, FC, CN, checksum, raw |
| Example out | FC 96, CN 40426, hex `0F555556` |
| Limits | checksum reported, not verified |

## 8. KeriParser

| Item | Value |
|---|---|
| Protocol | `Lf::Protocol::Keri` |
| Detects | `KERI - Internal ID`, `KERI NXT - Raw`, `(Des)Scrambled MS` |
| Fields | type, decimal ID or FC/CN, raw |
| Example out | `Keri Internal`, CN 12345; `Keri MS`, FC 1 CN 12544; `Keri NXT`, raw 32 hex |
| Limits | scrambled values are printed as-is with a warning |

## 9. VikingParser

| Item | Value |
|---|---|
| Protocol | `Lf::Protocol::Viking` |
| Detects | `Viking - Card` |
| Fields | hex card ID, raw |
| Example out | hex `1A2B3C4D`, raw `F200001A2B3C4D1A` |
| Limits | no decimal card number is printed by the client |

## 10. PyramidParser

| Item | Value |
|---|---|
| Protocol | `Lf::Protocol::Pyramid` |
| Detects | `Pyramid - len:` |
| Fields | bit length, FC, CN, raw |
| Example out | `Pyramid 26-bit`, FC 123, CN 4567 |
| Limits | unknown length keeps only the client CN |

## 11. NexWatchParser

| Item | Value |
|---|---|
| Protocol | `Lf::Protocol::NexWatch` |
| Detects | `NexWatch raw id`, `88bit id`, `NexWatch … Raw` |
| Fields | 88-bit decimal ID, hex, fingerprint, mode, raw |
| Example out | CN 521512301, raw `5600000000213C9F8F150C00` |
| Limits | raw id alone → Partial |

## 12. PrescoParser

| Item | Value |
|---|---|
| Protocol | `Lf::Protocol::Presco` |
| Detects | `Presco Site code`, `Presco … Full code` |
| Fields | site, user, full code, raw |
| Example out | FC 0, CN 57470, full `0031E07E` |
| Limits | full-code-only output → Partial/Identified with no FC/CN |

## 13. NedapParser

| Item | Value |
|---|---|
| Protocol | `Lf::Protocol::Nedap` |
| Detects | `NEDAP (variant) - ID:` |
| Fields | ID, variant, subtype, customer code, raw, second card |
| Example out | `NEDAP 64b`, CN 12345, subtype 1, cc 101 |
| Limits | raw optional |

## 14. GProxIIParser

| Item | Value |
|---|---|
| Protocol | `Lf::Protocol::GProxII` |
| Detects | `G-Prox-II - Len:` / `Unknown len:` |
| Fields | bit length, FC, CN, xor, raw |
| Example out | `G-Prox-II 26-bit`, FC 123, CN 1234, xor 141 |
| Limits | unknown length has no FC |

## 15. GallagherParser

| Item | Value |
|---|---|
| Protocol | `Lf::Protocol::Gallagher` |
| Detects | `GALLAGHER - Region:` |
| Fields | FC, CN, region, issue level, displayed, raw, CRC |
| Example out | FC 22, CN 3333, region 1, issue 1 |
| Limits | region is not a facility code; multi-line accepted |

## 16. PACParser

| Item | Value |
|---|---|
| Protocol | `Lf::Protocol::PAC` |
| Detects | `PAC/Stanley - Card:` |
| Fields | hex card value, raw |
| Example out | hex `CD4F5552`, raw 32 hex |
| Limits | no decimal CN printed; Stanley shares this parser |

## 17. NoralsyParser

| Item | Value |
|---|---|
| Protocol | `Lf::Protocol::Noralsy` |
| Detects | `Noralsy - Card:` |
| Fields | CN, year, raw |
| Example out | CN 112233, year 2000 |
| Limits | year optional |

## 18. JablotronParser

| Item | Value |
|---|---|
| Protocol | `Lf::Protocol::Jablotron` |
| Detects | `Jablotron - Card:` |
| Fields | hex card ID, printed code, raw |
| Example out | hex `1B669`, printed `1410-00-0011-2233` |
| Limits | card value is hex |

## 19. SecuraKeyParser

| Item | Value |
|---|---|
| Protocol | `Lf::Protocol::SecuraKey` |
| Detects | `Securakey - len:`, `Securakey Short - CN:` |
| Fields | bit length, FC (hex), CN, raw, Wiegand, parity |
| Example out | `SecuraKey 26-bit`, FC 53, CN 64169 |
| Limits | short variant has no FC/length |

## 20. Visa2000Parser

| Item | Value |
|---|---|
| Protocol | `Lf::Protocol::Visa2000` |
| Detects | `Visa2000 - Card` |
| Fields | decimal CN, raw |
| Example out | CN 112233 |
| Limits | — |

## 21. MotorolaParser

| Item | Value |
|---|---|
| Protocol | `Lf::Protocol::Motorola` |
| Detects | `Motorola - fmt:` |
| Fields | bit length, FC, CN, raw |
| Example out | `Motorola 26-bit`, FC 12, CN 3456 |
| Limits | **not emitted by `lf search`**; reader output only |

## 22. IDTECKParser

| Item | Value |
|---|---|
| Protocol | `Lf::Protocol::IDTECK` |
| Detects | `IDTECK Tag Found` |
| Fields | CN, hex ID, raw, checksum state |
| Example out | CN 4963871, raw `4944544B351FBE4B` |
| Limits | checksum failure is a warning, not a rejection |

---

## 23. T55xx (read-only, separate)

```cpp
LfT55xx::DetectInfo info = LfT55xx::parseDetect(text);
LfT55xx::Block0 block = LfT55xx::parseBlock0(QStringLiteral("00148040"));
```

| API | Output |
|---|---|
| `parseDetect` | detected, chip type, password flag, block0, modulation text, bit rate, offset, inverted |
| `parseInfo` | alias of `parseDetect` |
| `parseBlock0` | modulation, extend, bit-rate index/divisor, max block, flag bits, master key |

Block0 bit layout used:

| Bits | Meaning | Verified |
|---|---|---|
| 12–16 | modulation | mapped for known codes |
| 17 | extend | yes |
| 18–23 / 18–20 | data bit rate index | 0–7 mapped |
| 5–7 | max block | yes |
| 0–4 | flags | **unverified**, exposed + warning |
| 8–11 | master key | **unverified**, exposed + warning |

No command generation, no write path.

---

## 24. Failure behaviour

| Situation | Result |
|---|---|
| empty input | `InvalidInput`, warning |
| no protocol evidence | `Unidentified`, warning |
| protocol marker only | `Partial`, no identity |
| conflicting HID candidates | `Partial`, `Ambiguous` |
| invalid numeric field | field absent, rest parsed |
| multiple protocols | highest score only, deterministic tie-break |

---

## 25. Using a parser directly

```cpp
ILfParser *parser = LFParserRegistry::instance().parser(Lf::Protocol::Keri);
if (parser) {
    const LFParseResult result = parser->parse(normalizedText, originalText);
}
```

`parseAs(protocol, text)` performs normalization for you.
