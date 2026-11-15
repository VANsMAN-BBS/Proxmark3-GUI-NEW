# Stage 4 报告：UI 整理、功能入口合并与 Git 提交准备

日期：2026-11-15
项目：`E:\Opencode_project\Proxmark3GUI-master`

## 1. 本阶段目标

- 将 T55XX 页面并入 LF 页面。
- 统一主页面名称：MIFARE、LF、NTAG/UL、eMRTD。
- 设置页删除旧“检查更新/反馈问题”入口，增加原项目地址与说明。
- 将 NTAG 的 NDEF 与 Quick Launch 合并到同一功能区域。
- 完成回归测试、DPI 检查、代码审查和本地 Git 提交准备。

本阶段未改变底层通信、NTAG 写入/清空语义、Amiibo 格式/模拟逻辑、LF 解析能力或 Proxmark3 命令参数。

## 2. 修改前 Git 基线

Git：`C:\msys64\usr\bin\git.exe`

- 分支：`upgrade/v0.4.1-polish`
- HEAD：`245b184b87105c0f426bd08a1fdba978ae25d063`
- 最近 5 条提交：

```text
245b184 fix: keep eMRTD protocol output UTF-8 on Windows pipes
058d479 release: prepare v0.4.1
f777c5a fix: complete DPI and localization audit
4bf0691 feat: add new Proxmark3 application icon
1c5f426 refactor: move Amiibo into NTAG module
```

- 已暂存内容：无（`git diff --cached` 为空）。
- 未提交修改（Stage 4 开工时）：
  - `i18n/en_US.ts`
  - `i18n/zh_CN.qm`
  - `i18n/zh_CN.ts`
  - `src/Proxmark3GUI.pro`
  - `src/module/amiibopage.cpp`
  - `src/module/amiibopage.h`
  - `src/module/ntagtab.cpp`
  - `src/module/ntagtab.h`
  - `src/ui/mainwindow.cpp`
  - `src/ui/mainwindow.h`
- 未跟踪内容：
  - `src/lf/`
  - `src/module/amiibo/`
  - `src/module/ntag/`
  - `src/module/lfparsertab.cpp/.h`
  - `tests/`
  - `docs/` 下 Stage 3、LF、Amiibo、审计报告
  - `scripts/translate-*.py`
  - `deliverables/`
- 远程：**未配置任何 remote**（`git remote -v` 为空）。

这些未提交内容包含 Stage 2/3/3.1/3.2/3.3/3.4 的成果，Stage 4 开工时已被完整保留，未执行 reset、checkout、clean 或 rebase。

## 3. 本阶段修改文件

| 文件 | 修改内容 |
|---|---|
| `src/ui/mainwindow.ui` | MIFARE 页名称；T55XX 页名称；T55 实验性标题；设置页移除 Releases/Issues，新增原项目链接和说明 |
| `src/ui/mainwindow.cpp` | T55XX 移入 LF 子标签；LF 子标签名称；NTAG/UL、eMRTD 名称；页面顺序；删除 Check Update 动作和菜单项 |
| `src/ui/mainwindow.h` | 删除 `checkUpdate` 成员 |
| `src/module/t55xx.cpp` | 精简未实现控件提示，统一、缩短实验性工具提示 |
| `src/module/ntagtab.cpp` | NDEF 与 Quick Launch 合并为一个入口；Quick Launch 增加滚动区域以适配高 DPI |
| `i18n/zh_CN.ts` / `i18n/zh_CN.qm` | 新增/调整页面名称、设置页、T55 提示、NDEF/Quick Launch 翻译 |
| `i18n/en_US.ts` | lupdate 同步源文本，移除旧入口翻译 |
| `docs/STAGE4_UI_REPORT.md` | 本报告（原 `docs/STAGE4_REPORT.md` 是上游已有报告，已保持原样） |

构建产物 `dist/Proxmark3GUI/Proxmark3GUI.exe` 已更新，但不属于应提交的源码文件。

## 4. T55XX 并入 LF

实现方式：复用现有 QTabWidget 和现有 `t55xxTab`，不重写 T55xx 页面。

在 `MainWindow` 初始化中：

1. 先按既有逻辑创建 LF 容器 `LF_subTabs`。
2. 将原 `ui->lfTab` 作为 “LF Functions”。
3. 将 `LfParserTab` 作为 “LF Parser”。
4. 从 `funcTab` 中移除原一级 `t55xxTab`，加入同一 LF 容器，标题 “T55XX”。
5. 再把 LF 容器放回原一级页面位置。

结果：

```text
LF
├── LF Functions
├── LF Parser
└── T55XX
```

- T55XX 不再是主窗口一级页面。
- `T55xx` 对象、全部控件、命令、槽函数、参数、结果显示逻辑未修改。
- T55XX 页面在构造前已完成 tab 迁移，未重复创建 Client 或重复连接信号。
- LF 原有功能、LfParserTab 和 T55XX 共用同一父级 QTabWidget，页面切换仅切换可见性，不涉及后台任务生命周期。
- dock 顺序调整为：MIFARE、LF、NTAG/UL、eMRTD、RawCommand、Settings。

T55XX 说明精简：

- 未实现控件提示由长句改为 “Not implemented in this build.” / “此版本未实现。”
- 两个实验性分组提示统一为：“Experimental. Requires LF-capable firmware; results are not hardware-verified in this build.”
- 分组标题统一为 “T55xx Read Config (Experimental)” 和 “Clone to T55xx (Experimental)”。
- 写 EM410x 前的安全确认对话框保留，未删除任何影响操作安全或结果准确性的提示。
- 未扩展或缩减 T55XX 功能。

## 5. 页面名称调整

| 位置 | 旧名称 | 新名称 |
|---|---|---|
| 一级页面 | Mifare | **MIFARE** |
| 一级页面 | LF（中文翻译“低频”） | **LF**（中英文均为 LF） |
| 一级页面 | NTAG/Ultralight | **NTAG/UL** |
| 一级页面 | eMRTD Reader | **eMRTD** |
| LF 子页面 | T55xx | **T55XX** |

- 未重命名 C++ 类、源文件、对象名、内部接口。
- 中文 `LF` 翻译已从“低频”改为 `LF`，避免语言切换后残留旧名称。
- 内部 `mifareTab`、`t55xxTab`、`ntagTab` 等对象名保持不变。

## 6. 设置页调整

删除：

- `Latest Release:` 标签及指向 `.../Proxmark3GUI/releases/` 的链接。
- `Report Bugs:` 标签及指向 `.../Proxmark3GUI/issues/` 的链接。
- 上下文菜单 `Check Update` 动作及 `QDesktopServices` 跳转逻辑。
- 对应成员 `checkUpdate`。

新增：

- 标签：`Original Project:` / `原项目地址：`
- 可点击链接：`https://github.com/wh201906/Proxmark3GUI/`
- 说明：`This project is based on the original project, with firmware compatibility maintenance and additional features.`
- 中文说明：`本项目基于原项目进行固件兼容维护与新功能增加。`

链接使用 QLabel + `openExternalLinks=true` + `Qt::TextBrowserInteraction`，点击后由系统默认浏览器打开。未删除其他设置项；作者链接 `wh201906` 上下文菜单保留，因为它不是检查更新/反馈入口。

## 7. NDEF / Quick Launch 整合

审计结果：

- NDEF 页负责显示解析后的 CC/TLV/NDEF 和记录。
- Quick Launch 生成 NDEF URI 记录并写入用户页，复用 `Ndef::buildLaunch()`、`Ntag::parseNdef()`、现有写入/复读逻辑。
- 两者共享 NDEF 数据区域和同一个 NTagTab/ClientSession，但 UI 原本是两个并列顶层子标签。
- Quick Launch 仍为实验性；其同步写入路径未在本阶段重构。

整合方式：

```text
NTAG/UL
└── NTAG
    ├── Memory
    └── NDEF / Quick Launch
        ├── NDEF
        └── Quick Launch
```

- 未合并底层 NDEF 数据结构，未改命令参数、写入流程或校验流程。
- Quick Launch 标题、实验性提示、错误提示、写入结果保持独立。
- Quick Launch 内容加入 QScrollArea，避免高 DPI 或较矮 dock 下控件不可达。
- NDEF 仍使用原 QPlainTextEdit 显示，读取/解析/写入能力未变。
- 读写互斥、Stage 3.3 防重入和 Stage 3.4 异步写入状态机均未改动。

Quick Launch 仍在确认写入时使用原有同步流程；这是 Stage 3.4 已记录的遗留项，本阶段没有扩大为异步重构。

## 8. i18n 修改

- `lupdate -no-obsolete` 后中文条目：**1055**。
- `lrelease`：**1055 finished / 0 unfinished**。
- 新增/更新中文：

| 英文 | 中文 |
|---|---|
| MIFARE | MIFARE |
| LF | LF |
| NTAG/UL | NTAG/UL |
| eMRTD | eMRTD |
| LF Functions | LF 功能 |
| LF Parser | LF 解析 |
| T55XX | T55XX |
| Original Project: | 原项目地址： |
| This project is based on the original project... | 本项目基于原项目进行固件兼容维护与新功能增加。 |
| NDEF / Quick Launch | NDEF / 快捷启动 |
| Not implemented in this build. | 此版本未实现。 |
| Experimental. Requires LF-capable firmware... | 实验性：需要支持 LF 的固件；此构建未进行硬件验证。 |

- 英文启动实测显示：MIFARE、LF、NTAG/UL、eMRTD、RawCommand、Settings、LF Functions、LF Parser、T55XX、NDEF / Quick Launch、Original Project 和完整说明。
- 中文启动实测显示对应中文名称和说明。
- 语言切换按现有机制需要重启程序；重启后名称与说明正确。

## 9. 构建结果

最终 Clean Build：

- 返回码：**0**
- warnings：**0**
- errors：**0**
- 技术栈：Qt 5.15 / MinGW / qmake，未更换
- 未新增依赖
- 产物：`dist\Proxmark3GUI\Proxmark3GUI.exe`（2026-11-15 16:38）

## 10. 自动化测试

| 测试 | 结果 |
|---|---:|
| `tests/ntagmodel` | **67 passed / 0 failed** |
| `tests/amiibodata` | **22 passed / 0 failed** |
| `tests/lfparser` | **59 passed / 0 failed** |
| `tests/lfparser_stage2` | **126 passed / 0 failed** |

测试数量与 Stage 3.4 基线一致；本阶段未新增或删除测试，也未降低断言。

## 11. 功能回归

已执行：

- 程序正常启动。
- 主页面一级导航：MIFARE、LF、NTAG/UL、eMRTD、RawCommand、Settings。
- LF 打开正常，子页 LF Functions、LF Parser、T55XX 可切换。
- T55XX 页面控件、分组、按钮和实验性提示正常显示。
- MIFARE 页面正常打开。
- NTAG/UL 页面正常打开，NTAG/Amiibo 子页正常。
- NTAG → NDEF / Quick Launch → NDEF / Quick Launch 两级标签正常。
- 设置页打开，原项目链接和说明可见；旧 Releases/Issues 入口不存在。
- 英文/中文资源分别启动检查。
- PM3 连接、Detect、Read All 只读冒烟通过。
- 正常关闭程序。
- `dist\Proxmark3GUI` 中没有 `pm3gui_ntag_dump*` 文件，也没有 `pm3gui-ntag-*` 临时目录残留。

回归保护确认：

- Stage 3.3 的 `QSignalBlocker` / `m_updatingTable` 仍存在。
- Stage 3.4 的 `WriteJobState`、异步写入、逐页复读、互斥和 `DumpWorkspace` 未修改。
- 本阶段未执行 NTAG 写入/清空，不需要重新做写入回归。
- Amiibo BIN 直通和 LF 模块未被改动。

## 12. DPI 检查

使用 `QT_SCALE_FACTOR=1`、`1.25`、`1.5` 启动检查：

- 100%：页面名称、LF/T55XX 子标签、设置页链接和说明完整显示。
- 125%：设置页链接、说明和字体/主题行正常；无截断。
- 150%：
  - LF 的三个子标签和 T55XX 页面正常。
  - NTAG/UL 的内层 NDEF / Quick Launch 标签正常。
  - Quick Launch 原内容在较矮窗口下会被压缩；本阶段加入 QScrollArea，右侧滚动条可用，控件不再不可达。
  - MIFARE 页中 “Nested攻击/Hardnested攻击/Darkside攻击” 在 150% 下有既有按钮文字截断；该页布局本阶段未改，属于原布局在 150% 下的限制。
- 未通过固定坐标解决布局；沿用现有布局管理器和 QTabWidget/QScrollArea。

## 13. 未完成事项与未验证项目

- 本阶段未执行 NTAG 写入、清空、恢复，按任务要求不重复大范围写入。
- 未实机测试 Quick Launch 写入（只验证 UI 入口和既有逻辑未改）。
- 未逐项点击 MIFARE、eMRTD、RawCommand 的所有子功能；只做页面打开和只读冒烟。
- 150% 下 MIFARE 既有按钮截断未修复，属于既有布局问题。
- Quick Launch 仍使用同步写入流程，未在本阶段异步化。
- 原项目链接的浏览器打开使用 QLabel 标准外部链接机制；已检查链接配置，未在自动化中强制启动浏览器。

## 14. Git 提交与推送准备

### 14.1 备份

提交前已完整备份工作区到项目目录之外：

```text
E:\Opencode_project_Backup_Stage4_20261115_1625
```

备份包含 `.git`、已跟踪修改、全部未跟踪源码/测试/文档/脚本、构建产物和用户配置。已对关键文件（`mainwindow.cpp`、`ntagtab.cpp`、`i18n/zh_CN.ts`、`.git/HEAD`、`.git/index` 等）做 SHA-256 对比，源文件与备份一致。

### 14.2 提交边界重建

- Commit 1 使用“Stage 3.4 基线 blob”暂存：对 `mainwindow.cpp`、`mainwindow.h`、`ntagtab.cpp`、`i18n/*` 暂存去掉 Stage 4 修改后的版本；工作区本身保持 Stage 4 不动。
- Stage 2–3.4 的历史源码、测试、文档、脚本，以及 LF Parser Standalone 源码/文档，使用显式文件清单暂存，未使用 `git add -A` 或 `git add .`。
- 排除内容：`build*`、`dist/`、`data/`、日志、`.o/.exe/.dll`、测试输出、`deliverables/.../build*`、`docs/STAGE4_UI_REPORT.md`。
- 已验证 Commit 1 的暂存树不含 `mainwindow.ui`、`t55xx.cpp`、`docs/STAGE4_UI_REPORT.md` 等 Stage 4 内容。

### 14.3 Commit 1

```text
b22266891f944bfa4a34e7d57db0159a4ff7ff01
feat: add NTAG, Amiibo and LF functionality
```

- 文件数：315
- 内容：NTAG 模型/页面、异步读取/写入、Amiibo 数据/目录/页面、LF Parser、T55XX 既有代码、测试、历史报告、翻译脚本、LF Parser Standalone 源码/文档。
- 独立验证：将暂存树导出到 `E:\Opencode_project\stage4-commit1-verify`，Clean Build 通过（0 warnings / 0 errors），四个测试全部通过（67/22/59/126）。

### 14.4 Commit 2

```text
（本提交自身，使用 `git log -1 --oneline` 查看）
refactor(ui): consolidate LF, NTAG and navigation layouts
```

文件：

```text
docs/STAGE4_UI_REPORT.md
i18n/en_US.ts
i18n/zh_CN.ts
i18n/zh_CN.qm
src/module/ntagtab.cpp
src/module/t55xx.cpp
src/ui/mainwindow.cpp
src/ui/mainwindow.h
src/ui/mainwindow.ui
```

- T55XX 并入 LF。
- 页面名称统一。
- 设置页删除旧 Releases/Issues 和 Check Update，增加原项目链接/说明。
- NDEF / Quick Launch 合并入口，Quick Launch 增加滚动区域。
- 对应中英文翻译和本报告。
- 提交前 `git -c core.whitespace=cr-at-eol diff --check` 通过（i18n 文件保持仓库原有的 CRLF 风格）。
- 提交前 Clean Build：0 warnings / 0 errors；测试 67/22/59/126 全部通过。

### 14.5 提交后状态

- 分支：`upgrade/v0.4.1-polish`
- HEAD：本提交（`git log -1 --oneline`）
- 已跟踪修改：无
- 暂存区：空
- `git diff`：空
- 未跟踪：仅剩原有构建输出（`deliverables/LF_Parser_Standalone/build*`、`tests/*/build`、`tests/lfparser_stage2/stage2-tests.log`），未提交、未删除。
- `docs/STAGE4_REPORT.md`：上游已有报告，已从 HEAD 恢复，两次提交均未修改该文件。

### 14.6 推送前检查

- 远程：未配置（`git remote -v` 为空），无法计算 ahead/behind。
- 未执行 `git remote add`、未修改 remote URL、未执行 `git push`、未创建 GitHub 仓库、未修改分支或标签。
- 源码中未发现新令牌、密钥或证书。
- 文档/测试脚本包含本机路径（如 `E:\Opencode_project\...`）；推送前需确认是否保留。
- 不应公开的内容：`dist/`、日志、`PM3-HW-Test-*` 测试 dump、`deliverables` 构建输出；这些均未进入两次提交。
- 提交可独立审查：Commit 1 为历史功能，Commit 2 仅包含 UI 整理和对应翻译/报告。
- 推送前需用户确认 GitHub 仓库地址，并自行配置 remote。

本阶段到此停止，未执行 GitHub 推送。
