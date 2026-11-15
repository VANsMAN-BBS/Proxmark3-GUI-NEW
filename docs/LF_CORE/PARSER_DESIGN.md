# LF Parser 设计说明

**模块：** `src/lf/`
**状态：** Stage 1 已实现

---

## 1. 目标与非目标

### 目标

1. 输入 Client 输出文本 → 输出结构化卡信息。
2. 纯软件、可单测、无 GUI/硬件依赖。
3. 兼容 Iceman v4.23346 家族输出，同时容忍旧版本变体。
4. 识别失败必须显式表达，不能伪造字段。

### 非目标

- 波形/信号解码。
- 执行 PM3 命令。
- 写卡/擦卡/克隆/模拟。
- 覆盖全部 22 种 LF 协议（Stage 1 只做 4 种）。

---

## 2. 数据模型

### 2.1 枚举

```cpp
enum class Protocol { Unknown, EM4100, HIDProx, AWID, Indala };

enum class Status {
    Identified,   // 协议 + 核心身份
    Partial,      // 协议已识别，身份不完整
    Unidentified, // 未匹配任何协议
    InvalidInput  // 空输入
};

enum class Confidence { None, Low, Medium, High };
```

### 2.2 Optional<T>

C++11 无 `std::optional`，因此提供一个最小实现：

```cpp
template <typename T>
class Optional {
public:
    bool isPresent() const;
    bool isAbsent() const;
    const T &value() const;      // 调用方必须先检查 isPresent()
    T valueOr(const T &fallback) const;
    void set(const T &value);
    void clear();
};
```

用途：`facilityCode`、`cardNumber`、`bitLength`、`decimalId`。

**为什么不用 0 / -1 作为哨兵：**
卡片合法的 FC/CN 可以是 0，`-1` 对 `quint64` 无意义。哨兵值会污染下游判断。

### 2.3 LFParseResult

| 字段 | 类型 | 缺失表示 | 说明 |
|---|---|---|---|
| `protocol` | `Lf::Protocol` | `Unknown` | |
| `status` | `Lf::Status` | — | |
| `confidence` | `Lf::Confidence` | `None` | 由证据强度推导 |
| `displayName` | `QString` | 空 | "EM4100" / "HID Prox" … |
| `format` | `QString` | 空 | "H10301 (26-bit)" 等 |
| `formatCandidates` | `QStringList` | 空 | HID 的全部候选 |
| `bitLength` | `Optional<int>` | absent | |
| `facilityCode` | `Optional<quint32>` | absent | |
| `cardNumber` | `Optional<quint64>` | absent | 37-bit huge ID 需要 64 位 |
| `rawData` | `QString` | 空 | 大写 hex，无分隔符 |
| `hexId` | `QString` | 空 | 协议 hex 身份 |
| `decimalId` | `Optional<quint64>` | absent | **仅**来自 Client 输出 |
| `extraFields` | `QList<LfExtraField>` | 空 | Issue/OEM/parity/去扰值等 |
| `warnings` | `QStringList` | 空 | 不确定性说明 |
| `unparsedFields` | `QStringList` | 空 | 未理解的字段 |
| `sourceText` | `QString` | 空 | 原始输入（未归一化） |
| `matchScore` | `int` | 0 | 分派得分，调试用 |

`hasIdentity()` 的判定：`rawData` / `hexId` / `facilityCode` / `cardNumber` / `decimalId` 任一存在。

---

## 3. 分派机制

### 3.1 为什么不用 `contains` 链

Phosphor 的顺序是固定的 `if contains(...) return parse_xxx()`：

- 先匹配先返回；
- 关键词即可触发；
- 解析失败直接落到下一段甚至返回 `None`；
- 多协议文本无法判断优先级。

本模块改为 **证据评分**：

| 分数 | 含义 | 示例 |
|---|---|---|
| 100 | 协议专属结构 | `EM 410x ID <hex>`、`AWID - len: N`、`Indala (len N) Raw:`、`[FMT] … N-bit … FC:` |
| 70–90 | 强标签 | `HID Prox` + `FC:`、`Fmt 26 FC:` |
| 40–60 | 协议关键词 / 标记 | `EM410x`、`AWID`、`Indala`、`HID Prox ID found!` |
| 0 | 无证据 | |

### 3.2 选择规则

```
1. 归一化文本；空 → InvalidInput
2. 对每个注册解析器调用 detect()，收集 score > 0 的候选
3. 无候选 → Unidentified（并区分"不像 LF 输出" / "无支持协议"）
4. 按 score 降序排序；同分按固定优先级
   EM4100 > HIDProx > AWID > Indala
5. 只调用最高分解析器一次
6. 若返回 Unidentified，改为 Partial + warning（协议已知但字段缺失）
7. 若第二名同分且协议不同，附加歧义 warning
```

**关键点：第 5 步只调用一次。** 解析失败不会自动尝试其他协议，避免"EM4100 解析失败被当成 AWID"。

### 3.3 优先级依据

- 同分意味着两者证据强度相同（极少见，通常是文本混合）。
- EM4100 的结构性最强（固定 10 hex ID），排第一。
- 其余按 Stage 1 实现顺序，纯确定性，无业务含义。

---

## 4. 解析器接口

```cpp
class ILfParser {
public:
    virtual ~ILfParser() {}
    virtual Lf::Protocol protocol() const = 0;
    virtual QString name() const = 0;
    virtual int detect(const QString &normalizedText, QStringList *evidence) const = 0;
    virtual LFParseResult parse(const QString &normalizedText,
                                const QString &originalText) const = 0;
};
```

约束：

- `detect()` 无副作用、无 I/O。
- `parse()` 只读文本，必须返回 `sourceText`。
- 解析器之间互不调用。
- 不抛出异常（Qt 容器与正则均不抛）。

注册表：

```cpp
class LFParserRegistry {
public:
    static LFParserRegistry &instance();
    LFParseResult parse(const QString &text) const;
    LFParseResult parseAs(Lf::Protocol, const QString &text) const;
    QList<Lf::Protocol> protocols() const;
    ILfParser *parser(Lf::Protocol) const;
    void registerParser(ILfParser *parser);   // 接管所有权
};
```

---

## 5. 文本归一化

`LFTextNormalizer::normalize()` 只做传输层清洗：

| 处理 | 原因 |
|---|---|
| 去 UTF-8 BOM | Windows 重定向文件常见 |
| 去 ANSI CSI/OSC | Client 可能带颜色 |
| `\r\n` / `\r` → `\n` | 串口/管道差异 |
| 去 NUL | 管道截断 |
| 行尾右裁剪 | 对齐空格无意义 |

**不做**：

- 不合并行内空格（`[H10301  ]` 的对齐需要保留）；
- 不删除空行（诊断需要）；
- 不转小写（hex 由各解析器自行 `normalizeHex`）；
- 不改写 hex 内容。

`normalizeHex()`：去 `0x`、空格、`:`、`-`，转大写；含非 hex 字符返回空串。

---

## 6. 错误处理策略

| 情况 | 结果 |
|---|---|
| 空 / 纯空白 | `InvalidInput` + warning |
| 无任何协议证据 | `Unidentified` + warning |
| 有证据但无身份字段 | `Partial` + protocol + warning |
| 字段存在但冲突（HID） | `Partial` + `format="Ambiguous"` + warning |
| 字段格式非法（超长数字、非 hex） | 该字段 absent，其余照常，warning 可选 |
| 输入含多个协议 | 最高分者胜；同分附加 warning |

**从不**：

- 用 0 填充缺失字段；
- 用 H10301 填补未知 HID；
- 把解析失败转成另一个协议；
- 因单个字段缺失丢弃整条结果。

---

## 7. 与主项目架构的关系

```
MainWindow / 未来 LF 页面
        │  (将来)
        ▼
LFParserRegistry::parse(text)
        │
        ├── ClientSession（既有）：提供已清洗文本
        ├── Util（既有）：命令发送，本模块不调用
        └── Logger（既有）：本模块不直接写日志（保持可测试）
```

模块不修改 `ClientSession`、`Util`、`Logger`。未来的 LF 页面只需把命令输出传进来。

---

## 8. 扩展方式（Stage 2+）

新增协议只需：

1. 新建 `xxxparser.h/.cpp` 实现 `ILfParser`；
2. 在 `LFParserRegistry` 构造函数注册；
3. 在 `.pro` 添加文件；
4. 添加 fixtures 与测试。

无需修改现有解析器或分派逻辑。
