# Proxmark3GUI v0.3.0

A cross-platform Qt Widgets GUI for the Proxmark3 **Iceman/RRG** client.

This repository is a modernized fork of [wh201906/Proxmark3GUI](https://github.com/wh201906/Proxmark3GUI) v0.2.8. The original GUI layout, MIFARE workflow, Raw Command page and settings are preserved; the communication layer and selected features were upgraded for **Iceman v4.23346**.

> Target Client: **RfidResearchGroup/proxmark3 v4.23346**  
> Build stack: **Qt 5.15.x / qmake / Qt Widgets / C++11 / Windows**  
> The GUI does not implement USB or serial protocol itself. All device operations go through the official `proxmark3.exe` client.

---

## 1. Features

### Preserved / repaired

- Serial port discovery and client process connection.
- Raw Command terminal with history and output.
- MIFARE Classic page: data/key tables, read/write selected blocks, dump/restore, Check Default, Nested/StaticNested, Hardnested, Darkside.
- Chinese Magic Card read/write/wipe/lock/set parameter.
- MIFARE Classic simulation (updated for v4.23346 `-x`; `--crack` removed).
- HF sniff / HF 14A sniff.
- Trace Load / Save / List (v4.23346 `trace save -1` buffer semantics).
- LF page: search/read/sniff/tune/config.
- T55xx page and EM410x clone.
- Trailer/Access Bits decoder.
- Settings: client path, port, arguments, working directory, preload script, config file, language, theme, fonts, opacity.
- Docking layout and dark/light QSS.

### New

- **Core `ClientSession`**
  - one `QProcess` owner;
  - connection/version/firmware/model parsing;
  - FIFO command queue;
  - `rem` sentinel completion;
  - UTF-8 / ANSI / CR handling;
  - process exit/error detection;
  - no `QSerialPortInfo::isBusy()` disconnect polling.
- **NTAG / Ultralight page**
  - `hf mfu info`, `hf mfu rdbl`, `hf mfu wrbl`;
  - page table, read all/selected/single, edit, write, verify;
  - page protection for UID/lock/CC/config/OTP pages;
  - unknown layouts are read-only.
- **T55xx restored page**
  - original `mainwindow.ui` configuration area restored;
  - EM410x clone group restored;
  - Block 0 hex/bin editor with verified bit fields;
  - `lf t55xx detect/info/config` hooks;
  - unimplemented fields disabled and marked Experimental.
- **Optional LF waveform**
  - exports the Client GraphBuffer with `data save`;
  - QPainter drawing, zoom/pan/fit, sample text tab;
  - no fabricated time axis: `.pm3` export has no sample-rate metadata.
- **Config for v4.23346**
  - `config/config_rrgv4.23346.json` (default).

---

## 2. Verified status

Legend:

- **Verified (client/device)** — actually exercised against the specified client and real device.
- **Client-integrated** — command/parameters checked against v4.23346 help/source; not successful on real hardware.
- **Not verified** — no compatible tag/firmware for a success test.
- **Experimental** — UI/implementation present but hardware-verified status is incomplete.

| Area | Status |
|---|---|
| Build (Qt 5.15.19, MinGW 16.2.0) | Verified |
| Standalone dist without MSYS2 PATH | Verified |
| Chinese installation path startup | Verified |
| Connect to v4.23346 / version/model/firmware | **Verified (client/device)** |
| Disconnect / client crash detection | **Verified (client/device)** |
| Raw terminal / UTF-8 command text | Verified |
| `hf 14a info` UID/ATQA/SAK parsing | **Verified (client/device)** |
| `hf mf chk --1k` (27 s) | **Verified (client/device, read-only)** |
| MIFARE Classic successful read/write/attacks | **Not verified** (current card is HID SEOS/EMV) |
| MIFARE simulation | Client-integrated (`-x`); not executed |
| Trace GUI Load → Save round-trip | **Verified (client/device, SHA-256 identical)** |
| LF Read/Sniff | **Not verified on physical hardware** — current 256K firmware has no LF |
| T55xx | **Experimental / Not verified** — no LF firmware |
| NTAG / Ultralight | **Not verified** — no NTAG/UL tag |
| LF waveform | Verified with synthetic GraphBuffer; real LF not verified |

The connected 256K `PM3 GENERIC` firmware has no LF support (`hw status` has no LF sampling/T55xx sections; `lf read` returns “not available”). No firmware was flashed or modified.

---

## 3. Build

See **[BUILD_GUIDE.md](BUILD_GUIDE.md)** for the exact environment and packaging steps.

Quick build (MSYS2 UCRT64 Qt 5.15.19):

```powershell
$env:PATH = "C:\msys64\ucrt64\bin;" + $env:PATH
mkdir build
cd build
qmake-qt5 ..\src\Proxmark3GUI.pro -spec win32-g++ CONFIG+=release
mingw32-make -j4
```

Output:

```text
build\release\Proxmark3GUI.exe
```

A ready-to-run directory is generated at:

```text
dist\Proxmark3GUI\
```

It includes Qt DLLs, platform/style plugins and the required MSYS2 transitive DLLs.

---

## 4. Run

1. Start `dist\Proxmark3GUI\Proxmark3GUI.exe`.
2. Set **Client Path** to your `proxmark3.exe`.
3. Select the Proxmark3 **Port**.
4. Settings → Client:
   - Start arguments: `<port> -f` (the GUI also ensures `-f`);
   - Config file: `config_rrgv4.23346.json`;
   - Working directory: default `../data`;
   - Preload script only if the client needs environment variables.
5. Click **Connect**.

Expected status:

```text
HW Version: Iceman/master/v4.23346-suspect ... / PM3 GENERIC / ...
PM3: Connected
```

---

## 5. Page guide

### 5.1 MIFARE

Original workflow preserved. Destructive actions now ask for confirmation:

- Restore;
- Magic Card wipe;
- Lock UFUID;
- Set Parameter.

### 5.2 T55xx (Experimental)

- Original page restored, including Block 0 and Option Block editors.
- `Get from Data` / `Set to Data` encode/decode the verified Block 0 fields.
- `Detect` / `Read Info` call `lf t55xx detect` / `lf t55xx info`.
- `Apply` calls `lf t55xx config` with modulation/rate/offset/inverted/ST/q5.
- Fields whose bit layout is not verified are disabled, not faked.
- EM410x Read/Clone calls `lf em 410x reader` / `lf em 410x clone --id ... [--q5]`.

Requires LF-capable firmware. The current test device does not have LF.

### 5.3 NTAG / Ultralight

- `Detect / Read Info` → `hf mfu info`.
- `Read All` / `Read Selected` / `Read Page` → `hf mfu rdbl`.
- `Write Page` / `Write Selected` → `hf mfu wrbl`, followed by `rdbl` verification.
- Pages 0–3 (UID/lock/CC) are read-only.
- Config/OTP/key pages are read-only once `cfg0` reports the config start.
- Unknown card layouts disable writes.
- Optional key supports 4-byte (NTAG/EV1) or 16-byte (UL-C/AES) hex.

### 5.4 LF waveform

- Run LF Read/Sniff first on LF-capable firmware (or load a GraphBuffer with `data load`).
- Click **Show Waveform**.
- The GUI runs `data save -f pm3gui_lf_wave`, reads the `.pm3` text file and plots the raw samples.
- The `.pm3` file has no sample-rate metadata, so the x-axis is **sample index**, not a fabricated time axis.
- Decoded protocol text remains in RawCommand; it is never converted into a waveform.

---

## 6. Known limitations

- No real-hardware success test for MIFARE Classic read/write/attacks (current card is HID SEOS/EMV).
- No LF/T55xx success test: current 256K firmware is built without LF.
- No NTAG/Ultralight success test: no tag available.
- No persistent log file; RawCommand is the only output view.
- Raw/fire-and-forget commands do not enter the FIFO; long commands still require Stop.
- `keepClientActive` checkbox is retained for settings compatibility but no longer controls serial-port polling.
- T55xx option-block field semantics and some Block 0 fields remain disabled/unimplemented.
- No dynamic command discovery or multi-version compatibility framework (intentional scope limit).
- Qt 5 is end-of-life; Qt6 migration is out of scope.

---

## 7. Documentation

- [AUDIT_STAGE1.md](AUDIT_STAGE1.md)
- [ICEMAN_COMPATIBILITY_MATRIX.md](ICEMAN_COMPATIBILITY_MATRIX.md)
- [ARCHITECTURE_UPGRADE.md](ARCHITECTURE_UPGRADE.md)
- [TEST_REPORT.md](TEST_REPORT.md)
- [BUILD_GUIDE.md](BUILD_GUIDE.md)
- [CHANGELOG.md](CHANGELOG.md)
- Stage reports: `docs/STAGE0_REPORT.md` … `docs/STAGE5_REPORT.md`

---

## 8. Credits / License

Original project: [wh201906/Proxmark3GUI](https://github.com/wh201906/Proxmark3GUI).  
Proxmark3 client/firmware: [RfidResearchGroup/proxmark3](https://github.com/RfidResearchGroup/proxmark3).  
QDarkStyleSheet: [ColinDuquesnoy/QDarkStyleSheet](https://github.com/ColinDuquesnoy/QDarkStyleSheet) (MIT).

This project retains the original LGPL-2.1 license (`LICENSE`).  
The Proxmark3 client is GPLv3 and is used as a separate process; if redistributed with the GUI, its GPLv3 obligations apply.
