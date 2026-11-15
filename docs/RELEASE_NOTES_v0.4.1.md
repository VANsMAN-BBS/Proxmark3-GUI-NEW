# Proxmark3 GUI v0.4.1

## Overview

Proxmark3 GUI v0.4.1 is a Windows x64 Qt 5 desktop front-end for the
Proxmark3 Iceman/RRG client. The GUI does not implement USB/serial or RF
protocols itself: it starts and supervises a separate `proxmark3.exe` process
and parses its output.

The verified compatibility baseline is **Iceman/RRG client v4.23346** with a
Proxmark3 `PM3 GENERIC` / AT91SAM7S256 / 256 KB firmware. Other client versions,
firmware builds and device models were not verified for this release.

## Highlights

- **MIFARE** — Classic 1K/2K/4K tables, read/write, dump/restore, default-key
  check, Nested/StaticNested/Hardnested/Darkside, Magic Card operations,
  simulation parameters and Trailer decoder.
- **LF** — unified LF entry with LF Functions, LF Parser and T55XX:
  - LF Functions: config/search/read/tune/sniff and optional waveform view.
  - LF Parser: offline parsing of Proxmark3 LF text for EM4100, HID Prox,
    Indala, AWID, FDX-B, T55XX and other registered formats.
  - T55XX: Block 0/option editor, detect/info/config and EM410x clone
    (Experimental; requires LF-capable firmware).
- **NTAG / Ultralight** — async reads, page editing, single/multi-page writes,
  dirty-page writing, read-back verification, erase workflow and verify.
  UID/lock/CC/OTP/configuration/PWD-PACK pages are protected from normal writes.
- **NDEF / Quick Launch** — NDEF parsing/display and experimental NDEF URI
  writing (phone compatibility not fully verified).
- **Amiibo** — NTAG215 image library, import/save, optional metadata catalog,
  write/read-back verification and `hf mfu eload` + `hf mfu sim -t 7`
  emulation. No commercial Amiibo binary data is bundled.
- **eMRTD** — MRZ validation, BAC, secure messaging, DG1/DG2/DG11/DG12,
  portrait display/save and field/MRZ copy. Includes a standalone Python
  runtime; real documents were not verified.
- **Communication core** — single client process owner, FIFO queue, `rem`
  sentinel completion, UTF-8/ANSI/CR handling, command timeouts and detached
  long-running commands.
- **UI** — MIFARE, LF, NTAG/UL, eMRTD, RawCommand and Settings navigation,
  dock layout, dark/light themes, Chinese/English translations and persistent
  logging.

## Compatibility

| Item | Status |
|---|---|
| Windows 10/11 x64 | Build and runtime verified |
| Qt | 5.15.19 (MSYS2 UCRT64, OpenSource) |
| Compiler | MinGW-w64 GCC 16.2.0 |
| Build system | qmake, `win32-g++`, Release |
| Client baseline | Iceman/RRG v4.23346 |
| Test hardware | PM3 GENERIC, AT91SAM7S256, 256 KB, COM14 |
| Linux / macOS | Not verified |

The 256 KB test firmware has no LF support. LF/T55XX radio success paths could
not be hardware-verified on that firmware. Other firmware/device combinations
may have different capabilities.

## Installation

1. Download `Proxmark3-GUI-v0.4.1-win-x64.zip`.
2. Extract it to a writable directory.
3. Obtain a compatible Proxmark3 client separately; no client or firmware is
   included in this package.
4. Start `Proxmark3GUI.exe`.
5. Set **Client Path** to your `proxmark3.exe`.
6. Select the Proxmark3 serial port.
7. In **Settings → Client**, set:
   - Start arguments: `<port> -f`
   - Config file: `config_rrgv4.23346.json`
   - Client working directory: your client's directory/data directory
8. Click **Connect**, verify version/firmware, then start with read-only
   detection.

The `emrtd\` folder contains the standalone Python runtime used by the eMRTD
page. No system Python installation is required.

Verify the download with `SHA256SUMS.txt` before running it.

## Known limitations

- MIFARE Classic read/write/attack success paths were not hardware-verified on
  the available non-Classic test card.
- LF/T55XX radio functions were not hardware-verified because the test firmware
  lacks LF.
- NTAG213 and NTAG216 were not hardware-verified. NTAG215 reads, writes and
  read-back verification passed on a test tag; the erase workflow was tested
  only through snapshot/plan/cancel on 3 pages, not a full 126-page erase.
- Amiibo emulation was not tested with a game console. No commercial Amiibo
  binary data is bundled.
- eMRTD protocol/UI tests used FakeChip and synthetic documents; a real
  passport or HK/Macau permit was not available.
- The GUI requires an external Proxmark3 client and firmware. Commands may fail
  or be unavailable depending on the client/firmware build.
- Write, restore, erase, lock and configuration operations can be irreversible.
  Use only authorized tags and devices.
- Linux and macOS builds were not verified.

## License

Proxmark3 GUI is licensed under **GNU LGPL-2.1-or-later**. See `LICENSE`.

Third-party notices and license texts are in `THIRD-PARTY-NOTICES.txt` and the
`licenses\` directory. QDarkStyleSheet is MIT/CC-BY-4.0; Qt is dynamically
linked under LGPL-3.0; the bundled Python runtime, PyCryptodome and Pillow keep
their respective licenses. The Proxmark3 client and firmware are separate
GPLv3 projects and are not included.
