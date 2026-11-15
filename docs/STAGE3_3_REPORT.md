# Stage 3.3 报告：NTAG 读取卡死与崩溃排查及最小修复

日期：2026-11-15  
项目：`E:\Opencode_project\Proxmark3GUI-master`

## 1. 开工审计

- Git：`C:\msys64\usr\bin\git.exe`
- 分支：`upgrade/v0.4.1-polish`
- HEAD：`245b184`
- 工作区：Stage 2/3/3.1/3.2 的未提交修改和未跟踪文件仍在；本轮未 reset/checkout/clean/rebase，未提交，未移动标签。
- 修改前基线：Clean Build 正常；现有测试 62/22/59/126 通过。
- Client：`E:\Opencode_project\pm3-256-iceman-v4.23346\proxmark3.exe`（Iceman v4.23346-suspect）。
- 本轮只修改 NTAG 读取任务、局部表格刷新、读取状态机/日志与相应测试；未修改 NTAG 写入安全策略、Amiibo、LF、ClientSession、CommandQueue 或主题。

## 2. 问题复现

测试标签：NTAG215 兼容芯片，UID `53 6C AF 22 64 00 01`，ATQA `00 44`，SAK `00`，CC `E1 10 3E 00`，135 页，用户页 4..129，配置页 130..134。

复现结果（修复前）：

| 操作 | 结果 |
|---|---|
| Detect | 正常返回；`hf mfu info` / `hf 14a info` 成功 |
| Read Page | 进入相同的行更新路径，存在同样崩溃风险；未反复触发 |
| Read Selected | 进入逐页异步路径，行更新时崩溃/卡死 |
| Read All | 第一段 dump 完成（`finished #4`）后进入 `updateRow`，GUI 无响应；窗口显示未响应；最终进程退出 |
| Refresh | 同 Read All |
| PWD/PACK 显示 | 纯本地刷新，未造成长时间阻塞；但仍受全局行刷新逻辑影响 |

Windows 事件：

- `AppHangB1`（12:26）
- `APPCRASH`，异常代码 **`0xc00000fd`（STATUS_STACK_OVERFLOW）**，故障模块 `ntdll.dll`（12:36:44）
- 崩溃转储：`C:\ProgramData\Microsoft\Windows\WER\...`

修复后日志显示 Read All 的 5 个 dump 批次每批约 1.2 s，135/135 完成；修复前则在第一个/第二个批次后停止响应且没有 timeout/finish 日志。

## 3. 根因分析

### 3.1 直接原因：`updateRow` 无限递归

使用带符号的 release 构建（`build-sym`）在卡死时附加 gdb，主线程栈反复出现：

```text
#21 NtagTab::updateRow(...) at ../src/module/ntagtab.cpp:633
#22 NtagTab::setRowData(...)
...
#30 NtagTab::updateRow(...) at ../src/module/ntagtab.cpp:633
#37 NtagTab::updateRow(...) at ../src/module/ntagtab.cpp:633
...
```

`updateRow()` 通过 `QTableWidgetItem::setText/setFlags` 修改单元格；这些操作会触发 `QTableWidget::itemChanged`。  
`NtagTab::onTableItemChanged()` 的原逻辑是：

```cpp
if(m_readJob.busy)
{
    if(item)
        updateRow(item->row()); // <-- 再次 setText/setFlags
    return;
}
if(m_updatingTable || !item)
    return;
```

读取任务运行时 `m_readJob.busy == true`，因此 `updateRow` 自己触发的 `itemChanged` 又调用 `updateRow`，形成无界递归，最终栈溢出 `0xC00000FD`。  
这解释了：命令已经完成、但 UI 在 `setRowData` 中崩溃/挂起，而不是 Client 或 CommandQueue 阻塞。

**修复**：

1. `onTableItemChanged` 首先检查 `m_updatingTable`，并在 `m_readJob.busy` 时直接返回，不再调用 `updateRow`。
2. `updateRow` 内加 `QSignalBlocker blocker(table)`，程序化更新期间彻底阻断 `itemChanged` 回环。

### 3.2 加剧卡顿的原因：表头 `ResizeToContents`

表格 0..8 列全部使用 `QHeaderView::ResizeToContents`。每个单元格更新都会触发表头重新测量所有行/列；读取 32 页时，每个批次耗时约 16 秒，GUI 长时间不响应。

**修复**：改为固定/交互宽度（页 55、B0-B3 44、ASCII Stretch、类型 120、访问 150、状态 100），只在必要时由用户调整。修复后每批 dump 处理立即完成，批次间隔约 1.2 s（全部是 Client 命令耗时）。

### 3.3 ClientSession / CommandQueue 审计

- `ClientSession::executeCommand()` 只入队并返回 id，不等待命令完成。
- 命令完成由 `rem` sentinel 驱动，`commandFinished` 正常发出；修复前日志中 `finished #4` 已出现，说明 ClientSession 没有阻塞。
- `readCommandId` 与新任务 ID 不匹配时会被忽略；Client 命令 ID 单调递增，旧回调不会修改新任务。
- `finishReadJob()` 只执行一次；`ReadJobState::finish()` 幂等。
- 未发现对象生命周期、重复连接或 Model reset 问题。

### 3.4 其他检查

- PWD/PACK 切换只刷新 `PageClass::Security` 页，约 1–3 ms，不再遍历/重建全表。
- 读取期间按钮禁用、编辑禁用、重复任务拒绝。
- 读取与写入/清空/校验互斥，命令仍全部经 CommandQueue 串行执行。

## 4. 修复内容

| 文件 | 函数/位置 | 修改 |
|---|---|---|
| `src/module/ntagtab.cpp` | `onTableItemChanged` | `m_updatingTable` 优先；读取中直接返回，不再重入 `updateRow` |
| `src/module/ntagtab.cpp` | `updateRow` | `QSignalBlocker` 阻断程序化更新产生的 `itemChanged` |
| `src/module/ntagtab.cpp` | `buildUi` | 去掉全部 `ResizeToContents`，使用固定/交互列宽 |
| `src/module/ntagtab.cpp` | `onReadCommandFinished` | 超时后结束当前任务，不再排队下一批/页 |
| `src/module/ntagtab.cpp` | `startReadJob/finishReadJob` | 增加 job id、模式、页数、失败数、耗时日志 |
| `src/module/ntag/ntagmemory.cpp` | `ReadJobState::failure` | 失败页不增加“成功读取”计数 |
| `tests/ntagmodel` | ReadJobState | 更新失败计数断言，新增 finish 幂等测试 |

未修改 NTAG 写入安全策略、UID/CC/Lock/Config/PWD/PACK 保护、`--force` 条件、写入重试/停止逻辑。

## 5. NTAG215 实机验证

标签信息：

- UID：`53 6C AF 22 64 00 01`
- ATQA：`00 44`；SAK：`00`
- 型号：`NTAG215 (Probable)`（GET_VERSION 无有效返回，依赖 CC/布局）
- CC：`E1 10 3E 00`
- 页数：135；用户页 4..129；配置/锁页 130..134
- 基准：596 B，SHA-256 `6b74571e8dd44ee78af8f76546468e6467280ca80de36f5c9b4b7252b0e0a4aa`
- 基准目录：`E:\Opencode_project\PM3-HW-Test-Stage3_3\`

修复后实测：

| 操作 | 结果 |
|---|---|
| Detect | 约 162 ms；UID/ATQA/SAK/CC 正常 |
| Read Page（page 4） | 198 ms；状态“已读取 1/1 页”，GUI 响应 |
| Read Selected（单页，鼠标选择） | 194 ms；1/1，GUI 响应 |
| Read All | **6906 ms**，135/135，0 失败，GUI 全程可响应 |
| Refresh | 6847 ms，135/135，0 失败 |
| Read All 连续重复 2 次 | 6848 ms / 6838 ms，均 135/135 |
| 读取中切换主页面再返回 | 正常；进度继续，最终 135/135 |
| Cancel | 约 3.36 s 后停止，状态“已取消读取：64/135 页，失败 0”；GUI 响应，随后可再次读取 |
| PWD/PACK 显示切换 | 3 ms / 1 ms；不重建 MemoryImage |
| 崩溃次数 | **0**（修复后所有测试） |
| 读取数据一致性 | GUI 读取后直接完整 dump，与基准逐字节相同；SHA-256 相同 |

备注：Read Selected 的“多页非连续”UI 自动化受 Qt 表格 Accessibility 限制，Ctrl+点击未能在自动脚本中稳定加入第二页；已用“全选后 Read Selected + 取消”验证多页路径（状态 5/135），非连续选择仍建议人工复测。

## 6. 写入回归

本轮**未执行 NTAG 写入**。原因：

- 本轮目标是读取稳定性修复；
- 读取修复已用基准镜像完成验证；
- 为避免在读取测试后引入不必要风险，未触碰标签用户数据。

NTAG215 基准文件未被覆盖；Stage 3 的 NTAG216 基准目录未修改。

## 7. 测试与构建

| 项目 | 结果 |
|---|---|
| `tests/ntagmodel` | **63 passed / 0 failed** |
| `tests/amiibodata` | **22 passed / 0 failed** |
| `tests/lfparser` | **59 passed / 0 failed** |
| `tests/lfparser_stage2` | **126 passed / 0 failed** |
| Clean Build | **rc=0，0 warning，0 error** |
| 翻译 | **1020 finished / 0 unfinished** |

崩溃证据：

- `PM3-HW-Test-Stage3_3\gdb-sym-main.txt`（修复前 updateRow 递归栈）
- `PM3-HW-Test-Stage3_3\gdb-sym-blocker.txt`
- Windows WER `APPCRASH 0xc00000fd`

## 8. 修改文件

- `src/module/ntagtab.cpp`
- `src/module/ntagtab.h`
- `src/module/ntag/ntagmemory.cpp`
- `src/module/ntag/ntagmemory.h`（上一轮接口保持）
- `tests/ntagmodel/tst_ntagmodel.cpp`
- `dist/Proxmark3GUI/Proxmark3GUI.exe`（最新 clean build）
- `docs/STAGE3_3_REPORT.md`

未新增 UI 文案，未新增 translation 条目；最终 1020 finished / 0 unfinished。

## 9. 未验证事项

1. NTAG213 / NTAG216 本轮无实机标签，只通过模型/自动化测试确认布局；未实机读取。
2. Read Selected 的“多个不连续页面”自动选择未稳定复现；单页与全选多页路径已验证。
3. 超时/客户端重启路径未实机触发（正常读取未超时）；代码已改为超时后结束任务、不再排队。
4. 写入回归未执行（见第 6 节）。
5. 读取期间窗口关闭仅有代码审计，无独立实机测试。
6. 修复前 Read Page 未重复触发崩溃；其根因与 Read All 共用 `updateRow`，已按同一路径修复。
