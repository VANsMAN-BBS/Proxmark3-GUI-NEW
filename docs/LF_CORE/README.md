# LF-Core Stage 1 — LF Parser Module

**版本：** Stage 1（文本解析器）
**基线：** Proxmark3GUI v0.4.1 / `upgrade/v0.4.1-polish` / `245b184`
**模块位置：** `src/lf/`
**测试位置：** `tests/lfparser/`
**许可证：** 随主项目 LGPL-2.1-or-later

---

## 1. 模块是什么

把 Proxmark3 Client 的 **LF 文本输出**转换成结构化的 `LFParseResult`。

```
Client 文本  ──►  LFTextNormalizer  ──►  LFParserRegistry  ──►  LFParseResult
                        │                      │
                  ANSI/CRLF 清洗        按证据分数选择协议
                                               │
                    ┌──────────────┬───────────┼──────────────┐
                 EM4100Parser  HIDProxParser  AWIDParser  IndalaParser
```

**它不是：**

- 不是波形解码器（Manchester/FSK/PSK 仍由 Client + 固件完成）；
- 不执行任何 Client 命令；
- 不访问串口、USB、设备；
- 不依赖 QWidget；
- 不写卡、不擦卡、不模拟、不克隆。

---

## 2. 目录结构

```
src/lf/
├── lfparsetypes.h          枚举、Optional<T>、LFParseResult
├── lfparsetypes.cpp        名称映射、summary()
├── lftextnormalizer.h/.cpp ANSI/CRLF/Hex 归一化
├── lfparser.h/.cpp         ILfParser 接口 + 注册表 + 分派
├── em4100parser.h/.cpp     EM4100
├── hidproxparser.h/.cpp    HID Prox / Wiegand
├── awidparser.h/.cpp       AWID 26/34/36/37/40/50
└── indalaparser.h/.cpp     Indala 64/224

tests/lfparser/
├── lfparser_tests.pro      QtTest 独立工程
├── tst_lfparser.cpp        59 个测试用例
└── fixtures/               合成样本 + FIXTURES.md（来源说明）
```

规模：模块 **1,463 行**，测试 **600 行**。

---

## 3. 调用方式

```cpp
#include "lf/lfparser.h"

// 直接传入 ClientSession 收到的整段输出
const LFParseResult result = LFParserRegistry::instance().parse(clientOutput);

if (result.status == Lf::Status::Identified) {
    qInfo("protocol=%s format=%s raw=%s",
          qPrintable(Lf::protocolName(result.protocol)),
          qPrintable(result.format),
          qPrintable(result.rawData));
}

if (result.facilityCode.isPresent())
    qInfo("FC=%u", result.facilityCode.value());

for (const QString &warning : result.warnings)
    qWarning("%s", qPrintable(warning));
```

指定协议解析（调试/测试用）：

```cpp
const LFParseResult em = LFParserRegistry::instance().parseAs(Lf::Protocol::EM4100, text);
```

模块只依赖 QtCore，可直接被未来的 LF 页面、CLI 工具或单元测试调用。

---

## 4. 当前支持范围

| 协议 | 状态 | 说明 |
|---|---|---|
| EM4100 | ✅ | 标准 ID、XL、Electra、128b frame、去扰字段 |
| HID Prox | ✅ | H10301、34-bit、37-bit、Corporate 1000、未知格式、raw |
| AWID | ✅ | 26/34/36/37/40/50、未知长度、raw |
| Indala | ✅ | 64/224、Raw 可选、字段顺序无关、Heden/Keyscan 附加信息 |
| 其他协议 | ❌ | 不在 Stage 1 范围（见 `docs/LF_AUDIT/LF_CARD_SUPPORT_MATRIX.md`） |

---

## 5. 设计原则（摘要）

1. **缺失 ≠ 0**：所有可选值使用 `Lf::Optional<T>`。
2. **不猜测**：HID 无法确认格式时返回 `Unknown HID format` / `Ambiguous`，绝不默认 H10301。
3. **不级联**：选中一个协议后，解析失败不再降级到其他协议。
4. **结构优先**：识别依据是协议专属结构（`EM 410x ID …`、`AWID - len:`、`Indala (len N) Raw:`、`[FMT] … N-bit … FC:`），不是关键词。
5. **保留证据**：`sourceText`、`formatCandidates`、`warnings`、`unparsedFields` 全部保留。

详细设计见 `PARSER_DESIGN.md`，实现细节见 `PARSER_IMPLEMENTATION.md`。
