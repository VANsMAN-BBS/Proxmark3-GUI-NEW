# Amiibo → NTAG 迁移报告

**版本：** v0.4.1  
**提交：** `refactor: move Amiibo into NTAG module`

---

## 1. 问题

v0.4.0 将 Amiibo 放在新增的 **JTAG** 容器下，属于功能分类错误：

- Amiibo 是 NFC Forum Type 2 / NTAG215，与 JTAG 无关；
- 该 JTAG 容器本身没有任何真实功能，只包含一个占位页；
- 用户需要先点 JTAG 才能找到 Amiibo，路径不符合功能语义。

---

## 2. 迁移前后的结构

### 迁移前（v0.4.0）

```text
顶层标签
├─ Mifare
├─ LF
├─ T55xx
├─ NTAG/Ultralight        (NtagTab)
├─ eMRTD Reader
├─ JTAG                   ← 错误归组
│  ├─ JTAG                (占位说明)
│  └─ Amiibo
├─ RawCommand
└─ Settings
```

### 迁移后（v0.4.1）

```text
顶层标签
├─ Mifare
├─ LF
├─ T55xx
├─ NTAG/Ultralight
│  ├─ NTAG                (原 NtagTab，未修改)
│  └─ Amiibo              (原 AmiiboPage，未修改)
├─ eMRTD Reader
├─ RawCommand
└─ Settings
```

**JTAG 顶层标签已删除**：本版本没有任何 JTAG 功能，不再保留误导性的入口。

---

## 3. 代码改动

仅修改 `src/ui/mainwindow.cpp`（以及 `AmiiboPage` 的注释）：

```cpp
ntag = new NtagTab(util, this);
QTabWidget *ntagTabs = new QTabWidget(this);
ntagTabs->addTab(ntag, tr("NTAG"));
amiiboPage = new AmiiboPage(util, pm3, this);
ntagTabs->addTab(amiiboPage, tr("Amiibo"));
ui->funcTab->insertTab(3, ntagTabs, tr("NTAG/Ultralight"));
```

- 复用原有 `AmiiboPage`，**没有复制任何实现**；
- `stopRequested` 仍连接到 `pm3->restartClient()`；
- 端口/路径变化仍同步调用 `setDeviceInfo()`；
- 设备连接状态仍通过 `onPM3StateChanged()` 同步。

---

## 4. 兼容性

| 项目 | 结论 |
|---|---|
| 旧设置项 | `Amiibo/catalogUrl` 键未变，目录地址保留 |
| 页面索引 | 未持久化标签页索引；dock 布局（QDockWidget）与标签页无关 |
| 快捷入口 | 代码中无 JTAG/Amiibo 快捷入口引用 |
| 用户数据 | Amiibo 数据仅存在内存或用户选择的文件，不受迁移影响 |
| 旧 JTAG 入口 | 已移除；本版本本来也没有 JTAG 功能 |

---

## 5. 保留的功能（全部未改动）

- 内置合成示例（540 字节，明确标注非真实 Amiibo）；
- 导入/另存（540 / 1024 / 2048 字节校验）；
- 十六进制预览；
- Detect / Read / Write / Verify；
- 写入前确认 + 写入后自动复读校验；
- `hf mfu eload` + `hf mfu sim -t 7` 模拟；
- 可配置在线元数据目录（默认关闭，不内置二进制数据）。

---

## 6. 安全边界（未改变）

- 没有 UID 冒充；
- 没有 Lock / OTP / PWD 等不可逆操作；
- 所有物理写入都需要用户明确确认；
- 模拟与真实写入在界面文案和流程上明确区分。

---

## 7. 验证

| 测试 | 结果 |
|---|---|
| 顶层不再出现 JTAG 标签 | 通过 |
| NTAG/Ultralight 内含 NTAG + Amiibo 两个子标签 | 通过 |
| Amiibo 子页可切换、可添加内置示例 | 通过（540 字节 / NTAG215 格式） |
| 原有 NTAG 页面功能 | 未修改 |
| 导入/另存/校验/模拟 | 代码未改动；导入/另存/示例已在 v0.4.0 验证，本次复测示例通过 |
| 中文界面 | 通过（“NTAG”标签保持原文） |

真实 NTAG215 读写与模拟仍未验证（无标签/游戏机）。
