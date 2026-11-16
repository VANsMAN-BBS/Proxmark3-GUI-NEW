# Stage 11 — v0.5.0 Release Report

- Version: v0.5.0
- Release: https://github.com/VANsMAN-BBS/Proxmark3-GUI-NEW/releases/tag/v0.5.0
- Release ID: 447160243
- Tag commit: `fbaa89b6661468ed61dc4d4e54ebdd88d9eaf7ae`
- Branch: `upgrade/v0.4.1-polish`

## Assets

- `Proxmark3-GUI-v0.5.0-win-x64.zip` — 52,851,489 bytes
- SHA256: `1E1CB2FC8C944F02004AC4063EDC7B29046DEF94CDB2D686C5EF06021F1DA005`
- `SHA256SUMS.txt`

GitHub download was re-hashed: local == downloaded == above SHA256.

## Build

Qt 5.15.19 / MinGW GCC 16.2.0, qmake Release. 0 warnings, 0 errors.
EXE 2,627,072 bytes.

## Tests

FeliCa 22, HID SEOS 14, NTAG 67, Amiibo 22, LF 59, LF Stage2 126,
eMRTD 47, Git audit 17. All passed.

## Contents

FeliCa transit page (PASMO/Suica), Octopus balance, HID SEOS page
(T-money / Climate Card), localized station names, workspace scripts,
latest test package.

## Security

- Real card identifiers and commands were removed from public reports.
- No real UID/CSN/APDU logs or card dumps in the repository or release ZIP.
- Git push audit: ALLOW 70, BLOCK 0, REVIEW 0 before push.

## Limits

- T-money/Climate Card GUI was validated with sample data, not real cards.
- Octopus: 0x0117 block 0 only; old 350-deposit cards unsupported.
- Climate Card balance is N/A (6D00); calibration is card-level (-6 days).
- No writes/auth/cloning/emulation.

## Git

Start HEAD `7dbd45f`; release commit `fbaa89b`; push fast-forward
`688e38d..fbaa89b`. Working tree clean.
