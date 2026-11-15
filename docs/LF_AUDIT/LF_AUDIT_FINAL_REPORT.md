# LF-Audit 最终报告

**日期：** 2026-11-14
**审计对象：** Phosphor 1.1.0（GPL-3.0）→ Proxmark3GUI v0.4.1（LGPL-2.1-or-later）
**设备：** Proxmark3 `PM3 GENERIC` / AT91SAM7S256 / 256 KB / Iceman v4.23346-suspect

---

## 一、20 个必答问题

### 1. Phosphor 实际支持哪些 LF 卡型？

**22 个可克隆 + 3 个仅检测：**

可克隆：EM4100、HID Prox、Indala、IO Prox、AWID、FDX-B、Paradox、Viking、Pyramid、Keri、NexWatch、Presco、Nedap、GProxII、Gallagher、PAC/Stanley、Noralsy、Jablotron、SecuraKey、Visa2000、Motorola、IDTECK。

仅检测：COTAG、EM4x50、Hitag。

证据：`src-tauri/src/cards/types.rs:12-47`、`output_parser.rs:415-919`、`command_builder.rs:298-591`。

### 2. 哪些是真正实现，哪些只是 UI 或文档声明？

| 类别 | 卡型 | 说明 |
|---|---|---|
| **真正实现（解析 + 命令 + 单测）** | 上述 22 个中的 **18 个**有独立解析器 | 见矩阵 A 级 |
| **并入通用路径** | HID ProxII、HID Corporate 1000 | 都进 `parse_hid`，无专属字段；Corporate 1000 的 format 还会被静默降级 |
| **仅空白卡管理** | T55x7 / T5577 | 只有 detect/chk/wipe，无数据格式解析 |
| **仅存在性检测** | COTAG、EM4x50、Hitag | 只判断 `[+] XXX` 字符串，不提取 ID |
| **纯文档声明** | README 的 "22 types" 计数 | 数字对，但含义被夸大 |

### 3. 哪些 LF 算法由 Phosphor 自己实现？

**没有任何信号处理算法。**
Phosphor 自己实现的只有：

- 文本 → 字段的正则提取；
- 有序 `contains` 分派决策树；
- IO Prox 的 FC hex→decimal 转换；
- 22 条命令模板的参数组装；
- 写入后字段比较；
- T5577 密码流程编排。

### 4. 哪些能力来自 Iceman Client？

**全部底层能力：**

- ASK/FSK/PSK 解调
- Manchester / Biphase 解码
- 时钟恢复、偏移搜索
- 前导码识别、帧同步
- 行/列校验位、CRC
- 各协议字段格式化输出
- Wiegand / T55xx 编码
- 实际写入执行

### 5. 当前 Proxmark3GUI 是否已经具备部分相关功能？

**是，而且比任务书假设的多。**

| 已有 | 位置 |
|---|---|
| LF 页面 + 频率/采样参数 | `mainwindow.ui`、`lf.cpp` |
| `lf read` / `lf sniff` / `lf search` / `lf tune` | `lf.cpp:31-61` |
| `lf config` 读/写/reset | `lf.cpp:89-149` |
| GraphBuffer 导出 + 波形绘制 | `lf.cpp:182-260`、`waveformwidget.cpp` |
| T55xx 检测 / info / 配置 / block0 位域编辑 | `t55xx.cpp:348/416/457/201` |
| EM410x 读取 + 克隆 | `t55xx.cpp:493/503` |
| 单 Client 进程 + FIFO + 超时重启 | `clientsession.cpp` |

**缺的是**：解析、卡型模型、能力探测、写入向导、T5577 密码流程、EM4305。

### 6. 当前 Iceman v4.23346 Client 支持哪些相关命令？

LF 命令组 **35 个**（`proxmark3.exe -t` 实测；含 `lf em` / `lf hitag` 等父组）：

```
lf config / cmdread / read / relay / search / sim / simask / simfsk /
simpsk / sniff / tune
lf awid / cotag / destron / em / em 410x / em 4x05 / em 4x50 / em 4x70 /
fdxb / gallagher / gproxii / hid / hitag / hitag hts / hitag htu /
idteck / indala / io / jablotron / keri / motorola / nedap / nexwatch /
noralsy / pac / paradox / pcf7931 / presco / pyramid / securakey / ti /
t55xx / trovan / viking / visa2000
```

关键子命令均存在：`lf t55xx detect/chk/wipe/info/dump/read/write/config`、`lf t55xx dangerraw`（**高危，不建议暴露**）、22 条 `* clone`。

### 7. 当前固件与 Client 是否匹配？

**版本匹配，能力不匹配。**

- 版本：Client 与固件同为 `Iceman/master/v4.23346-suspect`（`9ce8a4b6c`）。
- 能力：Client 有完整 LF，固件**完全没有** LF（字符串 0 命中 + 无 `fpga_pm3_lf`）。

### 8. 256 KB PM3 是否具有实际 LF 能力？

**硬件：有。固件：没有。**

- LF 天线实测 125 kHz 33.8 V（空载谐振）→ 前端存在。
- 固件无 LF 代码、无 LF FPGA 比特流 → **当前状态下无法做任何 LF 操作**。
- 换固件后能否在 256 KB 内放下 LF：**未知**（Flash 已用 92%，本阶段禁止构建验证）。

### 9. 是否存在必须修改固件才能使用的功能？

**全部 LF 功能都需要。**

- `lf search`、`lf tune`、`lf read`、`lf sniff`、`lf config`
- `lf t55xx *`
- 所有 `lf <proto> clone`

### 10. 哪些功能可以直接移植为 C++？

| 可直接实现 | 说明 |
|---|---|
| 卡型枚举 + 显示名 | 纯数据 |
| `lf search` 文本解析 | `QRegularExpression` |
| 命令模板 | 字符串 + 参数校验 |
| 字段比较（复验） | 纯函数 |
| 状态机 | 枚举 + 转移表 |
| 风险分级与确认 | UI 逻辑 |
| 能力探测 | 命令 + 结果判定 |

**注意：** "直接移植"指**独立重写同样功能**，不是复制 Phosphor 代码（见第 13 题）。

### 11. 哪些功能应该调用现有 Client 而不是重新实现？

**所有信号处理与协议编解码：**

- 解调 / 解码 / 时钟 / 前导码 / 校验 / CRC
- Wiegand 编码、T55xx 编码
- 协议专用 reader/demod/watch/sim
- 实际写入

理由：Client 已成熟、随协议更新；GUI 重写会产生"第二套真相"，且必然与 Client 不一致。

### 12. 是否有必要引入 Phosphor 的任何外部依赖？

**完全不需要。**

| Phosphor 用到的 | Qt5 等价物 |
|---|---|
| `regex` crate | `QRegularExpression` |
| `serde` / `serde_json` | `QJsonDocument` / `QVariantMap` |
| `tokio` + `tauri-plugin-shell` | `QProcess`（已有 `ClientSession`） |
| `rusqlite` | `QSettings` / 文件（现有历史功能足够） |
| XState | 枚举状态机 / `QStateMachine`（可选） |
| React / Tauri / Tailwind | Qt Widgets + QSS（已有 qdarkstyle） |

### 13. 是否存在 GPL-3.0 许可证兼容问题？

**存在，且是硬约束。**

- 主项目：LGPL-2.1-or-later
- Phosphor：GPL-3.0-only
- 直接复制代码 → 整个主项目必须变 GPL-3.0。

**结论：只能"参考行为 + 独立重写"。**

另：Iceman Client 是 GPL-3.0，但主项目以**独立进程**调用且**不分发** Client，因此不构成衍生作品，无冲突。

### 14. LF 功能应该如何融入现有 Qt5 GUI？

- **独立一级页面**（沿用现有 LF Dock 页）。
- 页内分区：**状态横幅 / 识别（只读）/ 天线与采样 / T55xx（中高风险）/ 克隆（危险，默认折叠）**。
- 新增 `LfFlowController`（显式枚举状态机），不引入 `QStateMachine`。
- 复用 `ClientSession` 的 FIFO 与 detached 通道，**不新建进程管理**。
- 所有写入：命令可见 + 二次确认 + 写后复验 + 日志。

### 15. 最小可行版本应该包含哪些功能？

**MVP（8 人日）：**

1. LF 能力探测（三级）+ 状态横幅。
2. `LfCardInfo` + `LfCardType`。
3. `lf search` 解析：EM4100、HID Prox、AWID、Indala + 无卡/不支持。
4. LF 页展示：卡型、UID、字段表、原始输出、复制。
5. 保留现有 tune/read/sniff/config/波形。

**不含**：写入、T55xx 扩展、EM4305。

### 16. 完整 LF 功能体系应该包含哪些模块？

```
LfCardType / LfCardInfo        数据模型
LfParser (+ LfParseSpec)       文本解析（配置驱动）
LfCommandBuilder               命令构造 + 输入校验
LfCapabilityProbe              能力探测
LfFlowController               流程状态机
LfRiskGate                     风险分级/确认
LfJobRunner                    长任务/进度/取消
LfPage (扩展 LF)               UI
T55xxController (扩展 T55xx)   芯片管理
WaveformDialog (复用)          波形
```

### 17. 哪些功能可以在没有真实卡片的情况下完成开发？

- 全部解析器 + 单元测试
- 全部命令构造器 + 校验
- 能力探测（"不支持"分支可验证）
- UI 布局与交互
- 离线 Client 测试通路（`data load` + `lf search -1`）
- 风险门控与确认流程

### 18. 哪些功能必须等待真实 LF 卡片验证？

- 任何写入与擦除
- 写后复验
- T55xx dump/read（真实 block 数据）
- 多卡区分、识别率
- 天线调谐基线
- 协议专用 reader 输出格式

### 19. 预计需要多少个开发阶段？

**1 个前置门槛 + 4 个阶段：**

- Gate 0：固件路线决策（阻塞）
- P0：基础（8 人日）
- P1：常见协议 + 写入（10.5 人日）
- P2：T55xx（11.5 人日）
- P3：高级（可选，~15 人日）

**P0–P2 合计约 30 人日**（不含真实硬件联调与固件工作）。

### 20. 是否建议正式启动 LF-Core 移植？

**有条件建议：**

- ✅ **建议启动 P0 的"纯软件"部分**（能力探测 + 解析 + 模型 + UI），因为它不依赖硬件、可完全测试、风险低。
- ⏸ **暂缓 P1 的写入与 P2 的擦除**，直到 Gate 0（固件）解决。
- ❌ **不建议**在固件问题解决前对外宣称"支持 LF"。

---

## 二、最终判定

| 维度 | 评定 | 依据 |
|---|---|---|
| **技术可行性** | **高**（软件层） / **中**（端到端） | 解析、命令、状态机都可在 Qt5 内完成；端到端受固件限制 |
| **硬件可行性** | **中 / 未知** | 前端具备；固件缺失；256 KB 容量未知 |
| **软件移植难度** | **低–中** | 无算法移植，主要是文本解析 + 流程编排 |
| **是否需要新依赖** | **否** | Qt5 全覆盖 |
| **许可证风险** | **中**（可规避） | 独立重写即可 |
| **主要风险** | 固件不支持（阻断性）；输出格式漂移；误写 | 见下 |

### 主要风险（按严重度）

| # | 风险 | 等级 | 缓解 |
|---|---|---|---|
| 1 | 固件无 LF，所有 LF 功能不可用 | **阻断** | Gate 0 |
| 2 | 256 KB 放不下 LF | 高 | 构建评估（需批准） |
| 3 | Client 输出格式随版本漂移 | 中 | 配置化 + 版本化 + 回归样本 |
| 4 | 误写非目标卡 / 写坏卡 | 高 | detect + 命令可见 + 二次确认 + 复验 |
| 5 | 解析错误导致克隆错误 | 中 | 字段可编辑 + 复验 + 差异提示 |
| 6 | 许可证污染 | 中 | 独立重写，不复制 |
| 7 | 长命令阻塞 UI | 中 | detached + 忙状态 + Stop 语义 |

### 预计工作量

| 阶段 | 人日 |
|---|---|
| P0 | 8 |
| P1 | 10.5 |
| P2 | 11.5 |
| P3（可选） | ~15 |
| **P0–P2** | **~30** |
| 真实硬件联调 | 另计（依赖固件） |

### 最小可行版本（MVP）

> **能力探测 + `lf search` 解析（EM4100/HID/AWID/Indala）+ 卡型展示 + 保留现有采样/调谐能力。**
> 不含任何写入。

### 推荐实施顺序

```
Gate 0（固件决策）
   ↓
P0-1 数据模型 → P0-2 解析器 + 单测 → P0-3 能力探测 → P0-4 UI 识别区
   ↓
（固件可用后）P1 解析扩展 → 命令构造 → 风险门控 → 写入 → 复验
   ↓
P2 T55xx dump/read/write/wipe + EM4305
   ↓
P3（可选）
```

### 是否建议进入 LF-Core 阶段？

**建议：进入"LF-Core Stage 1（纯软件 P0）"，但设置明确边界：**

1. 不修改现有业务逻辑，只新增文件与页面控件；
2. 不实现任何写入；
3. 不宣称任何卡型"已支持"；
4. 交付物包含单元测试与合成样本；
5. Gate 0 未解决前，不进入 P1/P2。

---

## 三、暂不建议实施的功能

| 功能 | 原因 |
|---|---|
| `lf sim` / `simask` / `simfsk` / `simpsk` | 长期占用设备；可被滥用；当前无法验证 |
| `lf <proto> brute`（任何 brute） | 攻击性功能，合规风险 |
| `lf t55xx dangerraw` | 高危，可能砖卡 |
| T55xx 密码自动穷举 | 合规风险；仅允许用户输入已知密码 |
| `lf em 4x70` 系列 | 含 `brute`/`recover`，且 Phosphor 未实现 |
| 自行实现解调/解码器 | 与 Client 重复、易失效 |
| 批量克隆 / 一键克隆到未知卡 | 高风险 |
| Lock 位 / 熔断类操作 | 不可逆 |
| 把 `proxmark3.exe` 打包进安装包 | 引入 GPL-3.0 分发义务 |

---

## 四、关键发现摘要（给决策者的 8 句话）

1. **Phosphor 不是"LF 实现"，而是"LF 输出解析器 + 克隆向导"。**
2. 它的 22 卡型 = 22 条命令模板 + 50 条正则，**没有任何协议算法**。
3. 它的测试是**正则回归测试**，不是硬件验证；仓库内**无任何真实卡测试证据**。
4. 它的 T55xx 支持**比主项目还弱**（无 block 位域、无 dump/read/write）。
5. 它的正则基于 **v4.20728** 输出，与本项目 **v4.23346** 存在已知差异（Indala、HID format）。
6. **当前 256 KB 固件完全不含 LF**（0 个 LF 字符串、无 `fpga_pm3_lf`），`lf search` 返回 `unknown command:: 0x0228`。
7. **硬件有 LF 前端**（125 kHz 33.8 V），瓶颈在固件，不在天线。
8. **不需要引入 Phosphor 的任何依赖**；Qt5 足够；**不能复制它的代码**（GPL-3.0 vs LGPL-2.1）。

---

## 五、审计完整性与限制

### 已完成

- 两项目 Git/版本/许可证核实
- Phosphor 全量源码结构 + 调用链
- 22 卡型覆盖矩阵（A–E 分级）
- 29 条命令映射
- 算法归属判定
- T55xx 专题
- 固件/Client 静态能力对比（字符串级）
- 主项目架构适配与 12 问回答
- 许可证与依赖清单
- 离线 Client 测试通路验证

### 未完成（受阶段约束）

| 项 | 原因 |
|---|---|
| 编译/刷写固件 | 明令禁止 |
| 真实 LF 读取 | 固件无 LF |
| 真实写入/擦除 | 明令禁止 |
| 运行 Phosphor 的 225 个 Rust 测试 | 本机无 Rust 工具链 |
| 运行主项目构建/单测 | 本阶段不修改、不构建 |
| v4.23346 的真实卡输出样本 | 需要可用的 LF 设备 |

### 证据等级声明

本报告中：

- **"已确认"** = 有源码/二进制/运行输出直接支持；
- **"未验证"** = 有代码或命令但无硬件证据；
- **"未知"** = 无法在当前条件下判断（如 256 KB 容量）；
- **"推断"** = 由间接证据推理，未直接验证。

**未找到实现的功能，一律写"未找到"，未作任何猜测。**

---

**审计结束。等待用户审阅并批准下一步。**
