# Stage 2 Report — 修复现有业务功能

**日期：** 2026-11-10  
**分支：** `upgrade/iceman-v4.23346`  
**目标：** 修复 MIFARE Classic、MIFARE 仿真、Trace、LF Read/Sniff 与 v4.23346 的兼容问题。

---

## 1. 实际完成内容

### 1.1 MIFARE Classic — 卡信息解析

问题：

`hf 14a info` 的 UID 行包含注释：

```text
[+]  UID: 2F 7B 54 54   ( FNUID, fixed, non-unique ID )
```

旧代码先 `remove("UID")`，再去掉非 hex 字符，结果会把注释中的 `F` 保留下来，UID 变为 `F2F7B5454`。

修复：

- 改为按标签正则捕获：
  - `UID:\s*([0-9A-Fa-f ]+)`
  - `ATQA:\s*([0-9A-Fa-f ]+)`
  - `SAK:\s*([0-9A-Fa-f ]+)`
- 不再从整行删除标签。

验证：

在真实卡（HID SEOS / EMV，UID `2F 7B 54 54`）上打开 “Set Parameter” 对话框（未提交），读到的预填值为：

```text
UID  = 2F7B5454
ATQA = 0004
SAK  = 20
```

符合 `hf 14a info` 输出。

### 1.2 MIFARE Classic — Check Default 超时

问题：

v4.23346 的 `hf mf chk --1k` 在真实设备上耗时约 **27 秒**：

```text
[=] Time in checkkeys 27 seconds
```

旧 GUI 超时为 `1000 + 16 * 200 = 4200 ms`，会在结果表格出现前超时。

修复：

- `Mifare::chk()` 的 `ReturnTrigger` 等待时间改为 **60000 ms**。
- `ClientSession` 的超时为 `max(waitTime + 5000, 15000)`，因此实际约 65 秒。

验证：

- 通过 GUI 点击 “Check Default”；
- 状态栏 `State:Running` 约 27 秒；
- 随后回到 `State:Idle`；
- Raw 输出包含完整 16 行 key 表格；
- 未出现超时。

当前插入的卡不是 MIFARE Classic，因此表格全部为 `------------ | 0`，但命令、超时和输出解析路径已实际跑通。

### 1.3 MIFARE Classic — Nested 超时

- `Mifare::nested()` 等待时间从 15 秒提高到 **60 秒**。
- 原因：Nested/StaticNested 在真实卡上可能明显超过 15 秒。
- 未在当前卡上验证（卡不是 MIFARE Classic）。

### 1.4 文件读写健壮性

修复 `data_loadDataFile` / `data_loadKeyFile` / `data_saveDataFile` / `data_saveKeyFile` / `bin2text`：

- 不再按固定长度读取后直接索引；先读完整文件并检查长度。
- `.bin/.dump` 强制按二进制解析；`.eml/.txt` 强制按文本解析；无扩展名时才使用内容启发式。
- 文本文件要求至少 `block_size` 行；每行规范化为 32 hex（允许 `?` 表示未知）。
- 二进制文件要求至少 `block_size * 16` 字节。
- Key 文件支持：
  - 12 bytes/sector（6 + 6）；
  - 完整 dump 的 sector trailer 提取。
- 保存前验证每个块/密钥的长度与 hex 合法性；空值、非法值不再导致 `QString::at()` 越界。
- `bin2text` 增加边界检查。
- 保存文本时统一输出 32 hex + 换行。

未做 UI 文件对话框的完整回归（见第 6 节）。

### 1.5 MIFARE 仿真 — v4.23346 参数

旧代码在 Iceman 分支写入：

```cpp
ui->xBox->setText("--crack");
```

v4.23346 的 `hf mf sim --help` 明确没有 `--crack`：

```text
usage:
    hf mf sim [-hixyev] ... [-x] [-y] ...
options:
    -x    Performs the 'reader attack', nr/ar attack against a reader.
    -y    Performs the nested 'reader attack'. ...
```

修复：

- `xBox` 改为 **`-x`**。
- 保留 `-i`、`-n`、`-u`、`-e`、`-v`、`--atqa`、`--sak`。
- 未实现 `-y` 的 UI（旧 UI 没有对应语义），不猜测。

验证：

- 已用真实 Client 执行 `hf mf sim --help` 确认参数；
- 未执行实际仿真（当前卡不是 MIFARE Classic，且仿真会占用设备/射频）。

### 1.6 Trace Load / Save

问题：

- 旧 Load 依赖输出中的小写 `"loaded"`；
- v4.23346 输出：
  ```text
  [+] Loaded 3070 bytes from binary file ...
  [+] Recorded Activity (TraceLen = 3070 bytes)
  ```
- 旧 Save 使用 `trace save -f`；v4.23346 默认从设备下载，不保存 Client buffer。

修复：

- Load 触发文本改为 `Recorded Activity` / `TraceLen`；
- 实际完成仍由 `ClientSession` 的 `rem` 哨兵保证；
- `Mifare` 增加 `sniffBufferLoaded` 状态：
  - `loadSniff()` 成功后置 true；
  - 新的 `sniff()` / `sniff14a()` 置 false；
- `saveSniff()` 在 buffer 模式下生成：
  ```text
  trace save -1 -f <tmp file>
  ```
- 仍由 GUI 把临时文件复制到用户选择的位置，避免客户端绝对路径/空格问题。

验证（真实 Client + 真实设备，只读）：

1. 客户端级 round-trip：
   - 源 `climate-r1.trace`：3070 bytes；
   - `trace load` → `trace save -1`：
     - 输出 `Saved 3070 bytes ...`
     - SHA-256 完全一致。
2. GUI 级 round-trip：
   - 通过 GUI “Sniff → Load” 选择 `climate-r1.trace`；
   - Raw 输出显示 `Recorded Activity (TraceLen = 3070 bytes)`；
   - 通过 GUI “Sniff → Save” 保存到 `build-stage0/gui_trace_saved.trace`；
   - Raw 输出确认使用了 `trace save -1 -f tmp....trace`；
   - 保存文件 3070 bytes；
   - 源文件与 GUI 保存文件 SHA-256 完全一致。

这是本阶段最强的功能验收项。

### 1.7 危险操作确认

新增二次确认：

- `Restore`：提示会写入 dump、客户端按当前 UID 选择默认文件；
- `Wipe`（Magic Card）：提示会擦除可写数据；
- `Lock UFUID Card`：提示可能不可逆；
- `Set Parameter`（csetuid）：校验 UID/ATQA/SAK 长度与 hex，并确认写入。

未改变原有按钮位置和操作入口。

### 1.8 LF Read / Sniff

代码路径保留：

- `lf read -v`；
- `lf sniff -v`；
- `data plot`；
- `Util::gotoRawTab()`。

**但当前真实设备无法验证：**

- 设备 firmware 为 256K `PM3 GENERIC`，构建裁剪了 LF；
- `hw status` 输出没有 `LF Sampling config` / `T55xx config`；
- `lf help` 只列出协议子命令，没有 `read` / `sniff` / `tune` / `config`；
- 直接执行：
  ```text
  proxmark3.exe COM14 --incognito -c "lf read -v"
  [!] This command is not available in this mode
  ```

结论：

- **LF Read / Sniff：Not verified on physical hardware（firmware lacks LF）**；
- 未修改其业务逻辑；
- Stage 5 波形功能同样无法在该固件上做真实采样验证；只能依据 Client 源码/帮助实现，并明确限制。

---

## 2. 修改文件列表

| 文件 | 修改 |
|---|---|
| `src/module/mifare.h` | 增加 `sniffBufferLoaded` |
| `src/module/mifare.cpp` | UID/ATQA/SAK 正则、chk/nested 超时、Trace load/save、文件 IO 校验、ACBits 防越界、危险操作确认 |
| `src/ui/mf_sim_simdialog.cpp` | `--crack` → `-x` |
| `src/ui/mf_uid_parameterdialog.cpp` | UID/ATQA/SAK 校验、写入确认 |
| `docs/STAGE2_REPORT.md` | 本报告 |

---

## 3. 编译结果

```text
qmake-qt5 ..\src\Proxmark3GUI.pro -spec win32-g++ CONFIG+=release
mingw32-make -j4
```

结果：**编译、链接成功**。产物时间戳更新，未出现新增编译错误。

---

## 4. 测试结果汇总

| 项目 | 结果 | 等级 |
|---|---|---|
| MIFARE UID/ATQA/SAK 解析 | 通过 | 真实设备 + 真实卡（非 MF Classic） |
| Check Default 完整执行（27s） | 通过 | 真实设备 + Client |
| Nested 超时参数 | 已修改 | 未验证（卡不匹配） |
| 文件 IO 边界检查 | 已修改 | 静态检查，未做完整 UI 回归 |
| 仿真 `-x` 参数 | 已按 `--help` 修复 | 未执行仿真 |
| Trace Load（GUI） | 通过 | 真实 Client/设备，只读 |
| Trace Save（GUI，buffer） | 通过 | SHA-256 一致 |
| Trace 客户端 round-trip | 通过 | SHA-256 一致 |
| LF Read/Sniff | **不可用** | 固件无 LF，Not verified on physical hardware |
| 写卡/擦除/恢复 | **未执行** | 按纪律禁止 |

---

## 5. 已知问题

1. **当前设备 firmware 不具备 LF/T55xx 能力**
   - 无法验证 LF Read/Sniff、T55xx、Stage 5 波形真实数据。
   - 不能通过刷写固件绕过（项目纪律禁止）。
2. **当前卡不是 MIFARE Classic**
   - 无法验证 MIFARE Classic 成功读块/写块、Nested、Hardnested、仿真。
3. **文件 IO 尚未做完整 GUI 文件对话框回归**
   - 代码已加固；后续可用测试文件做 round-trip。
4. **Restore 仍按客户端默认 UID 文件名**
   - 已加确认，但尚未增加文件选择器；Stage 6 可考虑。
5. **`data plot` 在当前 Windows Client 中不可用**
   - Client 构建 `QT GUI support... absent`；LF 波形必须由 GUI 自己绘制（Stage 5）。

---

## 6. 下一阶段计划

Stage 3：T55xx UI 恢复与基础实现。

- 恢复原始 `mainwindow.ui` 的 T55xx 配置区；
- 保留 EM410x 读取/克隆；
- 对照 v4.23346 `lf t55xx` 命令逐项实现；
- 因当前固件无 LF，所有 T55xx 功能标记为 **Experimental / Not verified on physical hardware**；
- 不伪造成功结果。

---

**Stage 2 结论：通过（在可用硬件范围内）。** Trace、MIFARE 信息/检查超时、仿真参数和文件 IO 已修复；LF/T55xx 受当前固件能力限制，明确标记未验证。
