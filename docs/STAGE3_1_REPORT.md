# Stage 3.1 体验修复报告

日期：2026-11-15  
项目：`E:\Opencode_project\Proxmark3GUI-master`

## 1. 开工审计

- Git 可用：`C:\msys64\usr\bin\git.exe`（未加入 PATH，显式调用）。
- 分支：`upgrade/v0.4.1-polish`
- HEAD：`245b184b87105c0f426bd08a1fdba978ae25d063`
- 工作区：存在 Stage 2/3 大量未提交修改和未跟踪文件（`src/lf/`、`src/module/ntag/`、`src/module/amiibo/`、`tests/`、`docs/` 等）。本轮没有执行 reset/checkout/clean/rebase，没有提交，没有移动标签。
- 修改前基线：Stage 3 的 Clean Build 为 rc=0、0 warning；现有测试为 59/22/59/126，全部通过（本轮新增 3 个 ReadJobState 测试后为 62/22/59/126）。
- 审计文件：`docs/NTAG_AMIIBO_UPGRADE.md`、`docs/HW_TEST_STAGE3.md`、Amiibo/NTAG/内存模型、`ClientSession`、`CommandQueue`、主题 QSS。
- 当前主题由 `GUIsettings.ini` 的 `UI/Theme_Name` 选择 `qdss_dark` / `qdss_light`，启动时加载 QSS。

## 2. Amiibo 页面

### 2.1 文案精简

- 删除多段关于 plain MFU dump、标签读写移除、SHA-256 原理、临时文件策略的说明。
- 页面只保留一行说明：
  - 中文：`管理 Amiibo 数据文件并通过 Proxmark3 模拟。`
  - 英文：`Manage Amiibo files and simulate them through the Proxmark3.`
- 技术细节保留在 Client 输出区；状态只显示：未选择 / 已就绪 / 模拟运行中 / 模拟停止 / Client 错误。

### 2.2 恢复的库功能

- 左侧 Amiibo 库列表：显示已导入文件、来源、选中条目。
- `Import BIN...` 导入本地文件。
- `Remove` 只删除 GUI 库引用，不删除原始文件（已验证）。
- 选中条目后直接启动模拟。
- 详情区显示名称、来源、大小、SHA-256、格式；SHA-256 分两行显示，并可通过 tooltip 查看完整值。
- 本地图片：优先加载 BIN 同目录的 `<basename>.png/.jpg/.jpeg`，无图片时显示占位文字；图片只用于显示，不写入 BIN。
- 在线目录：恢复原 JSON API（`name/series/image_url/bin_url`）、搜索、图片加载、下载 BIN。
- 目录下载的字节原样保存到 GUI 工作目录（`pm3gui_amiibo_<hash>.bin`），并加入库；不改变 BIN 内容。
- 右侧分为 `Amiibo` / `在线目录` 两个标签页，避免默认窗口高度下目录被裁切。

### 2.3 模拟链路保持不变

仍然执行 Stage 3 已验证的流程：

```text
选择文件 → 重新读取 → 大小/SHA-256 校验
→ ASCII 路径直接使用；非 ASCII 路径复制相同字节到 ASCII 临时文件并复核
→ hf mfu eload -f "<path>"
→ 检查 Client 的 Loaded/Detected 输出
→ hf mfu sim -t 7
→ Stop → stopRequested()
```

未修改 `eload` / `sim` 参数，未加头、去头、重排、截断或结构化重建。

### 2.4 实机/本地验证

- 本地 plain 540 B 文件 + 同目录红色 PNG：库列表、图片、名称、大小、SHA-256 正常；截图 `PM3-HW-Test-Stage3\amiibo-library.png`。
- 在线目录：使用本地 HTTP 服务 `http://127.0.0.1:8765/catalog.json`，搜索返回 1 条；HTTP 日志确认请求了 `/image.png` 与 `/amiibo.bin`；下载后库中新增 `Local Test Amiibo`，大小 540 B，SHA-256 与源文件一致。
- 下载条目启动模拟：Client 输出 `Detected plain mfu dump format`，`hf mfu sim -t 7` 启动；Stop 后状态 `Simulation stopped.`。
- 文件大小/SHA-256 校验：导入 540 B 后把文件改成 541 B，点击模拟被拒绝，状态提示文件在导入后发生变化，未调用 eload。

## 3. Hex 编辑器暗黑主题

### 3.1 修改

`src/module/ntagtab.cpp` 的 `updateRow()`：

- 新增 `isDarkTheme()`：
  - 优先读取 `GUIsettings.ini` 的 `UI/Theme_Name`；
  - `qdss_dark` → dark；`qdss_light` → light；
  - 其他情况回退到 `QApplication::palette().Window` 亮度。
- 暗黑主题下：
  - 所有单元格清除 `Qt::BackgroundRole`，使用统一暗色底。
  - 状态改用 `Qt::ForegroundRole`：
    - 读取失败 / 写入失败：红色文字；
    - 已修改：黄色文字；
    - 已验证：绿色文字；
    - 未读取：灰色文字；
    - 只读/锁定：灰色文字。
  - State 列仍保留文字状态，不依赖颜色。
- 浅色主题保留原来的浅色背景区分。
- 新增 `changeEvent()`：在 `PaletteChange` / `StyleChange` 时重新应用行样式。
- 选中、焦点、编辑单元格仍由 Qt 的 selection/focus 绘制，不受自定义背景影响。

### 3.2 验证

- Amiibo 页面暗黑主题截图确认整体背景与控件协调（该页面不含 Hex 表格）。
- Hex 表格行颜色的实机截图/交互验证**未完成**：本轮测试期间 NTAG216 标签未被检测到（见第 4 节），无法生成真实行数据。
- 浅色主题的 Hex 行显示本轮未实机复核；代码保留原浅色背景逻辑。

## 4. NTAG 性能问题与异步读取

### 4.1 实际阻塞原因

- `Read All` 旧实现循环调用 `readAllFromDump()`，内部通过 `util->execCMDWithOutput()` 同步执行每个 32 页 dump 分块；命令在 GUI 线程等待，整个窗口冻结。
- `Read Selected` / `Read Page` 对每一页同步 `rdbl`，多选时逐页等待。
- 每次读取通过 `setRowData()` 更新单行，没有重建整个表格；但同步等待仍是主要卡顿。
- PWD/PACK 切换旧实现遍历全部 231 页调用 `updateRow()`；虽非 I/O，但会造成不必要的整表刷新。
- 未发现 Delegate 或 `data()` 中的昂贵操作；表格是 `QTableWidget`，没有自定义 Model 重置。

### 4.2 异步实现

- 不新建线程，不新建第二套 Client 进程。
- 使用现有 `ClientSession::executeCommand()` 的队列和 `commandFinished` 信号：
  - `Read All` / `Refresh`：每批最多 32 页执行一次 `hf mfu dump`；命令完成后再发下一批。
  - `Read Selected` / `Read Page`：逐页 `hf mfu rdbl`，每条完成后发下一条。
  - 所有命令仍按 `CommandQueue` 串行执行，不会并发发送射频命令。
- GUI 更新仍在主线程；每个分块/页完成后只更新对应行，不做全表 reset。
- 读取期间：
  - 操作按钮禁用，`Cancel Read` 启用；
  - 表格禁止进入编辑，避免与读回数据竞争；
  - 窗口移动、重绘、切换其他页面不受阻塞。
- 取消：设置取消标志，不强行中断当前命令；当前命令结束后停止后续批次/页，保留已成功页，未读取页保持原状态。
- 关闭窗口：析构时重置任务状态；没有后台线程或无限等待。
- 重复任务：`Ntag::ReadJobState::start()` 在 busy 时拒绝第二次启动。
- 读写互斥：读取任务运行期间，Write/Erase/Quick Launch/Verify 入口检查 `m_readJob.busy` 并拒绝；不改变原写入策略。
- PWD/PACK 显隐：只更新 `PageClass::Security` 的页面和详情，不再遍历/重建整表。

### 4.3 状态模型与测试

新增 `Ntag::ReadJobState`（`src/module/ntag/ntagmemory.{h,cpp}`）：

- `start()`：busy/total/mode，重复启动拒绝；
- `success()` / `failure()`：完成数、失败数，`done` 上限为 `total`；
- `requestCancel()` / `finish()`：取消与清理；
- 测试：`readJobStartsAndTracks`、`readJobRejectsDuplicateStart`、`readJobCancelAndFinish`。

### 4.4 实机验证状态

本轮测试期间（00:04–00:17）`hf mfu info` / `hf 14a info` 均未检测到标签；Stage 3 的 NTAG216 未在测试区出现。因此：

- 异步 `Read All` / `Read Selected` / `Read Page` 的**真实 GUI 时序和取消行为未实机验证**。
- 没有执行任何 NTAG 写入，Stage 3 基准镜像与历史测试数据未被覆盖。
- 写入互斥、可写页保护、`--force`、逐页复读逻辑未改动；单元测试仍通过。

## 5. 测试

| 项目 | 结果 |
|---|---|
| `tests/ntagmodel` | **62 passed / 0 failed**（新增 3 个 ReadJobState 测试） |
| `tests/amiibodata` | **22 passed / 0 failed** |
| `tests/lfparser` | **59 passed / 0 failed** |
| `tests/lfparser_stage2` | **126 passed / 0 failed** |
| Clean Build | **rc=0，0 warning，0 error** |
| 翻译 | **985 finished / 0 unfinished** |

UI 验证：

- Amiibo 本地库/图片：截图确认。
- Amiibo 在线目录/图片/BIN 下载：本地 HTTP 服务日志确认。
- Amiibo 下载条目模拟/停止：实机 Client 输出确认。
- Amiibo SHA-256 变更拒绝：实机 UI 状态确认。
- NTAG 异步读取、Hex 暗黑行、浅色主题：**未实机验证**（标签缺失）。

## 6. 文件改动

修改：

- `src/module/amiibopage.h`
  - 恢复 `AmiiboEntry`（路径/哈希/图片/系列/下载 URL），库/目录/图片成员和槽。
- `src/module/amiibopage.cpp`
  - 恢复库列表、详情、图片、在线目录、下载；
  - 精简文案；模拟链路保持 Stage 3；
  - 右侧 `Amiibo` / `在线目录` 标签页；
  - 本地 sidecar 图片、目录图片、下载 BIN；
  - 哈希两行显示。
- `src/module/ntagtab.h`
  - 增加 `ClientSession *`、`Ntag::ReadJobState`、异步读取槽/字段、Cancel Read、`changeEvent`。
- `src/module/ntagtab.cpp`
  - 暗黑主题统一背景 + 文字状态；
  - 异步 dump/rdbl 读取、取消、重复任务防护、读写互斥；
  - PWD/PACK 仅刷新安全页；
  - 读取错误保留到详情/State tooltip；
  - 移除写入确认中的“实验性操作”措辞。
- `src/module/ntag/ntagmemory.h/.cpp`
  - 新增 `ReadJobMode` / `ReadJobState`。
- `src/ui/mainwindow.cpp`
  - `NtagTab(util, pm3, this)` 传入现有 `ClientSession`。
- `tests/ntagmodel/tst_ntagmodel.cpp`
  - 3 个 ReadJobState 测试。
- `i18n/zh_CN.ts`、`en_US.ts`、`zh_CN.qm`、`en_US.qm`
  - 新增文案翻译，最终 985 finished / 0 unfinished。
- `scripts/translate-stage3_1.py`
  - 本轮翻译脚本。
- `dist/Proxmark3GUI/Proxmark3GUI.exe`
  - 最新 clean build。

删除：无。

## 7. 风险与遗留问题

### 已验证

- Amiibo 本地库/图片。
- Amiibo 在线目录 JSON、图片请求、BIN 下载、下载后模拟/停止。
- Amiibo 导入后文件大小/SHA-256 变化会被拒绝。
- 翻译完整性、Clean Build、全部自动化测试。
- 原 Stage 3 模拟命令、`--force`/写入逻辑未修改。

### 仅自动化/代码审计

- `ReadJobState` 状态转换。
- 异步读取的串行队列、重复任务拒绝、取消后停止后续命令、窗口关闭清理。
- 暗黑主题背景/前景颜色逻辑与 `changeEvent`。
- PWD/PACK 只刷新安全页。
- 浅色主题保留原逻辑。

### 尚未验证

- NTAG216 异步读取的真实耗时、GUI 响应、取消和失败页状态（测试时标签不在场）。
- Hex 表格暗黑/浅色主题的真实行截图与选中/编辑/焦点视觉。
- 读取期间窗口移动/切换页面/重复点击的实际 UI 行为（无标签无法进入读取任务）。
- NTAG 写入回归（本轮未执行写入，标签不在场）。
- 中文路径 GUI 文件对话框的端到端导入；代码仍使用 ASCII 临时副本策略，`tests/amiibodata` 覆盖路径判定和字节复制。
- 在线目录失效/非 JSON/HTTP 错误的实机提示（代码路径存在，未逐一触发）。
