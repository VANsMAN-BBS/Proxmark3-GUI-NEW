# LF-Core Stage 1 阶段报告

**阶段：** LF-Core Stage 1 — LF Parser Extraction & Native C++ Implementation
**日期：** 2026-11-14
**基线：** Proxmark3GUI v0.4.1 / `upgrade/v0.4.1-polish` / `245b184`
**结论：** **Stage 1 范围内全部完成，59/59 测试通过，0 警告。建议进入 Stage 2（条件见 §7）。**

---

## 1. 完成情况

| 任务 | 状态 |
|---|---|
| 开发前检查（分支/HEAD/工作区/工具链） | ✅ |
| 阅读 `docs/LF_AUDIT/` | ✅ |
| 提取 Phosphor 解析器信息（文件/函数/正则/顺序/字段/失败行为） | ✅ `STAGE1_DESIGN_RECORD.md` |
| 核对 v4.23346 输出格式 | ✅（二进制字符串 + 上游源码 + 历史样本） |
| 统一数据模型 | ✅ `lfparsetypes.h` |
| 文本归一化 | ✅ `lftextnormalizer.h/.cpp` |
| 通用分派器 | ✅ `lfparser.h/.cpp` |
| EM4100 解析器 | ✅ |
| HID Prox 解析器（含格式区分） | ✅ |
| AWID 解析器 | ✅ |
| Indala 解析器（Raw 可选） | ✅ |
| 独立单元测试工程 | ✅ 59 用例 |
| fixtures + 来源说明 | ✅ 15 个文件 |
| 主项目构建集成 | ✅ `.pro` + 全量构建通过 |
| 6 份交付文档 | ✅ |
| 未修改 GUI / HF / NTAG / ClientSession / 固件 | ✅ |

---

## 2. 代码变更清单

### 2.1 修改（1 个文件）

```
src/Proxmark3GUI.pro   +14 行（仅追加 SOURCES/HEADERS）
```

**没有修改任何既有 `.cpp/.h/.ui/.json`。**

### 2.2 新增模块（14 个文件，1,463 行）

| 文件 | 行数 |
|---|---|
| `src/lf/lfparsetypes.h` | 105 |
| `src/lf/lfparsetypes.cpp` | 87 |
| `src/lf/lftextnormalizer.h` | 25 |
| `src/lf/lftextnormalizer.cpp` | 69 |
| `src/lf/lfparser.h` | 55 |
| `src/lf/lfparser.cpp` | 149 |
| `src/lf/em4100parser.h` | 26 |
| `src/lf/em4100parser.cpp` | 170 |
| `src/lf/hidproxparser.h` | 26 |
| `src/lf/hidproxparser.cpp` | 388 |
| `src/lf/awidparser.h` | 21 |
| `src/lf/awidparser.cpp` | 143 |
| `src/lf/indalaparser.h` | 25 |
| `src/lf/indalaparser.cpp` | 174 |

### 2.3 新增测试（600 行 + fixtures）

```
tests/lfparser/lfparser_tests.pro
tests/lfparser/tst_lfparser.cpp
tests/lfparser/fixtures/*.txt         (15)
tests/lfparser/fixtures/FIXTURES.md
```

### 2.4 新增文档

```
docs/LF_CORE/STAGE1_DESIGN_RECORD.md
docs/LF_CORE/README.md
docs/LF_CORE/PARSER_DESIGN.md
docs/LF_CORE/PARSER_IMPLEMENTATION.md
docs/LF_CORE/PARSER_TEST_REPORT.md
docs/LF_CORE/LICENSE_REUSE_NOTES.md
docs/LF_CORE/LF_CORE_STAGE1_REPORT.md
```

---

## 3. 测试结果

### 3.1 新增 LF 测试

```
Totals: 59 passed, 0 failed, 0 skipped, 0 blacklisted, 27ms
```

- 全量 clean rebuild：`-Wall -Wextra`，**0 warning**。
- 覆盖 10 类必需场景 + HID/Indala 专项。

### 3.2 主项目原有测试

| 项 | 结果 |
|---|---|
| C++ 单元测试 | 项目无既有 C++ 测试工程（不是本阶段引入） |
| Python eMRTD 测试 | `py -3 -m pytest emrtd_pm3/tests -q` → **47 passed** |
| 主程序构建 | 独立 shadow build，`Proxmark3GUI.exe` 链接成功，0 错误/0 新增警告 |

### 3.3 构建环境

| 项 | 值 |
|---|---|
| Qt | 5.15.19（MSYS2 UCRT64，与 `build-final` 一致） |
| 编译器 | MinGW-w64 GCC 16.2.0 |
| 标准 | `-std=gnu++11` |
| 测试 Qt | 5.15.19 QtTest |

> 备注：`E:\Qt\5.15.2\mingw81_64` 的 qmake 因许可证检查失败不可用（"Qt license file was not found"），因此统一使用 `C:\msys64\ucrt64`。

---

## 4. 15 个必答问题

### 1. 四类解析器分别支持哪些 Client 输出格式？

| 协议 | 支持格式 |
|---|---|
| **EM4100** | `EM 410x ID %010X`；`EM 410x XL ID`；`Electra`；`on 128b frame with data`；`EM410x ( RF/N )`；`Unique TAG ID`；`DEZ 8/10/14/15`；`Pattern CDVI`；`Short ID on 128b frame` |
| **HID Prox** | `[NAME] description N-bit … FC: … CN: … Issue: … OEM: … parity ( ok/fail )`；`raw: <hex>`；`found N matching M-bit format(s)`；无格式名的裸 `FC:/CN:` |
| **AWID** | `AWID - len: N FC: … Card: … - Wiegand: …, Raw: …`；`AWID - len: N -unknown- (value) … Raw: …`；无 Raw 变体；`Cansec 37-bit`；`HCC40`；`RBH - CN` |
| **Indala** | `Indala (len N)  Raw: …`（64/224）；`Fmt 26 FC: … Card: … Parity: …`；`Printed`；`Internal ID`；`Heden-2L`；`Keyscan 40010`；`Odd size` warning |

### 2. 每个解析器提取哪些字段？

| 字段 | EM4100 | HID | AWID | Indala |
|---|---|---|---|---|
| `bitLength` | — | ✅（description / summary） | ✅（len） | ✅（len） |
| `facilityCode` | —（不提升派生值） | ✅ | ✅ | ✅（Fmt 26） |
| `cardNumber` | — | ✅ | ✅ | ✅（Fmt 26） |
| `rawData` / `hexId` | ✅（ID） | ✅（raw） | ✅（raw） | ✅（raw） |
| `decimalId` | ✅（仅 Client `DEZ`） | — | — | 仅当缺 CN 时用 internalId |
| `format` | EM4100/XL/128b | H10301 / HID 34/37-bit / Corporate 1000 / Unknown / Ambiguous | AWID N-bit | Indala N-bit / Fmt |
| `extraFields` | RF、DEZ8/14/15、Electra、frame、CDVI | Issue、OEM、parity、wiegandFormat | Cansec、HCC40、RBH | wiegandFormat、parity、internalId、Heden、Keyscan、Printed |

### 3. 哪些字段允许缺失？

- `facilityCode` / `cardNumber`：所有协议均允许（HID 的 FC-only 格式、Indala 仅 Raw、AWID 仅 Raw）。
- `bitLength`：EM4100 永远缺失（Client 不打印）；其他协议可缺失。
- `decimalId`：仅 EM4100 且 Client 输出去扰段时存在。
- `rawData`：Indala 明确允许缺失；AWID 允许缺失（warning）。
- 任何缺失都以 `Optional::isAbsent()` 表示，**不会写 0**。

### 4. HID 34-bit、37-bit、Corporate 1000 是否能正确区分？

**能。**

- 34-bit → `HID 34-bit (H10306)`
- 37-bit → `HID 37-bit (H10304)` / 其他 37-bit 名称
- Corporate 1000 → `Corporate 1000 (35-bit)`（也支持 48-bit 变体，按 description 判断）
- 无法确认 → `Unknown HID format` / `Ambiguous`，**绝不默认 H10301**

### 5. Indala 缺少 Raw 字段时是否仍可解析？

**是。**
`indala_withoutRawFixture`：无 `Raw:`，仅 `Fmt 26 FC: 123 Card: 1337 Parity: 11` → `Identified`，FC=123，CN=1337，附 warning。
若只有 `Indala` 关键词 → `Partial`，不丢弃协议。

### 6. 是否修复了 Phosphor 原有的错误分派问题？

**是。**

| 原问题 | 修复 |
|---|---|
| `contains` 先匹配先返回 | 证据评分 + 只调用最高分解析器 |
| 解析失败继续降级 | 不级联；返回 Partial + warning |
| 关键词即可判定 | 结构证据 100 分，关键词仅 40–50 分 |
| 兜底只认 viking/nexwatch | 移除跨协议兜底 |
| 多协议无歧义提示 | 同分附加 warning |

### 7. 是否存在解析器之间的冲突？

- 设计上允许同分，但按固定优先级（EM4100 > HID > AWID > Indala）确定性选择，并附加 warning。
- 测试覆盖：`mixed_protocols`（EM+HID 各 100 分）→ EM4100；`hid_indalaKeywordDoesNotStealHid` → HID。
- 未发现实际冲突用例。

### 8. 当前 Client v4.23346 输出格式是否有充分证据？

**对实现足够，对硬件验证不足。**

- **有**：本机 `proxmark3.exe` 字符串（`EM 410x ID`、`HID Prox ID`、`AWID`、`Indala`、`FC:`/`CN:`/`Raw:` 等）。
- **有**：Iceman 上游源码中的精确 `PrintAndLogEx` 格式。
- **有**：审计阶段的 v4.20728 真实样本（用于旧版本兼容）。
- **缺**：v4.23346 + 含 LF 固件的真实卡输出。当前固件无 LF，无法采集。

因此所有结论标注为"源码依据 + 合成测试"，不是"硬件验证"。

### 9. 哪些测试使用真实输出，哪些使用合成数据？

- **真实设备输出：0 个。**
- **上游源码格式推导**：全部 fixtures 的布局。
- **审计历史样本值**：HID 的 FC/CN/raw、AWID 值。
- **人工构造**：所有 fixtures 与内联边界用例。

逐文件来源见 `tests/lfparser/fixtures/FIXTURES.md`。

### 10. 是否存在许可证风险？

**低。**
未复制 Phosphor 代码/正则集合/函数实现；未引入 GPL 依赖；未修改主项目许可证。详见 `LICENSE_REUSE_NOTES.md`。

### 11. 是否影响现有 HF / NTAG 功能？

**否。**
- 未修改任何既有源码（仅 `.pro` 追加 14 行）。
- 全量构建通过。
- 既有 Python eMRTD 测试 47/47 通过。
- 新模块不注册任何 Qt 全局对象、不启动线程、不访问设备。

### 12. 新模块是否能够被未来 Qt GUI 直接调用？

**可以。**
`LFParserRegistry::instance().parse(text)` 一行调用；只依赖 QtCore；返回值类型 `LFParseResult`。

### 13. 原有测试是否全部通过？

- 项目无既有 C++ 测试工程。
- 既有 Python 测试：47 passed。
- 主程序构建：通过。

### 14. 新增测试通过率是多少？

**59 / 59 = 100%**，0 skipped，0 warning。

### 15. 是否可以进入 LF-Core Stage 2？

**建议可以，但带条件：**

1. Stage 2 仍应保持"只读解析/展示"，不进入写入。
2. 需要尽快采集 **v4.23346 + LF 固件** 的真实输出样本，替换/补充合成 fixtures。
3. 需要决定 Gate 0（固件路线），否则所有真实硬件结论仍无法闭环。
4. 扩展协议时沿用"证据评分 + 独立解析器 + fixtures"模式。

---

## 5. 交付状态分类

### 5.1 已实现

- 4 个协议解析器 + 归一化 + 注册表 + 数据模型。
- HID 多候选/歧义处理。
- Indala Raw 可选。
- 独立测试工程与 fixtures。

### 5.2 已通过软件测试

- 59/59 单元测试。
- 主程序全量构建。
- 既有 Python 测试 47/47。

### 5.3 有源码依据但尚未验证

- 所有针对 v4.23346 的字段格式（来自二进制字符串 + 上游源码）。
- HID 35/48-bit Corporate 1000 的实际输出（源码有格式，未见过真实样本）。
- AWID 36-bit（源码存在，未构造用例）。
- Indala 224-bit 的真实输出（仅合成）。
- EM4100 `XL` / `Electra` / `128b frame` 的真实输出。

### 5.4 尚未实现

- 其余 18 种 LF 协议。
- 协议专用 reader 输出解析。
- 与 GUI 的接线（本阶段明确不做）。
- 任何写入/擦除/克隆/模拟。
- 真实硬件验证。

### 5.5 存在风险

| 风险 | 说明 | 缓解 |
|---|---|---|
| Client 输出漂移 | 字段名/顺序变化会导致解析退化 | 评分 + 多正则 + warning；未来用真实样本回归 |
| 合成样本过拟合 | 测试全绿不代表真实输出 | 明确标注；Stage 2 采集真实样本 |
| HID 多候选歧义 | 物理卡可能有多种 Wiegand 解释 | 不猜测，返回 Ambiguous |
| 关键词误触发 | 纯日志文本含 "AWID"/"Indala" 会返回 Partial | 可接受；不会误填字段 |
| 固件无 LF | 无法端到端验证 | Gate 0 |

---

## 6. 验证命令

```powershell
# 测试
$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"
cd C:\Users\van\AppData\Local\Temp\opencode\lfcore-build2
mingw32-make -j4
release\tst_lfparser.exe

# 主程序构建（shadow build）
cd C:\Users\van\AppData\Local\Temp\opencode\lfcore-mainbuild
qmake-qt5 E:\Opencode_project\Proxmark3GUI-master\src\Proxmark3GUI.pro -spec win32-g++ "CONFIG+=release"
mingw32-make -j4

# 既有 Python 测试
cd E:\Opencode_project\Proxmark3GUI-master
py -3 -m pytest emrtd_pm3/tests -q
```

---

## 7. 下一阶段建议

1. **采集真实样本**（最高优先级）：需一台 LF 可用的设备；采集 EM4100/HID/AWID/Indala 的 `lf search` 原始输出，加入 fixtures 并标注真实来源。
2. **扩展协议**：按审计矩阵的 P1 顺序（IOProx、Paradox、Keri、Pyramid、Viking、NexWatch、FDX-B…），沿用同一架构。
3. **GUI 接线（独立阶段）**：LF 页识别区调用 `LFParserRegistry`，只读展示；不做写入。
4. **Gate 0 决策**：固件路线未定前，不进入任何写操作。

**本阶段到此停止，不启动 Stage 2。**
