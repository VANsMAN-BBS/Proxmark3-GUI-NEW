# Change Log

[中文](README.md#中文介绍)

### V0.5.0\n+ Add FeliCa transit page (PASMO/Suica read-only)\n+ Add Octopus balance read-only support\n+ Add HID SEOS page (T-money / Climate Card)\n+ Add localized FeliCa detail labels and station names\n+ Add workspace scripts and latest test package\n\n### V0.4.1
+ Fix eMRTD DG2 appearing to hang: APDU completion now waits for the client's `<<<` response marker instead of a fixed 350 ms quiet window (measured 0.552 s 鈫?0.142 s per APDU; a 20 KB DG2 drops from ~49 s to ~13 s)
+ Add real DG2 progress: byte-level `Reading DG2 (x / y bytes)` plus parsing / extracting / decoding / preview sub-stages; no synthetic progress timers
+ Report DG2 files beyond the 15-bit READ BINARY limit (32768 bytes) immediately with the actual size instead of failing after a long silent read
+ Decode portraits on a worker thread (QImage in a QThread, QPixmap on the GUI thread); cancel now works during DG2 receive and during decoding
+ Add a 15-second no-output watchdog that reports "Still working..." without fabricating progress
+ Normalize full-width/typographic MRZ characters (`銆奰, full-width digits) entered from Chinese documents
+ Move Amiibo into the NTAG group (`NTAG/Ultralight` 鈫?`NTAG` + `Amiibo`) and remove the incorrect JTAG container; Amiibo implementation is reused unchanged
+ Add an original Proxmark3 application icon: SVG source, 16鈥?56 px PNGs, a 7-frame ICO, EXE resource, window and taskbar icon
+ Complete the Chinese translation: 587 strings finished, 0 unfinished (including the v0.3.0 T55xx/NTAG leftovers); shorten four legacy labels that clipped at 125% DPI
+ Add 15 eMRTD protocol tests (DG2 progress, large DG2, oversized DG2, corrupt TLV, unsupported image, cancel, NDJSON framing, full-width MRZ)
+ Fix mixed-encoding results: the reader now writes NDJSON as UTF-8 bytes regardless of the Windows console code page (GBK results were silently dropped by the GUI, which looked like 
eader exited with code 0 on documents with Chinese names)

### V0.4.0
+ Add **eMRTD Reader** page: integrates the existing `emrtd_pm3` Python project for passports and HK/Macau permits (TD1/TD2/TD3 + national MRZ), with BAC, secure messaging and DG1/DG2/DG11/DG12 decoding kept in Python
+ Add a machine interface `emrtd_pm3 gui` (NDJSON progress on stdout, JSON request on stdin, cancel, exit codes); MRZ never appears on the command line
+ Hand the serial port over explicitly: the GUI stops its `ClientSession` before the reader starts and reconnects afterwards; only one `proxmark3.exe` owns the port
+ Ship a standalone Python runtime (`emrtd\`) built by `scripts/package-emrtd-runtime.ps1`; no user Python/MSYS2 install, works from a Chinese path
+ eMRTD UI: MRZ validation with field-level issues, real progress stages, cancel, personal data table, portrait display/zoom/fit/save (JPEG/PNG, no re-encoding for JPEG), copy field/all/MRZ, EF.COM/DG11/DG12/hex tabs
+ Add **JTAG** container tab with an explicit placeholder and an **Amiibo** sub-page (NTAG215 / Type 2)
+ Amiibo: built-in synthetic sample, import/save 540-byte images, hex view, size validation, Detect/Read/Verify via `hf mfu`, write with confirmation and read-back verification, emulation via `hf mfu eload` + `hf mfu sim -t 7`
+ Optional online catalog (configurable URL, metadata only, user-initiated); no commercial Amiibo binary data is bundled or redistributed
+ Add Chinese translations for the new eMRTD, Amiibo and session strings
+ Add `docs/EMRTD_INTEGRATION.md`, `docs/AMIIBO_COMPATIBILITY.md`, `docs/HARDWARE_VALIDATION_MATRIX.md` and stage reports

### V0.3.1
+ Add lightweight persistent logging: `%LOCALAPPDATA%\Proxmark3GUI\logs\Proxmark3GUI.log`, levels, timestamps, module names, size rotation (2 MiB x 5 by default, `PM3GUI_LOG_MAX_BYTES` / `PM3GUI_LOG_MAX_FILES` overrides). Falls back to `<app>/logs` when the user profile is not writable.
+ Log startup/exit, client path and version, connect/disconnect, session state, queued/running/finished commands, timeouts, client exit and file IO failures. Sensitive values (MRZ, BAC keys, APDU payloads, full Amiibo data) are never logged.
+ Route Raw commands through the same FIFO queue and `rem` sentinel as module commands so long commands can no longer interleave output.
+ Add explicit detached-command state for long/interactive commands (`hf sniff`, `hf 14a sniff`, hardnested, MIFARE simulation, LF tune, Show Waveform): the queue is blocked and the UI shows "Long command running" until Stop.
+ Fix queue cleanup on stop/crash: queued commands are failed explicitly instead of silently dropped, and no next command is written while a detached command may still run.
+ Recover automatically from a command timeout by restarting the client, so the command queue cannot stay blocked.
+ Log deliberate stops as normal events instead of "process crashed", and keep the GUI state in sync with real client/device state.

### V0.3.0
+ Target Iceman/RRG v4.23346; add `config_rrgv4.23346.json`
+ Add Core `ClientSession`: FIFO queue, `rem` completion sentinel, timeouts, UTF-8/ANSI/CR, process exit/error handling
+ Remove `QSerialPortInfo::isBusy()` disconnect polling
+ Fix `hf 14a info` UID/ATQA/SAK parsing
+ Increase `hf mf chk` timeout (v4.23346 takes ~27 s) and Nested timeout
+ Fix MIFARE simulation `--crack` -> `-x`
+ Fix Trace Load trigger and `trace save -1` buffer semantics
+ Harden MIFARE dump/key file IO and Access Bits bounds
+ Add confirmations for Restore/Wipe/Lock/Set Parameter
+ Restore the original T55xx page and implement verified Block 0 / `lf t55xx detect/info/config` experiments
+ Add NTAG/Ultralight page with page protection, read/write and verify
+ Add optional LF raw-sample waveform via Client GraphBuffer export
+ Add standalone dist packaging and Qt5.15 build guide (MSYS2 UCRT64)

### V0.2.8
+ Add support for Iceman/RRG repo v4.16717  
+ Fix some bugs  
+ Make it easier for testing this GUI across multiple clients  
+ Add support for Bluetooth and TCP connection  

### V0.2.7
+ Fix writing to Block 0 failure when using with RRG repo v4.15864  
+ Disable disconnection detection on Linux/macOS by default  
+ Fix a little bug in the config file  
+ Fix the Trailer Decoder  
+ Show more details in the Trailer Decoder  
+ Add dark theme(from https://github.com/ColinDuquesnoy/QDarkStyleSheet)  
+ Add support for customizable theme, opacity and fonts  
+ Fix translations  

### V0.2.6
+ Add support for Iceman/RRG repo v4.15864 [#37](https://github.com/wh201906/Proxmark3GUI/issues/37)  
+ Optimize mifare classic block writing logic  
+ Fix the default lf config  
+ Add feedback for the GUI failing to start the client  
+ Add feedback for the client failing to connect to PM3 hardware  
+ Detect PM3 hardware when searching serial ports  
+ Remove extra empty lines in raw command output  
+ Use embedded config files  
+ Remove the wait time between performing nested attack then switching to staticnested attack  

### V0.2.5
+ Fix bug [#28](https://github.com/wh201906/Proxmark3GUI/issues/28)  

### V0.2.4
+ Clone EM410x card to T55xx card  

### V0.2.3
+ Fix bug [#27](https://github.com/wh201906/Proxmark3GUI/issues/27)  
+ Try to support Non-ASCII path  

### V0.2.2
+ Load command format from external json file  
+ Fix bug [#20](https://github.com/wh201906/Proxmark3GUI/issues/20), [#21](https://github.com/wh201906/Proxmark3GUI/issues/21), [#22](https://github.com/wh201906/Proxmark3GUI/issues/22)  
+ Support Iceman/RRG repo v4.13441

### V0.2.1
+ Optimize MIFARE Classic reading logic  
+ Fix bug [#16](https://github.com/wh201906/Proxmark3GUI/issues/16)  
+ Fix bug [#15](https://github.com/wh201906/Proxmark3GUI/issues/15) partially (the path can contain spaces now)  

### V0.2
+ Use Dock widget for more flexible layout  
+ Support basic LF commands  
+ Fix a bug in RawCommand tab  

### V0.1.4
+ Optimize performance  
+ Optimize UI  
+ Search available ports automatically  
+ Add High-DPI support  
+ Support configuring environment variables by script  
(Useful when the client requires specific environment variables)  
+ All functions are compatible with Iceman/RRG repo(tested on v4.9237)  
+ Support specifying client working directory
+ Fix some bugs

### V0.1.3
+ Fix Trailer Decoder
+ Add feedback when writing selected blocks

### V0.1.2
+ Optimize read logic
+ Make UI Customizable
+ Save client path automatically
+ Add Trailer Decoder(Deprecated, plz use V0.1.3 or higher version)
+ Support read/write selected blocks
+ Support a few Iceman functions
+ Fix some bugs

### V0.1.1
+ Complete Mifare module(support simulate and sniff)

### V0.1
+ Able to deal with Mifare card and related files

### V0.0.1
+ a dumb version with a useless GUI and a serial choose box.
