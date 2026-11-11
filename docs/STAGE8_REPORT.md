# Stage 8 Report — eMRTD 护照 / 港澳通行证读取页面

**日期：** 2026-11-12  
**版本：** Proxmark3GUI v0.4.0 开发中（Stage 8）  
**分支：** `upgrade/v0.4.0-emrtd-amiibo`

---

## 1. 前置审计

| 项目 | 结果 |
|---|---|
| Python 版本 | 3.10+（实测 3.12.10） |
| 依赖 | `pycryptodome` 4.3.0；可选 `Pillow` 13.0.0 |
| 现有测试 | **32 passed**（`python -m pytest -q`） |
| CLI | `emrtd-pm3 {selftest,read}`，退出码 0/2/3 |
| PM3 桥接 | 自建长连接 `proxmark3.exe -f <port> --incognito`，`hf 14a apdu -k/-sk` |
| 进程冲突 | **存在**：与 GUI `ClientSession` 会争用串口 → 必须显式交接 |
| 人像格式 | JPEG / JPEG 2000 codestream / JP2 / ISO 19794-5 FAC |
| MRZ | TD1/TD2/TD3 + 国内（港澳通行证）非 ICAO 版式 |
| 模拟芯片 | `tests/fake_chip.py` 可离线跑完整协议 |

---

## 2. 实现

### 2.1 Python 侧（`emrtd_pm3`）

- 新增 `emrtd_pm3/gui.py`：stdin JSON → stdout NDJSON。
  - 12 个阶段进度事件（真实阶段，不是假进度条）；
  - `validate` 动作：只解析 MRZ，不访问设备；
  - 协作式取消 + 进程树终止兜底；
  - 不写任何文件；DG2 仅返回解码后图片（JPEG 直传，JP2/J2K 用 Pillow 转 PNG）。
- `cli.py`：新增 `gui` 子命令。
- `pm3.py`：client 提前退出时给出可读错误（包含 client 输出尾部），不再抛出裸 `OSError`。

### 2.2 C++ 侧

- 新增 `src/module/emrtd.h/.cpp`（`EMRTDPage`）：
  - MRZ 输入 / 校验 / 清空；
  - Start / Cancel / 进度 / 阶段 / 详情；
  - 个人信息表、人像（缩放/适应/另存）、其他数据（EF.COM/DG11/DG12/hex）；
  - 复制全部 / 复制 MRZ / 复制选中字段；
  - 串口交接信号 `aboutToRead` / `readFinished`。
- `MainWindow`：插入 `eMRTD Reader` 标签页（位于 NTAG 后），连接交接信号，端口变化同步到页面，读取结束后自动重连。
- 端口显示文本 `"COM14 *"` 会规范化为 `COM14`（修复真实读取失败）。
- 新增 `scripts/package-emrtd-runtime.ps1`：用本地 CPython 组装独立运行时（约 50 MB）。

### 2.3 国际化

- `lupdate` 更新 `i18n/zh_CN.ts`，新增 114 条中文翻译（eMRTD 页面、ClientSession 错误、主窗口新状态）。
- `lrelease` 重新生成 `i18n/zh_CN.qm`（352 条已完成）。
- 中文界面实测：标签页显示“eMRTD 读取”，按钮显示“校验 MRZ / 开始读取”，结果标签显示“个人信息 / 人像照片 / 其他数据”。

---

## 3. 验证

### 3.1 Python

| 测试 | 结果 |
|---|---|
| 原有 32 项 pytest | 通过 |
| `gui` 完整读取（FakeChip） | 通过：TD3、DG1/DG2/DG11、JPEG 人像 |
| `validate`（合法 MRZ） | 通过 |
| `validate`（错误校验位） | 通过：返回 `check_digit:document_number` |
| 取消请求 | 通过：退出码 5，`Cancelled by the user` |

### 3.2 GUI（真实窗口 + UI Automation）

| 测试 | 结果 |
|---|---|
| 页面打开、标签正确 | 通过 |
| MRZ 粘贴 + 校验 | 通过：“MRZ valid. TD3 / L898902C3 / 740812 / 120415” |
| 串口交接 | 通过：GUI client `exit=0`，仅 1 个 `proxmark3.exe` |
| 读取失败提示 | 通过：无卡时显示 “no ISO14443-A card in the field” |
| 读取后自动重连 | 通过：`PM3 state=1` 恢复 |
| 结果表格 | 通过：姓名/证件号/国籍/签发地/生日/性别/有效期 |
| 人像显示 + 另存为 PNG | 通过：生成 900 字节 PNG |
| 复制选中字段 | 通过：`ERIKSSON` |
| 复制全部信息 | 通过：`Surname: ERIKSSON | Given names: ...` |
| 复制 MRZ | 通过 |
| 其他数据标签 | 通过：EF.COM / DG11 / 原始（hex） |
| 中文界面 | 通过 |

### 3.3 未完成 / 未验证

1. **真实护照 / 港澳通行证读取未验证**（本环境无证件；当前卡片为 HID SEOS/EMV）。
2. 真实 DG2 大图（数十 KB JPEG2000）在界面中的显示未用真实证件验证，仅用合成数据验证渲染路径。
3. “Connect / 检查设备状态”未单独做按钮，沿用主窗口 Connect 与状态栏（保持原有操作习惯）。
4. 约 122 条 v0.3.0 遗留字符串（T55xx/NTAG 页面）仍未翻译，属既有缺口，本次未扩大翻译范围。

---

## 4. 结论

**Stage 8 功能完成，真实证件验证待硬件条件。**  
GUI 能在不安装 Python 的前提下调用原 Python 项目，串口交接、进度、错误、取消、结果展示与导出均工作正常，且未将任何证件数据写入日志或磁盘。
