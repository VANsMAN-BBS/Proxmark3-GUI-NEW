# Stage 1 Report — Client 通信层整理

**日期：** 2026-11-10  
**分支：** `upgrade/iceman-v4.23346`  
**阶段目标：** 在不重写业务模块的前提下，建立可复用的 Client Core 层，修复进程生命周期、命令完成判定、编码/ANSI/CR、超时和退出状态。

---

## 1. 实际完成内容

### 1.1 新增 Core 层

新增：

- `src/core/clientinfo.h`
- `src/core/clientsession.h`
- `src/core/clientsession.cpp`

`ClientSession` 负责：

- 唯一的 `QProcess` 实例与 Client 生命周期；
- `startClient` / `stopClient` / `restartClient`；
- 连接握手与 Client/Firmware/型号/硬件信息解析；
- 普通命令 FIFO 队列；
- 本地 `rem <token>` 完成哨兵；
- 命令超时；
- 增量 UTF-8 解码；
- ANSI 控制序列清理；
- CR/CRLF 归一化；
- stdout/stderr 读取；
- `QProcess::finished` / `errorOccurred` 状态处理；
- 版本信息信号 `versionUpdated(ClientInfo)`。

### 1.2 移除旧的通信核心

- `common/pm3process.*` 不再加入编译（文件保留在 Git 中，便于回滚与对照）。
- 移除 `PM3Process` 使用的 `QThread` 工作线程和 `QSerialPortInfo::isBusy()` 断开检测。
- 断开/崩溃检测改为 `QProcess::finished` + `QProcess::errorOccurred`。
- 连接成功不再只依赖 `[=]` 与 `OS....` 的启发式判断，而是：
  - 启动 Client；
  - 发送 `hw version`；
  - 追加 `rem <token>`；
  - 收到 `remark: <token>` 后解析 `OS.........` / `Model` / `uC`；
  - 解析失败才判定连接失败。

### 1.3 命令完成机制

旧机制：

- GUI 线程 `QApplication::processEvents()` 忙等；
- 依赖命令专属文本（如 `"loaded"`、key 表格正则）；
- 有新输出就刷新超时；
- 无命令 ID，输出与命令不能可靠对应。

新机制：

- 普通命令入 FIFO 队列；
- 写入 `command\nrem PM3GUI_<id>_<timestamp>\n`；
- Client 执行完命令后执行本地 `rem`，输出 `remark: <token>`；
- 收到哨兵即认为该命令完成，取出该命令累积输出；
- 队列中的下一条命令才继续写入；
- 超时会上报调用者，但保留 active 状态等待真实哨兵，避免在上一条命令仍运行时写入下一条；
- 内部哨兵行不会显示在 Raw 输出。

> 选择 `rem` 的依据：v4.23346 离线/在线实测 `rem <token>` 输出 `remark: <token>`；它是 Client 本地命令，不访问设备。见 Stage 0 报告第 6 节。

### 1.4 输出处理

`ClientSession` 增加了：

- 增量 UTF-8 解码：跨 `readyRead` 块保留不完整多字节序列；
- ANSI CSI/OSC 清理；
- `\r\n` / 单独 `\r` 归一化为 `\n`；
- stdout 与 stderr 分开读取，但统一送入输出流；
- 命令解析使用清理后的文本，Raw 显示也使用清理后的文本；
- 保留 `rawOutput(QByteArray)` 信号供后续日志使用。

### 1.5 版本信息

`ClientInfo` 当前可解析：

- `clientVersion`：`Iceman/master/v4.23346-suspect...`
- `firmwareVersion`：`OS.........` 后的版本
- `bootromVersion`：`Bootrom....` 后的版本
- `model`：`[ Model ]` 中的 `Firmware...`
- `hardware`：`uC: ...`
- `rawVersionOutput`

连接成功后状态栏显示：

```text
HW Version: Iceman/master/v4.23346-suspect 2026-09-18 00:22:18 9ce8a4b6c
            / PM3 GENERIC / AT91SAM7S256 Rev D
```

无法解析的字段保持空字符串，不猜测。

### 1.6 同步兼容层

保留原有 `Util::execCMD` / `Util::execCMDWithOutput` 接口：

- `execCMD` 继续用于 Raw 终端和 fire-and-forget 命令；
- `execCMDWithOutput` 改为：
  - 调用 `ClientSession::executeCommand`；
  - 用 `QEventLoop` + `QTimer` 等待 `commandFinished`；
  - `QEventLoop::ExcludeUserInputEvents`，避免按钮重入；
  - 不再使用 `QApplication::processEvents()` 忙等；
  - 不再依赖 `QTime` 和固定文本触发。

这样 MIFARE/LF/T55xx 旧模块无需在 Stage 1 全量改写。

---

## 2. 修改文件列表

| 文件 | 修改 |
|---|---|
| `src/core/clientinfo.h` | 新增 |
| `src/core/clientsession.h` | 新增 |
| `src/core/clientsession.cpp` | 新增 |
| `src/Proxmark3GUI.pro` | 加入 Core 文件；移除 `common/pm3process.*` 编译 |
| `src/common/util.h` | 增加 `ClientSession*`、`setSession`、初始化 `isRunning` |
| `src/common/util.cpp` | 新同步等待实现、`QEventLoop`、sentinel 结果 |
| `src/ui/mainwindow.h` | 类型替换、移除线程/QProcess 错误槽、增加版本/错误/连接中槽 |
| `src/ui/mainwindow.cpp` | `ClientSession` 接线、移除 serial listener、Stop 改为 restart、状态栏显示版本 |
| `docs/STAGE1_REPORT.md` | 本报告 |

未修改：

- MIFARE 业务逻辑；
- LF 业务逻辑；
- T55xx 业务逻辑；
- JSON 命令配置；
- UI 布局与控件。

---

## 3. 编译结果

使用 Stage 0 环境（Qt 5.15.19 / MinGW 16.2.0 / qmake）：

```text
qmake-qt5 ..\src\Proxmark3GUI.pro -spec win32-g++ CONFIG+=release
mingw32-make -j4
```

结果：**编译、链接成功**。

产物：

```text
build-stage0\release\Proxmark3GUI.exe
```

仅有已存在的弃用 API 警告和旧 UI 重名警告，没有新增编译错误。

---

## 4. 集成测试结果

测试设备：真实 Proxmark3，`COM14`，VID/PID `9AC4:4B8F`，序列号 `ICEMAN`，firmware `PM3 GENERIC`。  
测试 Client：`E:\Opencode_project\pm3-256-iceman-v4.23346\proxmark3.exe`。  
测试命令均为只读/本地命令；未写卡、未擦除、未刷写、未模拟。

### 4.1 连接与版本

| 项目 | 结果 |
|---|---|
| GUI 启动 | 通过 |
| Connect | 通过 |
| 状态栏版本 | `Iceman/master/v4.23346-suspect ... / PM3 GENERIC / AT91SAM7S256 Rev D` |
| `PM3:Connected` | 通过 |
| 内部哨兵隐藏 | 通过（Raw 中无 `remark: PM3GUI_`，无 `rem PM3GUI_` 握手行） |

### 4.2 Raw 命令

发送 `hw version`：

- 完整输出进入 Raw；
- 不再出现旧版固定等待导致的截断；
- `ClientSession` 能正确识别输出结束（哨兵在后台处理，未显示）。

### 4.3 UTF-8

发送本地命令：

```text
rem 中文测试
```

Raw 输出可见：

```text
[usb|script] pm3 --> rem 中文测试
[+] 2026-11-10T13:32:51Z remark: 中文测试
```

说明：

- `QProcess::write` 已使用 UTF-8，不再使用 `toLatin1()`；
- 增量 UTF-8 解码在 GUI 显示中正确还原中文；
- 该测试只验证命令通道的 UTF-8，不宣称 Windows 文件路径（`fopen`/Client 路径解析）已经全部验证。

### 4.4 LF Config

点击 LF “Get Config”：

- 无错误弹窗；
- 状态回到 Idle；
- 读回：
  - divisor `95`
  - bits per sample `8`
  - decimation `1`
  - trigger threshold `0`
  - samples to skip `0`

说明 `execCMDWithOutput` + 哨兵完成 + `hw status` 文本解析可用。

### 4.5 断开

点击 Disconnect：

- 状态变为 `PM3:Not Connected`；
- 无残留 `proxmark3.exe` 进程；
- 再次 Connect 正常。

### 4.6 Client 异常退出

连接后手动终止子进程：

- 状态栏正确变为 `PM3:Not Connected`；
- 显示 Client 退出错误信息；
- 修正了“错误弹窗先于状态更新”导致状态仍显示 Connected 的问题。

---

## 5. 已知问题 / 未完成

1. **`sendRawCommand` 仍绕过队列**
   - Raw 终端、`execCMD` 的 fire-and-forget 命令立即写入，不参与 FIFO。
   - 长命令（sniff/tune/sim 等）仍可能让用户继续点击其他命令；Stage 2 将逐项改为明确的 detached 模式并锁定 UI。
2. **超时语义**
   - 超时后调用者收到失败，但 Client 仍保持 active，等待迟到哨兵；用户需 Stop 才能强制恢复。
   - 这是为了避免在上一条命令仍在运行时写入下一条。
3. **`keepClientActive` 设置项**
   - 旧设置项保留以兼容配置文件，但串口 `isBusy` 监听已移除，因此该复选框当前不再影响行为。
   - Stage 2/6 需要决定是移除、重命名或改为“保留 Client 进程”的明确语义。
4. **Raw 仍显示连接握手输出**
   - 这是 Client 自身输出，未删除；只隐藏内部 `rem` 哨兵。
5. **`error` 弹窗为模态**
   - 状态更新已提前，但弹窗仍会阻塞操作直到用户关闭。
6. **旧 `common/pm3process.*`**
   - 文件保留在仓库但不再编译，避免历史丢失；Stage 6 可移动到 `legacy/` 或删除。
7. **未做真实 MIFARE 读写验证**
   - Stage 1 只验证通信层与 LF Config；MIFARE/Trace 在 Stage 2 处理。

---

## 6. 下一阶段计划

Stage 2：修复现有业务功能，顺序：

1. MIFARE Classic 相关命令/参数/输出；
2. MIFARE 仿真（`--crack` → v4.23346 实际参数）；
3. Trace Load / Save（新输出、`-1` buffer 语义）；
4. LF Read / Sniff（保留文本，准备 Stage 5 波形数据源）。

每项完成后重新编译，并用真实设备做只读/非破坏性验证。

---

**Stage 1 结论：通过。** Core 层已建立，连接、版本、哨兵、UTF-8、断开和异常退出已在真实 Client/设备上验证。进入 Stage 2。
