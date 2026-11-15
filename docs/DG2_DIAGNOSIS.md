# eMRTD DG2 卡死诊断报告

**版本：** v0.4.1  
**症状：** 读取港澳通行证时，进度停在 DG2 阶段不再更新。  
**结论：** 不是协议死锁，而是**每个 APDU 固定等待 350 ms + DG2 全程无进度事件**，导致 20–80 秒的“静止”，叠加 >32 KB DG2 的读取上限错误和 UI 线程同步解码。

---

## 1. 诊断方法

1. 在真实 Proxmark3 设备（Iceman v4.23346）上测量 `hf 14a apdu` 的端到端耗时。
2. 用 FakeChip 合成大尺寸 DG2，统计协议输出与阶段事件。
3. 代码审查：`pm3.py`、`card.py`、`gui.py`、`EMRTDPage`。
4. 用真实 GUI + 节奏化测试替身观察阶段文字与取消行为。

**没有真实护照/通行证**，因此协议侧的结论来自合成数据与真实设备时延测量；真实证件读取仍未验证。

---

## 2. 证据

### 2.1 每个 APDU 固定 ~0.55 s

`pm3.py::command()` 在写入命令后调用 `_settle(timeout, min_lines)`，而 `_settle` 要求输出**安静 0.35 s** 才返回。实测（无卡，仅 SELECT AID）：

```text
apdu round-trip seconds: [0.551, 0.553, 0.552, 0.552, 0.552]
average: 0.552 s
```

### 2.2 DG2 的耗时被放大约 90–270 倍

DG2 按 224 字节分块读取（`DEFAULT_CHUNK`）：

| DG2 大小 | 分块数 | 修复前 | 修复后（0.142 s/APDU） |
|---|---|---|---|
| 1 KB | 5 | 2.5 s | 0.6 s |
| 4 KB | 18 | 10.1 s | 2.6 s |
| 20 KB | 89 | 49.3 s | 12.7 s |
| 32 KB | 146 | 80.8 s | 20.8 s |

在这几十秒内，协议只发出了 `dg2 start` 一个事件，**没有任何后续输出**，界面自然“停住”。

### 2.3 >32 KB 的 DG2 无法读取

`read_binary()` 使用短 EF 的 READ BINARY，偏移为 15 位（≤32767）。超过上限时旧代码在读完约 80 秒后才抛出错误，且错误信息里没有实际大小：

```text
READ BINARY offset 32768 exceeds the 15-bit limit ...
```

### 2.4 图像解码在 UI 线程

`applyResult()` 直接 `QPixmap::loadFromData(...)`，大图或 JPEG 2000 会在 GUI 线程解码；同时 Python 侧 JPEG 2000 → PNG（Pillow）也可能耗时数秒，期间没有任何事件。

---

## 3. 根因

| 编号 | 根因 | 影响 |
|---|---|---|
| R1 | APDU 命令沿用“输出安静 350 ms”的通用完成判定 | 每个分块多等 0.35 s，DG2 累计数十秒 |
| R2 | DG2 读取没有字节级进度事件 | 界面在 R1 的整个时间段内无任何反馈 |
| R3 | >32 KB DG2 在长时间读取后才报错，且无明确大小 | 用户感知为“卡死” |
| R4 | 图像在 UI 线程同步解码 | 大图时界面短暂无响应 |
| R5 | 取消只在阶段边界检查 | DG2 读取期间取消不及时 |

---

## 4. 修复

### 4.1 APDU 完成判定（R1）

`hf 14a apdu` 每次固定输出一行 `<<< <hex>`。新增 `_settle_response()`：检测到 `<<<`（或 `[!]`/`[-]` 错误行）后仅等待 60 ms 收尾，不再等待 350 ms 静默。

- 实测：**0.552 s → 0.142 s（3.9×）**。
- 其他命令（`hf mfu`、`hw` 等）保持原有静默判定，互不影响。

### 4.2 DG2 进度与子阶段（R2）

`card.read_file(..., progress=...)` 在头解析后与每个分块后回调；`gui.py` 发出：

```json
{"event":"progress","stage":"dg2","status":"receiving","bytes_read":4800,"bytes_total":24000}
{"event":"progress","stage":"dg2","status":"parsing"}
{"event":"progress","stage":"dg2","status":"extracting","detail":"jpeg"}
{"event":"progress","stage":"dg2","status":"decoding"}
{"event":"progress","stage":"dg2","status":"preview"}
```

GUI 依次显示：

```text
Reading DG2 (4800 / 24000 bytes)
...
Extracting facial image
Decoding facial image
Done
```

进度条仍按 12 个阶段推进（不虚构百分比），字节数来自真实读取。

### 4.3 大文件上限（R3）

`read_file()` 在读到文件头后立即检查总长度：

```text
file length 40012 exceeds the 15-bit READ BINARY limit (32768);
this document needs an extended-length reader
```

DG2 阶段直接标记为跳过并给出警告，不再先等 80 秒。

### 4.4 解码移出 UI 线程（R4）

新增 `ImageDecodeTask`（`QThread`）：工作线程 `QImage::loadFromData`，GUI 线程只做 `QPixmap::fromImage` 与缩放。解码期间进度条为不确定状态，取消按钮保持可用；用 generation 计数丢弃过期结果。

### 4.5 取消与看门狗（R5）

- `report()` 在每个分块前调用取消检查 → DG2 接收中也能取消。
- 15 秒无任何输出时显示 “Still working...”，明确说明仍在进行且可取消（**不制造假进度**）。
- 解码阶段点击取消会立即失效当前解码结果。

---

## 5. 修复前后对比（真实 GUI）

| 场景 | 修复前 | 修复后 |
|---|---|---|
| DG2 20 KB | 约 49 s 无任何更新 | 每 ~0.2 s 更新 “Reading DG2 (x / 24000 bytes)” |
| DG2 >32 KB | 约 80 s 后报错，信息含糊 | 立即提示长度超限 |
| 大图显示 | UI 线程解码 | 工作线程解码，界面不阻塞 |
| DG2 期间取消 | 需等到阶段结束 | 立即取消 |
| 进程无输出 | 永久停在原阶段 | 15 s 后显示 “Still working...”，仍可取消 |

---

## 6. 自动化测试

新增 `tests/test_gui_protocol.py`（15 项），全部通过（总计 47 项）：

- 正常 DG2、20 KB 大 DG2、完整传输（字节数一致）；
- 字节级 `receiving` 事件与子阶段顺序；
- DG2 >32 KB 的明确错误（不挂起）；
- 损坏 TLV、不支持/非图像编码；
- DG2 接收期间取消（退出码 5，无结果）；
- 无效请求、坏 JSON、NDJSON 分块边界；
- 传输层错误映射为退出码 3；
- `read_file` 进度回调与超限错误；
- 全角 `《`/全角数字的 MRZ 规范化。

---

## 7. 未验证

- 真实护照 / 港澳通行证上的 DG2 读取（本环境无证件）。
- 真机 >32 KB DG2 的替代读取方案（当前明确报错，不支持）。
- 真实 JPEG 2000 人像的解码耗时（仅有合成数据）。
