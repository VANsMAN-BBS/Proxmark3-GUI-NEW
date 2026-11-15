# 许可证与代码复用说明

**阶段：** LF-Core Stage 1
**主项目许可证：** LGPL-2.1-or-later（`LICENSE`）
**参考项目许可证：** Phosphor GPL-3.0-only；Iceman proxmark3 GPL-3.0

> 本文是工程记录，不是法律意见。涉及分发/商用前仍需法务复核。

---

## 1. 结论先行

| 问题 | 结论 |
|---|---|
| 是否复制了 Phosphor 源代码？ | **否** |
| 是否复制了 Phosphor 的正则集合？ | **否**（按 Client 输出重新编写） |
| 是否复制了 Phosphor 函数实现后改名？ | **否** |
| 是否把 GPL-3.0 代码加入主项目？ | **否** |
| 是否修改主项目许可证？ | **否** |
| 当前是否存在许可证冲突？ | **否** |

本阶段新增的 `src/lf/` 与 `tests/lfparser/` 全部为独立编写，按主项目 LGPL-2.1-or-later 分发。

---

## 2. 参考过的 Phosphor 文件

仅阅读，未复制：

| 文件 | 用途 |
|---|---|
| `src-tauri/src/pm3/output_parser.rs` | 了解 LF 解析的字段集合、分派顺序、失败行为；识别已知缺陷 |
| `src-tauri/src/pm3/command_builder.rs` | 了解 22 种卡型与命令的对应关系（本阶段未使用命令） |
| `src-tauri/src/cards/types.rs` | 了解 `CardType` / `CardData` / `T5577Status` 的数据形状 |
| `src-tauri/src/commands/scan.rs`、`write.rs`、`blank.rs` | 了解流程顺序（本阶段未实现流程） |

**未参考**：前端 TS/TSX、Tauri 配置、图标/音频资源。

---

## 3. 参考了哪些"行为"和"协议字段"

这些属于功能性事实或公开接口语义：

1. LF 卡型名称（EM4100、HID Prox、AWID、Indala）——行业通用。
2. 字段语义：Facility Code、Card Number、Raw、Bit Length——协议术语。
3. Client 输出中出现的标签（`EM 410x ID`、`FC:`、`CN:`、`Raw:`、`Fmt 26`、`AWID - len:`）——来自 Iceman Client 本身，不是 Phosphor 的创作。
4. 审计阶段已记录的缺陷类型（HID 静默降级、Indala 依赖 Raw）——作为**需求**修复，而不是复制实现。

---

## 4. 独立实现说明

### 4.1 数据模型

- `LFParseResult` / `Lf::Optional<T>` / `Lf::Status` / `Lf::Confidence` 为本项目新设计。
- Phosphor 使用 `CardData { uid, raw, decoded: HashMap }`，本模块**没有**采用该结构，也没有采用其 `uid` 拼接字符串（`FC65:CN29334`）方案。

### 4.2 分派机制

| Phosphor | 本项目 |
|---|---|
| `contains()` 顺序 if 链，先匹配先返回 | 每个解析器 `detect()` 返回 0/40/45/50/60/70/80/85/90/100 的证据分数 |
| 解析失败继续向下 | 只调用最高分解析器一次，失败返回 Partial |
| 兜底只接受 viking/nexwatch | 无跨协议兜底 |
| 无歧义处理 | 同分不同协议附加 warning |

### 4.3 正则

全部按 Client 输出重新编写，并补充 v4.23346 结构。示例对比：

| 用途 | Phosphor | 本项目 |
|---|---|---|
| EM4100 ID | `EM 410x ID\s*[-:]?\s*([0-9A-Fa-f]{10})` | `\bEM\s*410x\s+ID\s+([0-9A-Fa-f]{10})\b` + XL/Electra/128b 变体 |
| HID 候选 | 单个 `FC/CN` 正则 + 关键词 | 逐行 `[NAME] … N-bit … FC:` 候选模型 + 多候选决策 |
| HID 格式 | 捕获后校验 4 个字面值，失败默认 H10301 | 按 `[NAME]` + description 分类，未知返回 `Unknown HID format` |
| AWID | `AWID.*?FC.*?CN` | `AWID - len:` 主结构 + `-unknown-` 变体 + 附加行 |
| Indala | `Indala.*?Raw` / `Indala.*?ID` 二选一 | `len`/`Raw`/`Fmt`/`Parity`/附加字段独立匹配，Raw 可选 |

模式相似是**因为输入格式本身固定**（例如 ID 就是 10 位 hex），属于功能性表达；结构、命名、错误处理与测试均为独立设计。

### 4.4 未使用的 Phosphor 资产

- 未复制任何 `.rs` / `.ts` / `.tsx` 文件内容。
- 未复制 README 文案、卡型描述句。
- 未复制图标、音频、字体。
- 未引入 Rust/Tauri/React/XState 依赖。

---

## 5. 参考的 Iceman Client 源码

为核对 v4.23346 输出格式，阅读了上游（GPL-3.0）以下文件：

- `client/src/cmdlfem410x.c`
- `client/src/cmdlfhid.c`
- `client/src/cmdlfawid.c`
- `client/src/cmdlfindala.c`
- `client/src/wiegand_formats.c`
- `client/src/wiegand_formatutils.c`

使用方式：

- 仅提取 `PrintAndLogEx` 的**输出格式字符串**（如 `EM 410x ID %010X`）作为解析目标；
- **未复制**其 C 代码、算法或数据结构到本项目；
- 本项目通过命令行调用 Client，不链接、不分发其二进制。

> 说明：输出格式字符串是软件接口的一部分，用于互操作；本项目未复制实现逻辑。

---

## 6. 许可证风险与未决问题

| 风险 | 等级 | 说明 |
|---|---|---|
| 正则"实质性相似" | 低 | 输入格式固定导致模式必然相近；结构、变量、错误处理不同；已保留本记录 |
| Client 输出文本的版权 | 极低 | 设备生成的事实性数据；测试夹具为合成 |
| 未来若复制 Phosphor 代码 | 中 | 会把主项目拖入 GPL-3.0；必须继续独立实现 |
| Iceman 源码引用 | 低 | 只读、只提取格式；未链接/分发 |
| 商标/卡型名称 | 低 | 仅用于识别，不宣称兼容认证 |

### 未决问题（需在 Stage 2 前确认）

1. 若未来需要移植 Phosphor 的**流程状态机**，是否仍采用"参考行为 + 独立实现"？建议是。
2. 若未来要引入任何 GPL-3.0 组件（包括 Client 二进制的再分发），需重新评估分发义务。
3. 若主项目改为闭源/商用，LGPL-2.1 与 Qt LGPL-3.0 的动态链接义务需法务复核。

---

## 7. 溯源记录

| 日期 | 动作 | 结果 |
|---|---|---|
| 2026-11-14 | 阅读 Phosphor 解析器 | 仅记录行为与缺陷，未复制 |
| 2026-11-14 | 阅读 Iceman 输出格式 | 仅提取格式字符串 |
| 2026-11-14 | 编写 `src/lf/*` | 独立实现，1,463 行 |
| 2026-11-14 | 编写测试 | 独立编写，600 行，59 用例 |

如后续审查需要，可按文件 diff 与提交历史核对。
