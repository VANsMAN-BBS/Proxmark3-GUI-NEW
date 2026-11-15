# 解析结果 → 界面字段映射

## 1. 状态映射

| `Lf::Status` | 界面文字（en / zh） | 说明区行为 |
|---|---|---|
| `Identified` | Identified / 已识别 | 显示协议与字段 |
| `Partial` | Partial parse / 部分解析 | 追加“必需字段不完整” |
| `Unidentified` | No supported protocol found / 未找到受支持的协议 | 无协议签名 |
| `InvalidInput` | Invalid input / 输入无效 | 空输入时另有提示 |

额外状态：

- `format == "Ambiguous"` 或 `formatCandidates` 非空 → 说明区列出候选格式，并注明“解析器不会擅自选择”；
- 文本包含 `unknown command` / `not supported` / `no response` → 追加“当前固件可能不含低频功能，文本解析仍可用”。

## 2. 核心字段

| 界面字段 | 数据来源 | 缺失时 |
|---|---|---|
| Protocol | `Lf::protocolName(result.protocol)` | 不显示该行（除非 Partial） |
| Format | `result.format` | “未提供” |
| Bit length | `result.bitLength` | “未提供” |
| Facility code | `result.facilityCode` | “未提供” |
| Card number | `result.cardNumber` | “未提供” |
| Hex ID | `result.hexId` | “未提供” |
| Decimal ID | `result.decimalId` | “未提供” |
| Raw data | `result.rawData` | “未提供” |

缺失值**不会显示 0**（`Lf::Optional<T>` 语义）。

## 3. 协议专有字段

`result.extraFields` 逐条显示，键名经 `extraLabel()` 映射为可读名称（59 个键已覆盖，未映射的键按原名显示）。

示例：

| 键 | 显示（en / zh） |
|---|---|
| `countryCode` | Country code / 国家代码 |
| `animalBit` | Animal bit / 动物位 |
| `facilityCodeHex` | Facility code (hex) / 设施码（十六进制） |
| `wiegandFormat` | Wiegand format / Wiegand 格式 |
| `cansec37Fc` | Cansec 37 facility code / Cansec 37 设施码 |
| `patternCdviCn` | CDVI pattern card number / CDVI 模板卡号 |

不适用字段**不生成行**，避免所有协议显示同一套空字段。

## 4. T55xx（只读）

输入文本中若存在 `lf t55xx detect/info` 输出：

| 界面项 | 来源 |
|---|---|
| Detected / Chip / Password set / Block 0 / Modulation / Bit rate / Offset / Inverted | `LfT55xx::DetectInfo` |
| Block 0 decoded：Modulation / Extended mode / Bit rate index / RF divisor / Max block / Master key | `LfT55xx::Block0` |
| 其他位域 | `Block0.fields` |
| 警告 | `DetectInfo.warnings`、`Block0.warnings` |

页脚固定显示：“本页面对 T55xx 仅做只读展示：不提供写入、锁定、OTP 或密码操作。”

## 5. 说明区（noteLabel）

按顺序追加，均换行显示：

1. `Ambiguous format: …`（或多格式）
2. `Protocol recognised, but required fields are incomplete.`
3. `No supported LF protocol signature was found in this text.`
4. 固件缺少命令提示（仅在文本命中关键字时）

## 6. 详情标签

| 标签 | 内容 |
|---|---|
| 原始输出 | 原始输入文本（只读等宽） |
| 警告与未解析字段 | `warnings`、`unparsedFields`、`formatCandidates`；无内容时显示“无警告” |
| T55xx（只读） | 上表内容；无数据时说明未找到 detect/info |
