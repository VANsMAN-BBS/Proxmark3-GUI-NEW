# Stage 10 Report — 整体整合、回归与发布

**日期：** 2026-11-12  
**版本：** Proxmark3GUI v0.4.0  
**分支：** `upgrade/v0.4.0-emrtd-amiibo`  
**标签：** `upgrade-v0.4.0`

---

## 1. 发布物

```text
dist\Proxmark3GUI\                    124.6 MB / 1150 files
dist\Proxmark3GUI-v0.4.0-win64.zip    52,776,118 bytes (~50.3 MB)
```

目录内容：

- `Proxmark3GUI.exe`（1,216,000 bytes，版本 0.4.0.0）；
- Qt 5.15 DLL：Core/Gui/Widgets/SerialPort/**Network**；
- `platforms/`、`styles/`、`imageformats/`；
- MSYS2 传递依赖（ICU/PCRE/freetype/harfbuzz/…）；
- `emrtd\`：Python 3.12 独立运行时（`python.exe`、`DLLs`、`Lib`、`site-packages\{Crypto,PIL,emrtd_pm3}`）；
- `GUIsettings.ini`（zh_CN、深色主题、Client 路径）。

zip 已核对包含 `emrtd\python.exe`、`Crypto`、`Qt5Network.dll`。

---

## 2. 功能回归

| 项目 | 结果 |
|---|---|
| 主窗口布局与原有标签页 | 通过（Mifare/LF/T55xx/NTAG/RawCommand/Settings 未改） |
| 新增 eMRTD Reader 页面 | 通过 |
| 新增 JTAG 容器 + Amiibo 子页 | 通过 |
| 页面切换 | 通过 |
| 中文 / 英文界面 | 通过（`zh_CN.qm` 共 460 条完成翻译，覆盖 eMRTD/Amiibo 与会话字符串） |
| 长文本 / 十六进制显示 | 通过（eMRTD hex、Amiibo hex） |
| 图像显示与另存 | 通过（合成人像 → 900 字节 PNG） |
| 文件对话框 | 通过（导入/另存/人像保存） |
| 复制到剪贴板 | 通过（字段/全部/MRZ） |
| 进度显示 | 通过（真实阶段事件） |
| 错误弹窗 | 通过（无卡、通信错误、MRZ 无效） |
| 125% / 150% DPI | **未测试**（本次环境未切换缩放） |
| 多显示器 | **未测试** |
| 窗口最大化 | 通过（自动化测试使用最大化窗口） |

---

## 3. 资源与进程管理

| 项目 | 结果 |
|---|---|
| eMRTD 读取期间 client 数量 | 恒为 1（GUI 先断开，Python 独占，结束后 GUI 重连） |
| 读取后自动重连 | 通过 |
| 读取取消 | 通过（协作式取消 + 4 s 进程树终止兜底） |
| 关闭页面 / 主窗口后的 Python 子进程 | 析构时终止进程树；未发现残留 |
| Amiibo 模拟（detached） | Stop 通过 `restartClient()` 结束，不留旧会话 |
| 大 DG2 / 大文件 | 仅用合成数据验证（无人像大图与真实 Amiibo 大文件） |
| 网络请求 | 仅用户点击时发生；断网不影响本地功能 |

---

## 4. 独立发布包验证

| 测试 | 结果 |
|---|---|
| PATH 仅 `C:\Windows\System32;C:\Windows` 启动 | 通过 |
| 中文/空格路径（`E:\Opencode_project\测试 路径`）启动 | 通过 |
| 版本显示 | 日志记录 `Starting Proxmark3GUI 0.4.0.0` |
| 发布目录内 Python 运行时 MRZ 校验 | 通过 |
| 发布目录内读取（无 eMRTD 卡） | 通过：正确显示 “no ISO14443-A card in the field” |
| 不依赖系统 Python | 通过（仅使用 `emrtd\python.exe`） |
| 不依赖 MSYS2 PATH | 通过 |
| 日志写入 | 通过（`<dist>/logs/Proxmark3GUI.log`；本机用户目录受环境限制时回退到程序目录） |

---

## 5. 多语言

- `i18n/zh_CN.ts` / `zh_CN.qm`：460 条完成翻译。
- 新增翻译：Stage 7 会话/状态、Stage 8 eMRTD 全部字符串、Stage 9 Amiibo 全部字符串。
- `en_US` 使用源字符串（英文）。
- 遗留：v0.3.0 的 T55xx/NTAG 页面约 121 条字符串仍未翻译（既有缺口，未扩大范围）。

---

## 6. 文档

新增：

- `docs/STAGE7_REPORT.md` … `docs/STAGE10_REPORT.md`
- `docs/EMRTD_INTEGRATION.md`
- `docs/AMIIBO_COMPATIBILITY.md`
- `docs/HARDWARE_VALIDATION_MATRIX.md`

更新：`README.md`、`CHANGELOG.md`、`BUILD_GUIDE.md`、`TEST_REPORT.md`、`ARCHITECTURE_UPGRADE.md`、`ICEMAN_COMPATIBILITY_MATRIX.md`。

---

## 7. 已知限制（必须保留）

1. 无真实护照/港澳通行证：eMRTD 仅用软件模拟芯片与合成数据验证。
2. 无 NTAG215 标签与游戏机：Amiibo 读写/校验/模拟仅验证命令与流程。
3. 当前固件 256 KB 无 LF；当前卡片为 HID SEOS/EMV，MIFARE Classic 未验证。
4. 125%/150% DPI 与多显示器未测试。
5. 在线目录未内置公共数据源（构建时公共 API 不可用），需用户自行配置。
6. 本机对该 GUI 二进制禁止写用户目录，日志回退到程序目录；正常环境使用 `%LOCALAPPDATA%`。

---

## 8. 结论

**v0.4.0 交付完成。** 原有功能保留，Stage 7 稳定性、Stage 8 eMRTD、Stage 9 Amiibo 均已实现；独立发布包可在无开发环境、无 MSYS2 PATH、中文路径下运行；所有未在真实硬件上验证的功能均已在文档中明确标注。
