# LF 命令映射：Phosphor ↔ Iceman Client

**Client 基线：** `E:\Opencode_project\pm3-256-iceman-v4.23346\proxmark3.exe`
（`Iceman/master/v4.23346-suspect`，通过 `-t` / `--fulltext` 离线导出帮助）
**固件基线：** `fullimage.elf`（无 LF，见 `PM3_HARDWARE_COMPATIBILITY.md`）

> **本文件的 "Client 是否支持" 是静态确认（帮助文本 + 二进制字符串），不是运行确认。**
> **"固件是否支持" 一律为 否**——当前固件无任何 LF 代码或 LF FPGA 镜像。

---

## 1. 命令全集概览

Phosphor 实际调用过的 LF 命令只有 **28 条**（`command_builder.rs` + `blank.rs` + `erase.rs` + `write.rs` 全量检索）：

```
lf search
lf t55xx detect
lf t55xx chk
lf t55xx wipe [-p <8hex>]
lf em 4x05 info
lf em 4x05 wipe
lf em 4x05 read -a <word>
lf em 410x clone --id <10hex> [--em|--q5]
lf hid clone -w <fmt> --fc <n> --cn <n> | lf hid clone -r <hex>
lf indala clone --raw <hex>
lf io clone --vn <n> --fc <n> --cn <n> | --raw <hex>
lf awid clone [--fmt <n>] --fc <n> --cn <n>
lf fdxb clone --country <n> --national <n> | --raw <hex>
lf paradox clone --fc <n> --cn <n> | --raw <hex>
lf viking clone --cn <hex>
lf pyramid clone --fc <n> --cn <n> | --raw <hex>
lf keri clone [-t i|m] [--fc <n>] --cn <n>
lf nexwatch clone --raw <hex>
lf presco clone -d <hex> | --sitecode <n> --usercode <n>
lf nedap clone --st <n> --cc <n> --id <n>
lf gproxii clone --xor <n> --fmt <n> --fc <n> --cn <n>
lf gallagher clone --rc <n> --fc <n> --cn <n> --il <n>
lf pac clone --cn <hex> | --raw <hex>
lf noralsy clone --cn <n> [-y <n>]
lf jablotron clone --cn <hex>
lf securakey clone --raw <hex>
lf visa2000 clone --cn <n>
lf motorola clone --raw <hex>
lf idteck clone --raw <hex>
```

**Phosphor 从未使用**：`lf tune`、`lf read`、`lf sniff`、`lf config`、`lf sim*`、`lf cmdread`、`lf relay`、任何 `lf <proto> reader/watch/demod/sim/brute`。

---

## 2. 主映射表

字段说明：
`Phosphor 位置` = 源文件:函数
`Client 支持` = v4.23346 静态确认
`固件支持` = 当前 `fullimage.elf`
`主项目封装` = Proxmark3GUI 现有代码

| # | 命令 | Phosphor 位置 | Client | 固件 | 关键参数 | 返回结构（解析相关） | Phosphor 解析方式 | 主项目封装 | 移植工作量 | 风险 |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | `lf search` | `scan.rs:33`、`write.rs:468`、`blank.rs:66`、`blank.rs:124` | 是 | **否** | `-u` 可选（Phosphor 用无参） | 见 §4.1 | 字符串 `contains` 分派 + 50+ 正则 → `CardData` | **无解析**；`lf.cpp:50` 只发命令 | **中** | 输出格式随固件变化；单次耗时 5–30 s |
| 2 | `lf t55xx detect` | `blank.rs:61`、`write.rs:177`、`erase.rs:33` | 是 | **否** | 无 | `Chip type..... T55x7` / `Password set...... Yes|No` / `Block0........ %08x` / `Modulation........` | 4 条正则 → `T5577Status` | **有** `t55xx.cpp:350`（未解析密码） | 低 | 无卡/弱耦合时超时 |
| 3 | `lf t55xx chk` | `write.rs:196` | 是 | **否** | 无 | `[+] Found valid password: XXXXXXXX` | `T5577_PASSWORD_FOUND_RE` | **无** | 低 | **密码穷举耗时不可控**；可能远超 30 s |
| 4 | `lf t55xx wipe` | `write.rs:239`、`erase.rs:96` | 是 | **否** | `-p <8hex>`（仅已知密码时） | 成功/失败文本 | 仅看退出码 + 后续 detect 复检 | **无** | 低 | **破坏性**；需二次确认 |
| 5 | `lf em 4x05 info` | `blank.rs:105`、`erase.rs:51` | 是 | **否** | 无 | `EM4x05/EM4x69` etc | `parse_em4305_info`：4 个 `contains` | **无** | 低 | 输出格式弱约束 |
| 6 | `lf em 4x05 wipe` | `write.rs:356`、`erase.rs:98` | 是 | **否** | 无 | 成功/失败文本 | 退出码 + 读 word0 复检 | **无** | 低 | **破坏性** |
| 7 | `lf em 4x05 read -a 0` | `write.rs:364` | 是 | **否** | `-a <word>` | `Word 0: 00000000` | `parse_em4305_word0` 正则 | **无** | 低 | 正则假设 "Word/Address 0" 前缀 |
| 8 | `lf em 410x clone --id` | `command_builder.rs:121` | 是 | **否** | `--id <10hex>`，可选 `--em`/`--q5` | `Written`/`[!!]` | 输出含 `[!!]`/`fail` 即判失败 | **有** `t55xx.cpp:519`（含确认框） | 低 | 写入失败可能仍返回 0 |
| 9 | `lf hid clone -w/-r` | `:126`、`:131` | 是 | **否** | `-w <H10301|…> --fc --cn` 或 `-r <hex>` | 同上 | 同上 | **无** | 低 | **Wiegand 格式缺陷**（见矩阵 §5.1） |
| 10 | `lf indala clone --raw` | `:135` | 是 | **否** | `--raw <hex>` | 同上 | 同上 | **无** | 低 | raw 长度/位宽需匹配 |
| 11 | `lf io clone` | `:140`、`:144` | 是 | **否** | `--vn --fc --cn` 或 `--raw` | 同上 | 同上 | **无** | 低 | FC 需 hex→dec（已处理） |
| 12 | `lf awid clone` | `:149` | 是 | **否** | `[--fmt 26|34|37|50] --fc --cn` | 同上 | 同上 | **无** | 低 | 无 raw 回退，缺字段直接失败 |
| 13 | `lf fdxb clone` | `:157`、`:164` | 是 | **否** | `--country --national` 或 `--raw` | 同上 | 同上 | **无** | 低 | `--animal`/`--extended` 未支持 |
| 14 | `lf paradox clone` | `:169`、`:173` | 是 | **否** | `--fc --cn` 或 `--raw` | 同上 | 同上 | **无** | 低 | |
| 15 | `lf viking clone --cn` | `:177` | 是 | **否** | `--cn <hex>` | 同上 | 同上 | **无** | 低 | 需 hex，非十进制 |
| 16 | `lf pyramid clone` | `:181`、`:185` | 是 | **否** | `--fc --cn` 或 `--raw` | 同上 | 同上 | **无** | 低 | |
| 17 | `lf keri clone` | `:192` | 是 | **否** | `-t i|m [--fc] --cn` | 同上 | 同上 | **无** | 低 | `-t` 语义需实测 |
| 18 | `lf nexwatch clone` | `:202` | 是 | **否** | `--raw <hex>` | 同上 | 同上 | **无** | 低 | raw 位宽 88/96 需匹配 |
| 19 | `lf presco clone` | `:211`、`:216` | 是 | **否** | `-d <hex>` 或 `--sitecode --usercode` | 同上 | 同上 | **无** | 低 | 两套参数互斥规则未验证 |
| 20 | `lf nedap clone` | `:225` | 是 | **否** | `--st --cc --id` | 同上 | 同上 | **无** | 低 | 无 raw 回退 |
| 21 | `lf gproxii clone` | `:234` | 是 | **否** | `--xor --fmt --fc --cn` | 同上 | 同上 | **无** | 低 | 无 raw 回退；xor 必须正确 |
| 22 | `lf gallagher clone` | `:242` | 是 | **否** | `--rc --fc --cn --il` | 同上 | 同上 | **无** | 低 | 无 raw 回退 |
| 23 | `lf pac clone` | `:250`、`:254` | 是 | **否** | `--cn <hex>` 或 `--raw` | 同上 | 同上 | **无** | 低 | `--cn` 是 hex 不是十进制 |
| 24 | `lf noralsy clone` | `:260` | 是 | **否** | `--cn <dec> [-y <year>]` | 同上 | 同上 | **无** | 低 | `-y` 单字符短选项 |
| 25 | `lf jablotron clone` | `:268` | 是 | **否** | `--cn <hex>` | 同上 | 同上 | **无** | 低 | |
| 26 | `lf securakey clone` | `:273` | 是 | **否** | `--raw <hex>` | 同上 | 同上 | **无** | 低 | |
| 27 | `lf visa2000 clone` | `:278` | 是 | **否** | `--cn <dec>` | 同上 | 同上 | **无** | 低 | 无 raw 回退 |
| 28 | `lf motorola clone` | `:283` | 是 | **否** | `--raw <hex>` | 同上 | 同上 | **无** | 低 | |
| 29 | `lf idteck clone` | `:288` | 是 | **否** | `--raw <hex>` | 同上 | 同上 | **无** | 低 | |

### 2.1 Phosphor 使用但主项目缺失的关键命令

| 命令 | 主项目现状 | 说明 |
|---|---|---|
| `lf t55xx chk` | 无 | 密码搜索，写入流程必需 |
| `lf t55xx wipe` | 无 | 仅 `t55xx.cpp` 的 `applyConfig` 会写配置，无擦除 |
| `lf em 4x05 info/wipe/read` | 无 | 完全不支持 EM4305 |
| 22 条 clone | 仅 `lf em 410x clone --id` | 其余全部缺失 |

### 2.2 主项目已用但 Phosphor 未用的命令（不应丢失）

| 命令 | 主项目位置 | 能力 |
|---|---|---|
| `lf read -v` | `lf.cpp:34`（配置 JSON） | 采集原始样本 |
| `lf sniff -v` | `lf.cpp:42` | 嗅探 |
| `lf tune --divisor <n>` | `lf.cpp:59` | 天线调谐（detached 长命令） |
| `hw status` | `lf.cpp:98` | 读取 LF 采样配置 |
| `lf config --divisor … --bps … --dec … --avg … --trig … --skip …` | `lf.cpp:139` | 设置采样参数 |
| `hw setlfdivisor -d <n>` | `lf.cpp:142` | 设置频率 divisor |
| `data save -f <name>` | `lf.cpp:185` | 导出 GraphBuffer 用于绘图 |
| `lf t55xx info` | `t55xx.cpp:418` | 读取 block0 |
| `lf t55xx config …` | `t55xx.cpp:488` | 配置芯片调制/速率 |
| `lf em 410x reader` | `t55xx.cpp:495` | 读取 EM410x ID |

> **移植时必须保留这些既有命令**，它们是 Phosphor 完全没有的采样/调谐能力。

---

## 3. `lf search` 专项（最关键的一条）

### 3.1 命令语义（Client 帮助原文 `[Client]`）

```
lf search [-h1cu]
    -1   Use data from Graphbuffer to search (offline mode)
    -c   Continue searching after successful match
    -u   Search for unknown tags
available offline: yes
```

- Phosphor 用 **无参数** `lf search`（不带 `-u`）。
- 主项目 `config_rrgv4.23346.json` 用 **`lf search -u`**（多打印未知标签信息）。
- **两者输出不同**：`-u` 会额外输出 `[=] Checking for unknown tags...` 段和调制/时钟分析。Phosphor 的解析器对这段没有防护，但因为它靠 `contains` 分派，多余文本一般不影响；唯一风险是未知段里的十六进制块被 `extract_first_hex_block` 误抓（`output_parser.rs:1975`，要求 ≥8 个 hex 字符）。

### 3.2 返回结构（分层）

`lf search` 的输出由三层组成：

1. **框架行**（固定）
   ```
   [=] Note: False Positives ARE possible
   [=] Checking for known tags...
   ```
2. **每个 demod 的识别块**（不定）
3. **结尾**：`[+] Valid <XXX> ID found!` 或 `[-] No known 125/134 kHz tags found!`

真实观测（`[实测]`，v4.23346 离线 + 合成 GraphBuffer，无卡）：

```
[=] Note: False Positives ARE possible
[=] 
[=] Checking for known tags...
[=] 
[-] No known 125/134 kHz tags found!
[=] Checking for unknown tags...

[=] FSK clock.......... no
[=] ASK clock.......... detected
[+] ASK/Manchester - clock 128 - decoded bitstream
[=] -----------------------------------------------
[+] DemodBuffer:
[+] .1111111100001111110000100001000
...
[=] ASK modulation / Manchester encoding detected!
[=]    could also be ASK/Biphase - try `data rawdemod --ab`
```

### 3.3 Client 内部的打印标签（`[Client]` 二进制字符串，v4.23346）

这些是移植时应当对齐的**权威格式**：

| 协议 | Client 打印模板 |
|---|---|
| EM410x | `EM 410x ID ` + 10 hex |
| HID | `HID Prox ID`、`HID-26........ %08X`、`[0m FC: `、`[0m  CN: ` |
| Indala | `Indala ID`、`INDALA 224.... %08X`、`INDALA 64..... %08X` |
| IO Prox | `IO Prox ID`、`XSF(%02d)%02x:%05d`、`IO PROX....... %08X` |
| AWID | `AWID ID`、`AWID.......... %08X` |
| FDX-B | `FDXB.......... %08X`、`Animal ID......... `、`Country Code...... ` |
| Paradox | `Paradox ID`、`PARADOX....... %08X` |
| Keri | `KERI ID`、`KERI.......... %08X` |
| Pyramid | `Pyramid ID`、`PYRAMID....... %08X` |
| Presco | `Presco ID`、`PRESCO........ %08X` |
| Nedap | `NEDAP ID`、`NEDAP 64...... %08X`、`NEDAP 128..... %08X` |
| GProxII | `Guardall G-Prox II ID`、`G-PROX II..... %08X`、`G-Prox-II - Len: ` |
| Gallagher | `GALLAGHER..... %08X`、`ISSUE............ %u` |
| PAC | `PAC........... %08X` |
| Noralsy | `Noralsy ID`、`NORALSY....... %08X` |
| Jablotron | `Jablotron ID`、`JABLOTRON..... %08X` |
| SecuraKey | `Securakey ID`、`SECURA KEY.... %08X` |
| Visa2000 | `Visa2000 ID`、`VISA2000...... %08X` |
| Motorola | `MOTOROLA...... %08X` |
| IDTECK | `Idteck ID`、`IDTECK........ %08X` |
| NexWatch | `NexWatch ID`、`NEXWATCH...... %08X` |
| Viking | `Viking ID`、`VIKING........ %08X` |
| COTAG | `[+] COTAG`（仅检测） |
| Hitag | `[+] Hitag`（仅检测） |
| Destron | `DESTRON....... %08X` |
| Paxton | `PAXTON........ %08X` |
| Verichip | `VERICHIP...... %08X` |
| 通用 | `Raw: %s`、`Raw............ %s`、`[=] raw: %s` |

> **注意 `%08X` 类标签**：存在 `IO PROX....... 00785960` 这种"只有 8 位 ID"的输出路径，与 `IO Prox - XSF(01)65:01337` 是**两条不同路径**。Phosphor 只处理了 XSF 路径（`IOPROX_XSF_RE`），对 `IO PROX....... %08X` 会回退到 `IOPROX_RAW_RE`（要求 `ID|Raw`），也可能失败。**移植时应同时支持两种。**

### 3.4 版本差异（v4.20728 → v4.23346）

| 项目 | v4.20728（Phosphor 的样本） | v4.23346（本项目） | 影响 |
|---|---|---|---|
| `lf search` 参数 | 同 | 同 | 无 |
| ASK/PSK 分析段 | 存在但更短 | 增加了 `data autodemod` 风格的 `spectrum/decision/margin` 提示 | 解析逻辑需忽略更多行 |
| 新增协议 | Destron/Paxton/Verichip 标签可能不存在 | 二进制度量字符串中存在 | 可能多出未被解析的卡型 |
| T5577 输出 | `Chip type.....`、`Password set......` | 同 | 无 |
| `lf t55xx chk` | 存在 | 存在 | 无 |
| `lf tune` | `--divisor` 存在 | 存在，另加 `--mix/--bar/--value/--rgb/-q` | 主项目只用了 `--divisor` |

> **必须重新采集 v4.23346 的真实输出样本**，不能直接沿用 Phosphor 的测试夹具。

---

## 4. T55xx 命令细节

### 4.1 `lf t55xx detect`

| 项 | 内容 |
|---|---|
| Client 帮助 | `available offline: no`，需设备 + 卡 |
| 输出（`[Client]` 模板） | `Chip type..... T55x7`、`Password set...... Yes/No`、`Block0........ %08x`、`Modulation........ `、`Bit rate......... `、`Offset............ %d`、`Inverted.......... %s` |
| Phosphor 正则 | `T5577_CHIP_RE` `Chip\s*(?:type)?\.+\s*(T55x7\|T5555\|T5577)`<br>`T5577_PASSWORD_RE` `Password\s*(?:set)?\.+\s*(Yes\|No)`<br>`T5577_BLOCK0_RE` `Block0\.+\s*([0-9A-Fa-f]{8})`<br>`T5577_MOD_RE` `Modulation\.+\s*(.+)` |
| 主项目 | `t55xx.cpp:348-414`，解析 `BIT RATE`、`OFFSET`、`INVERTED`、调制关键字 |
| 差异 | Phosphor 解析密码状态，主项目不解析；主项目解析 bit rate/offset/inverted，Phosphor 不解析 |

### 4.2 `lf t55xx chk`

- 用途：穷举/搜索已知密码。
- 风险：**耗时不可控**。Phosphor 用统一 30 s 超时，一旦超时会触发 Client 重启（`connection.rs` 无 kill 能力，只能 drop future）。
- 输出正则：`\[\+\]\s*(?:Found valid )?[Pp]assword[:\s]+([0-9A-Fa-f]{8})`。
- **合规提示**：这是"密码恢复"功能。按本项目安全约束，不得扩展为破解/绕过；建议只做"用户已知密码"的输入，不做自动穷举。

### 4.3 `lf t55xx wipe`

- `-p <8hex>` 仅在已知密码时添加。
- Client 帮助中 `wipe` 无交互确认；**破坏性操作**。
- Phosphor 的复检策略：wipe 后再 `detect`，要求 `detected && !password_set`（`write.rs:245-258`）。

### 4.4 Phosphor **没有**用到的 T55xx 命令

| 命令 | Client | 说明 |
|---|---|---|
| `lf t55xx info` | 是 | 读 block0 及配置 |
| `lf t55xx dump` | 是 | 读全部 block |
| `lf t55xx read -b <n>` | 是 | 单块读 |
| `lf t55xx write -b <n> -d <hex>` | 是 | 单块写 |
| `lf t55xx config` | 是 | 配置调制/速率/密码 |
| `lf t55xx chk` | 是 | （Phosphor 用了） |
| `lf t55xx wipe` | 是 | （Phosphor 用了） |
| `lf t55xx dangerraw` | 是 | **高危，禁止** |

详见 `T55XX_ANALYSIS.md`。

---

## 5. 通用行为矩阵（比命令名更重要）

| 维度 | 实测/源码结论 |
|---|---|
| **参数格式** | Client 使用 `--long-option` 与单字符短选项（`-w`=wiegand、`-r`=raw、`-t`=type、`-d`=data、`-y`=year、`-p`=password、`-a`=address/amp）。Phosphor 全部使用长选项，符合 Client 帮助。 |
| **默认参数** | `lf hid clone` 默认 `H10301`；`lf awid clone` 不传 `--fmt` 时由 Client 决定；`lf t55xx wipe` 无 `-p` 时尝试默认密码。 |
| **返回内容** | 全部为**人类可读文本**，无 JSON/CSV。Client 有 `--incognito` 与 `prefs set output` 但都不能输出结构化数据。 |
| **错误码** | Phosphor 只处理 `0=成功`、`-5/251=超时`、其余=失败（`connection.rs:151-171`）。**Client 的真实退出码语义未在帮助中说明**，此映射属 `[推断]`。 |
| **超时行为** | Phosphor 固定 30 s（`PM3_COMMAND_TIMEOUT`）。超时后无法 kill 子进程，靠 Tauri 丢弃 future。主项目 `ClientSession` 超时后会**重启 Client**（`clientsession.cpp:614-641`），更彻底但代价更高。 |
| **设备占用** | 每个命令一个独立 `proxmark3.exe` 进程，独占串口。因此**不能并发**；Phosphor 没有队列，靠 UI 串行。主项目有 FIFO 队列 + detached 互斥，**更安全**。 |
| **停止当前命令** | Phosphor：无。主项目：`Stop` = `restartClient()`，能强制结束 `lf tune`/`lf read` 这类 detached 命令。 |
| **FPGA 切换** | LF 命令会要求设备切换到 LF FPGA 模式；**当前固件没有 LF 镜像**，因此必然失败（见硬件文档）。Client 侧不携带 `.ncd`，镜像由固件提供。 |
| **固件版本依赖** | 强依赖。LF 命令存在**不代表**固件实现。建议移植前先做能力探测（见 `PM3_HARDWARE_COMPATIBILITY.md` §6）。 |

---

## 6. 移植工作量与风险评估

| 分组 | 命令数 | 预估工作量 | 主要风险 |
|---|---|---|---|
| 搜索/解析层 | 1（`lf search`）+ 22 个解析器 | 中（3–5 人日） | 输出格式漂移；需真实样本 |
| 空白卡检测 | 3（t55xx detect/chk、em info） | 低（0.5 人日） | 超时；密码穷举合规性 |
| 写入 | 22 + 2 wipe | 中（2–4 人日） | 破坏性操作；失败检测不可靠 |
| 复读验证 | 1（`lf search`） | 低（0.5 人日） | 字段比较规则 |
| 能力探测 | 新增（无 Phosphor 参考） | 中（1–2 人日） | 需要设计探测策略 |

**总计（P0+P1+P2 范围）：约 8–12 人日**，不含真实硬件联调。

---

## 7. 迁移到主项目"命令配置"体系的建议

主项目已有 `config/config_rrgv4.23346.json` 的 `"lf"` 节（命令字符串 + 正则）。建议**沿用该机制**新增 LF 卡型配置，而不是硬编码在 C++ 里：

```jsonc
"lf card": {
  "EM4100": {
    "detect": "EM 410x|EM410x",
    "pattern": "EM 410x ID\\s*[-:]?\\s*([0-9A-Fa-f]{10})",
    "clone": "lf em 410x clone --id <id>",
    "em4305": true,
    "fields": ["id"]
  },
  "HIDProx": {
    "detect": "HID Prox|HID Corporate",
    "pattern": "FC[:/\\s]*(\\d+)\\s*[,;]?\\s*CN[:/\\s]*(\\d+)",
    "clone": "lf hid clone -w H10301 --fc <fc> --cn <cn>",
    "raw clone": "lf hid clone -r <raw>",
    "fields": ["facility_code","card_number","raw","format"]
  }
  // ...
}
```

优点：
- 与既有 `config_rrgv*.json` 版本适配机制一致；
- 固件/Client 升级时只改 JSON，不改代码；
- 便于把 Phosphor 的正则"移植"为**配置数据**而非**代码**，规避 GPL 代码复制问题（见许可证文档）。

> 但要注意：JSON 只能描述"正则 + 模板"，**不能描述条件分支**（例如 HID 优先 raw、IO Prox hex→dec 转换、Keri 按类型选参）。这些仍需少量 C++ 策略代码。
