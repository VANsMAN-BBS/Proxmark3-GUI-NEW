# LF Parser 实现说明

**模块：** `src/lf/`
**目标 Client：** Iceman v4.23346 家族（本机 `4.23346-suspect`）

---

## 0. 格式证据来源

| 代号 | 来源 | 用途 |
|---|---|---|
| **[B]** | 本机 `proxmark3.exe` 字符串（`v4.23346`） | 标签名、字段名 |
| **[S]** | Iceman 上游源码 `client/src/*`（master，2026-11） | 精确打印格式与字段顺序 |
| **[A]** | `docs/LF_AUDIT/` 的 v4.20728 历史样本 | 旧版本兼容用例 |
| **[C]** | 人工构造 | 边界/异常用例 |

> v4.23346 的精确 commit `9ce8a4b6c` 在 GitHub 不可解析（`suspect` 构建），因此 [S] 取自当前上游源码，仅作为**格式语义**依据，不声称与二进制逐字节一致。

---

## 1. EM4100

### 1.1 Client 输出（证据）

```cpp
// [S] client/src/cmdlfem410x.c:115 / :158-171
PrintAndLogEx(SUCCESS, "EM 410x ID %010" PRIX64, id);
PrintAndLogEx(SUCCESS, "EM410x ( RF/%d )", g_DemodClock);
PrintAndLogEx(SUCCESS, "Unique TAG ID      : %010" PRIX64, id2lo);
PrintAndLogEx(SUCCESS, "    DEZ 10         : %010" PRIu64, id & 0xFFFFFFFF);
PrintAndLogEx(SUCCESS, "Pattern CDVI       : FC: %" PRIu64 " CN: %" PRIu64, ...);
```

`[B]` 亦可见 `EM 410x ID `、`EM 410x XL ID`、`EM410x ( RF/`。

### 1.2 识别规则

| 证据 | 分数 |
|---|---|
| `EM 410x (XL)? ID <hex>` | 100 |
| `EM 410x XL ID` | 90 |
| `EM410x` | 60 |
| `EM 410x` | 50 |

### 1.3 字段映射

| 结果字段 | 来源 | 缺失时 |
|---|---|---|
| `hexId` / `rawData` | `EM 410x ID (\w{10})`；XL 为 6+16 hex | 回退到 `Unique TAG ID`，并 warning |
| `decimalId` | `DEZ 10`（优先）/ `DEZ 14/IK2` | absent（**不计算**） |
| `extraFields.rfDivisor` | `EM410x ( RF/(\d+) )` | 省略 |
| `extraFields.uniquetagId` | `Unique TAG ID` | 省略 |
| `extraFields.dez8/dez14/dez15` | 对应行 | 省略 |
| `extraFields.electra` | `Electra (\d{3})` | 省略 |
| `extraFields.frame/frameData` | `on 128b frame with data (\w{11})` | 省略 |
| `extraFields.patternCdviFc/Cn` | `Pattern CDVI` | 省略 |

**重要：** `Pattern CDVI` / `Pattern Paxton` 等是 Client 的**去扰派生值**，不是 EM4100 的 FC/CN。实现只放进 `extraFields`，并加 warning，**不写入 `facilityCode` / `cardNumber`**。

### 1.4 状态

| 条件 | status | confidence |
|---|---|---|
| 有 ID + `DEZ 10` | Identified | High |
| 有 ID | Identified | Medium |
| 仅标记 | Partial | Low |

### 1.5 已知限制

- 不计算 40-bit ID ↔ 十进制转换（只采信 Client 的 `DEZ`）。
- 不推断 `bitLength`（Client 未打印卡身份位宽；128b 只记在 `extraFields.frame`）。
- `%010X` 保证 10 位，但旧固件可能输出小写；已统一大写。

---

## 2. HID Prox

### 2.1 Client 输出（证据）

```cpp
// [S] client/src/wiegand_formats.c:1598 hid_print_card()
PrintAndLogEx(SUCCESS, "[%-8s] %-32s %s", format.Name, format.Description, s);
// s = "FC: 65  CN: 29334  parity ( ok )"（按格式字段拼接）
// [S] client/src/cmdlfhid.c demodHID()
PrintAndLogEx(INFO, "raw: " _GREEN_("%08x%08x%08x"), hi2, hi, lo);
PrintAndLogEx(INFO, "found %u matching %d-bit format%s", ...);
```

示例（`+` 为日志前缀）：

```
[+] [H10301  ] HID H10301 26-bit                FC: 65  CN: 29334  parity ( ok )
[+] raw: 200078be5e1e
[+] found 1 matching 26-bit format
```

### 2.2 识别规则

| 证据 | 分数 |
|---|---|
| 行首可选 `[+]` + `[NAME] … N-bit … FC:` | 100 |
| `HID Prox` + `FC:` | 90 |
| `found N matching M-bit format` | 80 |
| `HID Prox` | 50 |

> 候选行正则要求括号内以字母开头：`\[[A-Za-z][A-Za-z0-9_]{0,31}\s*\]`。
> 这样 `[+]`、`[-]`、`[=]`、`[!!]` 日志标记不会被误认为 Wiegand 格式。

### 2.3 候选格式模型

每个候选保存：

```
wiegandName ("H10301", "H10306", "C1k35s", "ind26", ...)
description ("HID H10301 26-bit", ...)
bitLength   (从 description 的 "N-bit" 提取)
facilityCode / cardNumber / issueLevel / oemCode  (Optional)
parity      ("ok" / "fail")
```

### 2.4 决策表（核心）

| 候选情况 | 行为 | status | confidence |
|---|---|---|---|
| 0 个候选，有裸 `FC:/CN:` | `format="Unknown HID format"`，保留 FC/CN，warning 提及拒绝默认 H10301 | Partial | Low |
| 0 个候选，仅 `raw:` | 保留 raw | Partial | Low |
| 1 个候选 | 使用 | Identified（有值） | High/Medium |
| 多候选，`(bit,FC,CN)` 全相同 | 选优先候选（H10301 > HID 描述 > 首个），列全部候选 | Identified | Medium |
| 多候选，冲突，且仅 1 个 HID 描述 | 选该 HID 候选，warning | Identified | Medium |
| 多候选，多个 HID 描述且一致 | 选优先者，warning | Identified | Medium |
| 多候选，仍然冲突 | `format="Ambiguous"`，**不填 FC/CN**，warning 列出每个候选 | Partial | Low |

### 2.5 格式分类

| 条件 | `format` |
|---|---|
| name == `H10301` | `H10301 (26-bit)` |
| description 含 `Corporate 1000` | `Corporate 1000 (35-bit)` |
| description 含 `HID` 且位宽已知 | `HID 37-bit (H10304)` / `HID 34-bit (H10306)` / `HID 26-bit (NAME)` |
| 非 HID 描述 | `<NAME> (N-bit)` |

**这直接修复了审计发现的缺陷：** 旧实现会把 34/37-bit 与 Corp1000 静默降级为 H10301；本实现从不默认。

### 2.6 字段缺失语义

- `H10320` / `H10302` / `HGen37` 等格式**只有 FC 没有 CN**（源码 `hasCardNumber=0`）。
  此时 `cardNumber` 保持 absent，而不是 0。
- `Issue:` / `OEM:` 进 `extraFields`。
- `parity ( fail )` → warning，confidence 降为 Medium。

### 2.7 已知限制

- Client 可能同时匹配多个 Wiegand 解释（如 26-bit 的 H10301 与 ind26）。本模块保留全部候选；若值一致则合并。
- 不根据 raw 自行重算 Wiegand（Client 已做）。
- Prox II / Corporate 1000 的命名来自 Client 的 description，不额外推断物理卡型号。

---

## 3. AWID

### 3.1 Client 输出（证据）

```cpp
// [S] client/src/cmdlfawid.c awid_decode_bits()
"AWID - len: %d FC: %d Card: %u - Wiegand: %x, Raw: %08x%08x%08x"
"AWID - len: %d FC: %d Card: %u - Wiegand: %x%08x, Raw: ..."   // >32 bit
"AWID - len: %d -unknown- (%u) - Wiegand: %x, Raw: ..."
"Farpointe/Cansec 37-bit - FC: %u CN: %u"
"Farpointe HCC40 - ID without parity: %x%08x"
"RBH - CN: %u"
```

### 3.2 识别规则

| 证据 | 分数 |
|---|---|
| `AWID - len: N` | 100 |
| `AWID … Raw:` | 80 |
| `AWID ID found` | 60 |
| `AWID` | 40 |

### 3.3 字段映射

| 结果字段 | 来源 |
|---|---|
| `bitLength` | `len: (\d+)` |
| `facilityCode` | `FC: (\d+)` |
| `cardNumber` | `Card: (\d+)` |
| `rawData` / `hexId` | `Raw: ([0-9A-Fa-f]{6,24})` |
| `extraFields.cansec37Fc/Cn` | `Farpointe/Cansec 37-bit` |
| `extraFields.hcc40Id` | `Farpointe HCC40` |
| `extraFields.rbhCn` | `RBH - CN:` |

### 3.3 状态

| 条件 | status | confidence |
|---|---|---|
| len + FC + CN + raw | Identified | High |
| 缺 raw 或缺 len | Identified | Medium |
| `-unknown-` 变体 | Identified | Low（只取 Client 给出的 raw card 值） |
| 仅关键词 | Partial | Low |

### 3.4 已知限制

- `-unknown-` 变体的括号内数字是 Client 的推导值，不是 FC/CN；只写入 `cardNumber` 并 warning。
- 不解析 `Wiegand:` 字段（Client 已给出 FC/CN，重复解析无意义）。

---

## 4. Indala

### 4.1 Client 输出（证据）

```cpp
// [S] client/src/cmdlfindala.c indala_print_decoded()
"Indala (len %zu)  Raw: %x%08x"                        // 64 bit
"Indala (len %zu)  Raw: %x%08x%08x%08x%08x%08x%08x"    // 224 bit
"Fmt 26 FC: %u Card: %u Parity: %1d%1d"
"  Printed....... __%04d__  ( 0x%X )"
"  Internal ID... %llu"
"  Heden-2L...... %u"
"  Keyscan 40010. FC: %u CN: %u"
"Odd size,  false positive?"
```

### 4.2 识别规则

| 证据 | 分数 |
|---|---|
| `Indala (len N) Raw:` | 100 |
| `Indala (len N)` | 90 |
| `Fmt 26 FC: … Card: …` | 85 |
| `Indala … Raw:` | 60 |
| `Indala` | 45 |

### 4.3 字段映射

| 结果字段 | 来源 | 缺失时 |
|---|---|---|
| `bitLength` | `(len (\d+))` | absent |
| `rawData` / `hexId` | `Raw: ([0-9A-Fa-f]{8,64})` | **允许缺失**，继续解析 FC/CN |
| `facilityCode` / `cardNumber` | `Fmt 26 FC: … Card: …` | absent |
| `extraFields.wiegandFormat` | `Fmt (\d+)` | 省略 |
| `extraFields.parity` | `Parity: (\d+)` | 省略 |
| `extraFields.internalId` | `Internal ID` | 省略；若缺 CN 则同时作为 `decimalId` + warning |
| `extraFields.heden2l` | `Heden-2L` | 省略 |
| `extraFields.keyscan40010Fc/Cn` | `Keyscan 40010` | 省略（**不提升为 FC/CN**） |
| `extraFields.printed/printedHex` | `Printed` | 省略 |

### 4.4 状态

| 条件 | status | confidence |
|---|---|---|
| raw + FC/CN + Parity | Identified | High |
| raw + FC/CN | Identified | Medium |
| 仅 raw | Identified | Medium，warning "FC/CN absent" |
| 仅 FC/CN（无 raw） | Identified | Medium，warning "raw absent" |
| 仅 len / 附加信息 | Partial | Low |
| 仅关键词 | Partial | Low |

**这修复了审计发现的缺陷：** 旧实现两个正则都失败时直接丢弃协议；本实现把 Raw 设为可选、字段顺序无关，任一身份字段存在即返回结果。

### 4.5 已知限制

- `Raw` 由 `%x%08x` 拼接，首字不补零，长度可能为 9–16（64-bit）或 49–56（224-bit）；实现按 8–64 hex 接受，不做补零。
- `Fmt` 位宽（26）与 `len`（64/224）含义不同：`bitLength` 取 `len`，`wiegandFormat` 进 extra。
- Keyscan/Heden 是替代解释，不覆盖主 FC/CN。

---

## 5. 兼容性矩阵

| 输出变体 | EM4100 | HID | AWID | Indala |
|---|---|---|---|---|
| 带 `[+]` 日志前缀 | ✅ | ✅ | ✅ | ✅ |
| 带 ANSI 颜色 | ✅ | ✅ | ✅ | ✅ |
| CRLF | ✅ | ✅ | ✅ | ✅ |
| 字段顺序变化 | ✅（独立正则） | ✅（逐行） | ✅（同一行固定） | ✅ |
| 小写 hex | ✅ | ✅ | ✅ | ✅ |
| 多行/单行 | ✅ | ✅ | ✅ | ✅ |
| v4.20728 旧样本 | ✅（`EM 410x ID`） | ✅（`FC/CN` + `raw:`） | ✅ | ✅（若有 Raw） |
| v4.23346 结构 | ✅ | ✅（`[NAME] … N-bit`） | ✅（`AWID - len:`） | ✅（`Indala (len N) Raw:`） |

---

## 6. 不使用/未覆盖的格式

| 格式 | 原因 |
|---|---|
| `INDALA 224.... %08X`（无 `Raw:` 标签的旧路径） | 只在 `[B]` 出现；当前源码不打印。若真实遇到，Indala 关键词仍会命中 → Partial，不会崩溃 |
| `IO PROX....... %08X` 等通用标签 | Stage 2 协议 |
| `HID Prox II` 专有格式 | Client 以 Wiegand 格式名输出，无独立 ProxII 行 |
| 十进制/进制换算 | 一律采信 Client 输出，不在本地重算 |

---

## 7. 性能

- 所有正则使用 `QRegularExpression` 静态实例，编译一次。
- 最坏路径为逐行扫描；20 万字符输入测试耗时在毫秒级（见测试报告）。
- 无递归、无回溯型危险正则（模式内均含明确边界，无 `.*` 嵌套量词）。
