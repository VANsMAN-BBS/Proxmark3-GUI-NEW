# Proxmark3 GUI v0.5.0

## Highlights

- FeliCa transit page: PASMO/Suica read-only balance, transaction history,
  gate history, station candidates and JSON/CSV export.
- Octopus (Hong Kong) read-only balance (service 0x0117, block 0), including
  available balance, deposit and total.
- HID SEOS page: T-money and Climate Card read-only card info, balance or N/A,
  transportation pass, raw transactions and grouped trips.
- Station names: Japanese original plus verified Chinese/English names
  (Wikidata/community data); unknown names fall back to Japanese.

## Fixes and improvements

- FeliCa history no longer stops at 20 entries; duplicate records are dropped.
- Octopus read fixed: active field without select (-a) instead of -s.
- FeliCa detail labels localized (zh_CN/en_US) by code; raw bytes unchanged.
- Workspace: build/, artifacts/, unified scripts for build/test/package and a
  local "latest" test package.
- Local Git push audit (BLOCK/REVIEW) remains enabled.

## Verification

- Clean build: Qt 5.15.19 / MinGW GCC 16.2.0, 0 warnings, 0 errors.
- Tests: FeliCa 22, HID SEOS 14, NTAG 67, Amiibo 22, LF 59, LF Stage2 126,
  eMRTD 47, Git audit 17.
- Real hardware: PASMO and Octopus read-only paths were exercised by the
  maintainer. T-money/Climate Card GUI integration is validated with sample
  data; real-card GUI validation for those cards is not claimed here.

## Limits

- Read-only. No writes, authentication, key recovery, cloning or emulation.
- Octopus: only service 0x0117 block 0; old 350-deposit cards unsupported.
- Climate Card: balance unsupported (6D00); date calibration is card-level
  (1989-06-14 default, -6 days for the verified sample).
- MIFARE Classic/LF/T55XX success paths remain hardware-unverified.
- Linux/macOS not verified.
