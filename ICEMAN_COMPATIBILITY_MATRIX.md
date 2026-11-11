# ICEMAN_COMPATIBILITY_MATRIX.md

## v0.4.0 新增命令依赖

| 功能 | 命令 | 状态 |
|---|---|---|
| eMRTD 读取 | `hf 14a apdu -k/-sk` | Client 支持；真实证件未验证 |
| NTAG215 识别 | `hf mfu info` | Client 支持；无标签 |
| NTAG215 读取 | `hf mfu dump` | Client 支持；无标签 |
| NTAG215 写入 | `hf mfu restore` | Client 支持；无标签 |
| NTAG215 模拟 | `hf mfu eload` + `hf mfu sim -t 7` | Client 支持（8 KB 模拟内存）；未验证 |

**目标 Client：** Iceman v4.23346（Windows 64-bit）  
**上游 Tag：** `v4.23346`  
**Tag commit：** `dc0110c29d320e04b37ea436ee2924901407ae72`  
**本地 Client：** `Iceman/master/v4.23346-suspect 2026-09-18 00:22:18 9ce8a4b6c`  
**依据优先级：** 本地 Client 实际帮助/输出 > v4.23346 源码 > 文档  
**硬件：** 真实 Proxmark3，`PM3 GENERIC`，AT91SAM7S256，256K，`SKIP_LF` 构建

状态说明：

- **兼容**：命令/参数存在，GUI 路径已保留；未做硬件成功验证的另标注。
- **已修复**：旧行为与新 Client 不兼容，已修改。
- **未验证**：命令存在，但当前硬件/标签无法验证成功路径。
- **不可用**：当前固件/Client 构建不提供服务。
- **未实现**：GUI 没有该功能。

---

## 1. 连接 / 版本 / 基础设施

| GUI 功能 | 旧命令/假设 | v4.23346 实际 | 差异 | 状态 | 依据/修复 |
|---|---|---|---|---|---|
| 连接握手 | 初始输出含 `[=]`，`hw version` 输出 `os:` 或 `OS....` | 仍输出 `OS.........`；Client 横幅含 `[=]` | 小写 `os:` 不再是唯一形式 | **已修复** | `ClientSession` 用 `hw version` + `rem` 哨兵；`OS\.+` 解析 |
| Client 版本 | 未解析 | `Iceman/master/v4.23346-suspect ...` | GUI 原先不记录 | **已实现** | `ClientInfo` |
| Firmware/型号 | 未解析 | `OS.........` / `[ Model ] Firmware` / `uC:` | GUI 原先只显示 OS 片段 | **已实现** | `ClientSession::parseVersionOutput()` |
| 命令完成 | 固定超时 + 文本触发 | 管道模式无提示符 | 旧机制易误判 | **已修复** | `rem PM3GUI_<id>` 哨兵 + FIFO |
| 断开检测 | `QSerialPortInfo::isBusy()` | 无可靠串口占用语义（TCP/BT 更不适用） | 旧机制失效 | **已修复** | `QProcess::finished/errorOccurred` |
| 编码 | `toLatin1()` | Client/命令使用 UTF-8 | 中文路径损坏 | **已修复** | `QProcess::write(cmd.toUtf8())` + 增量 UTF-8 |
| ANSI/CR | 原样插入 | Client 可能输出 ANSI/CR | 解析/显示污染 | **已修复** | `cleanText()` |
| 中文命令参数 | Latin-1 | 实测 `rem 中文测试` 输出 UTF-8 | 旧通道损坏 | **已修复** | 实测通过（非硬件 LF 路径） |
| 启动参数 `-f` | 用户可删除 | 必需；否则输出缓冲 | 旧 GUI 不校验 | **已修复** | `ClientSession` 自动补 `-f` |

---

## 2. MIFARE Classic

| GUI 功能 | 旧命令 | v4.23346 命令 | 参数/输出差异 | 状态 | 依据/修复 |
|---|---|---|---|---|---|
| 卡信息 | `hf 14a info -nsv` | 仍存在 | `-n` 现为 `nacktest`，`-s` 为 `aidsearch`；UID 行带 `FNUID` 注释 | **已修复** | UID/ATQA/SAK 改按标签正则捕获；实测 `2F7B5454 / 0004 / 20` |
| 默认密钥检查 | `hf mf chk --1k` | 仍存在 | 实测耗时 **27 s**；旧超时 4.2 s | **已修复** | `chk` 超时 60 s；GUI 实测 27 s 完成 |
| Nested | `hf mf nested --1k --blk ... -a -k ...` | 仍存在 | 未发现参数删除 | **未验证** | 当前卡非 MF Classic；超时提高到 60 s |
| StaticNested | `hf mf staticnested ...` | 仍存在 | 未发现参数删除 | **未验证** | 当前卡非 MF Classic |
| Hardnested | `hf mf hardnested ...` | 仍存在 | `--tblk` / `--ta|--tb` 等保留 | **未验证** | 当前卡非 MF Classic |
| Darkside | `hf mf darkside` | 仍存在 | 无参数变化 | **未验证** | 当前卡非 MF Classic |
| 读块 | `hf mf rdbl --blk N -a|-b -k ...` | 仍存在 | 输出表格/数据格式未变（待卡验证） | **未验证** | 当前卡非 MF Classic |
| 读扇区 | `hf mf rdsc --sec N -a|-b -k ...` | 仍存在 | 同上 | **未验证** | 当前卡非 MF Classic |
| 写块 | `hf mf wrbl --blk N ... -d ... --force` | 仍存在 | `--force` 保留 | **未验证** | 未执行写卡 |
| Dump | `hf mf dump --1k` | 仍存在 | 默认文件名 `hf-mf-<UID>-dump.bin` | **未验证** | 未执行写/读全卡 |
| Restore | `hf mf restore --1k --force` | 仍存在 | 无 `-f` 时按当前 UID 选文件 | **高风险，已加确认** | 未执行；Stage 2 增加确认 |
| 仿真 | `hf mf sim ... --crack` | **`--crack` 已不存在** | 新参数 `-x` / `-y` / `-e` | **已修复** | `mf_sim_simdialog.cpp` 改 `-x`；`hf mf sim --help` 实测 |
| Magic Card 读/写 | `cgetblk/csetblk` | 仍存在 | 参数保留 | **未验证** | 需要 Magic Card |
| Magic Card 擦除 | `hf mf cwipe` | 仍存在 | 无 | **高风险，已加确认** | 未执行 |
| Magic Card 锁卡 | `hf 14a raw -ak ...` | 仍存在 | `-a/-k/-b` 保留 | **高风险，已加确认** | 未执行 |
| Magic Card 设置 UID | `hf mf csetuid --uid --atqa --sak` | 仍存在 | 参数保留 | **已校验输入，未写卡** | 长度/hex 校验 + 确认 |

---

## 3. Trace

| GUI 功能 | 旧命令/假设 | v4.23346 实际 | 差异 | 状态 | 依据/修复 |
|---|---|---|---|---|---|
| Trace Load | `trace load -f <file>`，等待 `"loaded"` | 输出 `Loaded ...` + `Recorded Activity (TraceLen = ...)` | 小写 `loaded` 不存在 | **已修复** | 新触发 `Recorded Activity`/`TraceLen`；哨兵保证完成 |
| Trace List | `trace list --buffer -t mf` | 仍支持 `-1/--buffer`、`-t` | 无 | **兼容** | 实测输出 |
| Trace Save（设备） | `trace save -f <file>` | 默认从设备下载 | 旧 GUI 以为保存 buffer | **已修复** | 设备 trace 仍用默认 |
| Trace Save（buffer） | 旧 `trace save -f` | 保存 buffer 需 `-1/--buffer` | 默认语义改变 | **已修复** | GUI 在 `loadSniff` 后加 `-1` |
| Trace 路径 | `prefs show` 的 `trace save path...` | 仍存在 | 无 | **兼容** | 实测 `prefs show` |
| 绝对路径 | 直接传绝对路径 | `trace save -f C:\...` 失败（`file not found or locked`） | 当前 Client 只接受可解析路径 | **规避** | GUI 使用客户端工作目录内的相对临时名，再复制到用户选择路径 |
| Trace round-trip | — | 3070 bytes，SHA-256 一致 | — | **已通过（真实设备，只读）** | `climate-r1.trace` GUI Load → Save |

---

## 4. LF

| GUI 功能 | 旧命令 | v4.23346 命令 | 差异 | 状态 | 依据/修复 |
|---|---|---|---|---|---|
| LF Read | `lf read -v` | 存在，但当前固件 `not available` | 256K firmware 无 LF | **不可用（当前硬件）** | `hw status` 无 LF sampling；`lf help` 无 read |
| LF Sniff | `lf sniff -v` | 同上 | 同上 | **不可用（当前硬件）** | 同上 |
| LF Tune | `lf tune --divisor N` | 参数存在 | 未发现删除 | **未验证** | 当前固件无 LF |
| LF Config | `lf config --divisor --bps --dec --avg --trig --skip` | 参数存在 | 未发现删除 | **未验证** | 当前固件无 LF |
| LF Search | `lf search -u` | 参数存在 | 未发现删除 | **未验证** | 当前固件无 LF |
| `hw status` LF 段 | `LF Sampling config` | 当前固件输出无该段 | firmware 裁剪 | **不可用（当前硬件）** | 实测 `hw status` |
| `data plot` | 调 Client 图形窗口 | Client 构建 `QT GUI support... absent` | 无图形窗口 | **不可用（当前 Client）** | `hw version` 实测 |
| 波形数据源 | 依赖 `data plot` | `data save -f` 导出 `.pm3` 文本（-127..127） | 需要 GUI 自绘 | **已实现** | Stage 5；合成 GraphBuffer 验证 |

---

## 5. T55xx

| GUI 功能 | 旧命令 | v4.23346 命令 | 差异 | 状态 | 依据/修复 |
|---|---|---|---|---|---|
| Detect | 旧 UI 无实现 | `lf t55xx detect` | 当前固件不可用 | **未验证** | Stage 3；help 实测 |
| Info | 旧 UI 无实现 | `lf t55xx info` | 当前固件不可用 | **未验证** | 同上 |
| Config | 旧 UI 无实现 | `lf t55xx config ...` | 当前固件不可用 | **未验证** | `CmdT55xxSetConfig` argtable |
| EM410x read/clone | `lf em 410x reader` / `clone --id --q5` | 仍存在 | 无 | **未验证（无 LF 固件/标签）** | Stage 3；`cmdlfem410x.c` |
| Block0 本地解码 | 旧 UI 无逻辑 | 本地 bit 编辑器 | — | **已实现并本地验证** | `00107060` → FSK2a/RF50/MaxBlock3 |
| Page1/Blk3 | 旧 UI 无逻辑 | 仅 hex/bin | 未实现语义 | **Experimental** | 控件禁用并提示 |
| T55xx read/write/dump | 未确认 | 当前 help 未展示 | 不猜测 | **未实现** | 需要 LF 固件再确认 |

---

## 6. NTAG / Ultralight

| GUI 功能 | 命令 | 参数/输出 | 状态 | 依据/修复 |
|---|---|---|---|---|
| 识别/信息 | `hf mfu info [-k]` | 支持 `-k` 4/16 bytes | **未验证（无标签）** | `hf mfu info --help` 实测 |
| 读页 | `hf mfu rdbl -b N [-k]` | 输出 `NN/0xNN | 4 bytes | ascii` | **未验证（无标签）** | `cmdhfmfu.c` + help |
| 写页 | `hf mfu wrbl -b N -d 8hex [-k]` | `-d` 4/16 bytes | **未验证（未写）** | `cmdhfmfu.c` + help |
| 复读验证 | `rdbl` after write | 比对 8 hex | **未验证** | Stage 4 实现 |
| 页面保护 | — | 0–3 只读；config/OTP 只读；未知型号禁写 | **已实现** | 源码常量 + cfg0 解析 |
| 型号页数 | — | UL 16 / ULC 48 / NTAG213 45 / 215 135 / 216 231 等 | **已实现（源码常量）** | `cmdhfmfu.c` `MAX_*` |

---

## 7. 其他

| 功能 | 状态 | 说明 |
|---|---|---|
| Raw Command | 兼容 | 保留终端、历史、清空；UTF-8/ANSI/CR 改善 |
| Settings | 兼容 | 客户端路径、参数、工作目录、环境脚本保留 |
| 配置 JSON | 新增 v4.23346 | `config_rrgv4.23346.json`；旧配置保留 |
| 日志文件 | 未实现 | 仅 Raw 窗口；Stage 6 未新增持久日志 |
| 动态命令发现 | 未实现 | 按项目负责人要求，不建立多版本/动态发现框架 |
| DESFire/iCLASS/ISO15693/FeliCa/Smart Card | 未实现 | 明确暂缓 |

---

## 8. 结论

- v4.23346 的核心命令集在源码层面基本保持；主要不兼容点是 **Trace 语义、`hf mf sim --crack`、连接/输出处理**。
- 已修复项：连接/版本、命令完成、Trace、仿真参数、MIFARE 信息/超时、文件 IO、UTF-8/ANSI/CR。
- 未能硬件验证项：MIFARE Classic 成功读写/攻击/仿真（卡不匹配）、LF/T55xx（固件无 LF）、NTAG/UL（无标签）。
- 当前 256K 固件无 LF，是本次测试的硬限制；不能通过刷写规避。
