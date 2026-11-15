# T55xx / T5577 专项分析

**范围：** 只分析，不执行任何真实写入、擦除、锁定。
**结论摘要：**
1. Phosphor 对 T55xx 的支持是**最薄的一层**（detect / chk / wipe + clone 命令带 `-p`），**没有 block 级读写、没有配置解析、没有位域模型**。
2. 主项目现有 `src/module/t55xx.cpp` 在 **block0 位域编辑** 上**强于** Phosphor。
3. 真正决定克隆结果的是 **Client 的协议编码器**（`lf <proto> clone`），不是 Phosphor。

---

## 1. Phosphor 的 T55xx 功能面（全量）

### 1.1 命令

| 命令 | 位置 | 用途 |
|---|---|---|
| `lf t55xx detect` | `command_builder.rs:68`、`blank.rs:61`、`write.rs:177`、`erase.rs:33` | 确认芯片 + 密码状态 |
| `lf t55xx chk` | `command_builder.rs:72`、`write.rs:196` | 搜索密码 |
| `lf t55xx wipe` | `command_builder.rs:76` | 无密码擦除 |
| `lf t55xx wipe -p <8hex>` | `command_builder.rs:81` | 有密码擦除 |
| `lf em 4x05 wipe` | `command_builder.rs:90` | EM4305 擦除（同时支持） |
| `lf em 4x05 info` | `command_builder.rs:96` | EM4305 检测 |
| `lf em 4x05 read -a <n>` | `command_builder.rs:102` | 读 EM4305 word（仅用于擦除验证） |

**没有使用**：`lf t55xx info`、`lf t55xx dump`、`lf t55xx read`、`lf t55xx write`、`lf t55xx config`、`lf t55xx dangerraw`。

### 1.2 数据模型

`src-tauri/src/cards/types.rs:238-246`：

```rust
pub struct T5577Status {
    pub detected: bool,
    pub chip_type: String,     // "T55x7" / "T5555" / "T5577" / ""
    pub password_set: bool,
    pub block0: Option<String>,  // 8 hex，仅保存不解析
    pub modulation: Option<String>,
}
```

**注意 `block0` 只是字符串**。Phosphor 从不把它拆成位域。

### 1.3 解析

`output_parser.rs:1802-1842`：

```text
detected    = 文本含 "T55xx" / "T5577" / "T5555" / "Chip type"
chip_type   = /Chip\s*(?:type)?\.+\s*(T55x7|T5555|T5577)/   否则 "T55x7"
password_set= /Password\s*(?:set)?\.+\s*(Yes|No)/ == "Yes"
block0      = /Block0\.+\s*([0-9A-Fa-f]{8})/  → 仅大写保存
modulation  = /Modulation\.+\s*(.+)/
```

`output_parser.rs:1846-1851`（密码）：

```text
password = /\[\+\]\s*(?:Found valid )?[Pp]assword[:\s]+([0-9A-Fa-f]{8})/
```

### 1.4 缺少的能力（逐项核对任务书要求）

| 任务书要求 | Phosphor | 证据 |
|---|---|---|
| 芯片检测 | ✓ | `lf t55xx detect` |
| 芯片型号判断 | ◐ | 只区分 T55x7/T5555/T5577 字符串；**不区分 Q5 变体/容量** |
| 配置块读取 | **✗** | 无 `lf t55xx info`，只从 detect 输出里抓 block0 字符串 |
| 数据块读取 | **✗** | 无 `lf t55xx dump/read` |
| 调制方式识别 | ◐ | 抓 `Modulation` 文本，不解析为枚举/位域 |
| 数据速率识别 | **✗** | 不抓 `Bit rate` |
| 密码保护状态识别 | ✓ | `Password set` → bool |
| 配置解析（block0 位域） | **✗** | 无 |
| 数据结构展示 | **✗** | 无 block 表 |
| 空白卡判断 | ◐ | 用 `lf search` 是否识别出卡型来间接判断（`blank.rs:66-72`） |
| 兼容性判断 | ◐ | 硬编码 `supports_em4305()`（`types.rs:153`），与芯片无关 |
| T5577 数据格式编码 | **✗**（交给 Client） | `lf <proto> clone` |
| 目标芯片写入 | ✓（命令层） | 22 条 clone |
| 复读验证 | ◐ | `lf search` + 字段比较，非 block 读回 |

> 也就是说：**任务书列出的 12 项 T55xx 能力，Phosphor 完整实现的只有 2 项（检测、密码状态），部分实现 5 项，完全缺失 5 项。**

---

## 2. Phosphor 把哪些功能组合成了统一向导

Phosphor 的 LF 写入是**一个**统一向导（`wizardMachine.ts` + `write.rs`），把以下不同性质的操作串在一起：

```
设备连接 → 扫描(lf search) → 选空白卡 → 检测空白(detect) 
        → [有密码? chk → wipe → 复检] → clone → lf search 复验 → 完成
```

**优点**：用户体验连贯；每步都有失败恢复（`RecoveryAction::Retry`）。
**问题（架构层面）**：

1. **混淆了"芯片管理层"和"协议克隆层"**。
   - `detect/chk/wipe` 属于 T55xx 芯片管理；
   - `clone` 属于协议编码；
   - `lf search` 属于识别。
   三者被绑在一个 FSM 里，导致无法单独使用"擦除一张卡"或"只检测芯片"。
   （Phosphor 后来在 `commands/erase.rs` 里补了独立的 `detect_chip`/`wipe_chip`，但向导里仍保留重复逻辑。）

2. **`erase` 与 `write` 的擦除策略不一致**：
   - `erase.rs:94-96`：无条件 `lf t55xx wipe`；
   - `write.rs:231`：仅当检测到密码才 wipe。
   两处对"什么时候需要擦"的判断不同。

3. **没有 block 级操作**，因此无法做"只改 block0 的调制方式""备份/恢复整卡"这类真正的 T55xx 管理。

---

## 3. 主项目现有 T55xx 实现（更强）

`src/module/t55xx.cpp`（524 行）+ `t55xx.h`。

### 3.1 能力

| 能力 | 实现 | 说明 |
|---|---|---|
| 芯片检测 | `detect()` :348 | `lf t55xx detect` + 解析调制/速率/offset/inverted |
| 读 block0 | `readInfo()` :416 | `lf t55xx info` + 正则提取 8 hex |
| 写配置 | `applyConfig()` :457 | `lf t55xx config --ASK/--FSK1/… --rate N -o N [-i] [--st] [--q5]` |
| block0 位域编辑 | `updateBlock0Display()` :201、`block0FromUi()` :226 | 见 §3.2 |
| 二进制/十六进制互转 | `block0ToBinary` / `binaryToBlock0` | 每 4 bit 分组显示 |
| EM410x 读取 | `onReadEM410x()` :493 | `lf em 410x reader` + 正则 |
| EM410x 克隆 | `onCloneEM410x()` :503 | 带确认框 + `--q5` 选项 |
| 未实现控件显式禁用 | `disableUnimplementedControls()` :85 | 诚实标注 Experimental |

### 3.2 block0 位布局（主项目实现）

```
bit 31..24  ── 未在 UI 中映射
bit 23..18  ── Data Bit Rate (DBR)
               extend=0 → 3 bit (bit 20..18)
               extend=1 → 6 bit (bit 23..18)
bit 17      ── Extend (DBR 扩展位)
bit 16..12  ── Modulation (5 bit)
               0x00 NRZ      0x01 PSK1    0x02 PSK2    0x03 PSK3
               0x04 FSK1     0x05 FSK2    0x06 FSK1a   0x07 FSK2a
               0x08 ASK      0x10 Biphase 0x18 Biphase inv
bit 11..8   ── Master Key（UI 禁用，未验证）
bit 7..5    ── Max Block
bit 4       ── Sequence Terminator / 相关 flag（UI 命名见 .ui）
bit 3       ── 相关 flag
bit 2       ── 相关 flag
bit 1       ── 相关 flag
bit 0       ── 相关 flag
```

对应代码：`t55xx.cpp:207-218`（解码）与 `:231-252`（编码）。

**重要保留意见**：`disableUnimplementedControls()`（`:85-102`）明确禁用了 `Locked`、`Master Key`、`PSK clock frequency`、`Answer on request`、`One time pad`、`Sequence start marker`，并注明"bit mapping is not verified in this build"。**这些位不要在没有数据手册/实测的情况下启用。**

### 3.3 主项目也缺的 T55xx 能力

| 能力 | 缺失 |
|---|---|
| 全卡 dump（`lf t55xx dump`） | ✗ |
| 单块读（`lf t55xx read -b n`） | ✗ |
| 单块写（`lf t55xx write -b n -d hex`） | ✗ |
| 密码搜索（`lf t55xx chk`） | ✗ |
| 擦除（`lf t55xx wipe`） | ✗ |
| 密码保护写入（`-p`） | ✗ |
| Q5/T5555 识别 | 仅 UI 单选，无自动判断 |
| 空白卡判断（block 全零） | ✗（只有 `lf search` 间接判断） |

---

## 4. 建议的 T55xx 模块设计（供后续 LF-Core 使用）

### 4.1 分层

```
┌─────────────────────────────────────────────┐
│ T55xxPage (UI)                              │
│  - 芯片信息卡 / block 表 / 配置编辑器        │
│  - 擦除 / 写入 / 复验 按钮（带风险确认）      │
└───────────────┬─────────────────────────────┘
                │
┌───────────────▼─────────────────────────────┐
│ T55xxController (QObject, 状态)              │
│  - detect() / readInfo() / dump()            │
│  - applyConfig() / writeBlock()              │
│  - wipe(confirm) / verify()                  │
│  - 密码：仅接受用户输入，不做自动穷举           │
└───────────────┬─────────────────────────────┘
                │
┌───────────────▼─────────────────────────────┐
│ T55xxCodec (纯函数，可单测)                   │
│  - block0 <-> 位域结构体                     │
│  - DBR / Modulation / MaxBlock 映射表        │
│  - dump 文本 -> 32bit[] 解析                 │
└───────────────┬─────────────────────────────┘
                │
┌───────────────▼─────────────────────────────┐
│ ClientSession / Util（既有）                 │
└─────────────────────────────────────────────┘
```

### 4.2 与向导的关系

**建议不要把 T55xx 管理塞进 LF 写入向导。** 理由：

- 芯片管理是"对一张空白卡的操作"，与"克隆哪种协议"正交；
- 用户可能只想擦卡、只想知道芯片型号、只想备份；
- 主项目已有独立的 T55xx 页面，保留它更符合现有架构。

向导只保留**必要的一步**：写入前调用 `T55xxController::ensureBlank()`，由它内部决定是否需要 wipe。

### 4.3 风险控制（必须）

| 操作 | 风险等级 | 要求 |
|---|---|---|
| `lf t55xx detect` / `info` / `read` | 低 | 直接执行 |
| `lf t55xx dump` | 低 | 直接执行，落盘 |
| `lf t55xx config` | **中** | 修改芯片配置，需确认 + 显示将写入的 32 bit |
| `lf t55xx write -b 0` | **高** | 需二次确认，禁止"危险原始写" |
| `lf t55xx wipe` | **高** | 需二次确认，明确不可恢复 |
| `lf t55xx chk` | 中 | **仅做用户已知密码验证**；不实现自动穷举（见 §5） |
| `lf t55xx dangerraw` | **禁止** | 不暴露 |

---

## 5. 合规边界（本阶段明确）

任务书要求："不得擅自加入密码破解、认证绕过或不可逆锁定功能。"

因此：

1. **不实现** T55xx 密码自动穷举 UI（即使 Client 有 `chk`）。
   - 若保留 `chk`，只能作为"用户已输入密码，验证是否正确"的内部步骤，且必须在 UI 明确说明。
2. **不实现** `lf t55xx dangerraw`。
3. **不实现** `lf em 4x70 brute` / `lf em 410x brute` / `lf awid brute` 等暴力功能。
4. **不实现** 任何"锁定/熔断"位（block0 的 lock 位）的一键操作。
5. 所有写操作默认 dry-run 展示命令 + 32 bit 数据，用户确认后才发送。

Phosphor 本身**没有**密码破解（它只在检测到密码后调 `chk` 尝试恢复，失败即中止），这一点是合规的；但其 `chk` 用法应重新评估。

---

## 6. T5577 写入流程（仅分析，不执行）

### 6.1 Phosphor 的无密码路径

```
1. lf t55xx detect          → 确认 detected && password_set == false
2. build_clone_command()    → lf <proto> clone <params>
3. 检查输出含 [!!]/fail
4. transition(WriteFinished)
```

### 6.2 Phosphor 的有密码路径

```
1. lf t55xx detect          → password_set == true
2. lf t55xx chk             → 得到 password（失败则中止）
3. lf t55xx wipe -p <pw>
4. lf t55xx detect          → 必须 detected && !password_set，否则中止
5. clone（不带 -p，因为密码已被擦除）
6. 复验
```

代码里有一行关键防护（`write.rs:261-263`）：

```rust
// SAFETY: If password was set, it was cleared by wipe above.
// Shadow the variable to prevent accidental re-lock on clone command.
let password: Option<String> = None;
```

意思是：**擦除后绝不再带 `-p`**，避免把密码写回去。这个细节值得保留。

### 6.3 建议的 C++ 流程

```
ensureBlank(port):
    detect → 
      not detected            → ERROR("未检测到 T55xx")
      detected && !pw         → OK
      detected && pw:
          require user password
          wipe(-p pw)
          detect again
            !detected or still pw → ERROR("擦除未生效，请勿移开卡片")
          OK

clone(port, cardType, decoded):
    cmd = buildCloneCommand(cardType, decoded)   // 失败 → 明确报错
    run(cmd)
    if output contains [!!]/fail → WARN/ERROR
    return

verify(port, sourceType, sourceFields):
    out = lf search
    parsed = parseLfSearch(out)
    compare type + fields
```

这套流程**不依赖 Phosphor 代码**，完全可以用主项目的 `Util`/`ClientSession` 实现。

---

## 7. 空白卡判断的正确做法（改进 Phosphor）

Phosphor 的做法：对一个 T5577 空白卡跑 `lf search`，如果能识别出卡型就认为"已有数据"。
问题：

- 空白 T5577 可能仍被识别为某种协议（残留配置）；
- 有数据但协议不认识的卡会误判为空白。

**建议**：

```
1. lf t55xx detect → 确认芯片存在
2. lf t55xx dump（或 read 0..N）→ 读取实际 block 数据
3. 若 block1..N 全 0 且 block0 为默认配置 → 空白
4. 否则显示"已有数据"，列出非零 block
```

这需要 Client 的 `dump`/`read` 命令支持，Phosphor 未实现。

---

## 8. 结论

| 问题 | 答案 |
|---|---|
| Phosphor 是否实现了完整的 T55xx 管理？ | **否**。只有 detect/chk/wipe。 |
| Phosphor 是否解析 block0？ | **否**。只保存 8 位 hex 字符串。 |
| Phosphor 是否有数据块读取？ | **否**。 |
| 主项目是否更完整？ | **在 block0 位域编辑方面是**；在 dump/read/write/wipe 方面主项目也缺。 |
| 移植 T55xx 应该照抄 Phosphor 吗？ | **不应该**。应保留主项目 `t55xx.cpp` 的位域能力，补上 dump/read/write/wipe 与密码流程。 |
| 是否可以完全不改固件就做 T55xx？ | 不能。T55xx 命令同样依赖固件 LF 支持（见硬件文档）。 |
