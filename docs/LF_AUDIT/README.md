# LF-Audit — Phosphor LF 功能拆解与 Proxmark3GUI 移植可行性审计

**审计阶段：** LF-Audit（只读审计，不实施移植）
**审计日期：** 2026-11-14
**主项目：** `E:\Opencode_project\Proxmark3GUI-master`（v0.4.1）
**参考项目：** `E:\Opencode_project\phosphor-master`（Phosphor 1.1.0）
**当前 PM3：** AT91SAM7S256 / 256 KB Flash / Iceman v4.23346-suspect

---

## 1. 审计范围

本次审计只做四件事：**阅读源码、核对 Client/固件证据、在合成数据上做离线验证、写报告**。

明确**没有**做的事（受阶段约束）：

- 未修改 Proxmark3GUI 任何 `.cpp/.h/.ui/.pro/config/i18n` 文件。
- 未修改、重编译或刷写 PM3 固件。
- 未对任何真实卡片执行写入、擦除、锁定或模拟。
- 未在真实设备上执行 LF 读取（当前固件本身不含 LF，见 `PM3_HARDWARE_COMPATIBILITY.md`）。
- 未把 Phosphor 的任何依赖（Rust / Tauri / React / XState / npm）引入主项目。

本阶段唯一产出是 `docs/LF_AUDIT/` 下的 12 份文档。

---

## 2. 审计对象的基线核实（实测）

### 2.1 主项目 Proxmark3GUI

| 项目 | 实测值 | 命令 |
|---|---|---|
| Git HEAD | `245b184` | `git log -1` |
| 分支 | `upgrade/v0.4.1-polish` | `git status` |
| 工作区 | 干净（无未提交修改） | `git status --short` |
| 标签指向 | `upgrade-v0.4.1` → `245b184` | `git tag --points-at HEAD` |
| 提交说明 | `fix: keep eMRTD protocol output UTF-8 on Windows pipes` | |
| Remote | **无配置 remote** | `git remote -v` 为空 |
| 版本号 | `0.4.1`（`src/Proxmark3GUI.pro` 的 `VERSION`） | |
| 许可证 | **LGPL-2.1**（`LICENSE` 首行） | |

结论：**v0.4.1 基线与任务书描述一致，未被改动**。

### 2.2 参考项目 Phosphor

| 项目 | 实测值 | 依据 |
|---|---|---|
| 本地目录 | `E:\Opencode_project\phosphor-master` | |
| 本地是否 Git 仓库 | **否**（无 `.git`） | `Test-Path .git` → False |
| 本地版本 | `1.1.0` | `package.json`、`src-tauri/Cargo.toml`、`tauri.conf.json` |
| 上游仓库 | `https://github.com/nikitaart2000/phosphor` | GitHub API |
| 上游默认分支 | `master` | GitHub API |
| 上游 HEAD | `440ecfcffc1bee4c386634de91feddf9606778c6`（2026-02-14，*Clean up .gitignore comments*） | GitHub API |
| 上游 tag | `v1.1.0` → `b19869518a6530d853f70a3922bd28abab16684e` | GitHub API |
| 本地与上游一致性 | **三个关键文件 blob SHA-1 完全相同** | 见下 |
| 许可证 | **GPL-3.0**（`LICENSE` = GNU GPL v3；`Cargo.toml` = `GPL-3.0-only`） | |

本地 `git hash-object` 与 GitHub `contents?ref=v1.1.0` 返回的 `sha` 对比：

| 文件 | 本地 blob | 上游 v1.1.0 blob | 结果 |
|---|---|---|---|
| `src-tauri/src/pm3/output_parser.rs` | `c9c3136eceb9fdcd008c88c000339c5d4dfe3221` | `c9c3136e…` | 一致 |
| `src-tauri/src/pm3/command_builder.rs` | `76d064465c29f24f0b0b697ec5e47c624bd6cc9a` | `76d06446…` | 一致 |
| `src-tauri/src/commands/write.rs` | `135ab91c3ba6f33172e911d8f0f79320decdea88`（上游） | `135ab91c…` | 一致 |

结论：**本地副本等价于上游 tag `v1.1.0`**。上游 `master` 比 tag 多 1 个提交（`440ecfc`，只改 `.gitignore` 注释），与 LF 无关。

> 注意：Phosphor 的版本号（1.1.0）与它推荐/内置的 PM3 Client 版本（README 写 "v4.20728+ recommended"，内置 sidecar 自带一份 Client）**不是同一套版本体系**，不要混淆。

### 2.3 当前 Iceman Client / 固件

| 项目 | 实测值 | 依据 |
|---|---|---|
| Client 二进制 | `E:\Opencode_project\pm3-256-iceman-v4.23346\proxmark3.exe`（7,057,878 B） | 文件系统 |
| Client 版本字符串 | `Iceman/master/v4.23346-suspect` | 运行时 `-h`/版本输出 + `README.md` |
| 固件文件 | `fullimage.elf`（234,272 B；烧录区 233,394 B @ `0x102000–0x13AFB2`） | `README.md` + 文件 |
| 固件构建 | `PLATFORM=PM3GENERIC / PLATFORM_SIZE=256 / PLATFORM_EXTRAS= / STANDALONE=` | `README.md` |
| 构建工具链 | arm-none-eabi-gcc 13.2.1 | `README.md` |
| Flash 占用 | 92%（README）/ 95%（含 recovery.bin） | `README.md` |
| FPGA 镜像 | 固件内仅 `fpga_pm3_hf` + `fpga_pm3_felica` | 固件字符串扫描 |
| LF 支持 | **无**（LF 命令、LF FPGA 镜像、LF 解码字符串全部不存在） | 见 `PM3_HARDWARE_COMPATIBILITY.md` |

---

## 3. 任务书中的三个假设需要修正

审计发现任务书的描述与仓库实际状态有三处不一致。这三点直接影响后续设计，必须先说明。

### 3.1 主项目并非"没有 LF 功能"

任务书写"本次任务不是直接开发 LF 功能"，暗示 LF 尚未实现。实际状态是：

- `src/module/lf.{h,cpp}`（261 行）**已存在**，实现了 `read / sniff / search / tune / getLFConfig / setLFConfig / resetLFConfig / showWaveform`。
- `src/ui/mainwindow.ui` 中已有完整的 `LF` 页面（频率滑块、divisor、bps、decimation、averaging、trigger、skip、Get/Set/Reset 按钮）。
- `config/config_rrgv4.23346.json` 中已有 `"lf"` 命令节（`lf read -v`、`lf search -u`、`lf tune --divisor <divisor>`、`lf config …`）。
- `src/ui/waveformwidget.{h,cpp}` + `waveformdialog.{h,cpp}` 已能读取 `data save` 导出的 GraphBuffer 文本并绘制波形。

缺的不是"LF 入口"，而是：**输出解析、卡型模型、写入向导、T55xx 完整流程、LF 能力检测**。

### 3.2 主项目没有 `CommandQueue` / `CapabilityDetector` / `RiskPolicy` 这三个类

任务书把它们列为既有基础设施。实际架构是：

- `core/ClientSession`：唯一 QProcess + FIFO 队列 + `rem <token>` 哨兵 + 超时自动重启 + detached 长命令。
- `common/Util`：同步门面（`execCMD` / `execCMDWithOutput` / `execDetached`）。
- `core/Logger`、`core/ClientInfo`。

全仓库 grep `RiskPolicy`、`CapabilityDetector`、`CommandQueue` 均为 **0 命中**；"风险控制"目前是各模块内联的 `QMessageBox` 确认（如 `T55xx::onCloneEM410x`）。能力检测目前只做 Client/固件版本解析（`ClientSession::parseVersionOutput`），**没有"设备是否具备 LF"的检测**。

### 3.3 当前 256 KB 固件的不只是"裁剪"，而是完全移除 LF

证据见 `PM3_HARDWARE_COMPATIBILITY.md` §3。要点：`fullimage.elf` 中 **不存在** `lf`、`t55xx`、`EM 410x`、`ASK`、`PSK`、`Manchester` 等任何 LF 字符串，也**不存在** `fpga_pm3_lf` 比特流；只保留 `fpga_pm3_hf` 与 `fpga_pm3_felica`。

---

## 4. 主要结论（先看这里）

1. **Phosphor 没有任何 LF 解码算法。** 它的全部 LF"解析"是用正则表达式去解析 Iceman Client 已经解码好的文本输出。Manchester / FSK / PSK / 时钟恢复 / CRC 全部在 Client（以及固件）内部完成。（`LF_ALGORITHM_ANALYSIS.md`）
2. **Phosphor 的 LF 卡型覆盖是"Client 解码能力的 UI 映射"，不是独立实现。** 22 个可克隆卡型 = 22 个 clone 命令模板 + 22 个正则。没有一处在 Phosphor 内做位域解析。（`LF_CARD_SUPPORT_MATRIX.md`）
3. **Phosphor 的 T55xx 支持很浅**：只有 `detect / chk / wipe`，没有 `lf t55xx read/dump/write/config/info`，不解析 block0 位域。主项目现有 `t55xx.cpp` 在 block0 位域解析上**反而比 Phosphor 更完整**。（`T55XX_ANALYSIS.md`）
4. **Phosphor 的工程价值集中在三处**：单命令→卡型的解析决策树（含真实输出回归用例）、22 条 clone 命令模板、T5577 密码保护的写入状态机。（`PHOSPHOR_ARCHITECTURE.md`）
5. **当前设备硬件有 LF 前端，固件没有 LF。** 硬件侧 LF 天线在 125 kHz 下实测 33.8 V（空载谐振），但固件既无 LF 命令也无 LF FPGA 镜像。要跑 LF 必须换一份含 LF 的 256 KB 固件（本阶段禁止刷写）。（`PM3_HARDWARE_COMPATIBILITY.md`）
6. **许可证是硬约束。** 主项目 LGPL-2.1，Phosphor GPL-3.0。**不能复制 Phosphor 代码**到主项目；只能"参考行为、独立重写"。正则表达式与命令字符串属于事实性/功能性内容，风险较低，但仍建议独立编写并保留审计记录。（`LICENSE_AND_DEPENDENCIES.md`）
7. **不需要引入 Phosphor 的任何外部依赖。** Qt5 自带 `QRegularExpression`、`QJsonDocument`、`QStateMachine`，足以覆盖 Phosphor 用 Rust+regex+serde+XState 做的全部事情。（`PROXMARK3GUI_INTEGRATION.md`）
8. **技术可行性：中偏高；软件移植难度：低-中；硬件可行性：当前固件下为零，换固件后为中等。** 建议先做 P0（能力检测 + LF Search 解析 + 常见卡型展示），并在动工前先解决固件问题。（`LF_MIGRATION_ROADMAP.md`、`LF_AUDIT_FINAL_REPORT.md`）

---

## 5. 文档索引

| 文档 | 内容 |
|---|---|
| `README.md` | 本文件：范围、基线、结论、证据规范 |
| `PHOSPHOR_ARCHITECTURE.md` | Phosphor 架构、模块清单、端到端调用链、Mermaid 图 |
| `LF_CARD_SUPPORT_MATRIX.md` | 22+4 个 LF 卡型的实现/命令/测试证据矩阵（A–E 分级） |
| `LF_COMMAND_MAPPING.md` | Phosphor ↔ Iceman 命令映射、参数、返回结构、版本差异 |
| `LF_ALGORITHM_ANALYSIS.md` | 哪些算法由谁实现、解析伪代码、可移植性判定 |
| `T55XX_ANALYSIS.md` | T55xx 功能拆解、数据结构、写入流程、与主项目对比 |
| `PM3_HARDWARE_COMPATIBILITY.md` | 256 KB 硬件/固件/FPGA 证据、能力分类、结论 |
| `PROXMARK3GUI_INTEGRATION.md` | 主项目架构适配、模块设计、UI 方案、12 个问题的回答 |
| `LICENSE_AND_DEPENDENCIES.md` | LGPL-2.1 vs GPL-3.0、依赖清单、复用边界 |
| `LF_MIGRATION_ROADMAP.md` | P0–P3 优先级、工作量、风险、验收条件 |
| `LF_TEST_PLAN.md` | 纯软件 / Client 集成 / 真实硬件三层测试矩阵 |
| `LF_AUDIT_FINAL_REPORT.md` | 20 个必答问题、最终判定、是否启动 LF-Core |

---

## 6. 证据规范

每条结论都带以下一种或多种标记：

| 标记 | 含义 |
|---|---|
| `[源码]` | 直接读取源文件得到，给出文件路径 + 函数/行号 |
| `[Client]` | 由 `proxmark3.exe` 的 `--fulltext` / `-t` 帮助或二进制字符串得到 |
| `[固件]` | 由 `fullimage.elf` 字符串扫描得到 |
| `[实测]` | 本次实际运行命令得到的输出（附命令） |
| `[文档]` | 来自仓库内既有文档（需注明该文档本身可能未经硬件验证） |
| `[推断]` | 由以上证据推理得出，**未经直接验证** |

**未找到实现的功能一律写"未找到"，不猜测。**

---

## 7. 复现方式

```powershell
# 1. 基线
C:\msys64\usr\bin\git.exe -C E:\Opencode_project\Proxmark3GUI-master status --short --branch
C:\msys64\usr\bin\git.exe -C E:\Opencode_project\Proxmark3GUI-master log -1 --oneline

# 2. Client 全量命令帮助（离线，不接触设备）
cd E:\Opencode_project\pm3-256-iceman-v4.23346
.\proxmark3.exe -t --incognito           > pm3_cmdlist.txt
.\proxmark3.exe --fulltext --incognito   > pm3_fulltext.txt

# 3. 固件字符串（只读）
#    见 PM3_HARDWARE_COMPATIBILITY.md §3 的 PowerShell 片段

# 4. Phosphor 关键文件核对
C:\msys64\usr\bin\git.exe hash-object E:\Opencode_project\phosphor-master\src-tauri\src\pm3\output_parser.rs
```

> `proxmark3.exe -t/--fulltext` 在无端口时进入 **OFFLINE mode**，只打印帮助，不打开串口、不访问设备，属于只读操作。
