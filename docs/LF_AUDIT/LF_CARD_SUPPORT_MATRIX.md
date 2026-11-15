# LF 卡型与协议覆盖矩阵

**判定基准：** 以 Phosphor v1.1.0 实际源码为准，不以 README 为准。
**核心结论：** Phosphor 的 "22 种 LF 卡" 全部是 **Client 已解码文本 → 正则提取 → 命令模板** 的映射，**没有任何一种卡型由 Phosphor 自己做协议解码**。

---

## 1. 证据等级定义

| 等级 | 定义 | 本次审计的判定标准 |
|---|---|---|
| **A** | 找到实现代码 **且** 有测试 | 有解析/构造函数 + 至少 1 个 `#[test]`（含真实输出回归用例） |
| **B** | 找到完整实现代码，但缺少测试 | 有函数、无对应测试 |
| **C** | 只有命令调用或 UI 入口 | 只有字符串拼接 / 只有 `contains()` 检测，无字段解析 |
| **D** | 只有文档声明 | README / 注释声称支持，源码中无对应实现 |
| **E** | 尚未找到实现 | 全仓库检索无果 |

> **重要区分**：等级 A 只表示"有代码 + 有单元测试"。Phosphor 仓库内**没有任何真实卡片的测试日志、截图或固件版本记录**，因此"真实硬件验证"列一律为 **无**。

---

## 2. Phosphor 实际支持的 LF 卡型（22 个可克隆）

数据模型：`CardType`（`src-tauri/src/cards/types.rs:12-47`）
解析入口：`parse_lf_search`（`output_parser.rs:415`）
命令构造：`build_clone_command`（`command_builder.rs:298`）

| # | 卡型 | 解析实现 | 专用命令调用 | 依赖 `lf search` | 自动识别 | 读取 | 字段解码¹ | 格式转换² | 写入 | 复读验证³ | 特殊硬件 | 自动化测试 | 真实硬件证据 | 等级 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | EM4100 | `EM4100_ID_RE` (:22) + 行 424 | `lf em 410x clone --id` | ✓ | ✓ | ✓ | ID(10 hex) | ✗ | ✓ | ◐ | 无 | ✓ `parse_em4100`/`clone_em4100` | **无** | **A** |
| 2 | HID Prox | `parse_hid` (:1306) | `lf hid clone -w/-r` | ✓ | ✓ | ✓ | FC/CN/raw/format | ◐⁴ | ✓ | ◐ | 无 | ✓ 3 个测试 | **无** | **A** |
| 3 | Indala | 内联 :447-482 | `lf indala clone --raw` | ✓ | ✓ | ◐ | raw / ID | ✗ | ✓ | ◐ | 无 | ✓ `parse_indala_raw`/`clone_indala` | **无** | **A** |
| 4 | IO Prox | `parse_ioprox` (:1349) | `lf io clone --vn/--fc/--cn/--raw` | ✓ | ✓ | ✓ | VN/FC/CN/raw | ✓ hex→dec | ✓ | ◐ | 无 | ✓ 3 个测试含真实 XSF | **无** | **A** |
| 5 | AWID | `parse_awid` (:1419) | `lf awid clone --fmt/--fc/--cn` | ✓ | ✓ | ✓ | FC/CN/len | ✗ | ✓ | ◐ | 无 | ✓ 3 个测试 | **无** | **A** |
| 6 | FDX-B | `parse_fdxb` (:1447) | `lf fdxb clone --country/--national` | ✓ | ✓ | ✓ | country/national | ✗ | ✓ | ◐ | 无 | ✓ 3 个测试 | **无** | **A** |
| 7 | Paradox | `parse_paradox` (:1503) | `lf paradox clone --fc/--cn/--raw` | ✓ | ✓ | ✓ | FC/CN/raw | ✗ | ✓ | ◐ | 无 | ✓ 2 个测试 | **无** | **A** |
| 8 | Viking | 内联 :857-894 | `lf viking clone --cn` | ✓ | ✓ | ✓ | card_id/raw | ✗ | ✓ | ◐ | 无 | ✓ 3 个测试 | **无** | **A** |
| 9 | Pyramid | `parse_pyramid` (:1622) | `lf pyramid clone --fc/--cn/--raw` | ✓ | ✓ | ✓ | FC/CN/raw | ✗ | ✓ | ◐ | 无 | ✓ 2 个测试 | **无** | **A** |
| 10 | Keri | `parse_keri` (:1558) | `lf keri clone -t i/m --cn/--fc` | ✓ | ✓ | ✓ | Internal/MS + FC/CN/raw | ✓ 类型判别 | ✓ | ◐ | 无 | ✓ 3 个测试 | **无** | **A** |
| 11 | NexWatch | 内联 :812-854 | `lf nexwatch clone --raw` | ✓ | ✓ | ✓ | 88bit id / raw | ✗ | ✓ | ◐ | 无 | ✓ 2 个测试含真实输出 | **无** | **A** |
| 12 | Presco | `parse_presco` (:1677) | `lf presco clone -d/--sitecode/--usercode` | ✓ | ✓ | ✓ | site/user/full code | ✗ | ✓ | ◐ | 无 | ✓ 3 个测试 | **无** | **A** |
| 13 | Nedap | 内联 :626-663 | `lf nedap clone --st/--cc/--id` | ✓ | ✓ | ✓ | subtype/cc/ID | ✗ | ✓ | ◐ | 无 | ✓ 3 个测试 | **无** | **A** |
| 14 | GProxII | 内联 :580-623 | `lf gproxii clone --xor/--fmt/--fc/--cn` | ✓ | ✓ | ✓ | FC/CN/xor/len | ✗ | ✓ | ◐ | 无 | ✓ 3 个测试 | **无** | **A** |
| 15 | Gallagher | 内联 :517-577 | `lf gallagher clone --rc/--fc/--cn/--il` | ✓ | ✓ | ✓ | RC/FC/CN/IL | ✗ | ✓ | ◐ | 无 | ✓ 3 个测试 | **无** | **A** |
| 16 | PAC / Stanley | `parse_pac` (:1715) | `lf pac clone --cn/--raw` | ✓ | ✓ | ✓ | Card(hex)/raw | ✗ | ✓ | ◐ | 无 | ✓ 3 个测试 | **无** | **A** |
| 17 | Noralsy | `parse_noralsy` (:1754) | `lf noralsy clone --cn -y` | ✓ | ✓ | ✓ | CN/year/raw | ✗ | ✓ | ◐ | 无 | ✓ 2 个测试 | **无** | **A** |
| 18 | Jablotron | 内联 :681-696 | `lf jablotron clone --cn` | ✓ | ✓ | ✓ | CN(hex) | ✗ | ✓ | ◐ | 无 | ✓ 2 个测试 | **无** | **A** |
| 19 | SecuraKey | 内联 :699-714 | `lf securakey clone --raw` | ✓ | ✓ | ◐ | raw | ✗ | ✓ | ◐ | 无 | ✓ 2 个测试 | **无** | **A** |
| 20 | Visa2000 | 内联 :717-732 | `lf visa2000 clone --cn` | ✓ | ✓ | ✓ | CN | ✗ | ✓ | ◐ | 无 | ✓ 2 个测试 | **无** | **A** |
| 21 | Motorola | 内联 :735-750 | `lf motorola clone --raw` | ✓ | ✓ | ◐ | raw | ✗ | ✓ | ◐ | 无 | ✓ 2 个测试 | **无** | **A** |
| 22 | IDTECK | 内联 :753-768 | `lf idteck clone --raw` | ✓ | ✓ | ◐ | raw | ✗ | ✓ | ◐ | 无 | ✓ 2 个测试 | **无** | **A** |

**注：**

1. "字段解码" 指 Phosphor 从 Client 文本里提取出的字段，不是从比特流解码出的字段。
2. "格式转换" 指 Phosphor 自身做了 numeric 转换（例如 IO Prox 的 FC hex→decimal）。其余卡型基本是字符串原样透传。
3. "复读验证" 仅有一种通用实现 `verify_clone` → `lf search` → 字段比较（`write.rs:438-497`）。它对所有卡型都可用，但**没有按卡型的专用 reader 命令**（注释 `write.rs:465-467` 明确说明这是为了避免输出格式不兼容）。因此标 ◐。
4. HID 的格式转换缺陷见 §5.1。

### 2.1 非可克隆 LF 卡型（仅检测）

| 卡型 | 检测实现 | 字段解析 | 写入 | 等级 | 说明 |
|---|---|---|---|---|---|
| COTAG | `COTAG_RE` `[\+].*COTAG` (:253) | ✗ | ✗（`build_clone_command` 返回 `None`） | **B** | 仅识别"存在"，UID 固定写 `"COTAG"` |
| EM4x50 | `EM4X50_RE` (:257) | ✗ | ✗ | **B** | 理由见 `types.rs:142`：需要原生 EM4x50 空白卡 |
| Hitag | `HITAG_RE` (:261) | ✗ | ✗ | **B** | 理由见 `types.rs:143`：需要原生 Hitag 芯片 |

> 这三种的"检测"代码只判断 `[+] ... COTAG` 这类存在性字符串，**不提取任何 ID**，UI 上只能显示 "COTAG / EM4x50 / Hitag"。

---

## 3. 任务书要求清单的逐项对照

| 任务书列出的卡型 | Phosphor 是否有独立类型 | 实现路径 | 等级 | 备注 |
|---|---|---|---|---|
| EM4100 | ✓ `EM4100` | 专用正则 + `lf em 410x clone` | A | |
| HID Prox | ✓ `HIDProx` | `parse_hid` | A | |
| **HID ProxII** | **✗** | 归入 `HIDProx`（靠 `contains("HID Prox")`） | **C** | 无 ProxII 专属字段/命令；`lf hid` 的 `-r` 可写原始 bit，但 Phosphor 不区分 ProxII 格式 |
| **HID Corporate 1000** | **✗** | `parse_hid` 里 `contains("HID Corporate")` 也进同一分支 | **C** | 见 §5.1 的 format 缺陷 |
| AWID | ✓ | `parse_awid` | A | |
| Indala | ✓ | 内联 | A | |
| IOProx | ✓ | `parse_ioprox` | A | |
| FDX-B | ✓ `FDX_B` | `parse_fdxb` | A | |
| Paradox | ✓ | `parse_paradox` | A | |
| Keri | ✓ | `parse_keri` | A | |
| Viking | ✓ | 内联 | A | |
| Visa2000 | ✓ | 内联 | A | |
| Noralsy | ✓ | `parse_noralsy` | A | |
| Presco | ✓ | `parse_presco` | A | |
| Jablotron | ✓ | 内联 | A | |
| NexWatch | ✓ | 内联 | A | |
| PAC | ✓ | `parse_pac` | A | |
| **Stanley** | ◐ | 与 PAC 同一类型（`PAC/Stanley`） | A | 显示名为 "PAC/Stanley" |
| SecuraKey | ✓ | 内联 | A | |
| Gallagher | ✓ | 内联 | A | |
| GProxII | ✓ | 内联 | A | |
| Pyramid | ✓ | `parse_pyramid` | A | |
| NEDAP | ✓ `Nedap` | 内联 | A | |
| **T55x7 / T5577** | ✓ `BlankType::T5577` | 只作为**空白卡**：`detect/chk/wipe` | **C** | **没有** T5577 数据格式解析、block 读写、配置块解析（见 `T55XX_ANALYSIS.md`） |

**结论：任务书列出的 22 项中，Phosphor 真正有独立解析实现的约 18 项；HID ProxII、HID Corporate 1000 只是被并入通用 HID 路径；T5577 只是空白卡管理。**

---

## 4. Client 支持但 Phosphor 未实现的 LF 协议

由 `proxmark3.exe -t` 的命令组清单（`[Client]`）：

| 协议组 | Client 命令存在 | Phosphor 实现 | 备注 |
|---|---|---|---|
| Destron (FDX-A) | `lf destron clone/reader/sim/demod` | **✗** | `destron clone --uid` |
| Trovan | `lf trovan` | **✗** | |
| EM4x50 | `lf em 4x50 rdbl/...` | 仅检测到卡型（不可克隆） | |
| EM4x70 | `lf em 4x70 auth/recover/write/...` | **✗** | 含 `brute`（密码穷举，**本项目不应引入**） |
| PCF7931 | `lf pcf7931 config/...` | **✗** | |
| Hitag 1/2/S/u | `lf hitag info/list/chk/rdbl/wrbl/hts/htu` | 仅检测到卡型 | Client 能力远超 Phosphor |
| TI (Tiris) | `lf ti` | **✗** | |
| Paxton / Verichip | 只有 `lf search` 的打印标签（`PAXTON........`、`VERICHIP......`），**无独立命令组** | **✗** | 识别输出存在，但无 clone 命令 |
| COTAG | `lf cotag reader/demod` | 仅检测 | 不可克隆 |
| EM410x 暴力/嗅探 | `lf em 410x brute/watch/spoof` | **✗** | `brute` 属攻击性功能，**不建议引入** |

> 结论：Phosphor 的 "22 种" 是 Client 能力的一个**真子集**。Client 实际支持的 LF 协议族（含 Hitag、EM4x50/70、Destron、Trovan、PCF7931、TI）比 Phosphor 多得多。

---

## 5. README 声明 vs 实际实现

README（`README.md`）声称：

```
### LF (125 kHz) - 22 types
HID ProxII, EM4100, AWID, IOProx, Indala, FDX-B, HID Corporate 1000,
Paradox, Keri, Viking, Visa2000, Noralsy, Presco, Jablotron, NexWatch,
PAC/Stanley, SecuraKey, Gallagher, GProxII, Pyramid, NEDAP, T55x7
```

逐项核对结果：

| README 声明 | 实际 | 差异 |
|---|---|---|
| "22 types" | 22 个 `CardType` 枚举值 | 数字对得上 |
| "HID ProxII" | 无独立类型，归入 `HIDProx` | **夸大**（无 ProxII 特有处理） |
| "HID Corporate 1000" | 归入 `HIDProx`，且 format 处理有缺陷 | **夸大 + 有 bug** |
| "T55x7" | 只作为空白卡；无 T55xx 数据功能 | **夸大**（把它算进"支持卡型"容易误导） |
| "cloned in seconds" | 有 clone 命令模板 | 逻辑成立，但**无硬件验证记录** |
| "Auto-detection of card type and frequency" | 先 `lf search` 再 `hf search` | 成立（但"频率自动判断"其实是"先试 LF 再试 HF"） |
| "T5577 chip detection and password-protected chip handling" | `detect` + `chk` + `wipe -p` | 成立 |
| "Blank card data check warns if the blank already has data" | `lf search` 后检查是否识别出卡型 | 成立（间接判断，非读 block） |

### 5.1 发现的两个具体缺陷（源码级）

#### 缺陷 1：HID 的 Wiegand 格式会被静默降级为 H10301

`HID_FORMAT_RE`（`output_parser.rs:38`）捕获的文本可能是 `"26-bit"`、`"34-bit"`、`"Corp 1000"`（带空格）等：

```rust
r"(?i)(?:H10301|H10302|H10304|Corp\s*1000|26[- ]?bit|34[- ]?bit|35[- ]?bit|37[- ]?bit)"
```

而 `validate_hid_format`（`command_builder.rs:52`）只接受 4 个字面值：

```rust
const VALID_HID_FORMATS: &[&str] = &["H10301", "H10302", "H10304", "Corp1000"];
```

于是：

- 捕获到 `"26-bit"` → 校验失败 → `format` 被丢弃 → `build_hid_clone` 默认 `H10301`（碰巧正确）。
- 捕获到 `"34-bit"` / `"37-bit"` / `"Corp 1000"` → 同样被丢弃 → **仍然用 H10301 编码**，写出来的卡是错的。

缓解因素：`build_clone_command` 对 HID **优先走 raw 克隆**（`-r <raw>`），只要 `lf search` 输出了 `raw:` 行就不会触发这个缺陷。但 raw 缺失时缺陷会显现。

#### 缺陷 2：Indala 在缺少 `Raw:` 行时可能完全识别失败

`INDALA_RAW_RE` 要求 `Indala.*?Raw[:/\s]*([0-9A-Fa-f]+)`，`INDALA_UID_RE` 要求 `Indala.*?ID[:/\s]*...`。
而 v4.23346 Client 的通用打印标签是 `INDALA 224.... %08X` / `INDALA 64..... %08X`（见 `LF_COMMAND_MAPPING.md` §4），**既没有 `Raw:` 也没有 `ID:`**。
若实际输出只有这一行，`parse_lf_search` 的 Indala 分支两个正则都失败，函数会继续往下走，最终走到兜底 `VALID_TAG_RE`——而兜底只接受 `viking` / `nexwatch`，于是**返回 `None`，UI 报"未检测到卡"**，尽管卡确实被识别了。

> 这两个缺陷说明：**Phosphor 的正则是针对 v4.20728 的真实输出样本拟合的，不是针对 v4.23346。** 移植时必须以本项目 Client 的实际输出重新采集样本。

---

## 6. 真实硬件验证状态

| 项目 | Phosphor 仓库证据 |
|---|---|
| 测试日志 | 无 |
| 硬件型号记录 | 无 |
| 固件版本记录 | README 仅写 "v4.20728+ recommended" |
| 卡片测试清单 | 无 |
| 已知问题列表 | 无（GitHub issue 未纳入审计范围） |

因此本节结论：**Phosphor 的 22 卡型 = "代码 + 正则单元测试"级别，不是"硬件验证"级别。**
任何移植都必须重新做真实硬件（C 类）测试，见项目 [测试报告](../../TEST_REPORT.md) 的未验证清单。

---

## 7. 对移植的直接建议

| 分组 | 卡型 | 建议 |
|---|---|---|
| 先做 | EM4100、HID Prox、AWID、Indala | Client 侧最稳定，`lf search` 输出字段最规整 |
| 次做 | IOProx、Paradox、Keri、Pyramid、Viking、NexWatch、FDX-B | 需要逐字段核对 v4.23346 输出 |
| 后做 | Presco、Nedap、GProxII、Gallagher、PAC、Noralsy、Jablotron、SecuraKey、Visa2000、Motorola、IDTECK | 输出格式最容易随固件变化；建议只做"识别 + 展示 + raw 克隆" |
| 不做 | HID ProxII、HID Corporate 1000 的**结构化**克隆 | 需要正确的 Wiegand format 映射表，且 Client 端 `--wiegand` 参数语义需实测 |
| 单独专题 | T5577 / EM4305 | 见 `T55XX_ANALYSIS.md`，需要 block 级能力，Phosphor 不足 |
