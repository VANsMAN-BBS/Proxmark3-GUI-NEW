# LF-Core Stage 1 — 实施设计记录

**日期：** 2026-11-14
**基线：** `upgrade/v0.4.1-polish` / `245b184` / v0.4.1
**工作区状态：** 干净（仅 `docs/LF_AUDIT/` 为上一阶段新增，未跟踪）
**工具链：** qmake + MinGW-w64 GCC 8.1（C++11）/ Qt 5.15.2（`E:\Qt\5.15.2\mingw81_64`）

---

## 1. 开发前检查结果

| 项 | 结果 |
|---|---|
| 当前分支 | `upgrade/v0.4.1-polish` |
| HEAD | `245b184 fix: keep eMRTD protocol output UTF-8 on Windows pipes` |
| 标签 | `upgrade-v0.4.1` → `245b184` |
| 未提交修改 | 无（新增文档除外） |
| C++ 标准 | `CONFIG += c++11`（`src/Proxmark3GUI.pro:5`） |
| Qt 版本 | 5.15.x（Widgets / SerialPort / Network） |
| 构建方式 | qmake（`.pro`），无 CMake |
| 既有测试框架 | 无 C++ 单元测试工程（仅 `emrtd_pm3/tests` 的 Python 测试） |
| Client 输出清洗 | `ClientSession::cleanText()` 已剥离 ANSI / 归一化 CR（但解析器仍需自带防御） |

**结论：** 基线未变化；本阶段新增独立测试工程，不改动现有构建目标的行为。

---

## 2. Phosphor 解析器提取记录（只读分析）

### 2.1 文件与函数

| 项 | 内容 |
|---|---|
| 文件 | `E:\Opencode_project\phosphor-master\src-tauri\src\pm3\output_parser.rs`（4,082 行） |
| 主入口 | `parse_lf_search(output: &str) -> Option<(CardType, CardData)>`（:415） |
| LF 子解析器 | `parse_hid`(:1306)、`parse_ioprox`(:1349)、`parse_awid`(:1419)、`parse_fdxb`(:1447)、`parse_paradox`(:1503)、`parse_keri`(:1558)、`parse_pyramid`(:1622)、`parse_presco`(:1677)、`parse_pac`(:1715)、`parse_noralsy`(:1754) |
| 内联分支 | EM4100、Indala、Gallagher、GProxII、Nedap、Jablotron、SecuraKey、Visa2000、Motorola、IDTECK、NexWatch、Viking、COTAG、EM4x50、Hitag |
| 正则总数 | **79 条** `static *_RE`（LF 相关约 45 条） |
| 测试 | 该文件 184 个 `#[test]`；全项目 225 个 |

### 2.2 分派顺序（`parse_lf_search`）

```
No known 125/134 kHz → None
EM410x → HID Prox/Corporate → Indala → IO Prox → AWID → FDX-B → Paradox
→ Keri → Pyramid → Gallagher → GProxII → Nedap → Presco → PAC → Noralsy
→ Jablotron → SecuraKey → Visa2000 → Motorola → IDTECK
→ COTAG / EM4x50 / Hitag → NexWatch → Viking → VALID_TAG 兜底 → None
```

### 2.3 字段映射（本次涉及的 4 种）

| 协议 | Phosphor 正则 | 提取字段 | 失败行为 |
|---|---|---|---|
| EM4100 | `EM 410x ID\s*[-:]?\s*([0-9A-Fa-f]{10})` | `id` | 无匹配 → 继续向下，最终 `None` |
| HID | `(?:HID\|Prox).*?RAW[:/\s]*([0-9A-Fa-f]+)`；`FC[:/\s]*(\d+).*?CN[:/\s]*(\d+)`；格式正则 `26-bit/34-bit/Corp 1000…` | `facility_code`、`card_number`、`raw`、`format` | 无 FC/CN/raw → `None` |
| AWID | `AWID.*?FC[:/\s]*(\d+).*?(?:CN\|Card)[:/\s]*(\d+)`；`AWID\s*(?:-\s*)?(?:len[:/\s]*)?(\d+)` | FC、CN、format | 无 FC/CN → `None` |
| Indala | `Indala.*?Raw[:/\s]*([0-9A-Fa-f]+)`；`Indala.*?ID[:/\s]*([0-9A-Fa-f]+)` | raw / id | **两个都失败 → 继续向下，最终 `None`** |

### 2.4 已确认的缺陷（本阶段必须修复）

1. **HID 静默降级**：`validate_hid_format` 只接受 `H10301/H10302/H10304/Corp1000`，而捕获值可能是 `26-bit`、`37-bit`、`Corp 1000`（带空格）→ 校验失败 → `build_hid_clone` 默认 `H10301`。
2. **Indala 缺 Raw 即失配**：无 `Raw:` 与 `ID:` 标签时整条协议被丢弃（Phosphor 兜底只接受 viking/nexwatch）。
3. **宽泛 `contains` + 先匹配先返回**：多协议文本可能选错。
4. **字段缺失无区分**：`CardData.uid` 用空串/拼接串表示，无法区分"缺失"与"0"。

---

## 3. v4.23346 输出格式证据

来源分三类，均在实现中标注：

### 3.1 本机 Client 二进制字符串（`proxmark3.exe`，v4.23346）

- `EM 410x ID `
- `HID Prox ID`、`HID-26........ %08X`
- `AWID.......... %08X`、`[0m FC: `、`[0m  CN: `
- `Indala ID`、`INDALA 224.... %08X`、`INDALA 64..... %08X`
- `Raw: `、`[=] raw: %s`

### 3.2 Iceman 上游源码（`RfidResearchGroup/proxmark3`，master，2026-11）

> 说明：本机 Client 为 `v4.23346-suspect`，其精确 commit `9ce8a4b6c` 在 GitHub 不可解析；以下格式取自当前上游源码，作为**格式语义依据**，不声称与二进制逐字一致。

| 协议 | 源码位置 | 打印格式 |
|---|---|---|
| EM4100 | `cmdlfem410x.c:115` 等 | `EM 410x ID %010X`；`EM410x ( RF/%d )`；`Unique TAG ID      : %010X`；`DEZ 10         : %010u`；`Pattern CDVI       : FC: %u CN: %u` |
| HID | `wiegand_formats.c:1598` `hid_print_card` | `[%-8s] %-32s FC: %u  CN: %u … parity ( ok/fail )`；`demodHID` 追加 `raw: %08x%08x%08x`；`found N matching M-bit formats` |
| AWID | `cmdlfawid.c` `awid_decode_bits` | `AWID - len: %d FC: %d Card: %u - Wiegand: %x, Raw: %08x%08x%08x`；未知长度变体 `AWID - len: %d -unknown- (%u) …` |
| Indala | `cmdlfindala.c` `indala_print_decoded` | `Indala (len %zu)  Raw: %x%08x`；`Fmt 26 FC: %u Card: %u Parity: %1d%1d`；`Internal ID... %llu`；`Heden-2L...... %u`；`Keyscan 40010. FC: %u CN: %u` |

### 3.3 审计阶段记录的历史样本（v4.20728，Phosphor 测试夹具）

用于构造"旧版本兼容"用例，例如：
`[+] HID Prox … FC: 65  CN: 29334`、`[+] raw: 200078BE5E1E`、`IO Prox - XSF(01)65:01337`。

**样本来源将在 `docs/LF_CORE/PARSER_TEST_REPORT.md` 与 fixtures 的 `FIXTURES.md` 中逐条标注。**

---

## 4. 模块设计

### 4.1 目录

```
src/lf/
├── lfparsetypes.h          枚举 / Optional<T> / LFParseResult
├── lftextnormalizer.h/.cpp 文本归一化（ANSI/CRLF/HEX）
├── lfparser.h/.cpp         ILfParser + LFParserRegistry（分派）
├── em4100parser.h/.cpp
├── hidproxparser.h/.cpp
├── awidparser.h/.cpp
└── indalaparser.h/.cpp
```

不依赖 QWidget / QProcess / 网络；仅使用 QtCore（`QString`、`QRegularExpression`、`QStringList`）。

### 4.2 分派模型（替代 `contains` 链）

- 每个解析器实现 `int detect(normalized, evidence)`，返回**结构化证据分数**：
  - 100：协议专属结构（如 `EM 410x ID <hex>`、`AWID - len: N`、`Indala (len N) Raw:`、`[FMT] … N-bit … FC:`）
  - 70–90：较强结构（如 `Fmt 26 FC:`、`HID Prox ID found!` + `FC:`）
  - 40：仅协议关键词
- 注册表取最高分；**只调用该解析器**（不级联降级）。
- 同分不同协议 → 记录歧义 warning，按固定优先级选择，并降低置信度。
- 最高分 < 40 → `Unidentified`。
- 空输入 → `InvalidInput`。

### 4.3 数据模型要点

- 全部可选字段使用 `Lf::Optional<T>`，**缺失 ≠ 0**。
- `facilityCode` / `cardNumber` / `bitLength` / `decimalId` 为 Optional。
- `rawData` / `hexId` 为空串表示缺失（hex 不可能合法为空）。
- `formatCandidates` 保留 HID 的全部候选格式。
- `warnings` / `unparsedFields` / `sourceText` 用于诊断。
- `status` 四态：`Identified` / `Partial` / `Unidentified` / `InvalidInput`。

### 4.4 HID 决策（核心）

1. 解析所有 `[NAME] description FC: … CN: …` 候选行。
2. 0 候选：若有 `FC:/CN:` 裸行 → `format="Unknown HID format"`，**禁止默认 H10301**。
3. 1 候选：使用该候选。
4. 多候选：
   - 若所有候选取值一致 → 采用第一个，`formatCandidates` 全量保留，`confidence=Medium`。
   - 若取值冲突 → `status=Partial`，不填 FC/CN，`format="Ambiguous"`，warning 列出冲突。
5. `format` 分类：`H10301` / `H10306` / `Corporate 1000` / `HID 37-bit (NAME)` / `HID <N>-bit (NAME)` / `<NAME> (<desc>)`。

### 4.5 Indala 决策

- `Raw` 可选；`Fmt/FC/Card` 可选；两者任一存在即可返回 `Identified/Partial`。
- 字段顺序无关（正则各自独立匹配）。
- 缺 Raw 但有 FC/CN → `Partial` + warning（不丢弃结果）。
- 仅 `Indala` 关键词 → `Partial` + warning。

---

## 5. 测试设计

- 独立工程：`tests/lfparser/lfparser_tests.pro`（`QT += core testlib`）。
- fixtures：`tests/lfparser/fixtures/*.txt` + `FIXTURES.md`（逐文件标注来源）。
- 覆盖：标准输出、字段顺序、可选缺失、非法值、空输入、无关文本、多协议关键词、大小写、CRLF/LF、未知格式。
- HID 额外：H10301 / 34-bit / 37-bit / Corporate 1000 / 未知。
- Indala 额外：有 Raw / 无 Raw / 顺序变化 / 关键字段缺失。
- 编译选项：`-Wall -Wextra`，要求零警告。

---

## 6. 许可证边界

- 不复制 Phosphor 代码、正则集合、函数实现。
- 仅参考：审计文档中的行为描述、Client 公开输出格式、协议字段语义。
- 正则与分派机制按本项目需求重新设计。
- 详细记录见 `docs/LF_CORE/LICENSE_REUSE_NOTES.md`。
