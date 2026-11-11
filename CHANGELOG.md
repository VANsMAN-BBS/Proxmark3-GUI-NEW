# Change Log

[中文](doc/CHANGELOG/CHANGELOG_zh_CN.md)  

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