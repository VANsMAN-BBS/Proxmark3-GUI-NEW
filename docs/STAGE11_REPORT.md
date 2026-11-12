# Stage 11 Report — v0.4.1 功能重构、DG2 修复与视觉完善

**日期：** 2026-11-12  
**版本：** Proxmark3GUI v0.4.1  
**分支：** `upgrade/v0.4.1-polish`  
**基线：** `upgrade-v0.4.0` = `1a9df9b`

---

## 1. Git

| 提交 | 内容 |
|---|---|
| `8f6e288` | fix: repair eMRTD DG2 progress and image handling |
| `1c5f426` | refactor: move Amiibo into NTAG module |
| `4bf0691` | feat: add new Proxmark3 application icon |
| `f777c5a` | fix: complete DPI and localization audit |
| 本提交 | release: prepare v0.4.1 |

标签：`upgrade-v0.4.1`。`upgrade-v0.3.0`（`183f199`）与 `upgrade-v0.4.0`（`1a9df9b`）均未改动。

---

## 2. DG2 卡死的根因与证据

**结论：** 不是协议死锁，而是 **每个 APDU 固定等待 350 ms + DG2 全程零进度事件**，叠加 >32 KB 读取上限与 UI 线程同步解码。

证据（真实设备 COM14，无卡，仅测 APDU 往返）：

```text
apdu round-trip seconds: [0.551, 0.553, 0.552, 0.552, 0.552]   # 修复前
apdu round-trip seconds: [0.143, 0.142, 0.142, 0.142, 0.142]   # 修复后
```

DG2 按 224 字节分块：

| DG2 | 分块 | 修复前 | 修复后 |
|---|---|---|---|
| 20 KB | 89 | 49.3 s | 12.7 s |
| 32 KB | 146 | 80.8 s | 20.8 s |

在这几十秒里，协议只发出 `dg2 start` 一个事件 → 界面“停住”。

次级根因：

- `READ BINARY` 只有 15 位偏移，>32768 字节在长时间读取后才报错且不含大小；
- `applyResult()` 在 GUI 线程 `QPixmap::loadFromData`；
- 取消只在阶段边界检查。

详见 `docs/DG2_DIAGNOSIS.md`。

---

## 3. 修复前后对比

| 场景 | v0.4.0 | v0.4.1 |
|---|---|---|
| DG2 进度 | 无更新（数十秒） | 每 ~0.2 s 更新 `Reading DG2 (x / y bytes)` |
| DG2 子阶段 | 无 | parsing / extracting / decoding / preview |
| DG2 >32 KB | 约 80 s 后含糊报错 | 立即提示长度超限 |
| 大图显示 | UI 线程解码 | 工作线程解码 |
| DG2 期间取消 | 等阶段结束 | 立即取消（分块前检查） |
| 进程无输出 | 永久停住 | 15 s 后提示 “Still working...”，仍可取消 |
| APDU 往返 | 0.552 s | 0.142 s |

GUI 实测（节奏化测试替身）：

```text
Reading DG2 (4800 / 24000 bytes)
Reading DG2 (9600 / 24000 bytes)
...
Extracting facial image
Decoding facial image
Parsing data
Done

before cancel: Reading DG2 (2400 / 24000 bytes)
after cancel:  Reading was cancelled. || Failed
```

---

## 4. 新增 / 修改文件

新增：

```text
emrtd_pm3/tests/test_gui_protocol.py     15 项协议测试
resources/icons/app-icon.svg
resources/icons/app-icon.png
resources/icons/app-icon-{16,24,32,48,64,128,256}.png
resources/icons/app-icon.ico             (7 帧)
resources/icons/icons.qrc
scripts/make-icons.py
docs/DG2_DIAGNOSIS.md
docs/AMIIBO_NTAG_REFACTOR.md
docs/ICON_DESIGN.md
docs/STAGE11_VALIDATION_MATRIX.md
docs/STAGE11_REPORT.md
```

修改：

```text
emrtd_pm3/emrtd_pm3/pm3.py      APDU 响应标记快速完成
emrtd_pm3/emrtd_pm3/card.py     读取进度回调、15 位偏移上限
emrtd_pm3/emrtd_pm3/gui.py      DG2 子阶段、取消检查、全角 MRZ 规范化
src/module/emrtd.{h,cpp}        子阶段显示、异步解码、看门狗、解码期取消
src/module/amiibopage.h         注释更新（NTAG 归属）
src/ui/mainwindow.cpp           NTAG 容器 + Amiibo，移除 JTAG 标签
src/main.cpp                    应用图标（多尺寸 QIcon）
src/Proxmark3GUI.pro            图标资源、RC_ICONS、VERSION 0.4.1
i18n/zh_CN.ts / zh_CN.qm        补齐全部翻译
```

---

## 5. Amiibo 迁移

迁移前：`JTAG` 顶层标签 → `JTAG(占位)` + `Amiibo`。  
迁移后：`NTAG/Ultralight` 顶层标签 → `NTAG` + `Amiibo`；**JTAG 标签已删除**。

- 仅修改 `MainWindow` 的容器构造，`AmiiboPage` 原样复用（无复制实现）；
- 设置键 `Amiibo/catalogUrl` 不变；未持久化标签页索引；
- 导入/另存/540·1024·2048 校验/十六进制/Detect/Read/Write/Verify/写入确认+复读/`eload + sim -t 7`/合成示例/在线目录全部保留；
- 未增加 UID 冒充、Lock、OTP、PWD 等操作。

详见 `docs/AMIIBO_NTAG_REFACTOR.md`。

---

## 6. 应用图标

- 主体为 Proxmark3 PCB：绿色板身、左侧银色 USB、右侧橙色天线线圈、两枚芯片与走线；
- 1024 单位矢量源 + 7 个 PNG 尺寸 + 7 帧 ICO；
- ≤32 px 使用加粗简化变体（单线圈、实心芯片），16×16 可辨识；
- 集成：`QApplication::setWindowIcon()`（多尺寸）、`MainWindow::setWindowIcon()`、`RC_ICONS` 嵌入 EXE；
- 验证：ICO 帧 = 16/24/32/48/64/128/256；从 EXE 提取图标得到 32×32 且 green=196 / orange=88 像素（与设计一致）；`WM_GETICON` 非空。

详见 `docs/ICON_DESIGN.md`。

---

## 7. DPI

使用 `QT_SCALE_FACTOR=1.0 / 1.25 / 1.5` 启动并截图检查（中文界面）：

| 缩放 | 结果 |
|---|---|
| 100% | 通过 |
| 125% | 通过（修复 T55xx “调制方式(Modulation)” 裁切） |
| 150% | 通过（eMRTD / Amiibo / T55xx 无裁切） |

- 修复内容：将 `Modulation:` / `Data Bit Rate:` / `Bit Rate:` / `Password:` 的中文翻译去掉英文括号，避免窄列裁切。
- 多显示器与跨 DPI 移动：**未验证**（单显示器环境）。

---

## 8. 国际化

- `zh_CN.ts`：**587 条全部完成，0 条 unfinished**（v0.4.0 遗留的 T55xx/NTAG/主窗口/波形/魔术卡字符串已补齐）。
- 仅保留不翻译的标识符：`Proxmark3GUI`、`MINI`、`1K/2K/4K`、`T55xx`、`T5577/T5555`、`config.json`、`../data`、`<port> -f`、`NTAG/Ultralight`、`Gen1/Gen2`、`A/B`。
- `lrelease`：`Generated 587 translation(s) (587 finished and 0 unfinished)`。
- 中英文切换后布局检查：125% / 150% 中文无裁切；英文在 100% 正常。

---

## 9. 测试

| 测试 | 结果 |
|---|---|
| 原有 pytest | 32 项通过（保留，未删除） |
| 新增 `test_gui_protocol.py` | 15 项通过 |
| 合计 pytest | **47 passed in 0.56 s** |
| GUI：DG2 进度/子阶段/Done | 通过（节奏化替身） |
| GUI：DG2 期间取消 | 通过（“Reading was cancelled.”） |
| GUI：Amiibo 在 NTAG 下、无 JTAG | 通过 |
| GUI：人像显示/另存/复制 | 通过（v0.4.0 结果，未改动） |
| 发布目录：最小 PATH / 中文路径 | 通过 |
| 发布目录：Python 运行时 MRZ 校验 | 通过 |
| EXE 图标 / 窗口图标 | 通过 |
| C++ 自测试 | 项目无独立 C++ 测试框架（沿用 Stage 7 约定，未新建） |

---

## 10. 发布包

```text
dist\Proxmark3GUI\                    124.7 MB / 1150 files
dist\Proxmark3GUI-v0.4.1-win64.zip    52,888,856 bytes
```

依赖：Qt5 Core/Gui/Widgets/SerialPort/Network + platforms/styles/imageformats + MSYS2 传递 DLL + `emrtd\`（Python 3.12 独立运行时）。无需安装 Qt/Python/MSYS2。

版本：日志 `Starting Proxmark3GUI 0.4.1.0`。

---

## 11. 真实硬件验证矩阵（摘要）

| 项目 | 状态 |
|---|---|
| APDU 时延测量 | 真机通过（0.552 → 0.142 s） |
| DG2 协议与界面流程 | FakeChip + 节奏化替身通过 |
| 真实护照/通行证 DG2 | **未验证**（无证件） |
| NTAG215 读写/模拟 | **未验证**（无标签/游戏机） |
| 独立发布包 | 真机环境启动通过 |

完整矩阵见 `docs/STAGE11_VALIDATION_MATRIX.md`。

---

## 12. 尚未解决 / 限制

1. 真实证件 DG2 未验证；>32 KB DG2 仍不支持（明确报错，未实现扩展长度读取）。
2. 15 s 看门狗提示未在真实长读取中触发验证（仅实现）。
3. 多显示器与跨 DPI 移动未验证。
4. Amiibo 写入/模拟依赖真实 NTAG215 与游戏机，未验证。
5. 在线目录无可验证的公共数据源，仍需用户配置。
6. 本机环境对 GUI 二进制禁止写用户目录，日志回退到程序目录（正常环境使用 `%LOCALAPPDATA%`）。

---

## 13. 交付路径

```text
E:\Opencode_project\Proxmark3GUI-master\dist\Proxmark3GUI\
E:\Opencode_project\Proxmark3GUI-master\dist\Proxmark3GUI-v0.4.1-win64.zip
```
