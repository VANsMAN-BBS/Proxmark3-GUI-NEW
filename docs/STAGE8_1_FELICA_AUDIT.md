# Stage 8.1 FeliCa audit

Date: 2026-11-16  
Base: `7dbd45f` (Stage 9), branch `upgrade/v0.4.1-polish`, ahead 7, no push.

## Read pipeline

`FelicaTab::readCard` → `FelicaReader`:

1. `hf felica reader` → IDm
2. `hf felica info` → card/system metadata
3. `hf felica raw -s -c` for services `0x008B`, `0x090F`, `0x108F`, `0x10CB`
4. blocks split by `FelicaChunks::plan` (15 max per request)
5. `CjrcProfile::parse` → `Transit::ReadResult`
6. UI tables / JSON / CSV

## Where the ~19-record limit comes from

- `CjrcProfile::services()` declares `0x090F` as **20 blocks** (the service's
  fixed size for CJRC cards).
- Reader reads blocks 0–14 and 15–19 (15 + 5). No page is skipped.
- Parser skips all-zero blocks and stops at the first completely blank slot;
  a card with one empty slot yields **19 valid records**.
- A second hard cap remained: `if (out.transactions.size() >= 20) break;`.
  It is redundant for a 20-block service but would hide extra blocks if a
  card/profile exposed more.

Conclusion: 19 is **card data** (20-slot ring buffer with one empty slot), not a
GUI page limit. The service cannot be read beyond block 19 on this profile;
block 20 returns status A8. Software cannot recover overwritten history.

## Language handling today

- Parser stores Japanese labels (`自動改札機`, `運賃支払`, …) in the model.
- UI prints those stored labels directly → Japanese mixed into zh/en UI.
- Station names are Japanese only; no zh/en mapping layer.
- JSON/CSV use stored labels (must stay stable).

## Plan (minimal)

1. Remove the redundant 20-record parser cap; de-duplicate identical 16-byte
   records; keep blank-slot skipping.
2. Add localized label lookup by numeric code for zh/en with generic localized
   fallbacks; parser/export keep Japanese labels.
3. Add `station_names_i18n.csv` (ja/zh/en/source/verified) for a small verified
   set; unknown stations show Japanese only.
4. UI detail re-renders from codes + current UI language; candidates grouped
   with source labels; raw hex stays unchanged.
5. Tests: 20+ records, duplicate removal, unknown codes, localized names,
   station fallback, export compatibility.
