# eMRTD 护照 / 港澳通行证读取集成说明

**版本：** v0.4.0  
**Python 项目：** `emrtd_pm3`（本仓库 `emrtd_pm3/` 目录，源自独立项目）

---

## 1. 架构

```text
Proxmark3GUI (C++/Qt)
   │  JSON 请求（stdin）
   ▼
emrtd_pm3 gui  （独立 Python 子进程，随 GUI 分发）
   │  复用现有 BAC / SM / TLV / DG 解码
   ▼
proxmark3.exe  （由 Python 启动，独占串口）
   │  hf 14a apdu
   ▼
证件芯片
```

- **不重写协议**：BAC、KDF、3DES、ISO 9797-1 MAC、Secure Messaging、BER-TLV、DG2 解码全部沿用 Python 实现。
- **不新增 Python 依赖安装步骤**：发行包内附带独立运行时。
- **GUI 只负责展示**：不解析 MRZ、不计算密钥、不解析 TLV。

---

## 2. 串口交接（关键约束）

`emrtd_pm3` 会启动**自己的** `proxmark3.exe`，因此 GUI 的 `ClientSession` 必须先释放串口：

1. 用户点击 “Start Reading”。
2. `EMRTDPage::aboutToRead()` → `MainWindow` 记录当前是否已连接，并调用 `ClientSession::stopClient()`。
3. Python 子进程启动自己的 client。
4. 读取结束（成功 / 失败 / 取消）→ `EMRTDPage::readFinished()` → 若之前已连接则 `restartClient()`。

保证任意时刻只有一个 `proxmark3.exe` 占用串口；回归测试中已用进程计数验证。

---

## 3. 机器接口（`emrtd_pm3 gui`）

### 输入

stdin 一行 JSON：

```json
{
  "mrz": "P<UTO...\nL898902C3...",
  "port": "<PORT>",
  "pm3_exe": "<CLIENT_DIR>/proxmark3.exe",
  "dgs": "DG1,DG2,DG11,DG12",
  "incognito": true
}
```

- MRZ 只通过 stdin 传递，**不出现在命令行 / 进程列表**。
- 校验专用请求：`{"action":"validate","mrz":"..."}`，不访问设备。
- 取消：追加一行 `{"action":"cancel"}`。

### 输出（NDJSON，stdout）

```json
{"event":"progress","stage":"dg2","index":8,"total":12,"status":"start"}
{"event":"info","message":"..."}
{"event":"result","data":{...}}
{"event":"error","code":2,"stage":"bac","message":"..."}
```

`stage` 为稳定标识，界面文字由 GUI 翻译：
`mrz_check, connect, select_aid, get_challenge, bac, ef_com, dg1, dg2, dg11, dg12, parse, done`。

### 退出码

| 码 | 含义 |
|---|---|
| 0 | 成功 |
| 2 | 卡片 / BAC 认证错误 |
| 3 | Proxmark3 通信问题 |
| 4 | 请求无效（MRZ 格式等） |
| 5 | 用户取消 |

### 返回数据

```json
{
  "layout": "TD3",
  "mrz": "...",
  "document": {"surname": "...", "given_names": "...", "...": "..."},
  "ef_com": {"lds_version": "1.7", "unicode_version": "4.0.0", "data_groups": [1,2,11]},
  "dg11": {...},
  "dg12": {...},
  "files": {"DG1": 94, "DG2": 48480},
  "face": {"format": "jpeg", "mime": "image/jpeg", "data_b64": "..."},
  "hex": {"EF_COM": "...", "DG11": "..."},
  "warnings": []
}
```

- **DG2 只返回解码后的人像**，不返回原始 DG2 十六进制。
- JPEG 2000 / JP2 在 Python 侧用 Pillow 转 PNG（保留原始宽高比）。
- 除 `hex` 字段外不返回原始 DG 数据；`hex` 有 32 KiB 上限。

---

## 4. 运行时打包

发行包结构：

```text
dist\Proxmark3GUI\
├─ Proxmark3GUI.exe
├─ emrtd\
│  ├─ python.exe
│  ├─ python312.dll
│  ├─ DLLs\
│  └─ Lib\site-packages\{Crypto,PIL,emrtd_pm3}
└─ ...
```

生成方式：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\package-emrtd-runtime.ps1 `
    -PythonHome "$env:LOCALAPPDATA\Programs\Python\Python312" `
    -Destination "build-runtime\emrtd"
```

约 50 MB。不依赖系统 Python、不修改 PATH、不需要管理员权限、中文路径可用。

查找顺序（`EMRTDPage::pythonExecutable`）：

1. `PM3GUI_EMRTD_EXE` 环境变量（调试 / 自定义）；
2. `<GUI 目录>/emrtd/python.exe`（发行包）；
3. `<GUI 目录>/../build-runtime/emrtd/python.exe`（开发目录）。

---

## 5. 界面

新增标签页 **eMRTD Reader**（位于 NTAG 与 RawCommand 之间），分三个区域：

- **Document MRZ**：粘贴 TD1/TD2/TD3；`Validate MRZ` 调用 Python 校验并显示版式、证件号、出生日期、有效期与校验位问题；`Clear` 清空并释放缓存。
- **Read**：端口显示、`Start Reading`、`Cancel`、真实进度（来自 Python 事件，不是定时假进度）、当前阶段、详情、错误。
- **结果**：`Personal information`（字段表 + 复制全部 / 复制 MRZ / 复制选中字段）、`Portrait`（等比缩放、放大/缩小/适应、另存为 PNG/JPEG）、`Other data`（EF.COM / DG11 / DG12 / 原始十六进制）。

人像另存为 JPEG 时，如果原始数据就是 JPEG，则直接写原始字节，不重新压缩。

---

## 6. 安全设计

| 项目 | 处理 |
|---|---|
| 上传 | 无任何网络请求 |
| 默认保存 | 不保存任何文件；只有用户点“保存人像”才写盘 |
| 日志 | 不记录 MRZ、BAC 密钥、APDU 载荷、DG2 数据；仅记录“开始/结束/错误码” |
| 命令行 | MRZ 经 stdin 传递，不出现在进程命令行 |
| 缓存 | 清空 / 重新读取时释放 MRZ、人像与结果缓冲 |
| 崩溃 | 不生成包含证件内容的报告；Python 异常只输出堆栈到 stderr（不含数据） |
| 取消 | 协作式取消（阶段间检查）+ 4 秒后终止整个进程树，避免残留 client |

eMRTD 仍为**只读**：不写入、不克隆、不模拟证件。

---

## 7. 无证件时的验证方式

1. **协议层**：`tests/fake_chip.py` 软件模拟芯片，覆盖 SELECT AID → GET CHALLENGE → BAC → 读文件。
2. **GUI 协议层**：用 `transport_factory` 注入 `FakeChip`，验证事件、进度、结果、校验、取消。
3. **界面层**：临时用合成结果填充页面，验证表格、人像显示与保存、复制、其他数据标签。
4. **真实设备**：连接的 Proxmark3 设备 + 非 eMRTD 卡片，可验证串口交接、client 启动、错误提示与自动恢复；**真实护照读取未验证**。

---

## 8. 已知限制

1. 需要 ISO 14443-A 且支持 BAC 的证件；当前 256 KB 固件不含 ISO 14443-B。
2. 真实护照 / 港澳通行证未在本环境测试（无证件）。
3. DG3/DG4/DG7 等未解码（与 Python 项目一致，仅读取 DG1/DG2/DG11/DG12）。
4. 超大 DG2（> 数 MB）需要更长时间；读取期间请保持证件静止。
