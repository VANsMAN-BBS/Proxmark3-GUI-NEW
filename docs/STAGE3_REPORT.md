# Stage 3 Report — T55xx UI 恢复与实验性实现

**日期：** 2026-11-10  
**分支：** `upgrade/iceman-v4.23346`  
**目标：** 恢复原 `mainwindow.ui` 中的 T55xx 配置页，保留 EM410x 克隆，实现能在 v4.23346 上追溯的基础功能，并明确 Experimental 状态。

---

## 1. 实际完成内容

### 1.1 UI 恢复方式

原项目：

- `mainwindow.ui` 中存在完整的 T55xx 配置页；
- `t55xxtab.cpp` 只实现 EM410x 克隆；
- 运行时先插入动态 `T55xxTab`，再删除原配置页，导致配置 UI 丢失。

本阶段：

- **不再删除原 T55xx 页**；
- 不再插入第二套动态页；
- 在原页面上追加 EM410x 克隆组；
- 保留原有三个分组：
  - `Basic Configuration(Page 0 Block 0)`
  - `Analog Front-End Option(Page 1 Block 3)`
  - `T55xx Read Config`
- 新增：
  - `Detect`、`Read Info`、`Apply` 按钮；
  - `Clone to T55xx (Experimental)` 分组（T5555/T55x7、EM410x Read/Clone）。
- 页面仍是一个 T55xx 标签页，没有新增第二个 T55xx 页。

### 1.2 Block 0 本地编辑器

实现：

- Hex ↔ Bin 双向转换；
- `Get from Data` / `Set to Data` 对 Block 0 字段进行解码/编码；
- 字段与 bit 映射仅使用可从 v4.23346 源码验证的部分：
  - Modulation：bits 12–16
  - eXtended Mode：bit 17
  - Data Bit Rate：bits 18–20（3-bit）/ 18–23（extended 6-bit）
  - Max Block：bits 5–7
  - Password/pwd flag：bit 4
  - Sequence Terminator：bit 3
  - Fast Downlink：bit 2
  - Inverse Data：bit 1
  - Init-Delay：bit 0
- 依据：
  - `client/src/cmdlft55xx.c` 的 `SetConfigWithBlock0Ex()`
  - `T55X7_*_CONFIG_BLOCK` 示例值
  - `lf t55xx --help` / `lf t55xx config --help`

验证（纯本地、无设备）：

输入：

```text
Hex = 00107060
```

结果：

```text
Bin        = 0000 0000 0001 0000 0111 0000 0110 0000
Bit Rate   = RF/50
Modulation = FSK2a
Max Block  = 3
```

与 v4.23346 中 `T55X7_HID_26_CONFIG_BLOCK = 0x00107060`（FSK2a、data rate 50、3 data blocks）一致。

### 1.3 未验证字段的处理

以下控件**保留界面但禁用**，并设置 tooltip “Not implemented in this version …”：

- Locked
- Master Key
- PSK Clock Freq
- Answer on Request
- One Time Pad
- Sequence Start Marker

原因：无法在 v4.23346 源码/帮助中确认其 bit 映射或完整语义，不猜测。

`Analog Front-End Option(Page 1 Block 3)`：

- 保留原控件；
- 仅实现 Hex ↔ Bin 本地转换；
- 详细 combo/spin/checkbox 全部禁用并标注未实现。

### 1.4 T55xx 读取/配置命令

仅使用 `lf t55xx --help` 中明确列出的命令：

```text
lf t55xx detect
lf t55xx info
lf t55xx config
```

`Apply` 构造：

```text
lf t55xx config
  --ASK / --FSK1 / --FSK1A / --FSK2 / --FSK2A /
  --PSK1 / --PSK2 / --PSK3 / --BI / --BIA / --NRZ
  --rate <0..7>
  -o <offset>
  [-i]
  [--st]
  [--q5]
```

依据：

- `client/src/cmdlft55xx.c` 的 `CmdT55xxSetConfig` argtable；
- `lf t55xx --help` 输出。

`Detect` / `Read Info`：

- 调用 `lf t55xx detect` / `lf t55xx info`；
- 尝试解析 modulation / bit rate / offset / inverted / block0；
- 如果 Client 返回 `not available`，显示明确提示，不伪造数据；
- 如果解析失败，提示查看 RawCommand 输出。

### 1.5 EM410x 克隆

保留并重接：

```text
lf em 410x reader
lf em 410x clone --id <10 hex>
lf em 410x clone --id <10 hex> --q5
```

依据：

- v4.23346 `cmdlfem410x.c`；
- Stage 2 已核对 `--id` / `--q5` 存在。

写入前增加确认；当前 firmware 无 LF，未执行写标签。

---

## 2. 修改文件列表

| 文件 | 修改 |
|---|---|
| `src/ui/mainwindow.ui` | 原 T55xx 页保留；追加克隆组、Detect/Read Info/Apply 按钮；调整部分几何 |
| `src/module/t55xx.h` | 新增 |
| `src/module/t55xx.cpp` | 新增 |
| `src/ui/mainwindow.h` | `T55xxTab*` → `T55xx*` |
| `src/ui/mainwindow.cpp` | 移除动态插入/删除 T55xx 页；创建 `T55xx` |
| `src/Proxmark3GUI.pro` | 替换 `t55xxtab` 为 `t55xx` |
| `docs/STAGE3_REPORT.md` | 本报告 |

旧 `src/module/t55xxtab.*` 与 `src/ui/t55xxtab.ui` 保留在 Git 中但不再编译。

---

## 3. 编译结果

Qt 5.15.19 + MinGW 16.2.0 + qmake：

```text
qmake-qt5 ..\src\Proxmark3GUI.pro -spec win32-g++ CONFIG+=release
mingw32-make -j4
```

结果：**编译、链接成功**。仅有原有弃用警告。

---

## 4. 测试结果

| 项目 | 结果 | 说明 |
|---|---|---|
| T55xx 页显示 | 通过 | 原配置区 + 克隆区同页显示 |
| Block 0 hex/bin 转换 | 通过 | 00107060 → 正确二进制 |
| Block 0 字段解码 | 通过 | FSK2a / RF50 / MaxBlock 3 |
| 禁用未验证控件 | 通过 | 灰显 + tooltip |
| `lf t55xx detect/info` | **Not verified on physical hardware** | 当前 256K firmware 无 LF；命令应返回 not available |
| `lf t55xx config` | **Not verified on physical hardware** | 同上 |
| EM410x clone | **Not verified on physical hardware** | 需要 T55xx 标签，未执行写入 |
| 实际 T55xx 读写 | **未实现/未验证** | 未在 v4.23346 help 中确认可用的 read/write 子命令；不猜测 |

### 硬件限制证据

```text
proxmark3.exe COM14 --incognito -c "hw status"
```

输出没有 `LF Sampling config` / `T55xx config`；`lf help` 不列出 `read` / `sniff` / `tune` / `config`；`lf read -v` 返回：

```text
[!] This command is not available in this mode
```

因此所有 T55xx 设备操作必须保持 **Experimental / Not verified on physical hardware**。

---

## 5. 已知问题

1. 当前 firmware 无 LF，无法进行任何真实 T55xx 验证。
2. Block 0 中未验证字段保持禁用，不伪造映射。
3. `Page 1 Block 3` 仅实现原始 hex/bin，不实现字段语义。
4. T55xx read/write/dump 未加入，因为当前设备上 help 未展示这些子命令；需要换有 LF 的固件/设备后再确认。
5. 页面几何为原固定布局，窗口较窄时右侧分组可能被裁切（原项目已有此问题）。

---

## 6. 下一阶段计划

Stage 4：NTAG / Ultralight 独立页面。

- 使用 `hf mfu info` / `rdbl` / `wrbl`；
- 页面保护：0–3 系统页、配置/OTP 页只读；
- 未知型号禁用写入；
- 当前无 NTAG 标签，成功读写只能标记未验证。

---

**Stage 3 结论：通过（UI 恢复与本地解码已验证；设备功能受固件限制，明确标记 Experimental）。**
