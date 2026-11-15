# PM3 256 KB 硬件与 LF 能力审计

**设备：** Proxmark3 `COM14`，`USB\VID_9AC4&PID_4B8F\ICEMAN`
**MCU：** AT91SAM7S256 Rev D（256 KB Flash / 64 KB RAM）
**Client：** `Iceman/master/v4.23346-suspect 2026-09-18 9ce8a4b6c`
**固件：** `fullimage.elf`（PLATFORM=PM3GENERIC, PLATFORM_SIZE=256）

> **结论先行：**
> **硬件有 LF 前端（含天线与驱动），但当前固件里没有任何 LF 代码，也没有 LF 的 FPGA 比特流。**
> 因此在这台设备的当前状态下，**LF 功能为零**——不是"未验证"，是"确定不可用"。
> 要让 LF 工作，必须更换固件（本阶段禁止）。是否能在 256 KB 内塞下 LF，**尚未验证，属未知**。

---

## 1. 硬件身份

| 项目 | 值 | 依据 |
|---|---|---|
| 型号 | `PM3 GENERIC` | `docs/HARDWARE_VALIDATION_MATRIX.md:6`、`pm3-256-iceman-v4.23346/README.md` |
| MCU | `AT91SAM7S256 Rev D` | 同上 |
| Flash / RAM | 256 KB / 64 KB | 同上 |
| USB（刷写前） | `VID_2D2D / PID_504D`，COM13 | `pm3-256-iceman-v4.23346/README.md` |
| USB（刷写后） | `VID_9AC4 / PID_4B8F`，COM14，序列号 `ICEMAN` | 同上 |
| 固件来源 | Iceman `9ce8a4b6c`（2026-09-18） | 同上 |
| 工具链 | arm-none-eabi-gcc 13.2.1 | 同上 |

> 注：`PM3 GENERIC` 是 Iceman 对"非 RDV4"设备的通用标识。它**不能**直接说明 PCB 版本（Easy / 2.0 / 3.0 等）。审计未拆机、未读取 PCB silkscreen，因此**PCB 具体版本未确认**（`[推断]` 为 PM3 Easy 类通用板）。

---

## 2. LF 模拟前端（硬件侧）

来自 `pm3-256-iceman-v4.23346/README.md` 的实测记录（刷写当天）：

| 项目 | 实测值 | 结论 |
|---|---|---|
| LF 天线 | 125 kHz 下 **33.8 V**；130.4 kHz 峰值 **37.9 V** @ 空载 | **LF 天线与驱动电路存在且可谐振** |
| HF 天线 | 13.56 MHz 下 **29.0 V** | 正常 |
| 读卡电路 | ISO14443A 可选卡执行，其他协议超时无卡 | HF 前端正常 |

**判定：硬件具备 LF 能力。** 这与"固件没有 LF"并不矛盾——天线是被动谐振回路，即使固件不支持 LF，`lf tune` 之外的手段（例如 FPGA 直接驱动）也可能让它起振；但当前固件根本无法调用 LF 采样路径。

> 注意：33.8 V / 37.9 V 是**空载谐振电压**，只能证明"天线能振"，**不能证明"能读卡"**。读卡还需要接收链路 + 采样 + 解码。

---

## 3. 固件字符串分析（决定性证据）

### 3.1 方法

```powershell
$f = "E:\Opencode_project\pm3-256-iceman-v4.23346\fullimage.elf"
$b = [System.IO.File]::ReadAllBytes($f)
$sb = New-Object System.Text.StringBuilder
$out = New-Object System.Collections.Generic.List[string]
foreach ($x in $b) {
    if ($x -ge 32 -and $x -lt 127) { [void]$sb.Append([char]$x) }
    else { if ($sb.Length -ge 4) { $out.Add($sb.ToString()) }; [void]$sb.Clear() }
}
$out | Set-Content fw_strings.txt -Encoding UTF8
```

- 共提取 **2,046** 个 ASCII 字符串（≥4 字符）。
- 对每个关键字做**大小写不敏感**匹配。

### 3.2 结果

| 关键字 | 命中数 | 说明 |
|---|---|---|
| `t55xx` | **0** | 无 T55xx 命令 |
| `T55` | **0** | |
| `410x` / `EM410` | **0** | 无 EM410x |
| `lf ` / `LF ` | **0** | 无任何 LF 前缀 |
| `hitag` | **0** | |
| `indala` / `awid` / `io prox` / `paradox` | **0** | |
| `keri` / `viking` / `pyramid` / `presco` / `nedap` | **0** | |
| `gprox` / `gallagher` / `noralsy` / `jablotron` | **0** | |
| `securakey` / `visa2000` / `idteck` / `fdxb` / `FDX` | **0** | |
| `ASK` / `PSK` / `Manchester` | **0** | 无调制/编码实现 |
| `fpga_pm3_lf` | **0** | **无 LF FPGA 比特流** |
| `fpga_pm3_hf.ncd image ...` | 1 | 有 HF 镜像 |
| `fpga_pm3_felica.ncd image ...` | 1 | 有 FeliCa 镜像 |
| `ISO15693 FSK: signal activity detected...` | 2 | 仅残留提示字符串 |

对比：同一台机器的 **Client**（`proxmark3.exe`）包含 **76,225** 个字符串，其中 `lf t55xx` 相关 77 条、`lf hitag` 123 条、`lf em 4x05` 44 条。

### 3.3 解释

- PM3 的 **FPGA 比特流是编译进固件的**（`fpga_pm3_hf.ncd`、`fpga_pm3_felica.ncd` 以数组形式存在），Client 不携带 `.ncd`（Client 二进制里检索 `fpga_pm3` 只有 `hw fpga config` 这类命令文本，没有镜像名）。
- 因此 **没有 `fpga_pm3_lf` = 无法进入 LF 采样模式**。
- 同时固件里连 `lf` 命令表都不存在，`lf search` 必然返回 `unknown command`。

### 3.4 与既有文档的交叉验证

`pm3-256-iceman-v4.23346/README.md` 明确记录：

```
LF | `lf search` → `unknown command:: 0x0228`（预期，已移除）
```

以及：

```
移除（256KB 让步版）：
- 全量 LF 125/134 kHz（EM4100 / HID / T5577 / ID 系列）
- ISO15693、Hitag、Legic、EM4x50、EM4x70、ISO14443B、NFC barcode、ZX8211
- DesFire 模拟、standalone 命令模式
保留：
- HF 13.56 MHz（ISO14443A / MIFARE Classic / DESFire / Ultralight）、FeliCa（含 NFC 通讯等）、iCLASS/SEOS
- HF sniff / HF plot / MIFFARE 硬件（hardnested）/ HF 相关
- 压缩（COMPRESSION）等
```

**两条独立证据（二进制字符串 + 构建记录）一致。**

---

## 4. Flash 使用情况

| 项目 | 值 |
|---|---|
| `fullimage.elf` | 234,272 B |
| 实际烧录区（README 记录） | 233,394 B @ `0x102000–0x13AFB2` |
| Flash 总量 | 256 KB（262,144 B） |
| 占用率 | **92%**（README） |
| `recovery.bin`（bootrom+固件） | 249,778 B = 95% |

**结论：Flash 已接近上限。** 加入 LF 必须移除其他功能（历史上移除的是 FeliCa/ISO15693 的一部分——但当前固件**仍保留 FeliCa**，说明当时的取舍是"保 FeliCa、砍 LF"）。

FeliCa 镜像约 **41 KB**（README 表述）。理论上把 FeliCa 换成 LF 可能放得下，但：

- LF 需要的代码体积（所有 `lf` 子命令 + T55xx + 各协议解码）**未必小于 41 KB**；
- 还需 LF FPGA 镜像；
- **本阶段禁止编译/刷写固件，因此无法验证**。

→ 标记为 **未知（需构建验证）**。

---

## 5. 当前 Client 的 LF 能力（静态确认）

`proxmark3.exe --fulltext --incognito` 离线导出（不接触设备），LF 命令组：

```
lf config / lf cmdread / lf read / lf relay / lf search / lf sim /
lf simask / lf simfsk / lf simpsk / lf sniff / lf tune
lf awid / lf cotag / lf destron / lf em / lf em 410x / lf em 4x05 /
lf em 4x50 / lf em 4x70 / lf fdxb / lf gallagher / lf gproxii / lf hid /
lf hitag / lf hitag hts / lf hitag htu / lf idteck / lf indala / lf io /
lf jablotron / lf keri / lf motorola / lf nedap / lf nexwatch / lf noralsy /
lf pac / lf paradox / lf pcf7931 / lf presco / lf pyramid / lf securakey /
lf ti / lf t55xx / lf trovan / lf viking / lf visa2000
```

**Client 侧：完整支持。**
**固件侧：零支持。**
这正是"命令存在 ≠ 硬件可用"的典型例子。

---

## 6. 能力分类（任务书要求的六档）

| 分类 | 项目 | 证据等级 |
|---|---|---|
| **已确认支持** | HF 13.56 MHz 协议（ISO14443A/MIFARE/DESFire/Ultralight/FeliCa/iCLASS） | `[文档]` + `[固件]`（fpga_pm3_hf/felica） |
| **已确认支持** | LF 天线与驱动电路存在（125 kHz 33.8 V） | `[文档]`（实测记录） |
| **软件支持但硬件未验证** | Client 的 35 个 LF 命令组 | `[Client]` |
| **硬件具备但固件未验证** | LF 接收链路 / 采样 / 解码 | `[推断]`（天线能振，但无固件验证） |
| **当前固件不支持** | 全部 LF 功能（`lf search`/`lf tune`/`lf read`/`lf config`/`lf t55xx`/所有 clone） | `[固件]` 字符串 + `[文档]` 实测 `unknown command:: 0x0228` |
| **需要更换硬件** | 无（LF 前端已具备；瓶颈在固件） | — |
| **证据不足 / 未知** | 256 KB 固件能否容纳 LF（换掉 FeliCa 后） | 需构建验证，本阶段禁止 |

---

## 7. 对"LF 能力检测"的设计建议（主项目当前没有）

任务书要求 P0 包含"LF 硬件能力检测"。当前主项目**没有任何能力检测**（`ClientSession` 只解析版本字符串）。

### 7.1 建议的三级探测

| 级别 | 方法 | 判定 |
|---|---|---|
| L1 固件命令存在性 | 发送 `lf help`（离线可执行，不需设备）或 `lf search` 并检查是否含 `unknown command` | 命令表是否存在 |
| L2 固件版本/构建标识 | `hw version` 输出中的 `OS` 与 `PLATFORM`；`hw status` 中的 `LF Sampling config` 段是否存在 | 是否含 LF 模块 |
| L3 实际探测（只读） | `lf tune`（空载）观察电压；`lf search`（无卡）观察是否返回 `No known 125/134 kHz tags found` 而不是 `unknown command` | 真正可用 |

**注意：**

- `lf help` 在 `available offline` 列标为 `Y`（Client 侧帮助），**不能证明固件支持**。
- 因此 L1 的判据必须是**执行结果**，而不是帮助文本。
- L3 的 `lf tune` 会持续驱动天线，属于只读但会发射场，应在 UI 明确提示。

### 7.2 建议的判定状态机

```
Unknown
  ├─ lf search 返回 "unknown command"  → FirmwareUnsupported（确定）
  ├─ lf search 返回 "No known 125/134 kHz tags found"
  │     → FirmwareSupported / NoCard
  ├─ lf search 返回具体卡型          → FirmwareSupported / CardPresent
  └─ 超时 / 无响应                    → Indeterminate（需重试）
```

这个状态必须持久化到 UI（例如 LF 页顶部显示"当前固件不支持 LF"），避免用户反复点击后困惑。

---

## 8. 若要启用 LF，需要做什么（仅说明，不执行）

> ⚠️ **本阶段禁止刷写。以下仅为技术路径说明，不作为操作指令。**

| 路径 | 需要 | 风险 | 可行性 |
|---|---|---|---|
| A. 换用含 LF 的 256 KB 固件 | 重新编译 Iceman（`PLATFORM_SIZE=256`，去掉 FeliCa/ISO15693，保留 LF） | 体积可能仍不够；需实测容量 | **未知** |
| B. 使用 512 KB 设备（RDV4 等） | 新硬件 | 成本 | 高 |
| C. 保留当前固件，仅做 UI/解析 | 不做真实 LF | 功能无法验证 | 只能做"离线解析 + 合成测试" |

**任何路径都不能在本阶段执行。**

---

## 9. 与主项目现有文档的一致性

`docs/HARDWARE_VALIDATION_MATRIX.md` 已记录：

```
| LF / T55xx | `lf *` | PM3 | 固件无 LF | 未验证 | — |
1. 当前 256 KB 固件无 LF 功能，LF/T55xx 无法验证。
```

`src/module/t55xx.h` 注释：

```
Hardware note: the currently attached device has LF disabled in firmware,
so every device operation is Experimental / Not verified on hardware.
```

`src/module/t55xx.cpp` 中 UI 提示：

```
"The current firmware has no LF support, so this is not hardware-verified."
```

**本次审计独立复核并确认了这些说法**（通过固件二进制字符串 + Client 静态能力对比），并补充了关键细节：**缺的不只是"LF 模块"，还包括 `fpga_pm3_lf` 比特流。**

---

## 10. 结论

| 问题 | 答案 |
|---|---|
| 当前 PM3 PCB 型号/硬件版本 | `PM3 GENERIC`，AT91SAM7S256 Rev D；**具体 PCB 版本未确认** |
| 是否有 LF 天线和模拟前端 | **有**（125 kHz 33.8 V 实测） |
| 是否有 LF 接收和驱动电路 | **有**（驱动已证明；接收链路未单独验证） |
| FPGA 镜像是否含 LF | **否**（仅 hf + felica） |
| 当前固件是否编译了 LF 模块 | **否**（0 个 LF 字符串） |
| Client 是否支持 LF 命令 | **是**（35 个命令组） |
| 设备能力检测是否报告 LF | **否**（`lf search` → `unknown command:: 0x0228`） |
| 当前固件是否经过裁剪 | **是**，明确为"保 FeliCa 砍 LF" |
| Flash 使用 | 92%（234 KB / 256 KB） |
| 是否存在硬件版本导致的功能限制 | 未发现；限制来自固件 |

**最终判定：硬件可行性 = 中（前端具备，固件缺失，需换固件且容量未知）；当前状态下的 LF 可行性 = 零。**
