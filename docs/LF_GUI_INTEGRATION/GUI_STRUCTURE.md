# GUI 结构

## 1. 迁移前后

```text
迁移前：
顶层：Mifare | LF | T55xx | NTAG/Ultralight | eMRTD Reader | RawCommand | Settings

迁移后：
顶层：Mifare | LF | T55xx | NTAG/Ultralight | eMRTD Reader | RawCommand | Settings
                └─ LF（容器，标签文字与原标签一致）
                   ├─ 低频功能          ← 原 ui->lfTab 原样移入
                   └─ 低频多协议解析    ← 新增 LfParserTab
```

- 顶层标签数量、顺序与索引不变（LF 仍是索引 1），`Util::rawTabIndex` 等按索引跳转的逻辑不受影响。
- 外层标签文字直接沿用 `ui->funcTab->tabText(index)`，不改变既有显示。

## 2. 原有低频功能如何保留

实现方式是最小侵入的**重新挂载**，而非重写页面：

```cpp
const int lfTabIndex = ui->funcTab->indexOf(ui->lfTab);
const QString lfTitle = ui->funcTab->tabText(lfTabIndex);
QTabWidget *lfTabs = new QTabWidget(this);
ui->funcTab->removeTab(lfTabIndex);
lfTabs->addTab(ui->lfTab, tr("Low frequency functions"));
lfTabs->addTab(lfParserTab, tr("LF multi-protocol parsing"));
ui->funcTab->insertTab(lfTabIndex, lfTabs, lfTitle);
```

因此：

- `ui->lfTab` 及其所有控件、objectName、信号槽、`LF` 模块（`src/module/lf.cpp`）完全未改；
- 原有 `LF` 页面仍使用同一 `Util` / `ClientSession` 命令路径；
- 原有 i18n 条目不变；
- 原有安全限制（确认、只读提示）不变。

**未修改的文件**：`src/module/lf.cpp`、`src/module/lf.h`、`src/ui/mainwindow.ui`（LF 页面部分）。

## 3. 新增页面结构

`LfParserTab`（`src/module/lfparsertab.{h,cpp}`）：

```text
┌ 客户端文本输入 ──────────────────────────────┐
│ 提示文字                                      │
│ [多行文本框：粘贴 / 导入 / RawCommand 输出]   │
│ [解析] [清空] [导入文本…] [使用原始命令输出]  │
│ 来源：手动输入 / 文本文件 / 原始命令输出       │
├ 解析结果 ────────────────────────────────────┤
│ 状态 | 协议 | 格式 | 位长 | 置信度            │
│ 说明（歧义/部分解析/固件不支持等）            │
│ 字段表（Field / Value）                       │
├ 详情标签 ────────────────────────────────────┤
│ 原始输出 | 警告与未解析字段 | T55xx（只读）   │
└──────────────────────────────────────────────┘
```

- 上下使用 `QSplitter`，可拖动；字段表占据主要空间。
- 所有控件均为原生 Qt Widgets，与项目既有风格一致。

## 4. 数据流

```text
用户粘贴 / 导入文本 / RawCommand 输出
        │
        ▼
LFParserRegistry::instance().parse(text)
        │
        ▼
LFParseResult（status / protocol / format / fields / warnings）
        │
        ├─► 字段表（只显示适用字段，缺失显示“未提供”）
        ├─► 说明区（Ambiguous / Partial / Unidentified / 固件无 LF）
        └─► 详情标签（原始文本、警告、T55xx 只读解码）
```

GUI 不包含任何协议解析逻辑，也不向 Parser 传递控件对象。

## 5. 与 Client 的关系

- 页面本身**不发送任何命令**；
- “使用原始命令输出”只是读取 `ui->Raw_outputEdit` 的文本（信号 `clientTextRequested` → MainWindow 取文本回填）；
- 因此当前无 LF 的固件下，手动粘贴也可完成全部解析验证。

## 6. 状态与安全

- 页面没有写入、锁定、擦除、模拟入口；
- T55xx 仅展示 detect/info 与 block0 位域，并在页脚明确说明只读；
- 解析失败不会影响原有低频页面与其他标签。
