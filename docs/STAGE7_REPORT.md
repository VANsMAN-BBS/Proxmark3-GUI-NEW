# Stage 7 Report — v0.3.1 稳定性维护

**日期：** 2026-11-11  
**版本：** Proxmark3GUI v0.3.1  
**分支：** `upgrade/v0.4.0-emrtd-amiibo`（从 `upgrade-v0.3.0` / `183f199` 建立）

---

## 1. 阶段目标

- 增加轻量级持久化日志，可轮转、有限增长；
- 收拢 Raw / fire-and-forget 长命令的调度路径；
- 完善设备拔出、Client 崩溃、命令超时、重连等异常恢复；
- 用真实 Client + 真实设备做回归（不新建自动化测试框架、不建 Mock Client）。

---

## 2. 代码改动

### 2.1 持久化日志

新增：

- `src/core/logger.h`
- `src/core/logger.cpp`
- 已加入 `src/Proxmark3GUI.pro`。

| 项目 | 实现 |
|---|---|
| 默认路径 | `%LOCALAPPDATA%\Proxmark3GUI\logs\Proxmark3GUI.log` |
| 回退路径 | `<AppLocalDataLocation>/logs`，再回退到 `<exe 目录>/logs`，最后 `<exe 目录>` |
| 级别 | DEBUG / INFO / WARN / ERROR / FATAL |
| 格式 | `yyyy-MM-dd HH:mm:ss.zzz [LEVEL] [Module] message` |
| 轮转 | 按文件大小，默认 2 MiB × 5 个文件（`.1` … `.5`） |
| 环境变量 | `PM3GUI_LOG_MAX_BYTES`、`PM3GUI_LOG_MAX_FILES`（最小值 1024 字节 / 1 个） |
| 线程安全 | `QMutex` 串行写入；WARN 及以上立即 flush |
| Qt 消息 | `qInstallMessageHandler` 转发 `qDebug/qWarning/qCritical` |

说明：候选目录**逐个实际打开验证**，只有真正 `open()` 成功才采用。这样即使某台机器禁止写用户目录，也不会出现“日志文件不存在且无提示”的情况。

已覆盖的日志点：

- 启动 / 退出、日志自身路径；
- 翻译文件加载失败；
- Client 路径与启动参数、连接成功后的 client / firmware / model / hardware；
- 连接、断开、Stop、重连；
- ClientSession 状态（queued / running / finished / timeout / detached）；
- 命令超时与超时后自动重启；
- Client 进程异常退出、进程错误；
- 设备 / UI 状态变化；
- 配置文件、Trace、dump 等文件打开/保存失败。

敏感数据保护：

- 日志只记录命令摘要（`Logger::commandSummary`，最多 3 个 token，长十六进制串替换为 `<hex>`）；
- 不记录 MRZ、Kseed/BAC 密钥、APDU 完整载荷、DG2 数据、Amiibo 文件内容。

### 2.2 Raw / fire-and-forget 调度

改动前：Raw 命令直接写 stdin，不进入 FIFO，也不参与完成判定，可与模块命令交错。

改动后：

- `sendRawCommand()` 统一走 `executeCommand(cmd, 30000, rawOutput=true)`，与模块命令共用 FIFO + `rem` 哨兵；
- `ClientSession::isBusy()` 计入 detached 状态；
- 新增 `executeDetached()` / `isDetached()` / `detachedChanged()` / `commandStateChanged()`；
- 长命令/交互命令改为 detached：
  - MIFARE：`sniff`、`sniff 14a`、hardnested、Simulation；
  - LF：`tune`、Show Waveform；
  - 说明：`darkside` 仍为普通队列命令（有明确结束）。
- detached 期间：
  - 新的 `executeCommand` 直接返回明确错误（"A long-running client command is active; press Stop first."）；
  - UI 状态栏显示 "Long command running"，并禁用操作；
  - Stop → `restartClient()`，恢复普通状态。

命令状态明确区分：已发送 / 正在执行 / 已完成 / 超时 / 进程异常退出 / 无法确认完成。

### 2.3 异常恢复

| 场景 | 处理 |
|---|---|
| Client 正常退出 | `onFinished` 清理活动命令与队列，状态置为未连接 |
| Client 崩溃 | 同上，并记录 exit code / CrashExit |
| 命令超时 | 报告超时 → **自动重启 Client**，队列不会永久阻塞 |
| Stop / 主动断开 | 活动命令与排队命令均收到明确失败结果，不静默丢弃 |
| 重复 Connect | `startClient()` 先同步终止旧进程，避免两个 Client 争用同一串口 |
| detached 命令未结束 | 队列阻塞，直到 Stop/重启，避免与下一条命令交错 |
| 旧会话输出 | 进程终止后清空活动 token/缓冲，新会话重新握手识别版本 |

未增加后台线程。

---

## 3. 构建

环境（保持不变）：

```text
Qt 5.15.19 (MSYS2 UCRT64)
MinGW-w64 GCC 16.2.0
qmake 3.1
```

```powershell
qmake-qt5 ..\src\Proxmark3GUI.pro -spec win32-g++ CONFIG+=release
mingw32-make -j4
```

结果：

- 编译、链接成功，`error` / `warning` 均为 0；
- `VERSION = 0.3.1`；
- 设备：`COM14`（`USB\VID_9AC4&PID_4B8F\ICEMAN`）；
- Client：`Iceman/master/v4.23346-suspect 2026-09-18 00:22:18 9ce8a4b6c`；
- 硬件：`PM3 GENERIC / AT91SAM7S256 Rev D`。

---

## 4. 实测结果（真实 Client + 真实设备）

UI 操作通过 Windows UI Automation 驱动真实窗口完成（仅用于本次验证，不进入产品代码）。

| 验证项 | 方法 | 结果 |
|---|---|---|
| 日志落盘 | 启动 GUI | 通过，生成 `Proxmark3GUI.log` |
| 日志轮转 | `PM3GUI_LOG_MAX_BYTES=2048`、`MAX_FILES=3/2`，写入 60 行 | 通过：`current + .1 + .2 + .3`；`maxFiles=2` 时为 `current + .1 + .2`，单文件不超过上限 |
| 启动/UI 初始化日志 | 正常启动 | 通过 |
| 连接 + 版本识别 | Connect | 通过，记录 client/firmware/model/hardware |
| 断开 | Disconnect | 通过，client `exit=0 crash=0` |
| 重连 | Connect → Disconnect → Connect | 通过 |
| Raw FIFO 串行化 | 连续发送 `hw status` / `hw ping` | 通过：#1 running，#2/#3 排队，按序完成，无交错 |
| detached 命令 | LF 页面 `Tune`（`lf tune`） | 通过：`detached started`、`detached state=1`，Stop 后 `detached state=0` 并自动重连 |
| 命令超时恢复 | Raw `hf tune`（30 s 超时） | 通过：超时 → 自动重启 → 重连 → 后续 `hw ping` 成功（3 ms） |
| Client 崩溃恢复 | 外部 `Stop-Process proxmark3` | 通过：`client finished: exit=-1`、状态回到未连接、重新 Connect 成功 |
| 无重复 Client | 上述全部场景 | 通过：任意时刻最多 1 个 `proxmark3.exe` |
| 多语言 | `en_US` 界面启动 | 通过（`zh_CN` 资源未改动） |

日志样例（真实输出）：

```text
2026-11-11 23:29:39.253 [WARN ] [ClientSession] timeout #1 after 30000 ms: hf tune
2026-11-11 23:29:39.253 [WARN ] [ClientSession] restarting client after timeout
2026-11-11 23:29:40.287 [INFO ] [ClientSession] connected: client=Iceman/master/v4.23346-suspect ...
2026-11-11 23:29:51.795 [INFO ] [ClientSession] finished #2 OK in 3 ms
```

---

## 5. 已知限制 / 未验证项

1. **未建立自动化测试框架、未建 Mock Client**（按本阶段要求）。以上验证通过真实 Client + UI Automation 手动/脚本化执行。
2. 本机该二进制写用户目录 `C:\Users\...` 会被系统拒绝（`fopen` 返回 `ERROR_ACCESS_DENIED`，token 为普通 `van` Medium 完整性、非 AppContainer；ACL 正常）。日志因此回退到 `<exe>/logs`。**这是本机环境限制，不是代码缺陷**；正常安装环境下使用 `%LOCALAPPDATA%`。
3. 未做物理拔插 USB 测试；以“外部杀死 Client 进程”覆盖同类恢复路径。
4. 当前卡为 HID SEOS/EMV、固件无 LF：MIFARE Classic / LF / T55xx / NTAG 的成功读写路径仍**未验证**（与 v0.3.0 结论一致）。
5. `hf search` / `hf 14a info` 已确认命令可正确下发，但当前天线无对应卡片，未取得卡响应结果。
6. 启动时 Qt 自动连接的既有告警（`No matching signal for on_Raw_keyPressed(...)` 等）属于 v0.3.0 遗留，本次仅将其写入日志，未修改 UI 代码。
7. detached 命令按下 Stop 会重启整个 Client（沿用 v0.3.0 的 Stop 语义），正在执行的其它工作会中断。

---

## 6. Git

- 提交：`fix: improve session scheduling and recovery` + `feat: add persistent logging`
- 标签：`upgrade-v0.3.1`
- 不修改 `upgrade-v0.3.0` 历史提交与标签。

---

## 7. 结论

**Stage 7 完成（在真实硬件上验证）。**

- 日志可持久化并正确轮转；
- Raw 命令不再绕过 FIFO；
- 超时、崩溃、主动停止均能恢复且不残留重复进程；
- 原有功能未删除，界面结构未改动。
