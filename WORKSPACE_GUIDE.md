# Workspace guide

Project root is the parent of `scripts/`. All commands below work from any
directory; the scripts resolve the project root themselves.

## Users

Download the official ZIP from the GitHub Release, extract it to a writable
directory and run `Proxmark3GUI.exe`. The GUI is separate from the Proxmark3
client and firmware; no build directory is needed.

## Developers

Requirements: Windows x64, Qt 5.15 (MSYS2 UCRT64), MinGW-w64, qmake.

```powershell
# Debug build  -> build/qt515-mingw64/debug/
.\scripts\build.ps1

# Release build -> build/qt515-mingw64/release/
.\scripts\build.ps1 -Configuration Release

# Remove generated build trees (artifacts/publish is untouched)
.\scripts\clean-build.ps1
```

The scripts locate `qmake-qt5` / `mingw32-make` from PATH or from
`C:\msys64\ucrt64\bin`. Missing tools produce exit code 2 and a readable error.

## Latest runnable test build

After every change, refresh the directly runnable package:

```powershell
.\scripts\update-latest-build.ps1
```

Output: `artifacts/latest/win-x64/Proxmark3-GUI-latest-win-x64/`

It builds Release, mirrors the verified v0.4.1 portable tree (Qt/MinGW
runtime, eMRTD Python runtime, licenses), replaces only `Proxmark3GUI.exe`,
removes local settings/logs, and writes `BUILD_INFO.txt` with the commit and
EXE SHA256. This directory is the one to launch for manual testing; it is not
a release and is ignored by Git.

## Testers

```powershell
# Build and run all suites
.\scripts\test.ps1

# Re-run existing test binaries without rebuilding
.\scripts\test.ps1 -SkipBuild
```

- Test binaries: `build/qt515-mingw64/tests/<suite>/`
- Reports: `artifacts/test/reports/<timestamp>/`
- Logs: `artifacts/test/logs/`

Suites: FeliCa, NTAG model, Amiibo data, LF parser, LF parser Stage 2, eMRTD
pytest, Git push audit self-tests.

Unit/parser tests use synthetic data. Real card reads (NTAG215, PASMO) are
separate hardware tests and are not implied by a green unit test.

## Release packaging

```powershell
.\scripts\package-release.ps1 -Version vX.Y.Z -StagingDir <prepared-dir>
```

The staging directory must already contain the deployable tree (EXE, Qt/MinGW
DLLs, translations, licenses, eMRTD runtime). The script refuses to overwrite
an existing version directory, writes the ZIP, `SHA256SUMS.txt` and
`MANIFEST.txt` under `artifacts/publish/win-x64/<version>/`.

Do not repackage or overwrite the published `v0.4.1` assets.

## Agents

- qmake entry: `src/Proxmark3GUI.pro`
- Build root: `build/qt515-mingw64/<config>`
- Tests: `tests/<suite>/*.pro`; reports under `artifacts/test/reports`
- Local Git only for this stage; no push.
- Before any push: `powershell -ExecutionPolicy Bypass -File scripts/audit-git-content.ps1`
- Policy: `docs/GIT_PUSH_POLICY.md`; hook installed via `scripts/setup-git-hooks.ps1`.
- Never commit real card dumps, UIDs, transaction records, local paths or
  build outputs.
