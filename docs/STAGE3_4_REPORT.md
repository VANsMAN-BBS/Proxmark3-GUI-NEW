# Stage 3.4 报告：NTAG 写入/清空异步化与临时 Dump 文件治理

日期：2026-11-15  
项目：`E:\Opencode_project\Proxmark3GUI-master`

## 1. 开工审计

- Git：`C:\msys64\usr\bin\git.exe`
- 分支：`upgrade/v0.4.1-polish`
- HEAD：`245b184`
- 工作区：Stage 2/3/3.1/3.2/3.3 的未提交修改和未跟踪文件仍在；本轮未 reset/checkout/clean/rebase，未提交，未移动标签。
- 修改前测试基线：
  - `tests/ntagmodel`：63 passed
  - `tests/amiibodata`：22 passed
  - `tests/lfparser`：59 passed
  - `tests/lfparser_stage2`：126 passed
- 修改前 Clean Build 正常（Stage 3.3 产物 2026-11-15 13:19）。
- Client：`E:\Opencode_project\pm3-256-iceman-v4.23346\proxmark3.exe`（Iceman v4.23346-suspect）。
- 本轮只修改 NTAG 写入/清空/校验、写入任务状态、临时 dump 文件生命周期、对应测试与翻译；未修改 Amiibo、LF、主题、ClientSession、CommandQueue、NTAG 型号识别或写入安全边界。

### 1.1 测试标签状态说明

Stage 3.4 开工时重新读取 NTAG215：

- UID：`53 6C AF 22 64 00 01`
- ATQA：`00 44`；SAK：`00`
- CC：`E1 10 3E 00`
- 布局：135 页，用户页 4..129，配置/锁页 130..134

但用户数据区 4..129 已经是全 `00`，与 Stage 3.3 基准不一致。GUI 日志显示，在本轮开始前的 13:28 存在一组 `hf mfu wrbl`/`hf mfu rdbl` 循环（UID `536CAF22640001`），随后 13:31 另有 UID `532460F3530001` 的读取记录，说明标签内容在本轮开始前已被其他会话改写/清空。

因此，在写入测试前使用 Stage 3.3 的 NTAG215 基准镜像逐页恢复用户页 4..129（不写 UID、CC、OTP、锁页、配置页、PWD/PACK），恢复后完整 dump 与基准逐字节一致：

- 596 B
- SHA-256：`6b74571e8dd44ee78af8f76546468e6467280ca80de36f5c9b4b7252b0e0a4aa`

Stage 3.1 的 NTAG216 基准文件未修改。

## 2. 写入/清空卡死根因

修改前的实际调用链（源码位置为修改前）：

| 操作 | 调用链 | 阻塞点 |
|---|---|---|
| Write Page | `writeOnePage()` → `writePageData()` → `util->execCMDWithOutput()` → `readPageData()` → `util->execCMDWithOutput()` → `verifyWrittenPages()` → `readPagesForVerify()` → `util->execCMDWithOutput()` | 每次写、复读、整片 dump 都在 GUI 线程内进入 `QEventLoop`，通过 `execCMDWithOutput()` 同步等待 |
| Write Selected | `writeSelectedPages()` → `writePages()` → 逐页 `writePageData()`，每页内含写命令 + 复读命令；循环内 `processEvents()` | 每页两次嵌套事件循环；失败重试时还有 `QThread::msleep(40)`；全部页完成后再整片 dump |
| Write Dirty | `writeDirtyPages()` → `writePages()` | 同上 |
| Erase | `eraseTag()` → `readAllFromDump()`（每 32 页一次同步 dump）→ `planErase()` → 逐页 `writePageData()` → `readPagesForVerify()`（同步整片 dump） | 清空前读取、逐页写入/复读、清空后整片校验全部在 GUI 线程同步等待；`processEvents()` 只维持重绘，不改变调用栈阻塞 |
| Verify Page | `verifyOnePage()` → `readPageData()` → `execCMDWithOutput()` | GUI 线程同步等待单页复读 |
| Verify Written | `verifyWritten()` → `verifyWrittenPages()` → `readPagesForVerify()` → `execCMDWithOutput()` | GUI 线程同步等待整片 dump 和解析 |

关键源码：

- `src/module/ntagtab.cpp`（修改前）
  - `writePageData()`：`util->execCMDWithOutput(cmd, 10000, true)`；随后 `readPageData()` 再次同步执行 `hf mfu rdbl`。
  - `writePages()`：`for(page : targets)` 内调用 `writePageData()` 两次（一次正常、一次重试），并在循环中 `processEvents()`。
  - `verifyWrittenPages()` / `readPagesForVerify()`：同步 `hf mfu dump`，再同步读文件、解析。
  - `eraseTag()`：先 `readAllFromDump()`，再逐页 `writePageData()`，最后 `readPagesForVerify()`。
  - `verifyOnePage()`：同步 `readPageData()`。
- `src/common/util.cpp`
  - `execCMDWithOutput()` 构造局部 `QEventLoop` 并 `loop.exec(QEventLoop::ExcludeUserInputEvents)`。
  - 这不是真正的异步调用：调用者一直停在 GUI 线程栈中；窗口只能重绘，不能正常处理用户输入；也无法通过任务状态机取消当前操作。

因此，问题不是 ClientSession 或 CommandQueue 阻塞：

- `ClientSession::executeCommand()` 只入队并返回 id。
- 完成由 `rem` sentinel 驱动，`commandFinished` 正常发出。
- 阻塞发生在上层继续使用 `execCMDWithOutput()` 的主线程调用链。

## 3. 异步化实现

### 3.1 复用的现有接口

- `ClientSession::executeCommand()`
- `ClientSession::commandFinished`
- 现有 CommandQueue 串行执行
- 现有日志 `Logger`
- Stage 3.3 的 `ReadJobState` 交互方式和 `setReadBusy()` 按钮/表格锁定
- `Ntag::MemoryImage`、`planErase()`、`verifyWrite()`

没有新增 Client 进程、线程、第二套命令队列或大型依赖。

### 3.2 写入状态机

新增 `Ntag::WriteJobState`（`src/module/ntag/ntagmemory.h/.cpp`）：

```text
Idle
  ↓
Writing ── wrbl 完成 ──→ ReadBack ── 匹配 ──→ 下一页面
   │                        │
   │ 失败/超时               │ 不匹配/失败
   └──── 一次受控重试 ───────┘
                              ↓
                        停止任务（记录失败页）
全部页面成功
  ↓
Verifying（整片 dump 校验）
  ↓
Idle
```

字段：

- `busy` / `cancelled`
- `mode`：`WritePages`、`Erase`、`Verify`
- `phase`：`Idle`、`Snapshot`、`Writing`、`ReadBack`、`Verifying`
- `total`、`written`、`failures`、`currentPage`、`retryUsed`

状态转换只允许：

- 启动成功后 `Idle → Writing`（或 `Snapshot` / `Verifying`）
- 页面写入成功 → `ReadBack`
- 复读匹配 → 下一页面，或进入 `Verifying`
- 失败 → 停止；取消 → 当前页处理完后停止
- `finish()` 幂等，结束后可再次启动

### 3.3 单页写入流程

`startWriteJob()` → `issueNextWriteCommand()`：

1. 校验设备连接、工作区、目标页、按键格式和写入权限。
2. 提交一个 `hf mfu wrbl`。
3. `commandFinished` 校验命令 id 和任务状态。
4. 只有写入命令成功后才提交 `hf mfu rdbl` 复读。
5. 复读值必须等于目标值，才标记 `Written` / `Verified`。
6. 失败按原策略重试一次（`Write Page` 保持不重试；`Write Selected` / `Write Dirty` 保持一次重试；`Erase` 保持不重试）。
7. 全部页面完成后提交一次整片 `hf mfu dump`，用 `Ntag::verifyWrite()` 生成与原来一致的校验报告。

同一时刻只提交一个射频命令，未并发发送。

### 3.4 清空流程

`eraseTag()`：

1. 保留原有型号/布局/CC 可写检查。
2. 异步提交一次整片 dump 作为快照（`Snapshot` 阶段）。
3. 在 `onWriteCommandFinished()` 中解析快照、更新 `MemoryImage`、重新检查 CC。
4. 继续使用现有 `Ntag::planErase()`，保留 UID/CC/OTP/锁页/配置/PWD/PACK 保护。
5. 保留原有确认对话框和 `--force` 使用条件。
6. 只对 `plan.clearPages` 逐页写入 `00 00 00 00`，每页复读。
7. 任一页失败立即停止，不尝试后续页面。
8. 完成后异步整片 dump，报告 cleared / remaining / unread，不把未读页当成功。

### 3.5 校验流程

- `Verify Written`：复用写入任务状态机，异步整片 dump + `verifyWrite()`，不写标签。
- `Verify Page`：异步单页 `hf mfu rdbl`，比较编辑值与实际值，再显示原报告。
- 复读失败不会转成成功；未读页保持 `Unknown`。

### 3.6 取消

- `Cancel` 不再只处理读取：`cancelActiveJob()` 同时处理读、写、清空、校验。
- 当前射频命令不强行中断。
- 如果当前页 `wrbl` 已发出，必完成该页 `rdbl` 后再停止。
- 如果写入失败/超时后正在等待重试，取消不会再次写；会改为复读当前页确认实际状态。
- 已成功写入的页面保留 `Written/Verified` 状态，不回滚、不伪装成未写。
- 取消后按钮恢复，可再次启动任务。

### 3.7 互斥

- `deviceBusy()` = 读任务或写任务任一运行。
- Detect、Read All/Selected/Page、Write Page/Selected/Dirty、Erase、Verify、Quick Launch、Undo/Redo/Discard、表格编辑在任一任务运行时拒绝或禁用。
- 读、写、清空、校验不会同时提交设备命令。
- 命令仍全部经过现有 CommandQueue 串行执行。

### 3.8 UI 更新

- 保留 Stage 3.3 的 `QSignalBlocker` 和 `m_updatingTable` 防重入。
- 未恢复 `ResizeToContents`。
- 写入/复读每页只更新当前行，未重建整表或 reset model。

## 4. 临时文件根因与修复

### 4.1 原生成链路

修改前：

```cpp
QString NtagTab::dumpPath() const
{
    return QDir(QCoreApplication::applicationDirPath())
            .filePath(QStringLiteral("pm3gui_ntag_dump.bin"));
}
```

Client 的 `hf mfu dump -f <path>.bin` 会同时写出：

- `<path>.bin`
- `<path>.json`（sidecar，包含 dump 元数据）

原实现只 `QFile::remove(<path>.bin)`：

- Read All / Refresh：每 32 页一个 chunk，每个 chunk 留下一个 JSON。
- Write / Verify / Erase：整片 dump 再留下 JSON。
- 文件名固定，多个任务共享同一路径。
- 软件目录因此持续累积 `pm3gui_ntag_dump.json`（以及 Client 在发现同名 JSON 时生成的 `-001`、`-002` 等变体）。

JSON 是 Client 的 dump 附带元数据，GUI 只解析 `.bin`，不依赖该 JSON 的内容；但 Client 会在命令执行期间写入它，所以不能在命令结束前删除 `.bin` 或 JSON。

### 4.2 新方案

新增 `Ntag::DumpWorkspace`：

- 每个 `NtagTab` 实例拥有一个私有 `QTemporaryDir`。
- 首选系统临时目录；若受限，则回退到应用数据目录、当前目录、应用目录（仍为私有临时目录）。
- dump 文件：`<workspace>/pm3gui-ntag-dump.bin`
- sidecar：`<workspace>/pm3gui-ntag-dump.json`
- `clearDump()` 只删除该私有目录内的 `pm3gui-ntag-dump*.bin` 和 `pm3gui-ntag-dump*.json`，包括 Client 生成的 `-001` 变体。
- 不接触用户手动保存的 dump，不扫描或删除其他目录/其他进程文件。

生命周期：

- 启动读取/写入/清空/校验前清理上一个工作文件。
- 命令完成、解析成功/失败、任务失败/取消后清理。
- 正常关闭窗口时 `QTemporaryDir` 析构删除整个私有目录。
- 程序被强制终止时可能留下一个空临时目录，但不会留下 `.bin`/`.json` 累积文件；正常退出实测无残留。

### 4.3 旧文件

本轮检查了 `dist\Proxmark3GUI`、仓库根目录和项目目录：

- 没有发现已存在的 `pm3gui_ntag_dump*` 文件。
- 未批量删除任何用户文件。
- 测试过程中产生的 `pm3gui-ntag-*` 私有临时目录均为空，已逐个确认后清理。
- 如果用户其他目录仍存在旧的 `pm3gui_ntag_dump.json`，它们已不被当前程序引用，可安全删除；但本轮未自动清理用户已有文件。

## 5. NTAG215 实机结果

### 5.1 标签

- UID：`53 6C AF 22 64 00 01`
- ATQA：`00 44`；SAK：`00`
- 型号：`NTAG215 (Probable)`
- CC：`E1 10 3E 00`
- 页数：135；用户页 4..129；配置/锁页 130..134
- 基准 SHA-256：`6b74571e8dd44ee78af8f76546468e6467280ca80de36f5c9b4b7252b0e0a4aa`

### 5.2 写入/校验实测

| 操作 | 页 | 结果 | 任务耗时（日志） | GUI |
|---|---|---|---|---|
| Write Page | 20 = `11 22 33 44` | 成功，复读一致，整片校验 Passed 1/1 | 2.59 s | 全程响应 |
| Write Dirty | 30 = `7A...`，32 = `5B...` | 成功，2/2 写入并复读 | 3.74 s | 全程响应 |
| Write Selected（非连续） | 36 = `6C...`，38 = `4D...` | 成功，2/2 | 3.74 s | 全程响应 |
| Verify Written | 36、38 | Passed 2/2 | 1.43 s | 全程响应 |
| Verify Page | 20 | Passed 1/1 | 0.58 s | 全程响应 |
| Erase（快照 + 确认） | 0..134 快照 | 成功建立清空计划 | 2.18 s | 全程响应 |
| Erase 取消 | 4、5、6 | 取消后完成当前页复读，状态 `3/126 已写入，3 页已校验` | 5.66 s（含确认） | 全程响应 |

说明：

- 写入期间状态栏实时显示 `Writing page ...` / `Reading page ... back for verification...`。
- 所有测试进程 `Responding=True`，未出现无响应或崩溃（0 次）。
- 写入成功页在表格中标记为 `Written/Verified`；取消后保留准确的部分完成数量。
- Erase 没有执行 126 页全清：为避免扩大写入范围，按“取消测试”在写入第 4、5、6 页后停止；快照、确认、逐页写入、复读、取消状态均实测。
- 取消 Erase 后没有继续执行整片清空后校验，因为任务按要求在当前页确认后停止；部分完成状态已明确显示。

### 5.3 恢复

测试页（4、5、6、20、30、32、36、38 等）和完整用户区随后从 Stage 3.3 基准逐页恢复（不触碰 UID/CC/OTP/锁/配置/PWD/PACK）。恢复后完整 dump：

- 596 B
- SHA-256：`6b74571e8dd44ee78af8f76546468e6467280ca80de36f5c9b4b7252b0e0a4aa`
- 与 `ntag215-baseline.bin` 逐字节相同

### 5.4 临时文件实测

- 每次 GUI 任务后 `dist\Proxmark3GUI` 中 `pm3gui_ntag_dump*` 数量为 0。
- 正常关闭窗口后无 `pm3gui-ntag-*` 临时目录残留。
- 强制结束测试进程会留下一个空的私有临时目录（无 `.bin`/`.json`）；已清理。
- Client 直接 dump 的测试输出仍会在 `PM3-HW-Test-Stage3_3` 生成 `.json`，这是测试脚本的显式输出，不是 GUI 临时文件。

## 6. 测试与构建

| 项目 | 结果 |
|---|---|
| `tests/ntagmodel` | **67 passed / 0 failed** |
| `tests/amiibodata` | **22 passed / 0 failed** |
| `tests/lfparser` | **59 passed / 0 failed** |
| `tests/lfparser_stage2` | **126 passed / 0 failed** |
| Clean Build | **rc=0，0 warning，0 error** |
| zh_CN 翻译 | **1057 finished / 0 unfinished** |
| 崩溃 | **0 次** |

新增测试：

- `WriteJobState` 启动/重复启动拒绝/写入计数/失败计数/重试标记/取消/幂等结束。
- `DumpWorkspace` 两个实例路径唯一；`clearDump()` 删除 `.bin`、`.json` 和 `-001.json`；不删除同目录外的用户 dump 文件。
- 原有 ReadJobState、MemoryImage、erasePlan、verifyWrite 测试全部保留。

Stage 3.3 读取回归（新构建）：

| 操作 | 结果 |
|---|---|
| Read Page | 185 ms，1/1 |
| Read All | 6.74 s，135/135 |
| Refresh | 6.69 s，135/135 |
| Cancel | 3.34 s，64/135 部分读取 |
| 连续 Read All | 6.77 s / 6.69 s，均 135/135 |
| PWD/PACK 切换 | 5 ms / 2 ms |
| GUI 响应 | 全程正常 |
| 崩溃 | 0 |

## 7. 修改文件清单

| 文件 | 修改 |
|---|---|
| `src/module/ntag/ntagmemory.h` | 新增 `WriteJobState`、`WriteJobMode`、`WriteJobPhase`、`DumpWorkspace` |
| `src/module/ntag/ntagmemory.cpp` | 实现写入状态机与私有临时 dump 工作区/清理 |
| `src/module/ntagtab.h` | 新增异步写入任务字段、命令槽、取消/报告方法 |
| `src/module/ntagtab.cpp` | Write Page/Selected/Dirty、Erase、Verify Page/Verify Written 改为异步；增加互斥；dump 路径改为私有工作区；保留逐页复读、重试、失败停止、取消和安全边界 |
| `tests/ntagmodel/tst_ntagmodel.cpp` | 新增写入状态与临时文件生命周期测试；`QTEST_GUILESS_MAIN` 以支持 QTemporaryDir |
| `i18n/zh_CN.ts` / `i18n/zh_CN.qm` | 新增 44 条写入/临时文件/取消文案中文翻译 |
| `i18n/en_US.ts` | lupdate 同步新增源文本 |
| `dist/Proxmark3GUI/Proxmark3GUI.exe` | 最新 Clean Build 产物 |

未修改：

- `ClientSession` / `CommandQueue`
- NTAG 型号识别、CC/布局规则
- `--force` 使用条件
- 写入安全边界（UID/厂商、OTP、CC、静态/动态锁、配置、PWD/PACK）
- Amiibo、LF、主题

## 8. 未验证事项

### 已实机验证

- NTAG215 单页写入、复读、整片校验。
- 两页非连续 Write Selected。
- 两页 Write Dirty。
- Verify Written、Verify Page。
- Erase 快照、确认、逐页写入、复读、取消与部分完成状态。
- 读取回归（Read Page / Read All / Refresh / Cancel / 连续读取）。
- 正常退出后临时文件清理。

### 自动化验证

- 写入状态机全部状态转换。
- 临时工作区唯一性与清理边界。
- 原有模型、布局、erasePlan、verifyWrite、Amiibo、LF 测试。

### 源码审计

- 写入失败、复读失败、重试耗尽后的停止逻辑。
- 取消时当前页复读优先。
- 读/写/清空/校验互斥。
- 写入期间只更新当前行，无全表 reset。

### 未验证

1. **写入失败/复读不匹配的实机注入**：本轮没有通过拔卡、写保护页或人为破坏数据来制造失败，只做了状态机与源码验证；失败/重试路径未实机触发。
2. **完整 126 页 Erase**：为避免扩大写入范围，未执行全用户区清空；只验证到取消前的 3 页。
3. **超时路径**：正常写入未超时，未实机触发 Client 超时/重启。
4. **写入期间关闭窗口**：仅代码审计；实测正常关闭发生在任务结束后。
5. **Quick Launch 写入**：仍沿用原有同步流程，不在本轮 Write Page/Selected/Dirty/Erase/Verify 范围内；其读取/写入仍可能阻塞 GUI，建议后续单独迁移。
6. **NTAG213 / NTAG216**：本轮无实机标签，仅保持原有型号/布局逻辑与自动化测试；未实机写入。
7. **强制终止清理**：强制结束进程会留下空临时目录，但不会留下 dump 文件；正常退出已实测清理。
