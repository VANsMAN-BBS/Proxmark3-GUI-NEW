# Stage 9 workspace audit

Date: 2026-11-16

Branch: `upgrade/v0.4.1-polish` @ `3edca79`

Backup: `external backup directory` (7,827 files, 720.3 MB)

## 1. State before cleanup

```text
root dirs:   build-final, build-release-v041, build-runtime, build-stage0,
             build-stage8, build-sym, build-test-*, build-tests-felica,
             artifacts, dist, deliverables, tests, src, resources, docs,
             scripts, config, i18n, deploy, emrtd_pm3
tracked      no build/ or artifacts/ files
ignored      all build-*, dist/, test build dirs, artifacts/
```

Sizes before:

| Area | Files | MB |
|---|---:|---:|
| `.git` | 1,332 | 7.0 |
| `src` + `config` + `i18n` + `resources` | ~580 | 2.4 |
| `tests` (source + local builds) | 172 | 3.3 |
| `deliverables` (source + local builds) | 199 | 2.8 |
| root `build-*` directories | ~1,800 | 214.0 |
| `dist/` (old local package) | 1,399 | 260.4 |
| `artifacts/` (v0.4.1 release) | 1,101 | 174.2 |

## 2. Classification

| Class | Contents |
|---|---|
| SOURCE | `src/`, `config/`, `i18n/*.ts`, `resources/`, `tests/*` sources |
| BUILD_CONFIG | `src/Proxmark3GUI.pro`, `scripts/*.ps1` |
| BUILD_INTERMEDIATE | `build-*/**/*.o`, `moc/`, `rcc/`, Makefiles |
| BUILD_OUTPUT | `build-*/**/Proxmark3GUI.exe`, `dist/` |
| TEST_SOURCE | `tests/*` (QtTest, pytest, git-audit) |
| TEST_RESULT | `tests/lfparser_stage2/stage2-tests.log` |
| RELEASE | `artifacts/release/v0.4.1/win-x64/` (ZIP 52,499,142 B, SHA256SUMS) |
| TEMP | `GUIsettings.ini` in build dirs, local logs, `.pytest_cache`, `__pycache__` |
| LOG | `build-*/**/*.log`, `Proxmark3GUI.log` |
| UNKNOWN | none remaining after inspection |

Notable non-deletable items:

- v0.4.1 ZIP / SHA256SUMS: released assets; hash `211BD1F3…BEFB9DE`.
- `resources/felica/station_codes*.csv`: runtime data (MIT + conflict subset).
- `i18n/*.qm`: runtime translations, tracked.
- `deliverables/LF_Parser_Standalone`: separate deliverable package.

## 3. Moves performed

| Before | After |
|---|---|
| `build-final`, `build-release-v041`, `build-runtime`, `build-stage0`, `build-stage8`, `build-sym`, `build-test-*` | `build/legacy/<same name>` |
| `dist/` | `build/legacy/dist/` |
| `tests/<suite>/build/` | `build/legacy/tests/<suite>/` |
| `deliverables/.../build*` | `build/legacy/deliverables/` |
| `artifacts/release/v0.4.1/win-x64/` | `artifacts/publish/win-x64/v0.4.1/` |

No release asset was modified. The v0.4.1 ZIP was re-hashed after the move and
matches the published SHA256.

## 4. New layout

```text
build/
├── README.md
├── legacy/                       old Stage 0-8 trees (safe to delete)
└── qt515-mingw64/
    ├── debug/
    ├── release/
    └── tests/<suite>/
artifacts/
├── README.md
├── publish/win-x64/v0.4.1/       ZIP + SHA256SUMS + extracted package
├── test/{reports,logs,coverage}/
├── screenshots/
└── temp/
```

## 5. Rules

- Builds always use qmake shadow builds under `build/`.
- Debug, Release and tests never share object files.
- Test output goes to `artifacts/test/`; build output stays in `build/`.
- `artifacts/publish/` is append-only per version.
- `artifacts/temp/` and test logs are ignored by Git.
- Real card data never enters `artifacts/` or Git.

## 6. Known issues

- `scripts/translate-*.py` still contain absolute local paths (known from
  Stage 8; out of scope here).
- Old build trees are kept under `build/legacy/` rather than deleted. They are
  regenerable and can be removed after the new build is verified.
- `tests/lfparser_stage2/stage2-tests.log` is a zero-byte leftover; ignored.
