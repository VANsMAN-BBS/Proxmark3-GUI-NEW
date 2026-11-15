# LF Parser 测试报告

**测试工程：** `tests/lfparser/lfparser_tests.pro`
**框架：** QtTest（`QT += core testlib`，无 GUI）
**构建：** qmake + MinGW-w64 GCC 16.2.0（Qt 5.15.19，UCRT64）
**日期：** 2026-11-14

---

## 1. 结果摘要

```
Totals: 59 passed, 0 failed, 0 skipped, 0 blacklisted, 27ms
```

| 指标 | 值 |
|---|---|
| 测试函数 | 59（含 init/cleanup） |
| 通过 | 59 |
| 失败 | 0 |
| 跳过 | 0 |
| 编译警告 | **0**（`-Wall -Wextra` 全量重编译） |
| 运行时间 | 27 ms |

分类统计：

| 分组 | 用例数 |
|---|---|
| 归一化（normalizer） | 5 |
| EM4100 | 11 |
| HID Prox | 14 |
| AWID | 7 |
| Indala | 8 |
| 注册表/分派 | 10 |
| 数据模型 | 2 |
| init/cleanup | 2 |

---

## 2. 样本来源

| 类别 | 数量 | 说明 |
|---|---|---|
| **Client 源码推导** | 全部 fixtures | 布局来自 Iceman `client/src/*`（见 `fixtures/FIXTURES.md`） |
| **审计历史样本（v4.20728）** | HID FC/CN/raw 值、AWID 值 | 来自 `docs/LF_AUDIT/` 记录 |
| **人工构造** | 所有 fixtures 与内联用例 | 明确标注为 synthetic |
| **真实设备日志** | **0** | 当前固件无 LF，无法采集 |

**没有任何样本被标记为真实设备输出。**

fixtures 清单（`tests/lfparser/fixtures/`）：

| 文件 | 协议 | 用途 |
|---|---|---|
| `em4100_standard.txt` | EM4100 | 完整去扰输出 |
| `em4100_descramble_only.txt` | EM4100 | 仅去扰段、无主 ID |
| `hid_h10301.txt` | HID | 单候选标准 |
| `hid_multi_identical.txt` | HID | H10301 + ind26 一致 |
| `hid_conflict.txt` | HID | H10301 vs H10306 冲突 |
| `hid_37bit.txt` | HID | 37-bit |
| `hid_corp1000.txt` | HID | Corporate 1000 |
| `hid_unknown_format.txt` | HID | 裸 FC/CN，无格式名 |
| `awid_26.txt` | AWID | 26-bit |
| `awid_50.txt` | AWID | 50-bit + RBH |
| `indala_64_raw.txt` | Indala | 有 Raw + 全部附加字段 |
| `indala_64_no_raw.txt` | Indala | 无 Raw（关键回归） |
| `no_card.txt` | — | 无卡 |
| `firmware_unsupported.txt` | — | 固件无 LF |
| `mixed_protocols.txt` | EM + HID | 分派优先级 |

---

## 3. 需求覆盖矩阵

任务书 §7 要求的 10 类测试：

| # | 要求 | 覆盖用例 | 结果 |
|---|---|---|---|
| 1 | 标准输出正常识别 | `em4100_standardFixture`、`hid_h10301Fixture`、`awid_26Fixture`、`indala_withRawFixture` | ✅ |
| 2 | 字段顺序变化 | `em4100_fieldOrderIndependent`、`indala_fieldOrderReversed` | ✅ |
| 3 | 可选字段缺失 | `indala_withoutRawFixture`、`awid_missingRaw`、`hid_facilityOnlyKeepsCardNumberMissing` | ✅ |
| 4 | 非法字段不崩溃 | `em4100_invalidPayloadDoesNotCrash`、`awid_invalidRawDoesNotCrash`、`hid_invalidNumericValuesDoNotCrash`、`normalizer_normalizeHexRejectsInvalid` | ✅ |
| 5 | 空输入未识别 | `registry_emptyInput`、`registry_whitespaceInput` | ✅ |
| 6 | 无关文本不误识别 | `registry_unrelatedText`、`registry_firmwareUnsupported`、`registry_noCard` | ✅ |
| 7 | 多协议关键词不误选 | `registry_prefersStrongerSignature`、`hid_indalaKeywordDoesNotStealHid` | ✅ |
| 8 | hex 大小写 | `em4100_lowercaseId`、`hid_lowercaseRaw`、`indala_lowercaseRaw` | ✅ |
| 9 | CRLF/LF | `em4100_crlfInput`、`hid_h10301_crlf` + 全局归一化测试 | ✅ |
| 10 | 未知格式明确返回 | `hid_unknownFormatDoesNotDefault`、`hid_conflictingCandidates`、`indala_markerOnlyIsPartial` | ✅ |

### HID 专项

| 要求 | 用例 | 结果 |
|---|---|---|
| H10301 | `hid_h10301Fixture` → `H10301 (26-bit)` | ✅ |
| 34-bit | `hid_34bit` → `HID 34-bit (H10306)` | ✅ |
| 37-bit | `hid_37bit` → `HID 37-bit (H10304)` | ✅ |
| Corporate 1000 | `hid_corporate1000` → `Corporate 1000 (35-bit)` | ✅ |
| 未知格式 | `hid_unknownFormatDoesNotDefault` → `Unknown HID format` + warning | ✅ |
| 冲突候选 | `hid_conflictingCandidates` → `Ambiguous`，FC/CN absent | ✅ |

### Indala 专项

| 要求 | 用例 | 结果 |
|---|---|---|
| 有 Raw | `indala_withRawFixture` | ✅ |
| 无 Raw | `indala_withoutRawFixture`（仍 Identified + FC/CN） | ✅ |
| 字段顺序变化 | `indala_fieldOrderReversed` | ✅ |
| 关键字段缺失 | `indala_markerOnlyIsPartial`、`indala_fmtWithoutValuesIsPartial` | ✅ |

---

## 4. 关键回归断言

| 断言 | 用例 |
|---|---|
| HID 34/37/Corporate 不再降级为 H10301 | `hid_34bit` / `hid_37bit` / `hid_corporate1000` |
| 无格式名时不默认 H10301 | `hid_unknownFormatDoesNotDefault`（warning 必须包含 "H10301"） |
| 候选冲突时不输出 FC/CN | `hid_conflictingCandidates` |
| Indala 缺 Raw 仍解析 | `indala_withoutRawFixture` |
| 缺失 ≠ 0 | `model_missingIsNotZero`（FC=0 与 absent 区分） |
| 大输入不崩溃 | `registry_largeInputDoesNotCrash`（约 20 万字符） |
| 空输入 → InvalidInput | `registry_emptyInput` |

---

## 5. 执行方式

```powershell
$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"

mkdir <BUILD_DIR>
cd <BUILD_DIR>
qmake-qt5 <PROJECT_DIR>\tests\lfparser\lfparser_tests.pro
mingw32-make -j4
release\tst_lfparser.exe
```

退出码 0 表示全部通过。

---

## 6. 未通过 / 未覆盖

| 项 | 状态 | 说明 |
|---|---|---|
| 失败用例 | 0 | — |
| 真实设备输出 | **未覆盖** | 当前固件无 LF，无法采集；fixtures 全部为合成 |
| Client 版本漂移 | 未覆盖 | 只有 v4.20728 历史值 + 上游源码布局；未用 v4.23346 真实卡输出验证 |
| 其他 18 种 LF 协议 | 未覆盖 | Stage 1 范围外 |
| 并发/多线程调用 | 未覆盖 | 注册表为只读单例，理论线程安全；未做压力测试 |
| 内存泄漏 | 未检测 | 未接入 ASAN/Valgrind；结构体全部为值类型，无明显风险 |

---

## 7. 结论

- 本阶段要求的 10 类测试全部有对应用例并通过。
- HID 与 Indala 的专项回归全部通过。
- 0 警告、0 失败。
- **结论仅适用于合成样本**；真实设备验证需等待固件支持 LF。
