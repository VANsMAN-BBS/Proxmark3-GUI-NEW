# artifacts/

Local deliverables and test output. Everything here is ignored by Git except
this README.

```text
artifacts/
├── publish/win-x64/v0.4.1/      released files (never overwrite; never re-zip)
├── test/reports/<timestamp>/    test logs from scripts/test.ps1
├── test/logs/                   Python / Git audit logs
├── test/coverage/               reserved for coverage output
├── screenshots/                 public screenshots only
└── temp/                        scratch exports; safe to clear
```

Rules:

- `publish/` keeps a released version's original ZIP and SHA256; new versions
  get a new directory. Do not regenerate or overwrite a published version.
- Never place real card dumps, UIDs, transaction data or private screenshots
  here. Those stay outside the repository.
- `temp/` is disposable; `test/` is disposable per run.
- Git push audit treats card dumps, large binaries and local paths as
  BLOCK/REVIEW even if they are ignored by `.gitignore`.
