# Stage 4 Report — NTAG / Ultralight 独立页面

**日期：** 2026-11-10  
**分支：** `upgrade/iceman-v4.23346`  
**目标：** 新增独立 NTAG/Ultralight 页面，实现卡片识别、信息读取、页面读取/编辑/写入/复读验证，并按型号保护系统与配置页。

---

## 1. 实际完成内容

### 1.1 独立页面

新增 `NtagTab`（`src/module/ntagtab.h/.cpp`），在 `MainWindow` 中作为独立 QWidget 插入：

```text
Mifare | LF | T55xx | NTAG/Ultralight | RawCommand | Settings
```

页面由 Qt Widgets 程序化构建，使用与原 GUI 相同的 QGroupBox / QPushButton / QTableWidget / 默认样式，没有引入 Qt Charts 或第三方 UI 依赖。

### 1.2 卡片识别与信息

`Detect / Read Info` 调用：

```text
hf mfu info [-k <key>]
```

解析：

- `UID:`；
- `TYPE:`；
- `cfg0 [<page>/0x..]`（配置起始页，若 Client 输出）；
- `Total memory...`（若 Client 输出，按 4 字节/页换算）。

型号 → 页数映射来自 v4.23346 源码 `client/src/cmdhfmfu.c` 的常量：

| 型号 | 页数 | 配置/特殊区起始 |
|---|---:|---:|
| MIFARE Ultralight (MF0ICU1) | 16 | 16（无 config） |
| MIFARE Ultralight C | 48 | 40 |
| Ultralight EV1 48 | 20 | 16 |
| Ultralight EV1 128 | 41 | 36 |
| NTAG 203 | 42 | 37 |
| NTAG 210 | 20 | 16 |
| NTAG 212 | 41 | 36 |
| NTAG 213 | 45 | 40 |
| NTAG 215 | 135 | 130 |
| NTAG 216 | 231 | 226 |
| NTAG 223 | 60 | 4（写入禁用） |
| NTAG 224 | 76 | 4（写入禁用） |
| Ultralight AES | 56 | 4（写入禁用） |
| FUDAN UL compatible | 16 | 4（写入禁用） |
| ST25TN512 / ST25TN01K | 16 / 40 | 4（写入禁用） |

如果 Client 的 `cfg0 [n/0x..]` 与实际型号不同，以 Client 输出覆盖配置起始页。  
如果型号未知：页数显示 unknown，不假设布局，**禁用写入**。

### 1.3 页面保护策略

| 页面 | 访问 |
|---|---|
| 0–3 | 只读（UID / lock / CC） |
| 4 .. configStart-1 | 可写 |
| configStart .. pages-1 | 只读（config / OTP / key / PWD / PACK 等） |
| 未知型号 | 全部只读，禁止写入 |

这一策略确保不会把所有页面都当作普通可写数据区。  
NTAG 213/215/216 的 config 页、UL-C 的 key/config 页、EV1 的 config 页均不会被 GUI 默认写入。

### 1.4 读取

- `Read All`：从 page 0 到 pages-1 逐页调用 `hf mfu rdbl -b <page>`；
- `Read Selected`：只读取表格选中的行；
- `Read Page`：读取 `Page` 数值框指定页；
- 数据以 4 字节十六进制 + ASCII 两列显示；
- 每页失败时保留错误状态，不伪造数据；
- 读取期间用 `QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents)` 更新界面，但不接受用户点击重入。

### 1.5 编辑与写入

- Hex 列可编辑（只读页不进入写入路径）；
- `Write Page`：写入 `Page` 数值框指定页，数据 8 hex；
- `Write Selected`：只对选中且 `isWritable()` 的页写入；
- 每次写入前弹出确认；
- 写入后立即 `rdbl` 复读并比较；不一致时明确报告“写入返回成功但校验不一致”；
- 支持可选 key（8 hex / 32 hex）传给 `-k`；
- 没有 `--force`；不绕过客户端地址检查。

命令：

```text
hf mfu wrbl -b <page> -d <8 hex> [-k <key>]
```

依据：

- v4.23346 `hf mfu wrbl --help`；
- `client/src/cmdhfmfu.c` 的 `CmdHF14AMfUWrBl`；
- `-k` 长度支持 4 / 16 字节。

### 1.6 复读验证

`Verify Page`：

- 重新读取指定页；
- 与表格中当前值比较；
- 显示 OK / mismatch；
- 如果表格为空，仅提示“已读取，无旧值可比较”，不宣称验证成功。

---

## 2. 修改文件列表

| 文件 | 修改 |
|---|---|
| `src/module/ntagtab.h` | 新增 |
| `src/module/ntagtab.cpp` | 新增 |
| `src/ui/mainwindow.h` | 增加 `NtagTab* ntag` |
| `src/ui/mainwindow.cpp` | 创建并插入 NTAG/Ultralight 页 |
| `src/Proxmark3GUI.pro` | 加入 ntagtab |
| `docs/STAGE4_REPORT.md` | 本报告 |

---

## 3. 编译结果

```text
qmake-qt5 ..\src\Proxmark3GUI.pro -spec win32-g++ CONFIG+=release
mingw32-make -j4
```

结果：**编译、链接成功**。仅有原有弃用警告。

---

## 4. 测试结果

| 项目 | 结果 | 说明 |
|---|---|---|
| NTAG/Ultralight 页显示 | 通过 | 独立标签页，含 Card / Pages 分组 |
| 页面布局与原有 GUI 风格 | 通过 | 默认 Qt 样式、QGroupBox、QTableWidget |
| Detect 在非 NTAG 卡上 | 通过 | 当前卡为 HID SEOS/EMV，`hf mfu info` 无 UID/TYPE，界面显示 “Tag information did not contain UID/TYPE” |
| 成功识别 NTAG/UL | **Not verified on physical hardware** | 当前没有 NTAG/Ultralight 标签 |
| 页面读取 | **Not verified on physical hardware** | 需要 NTAG/UL 标签 |
| 页面写入 | **未执行 / Not verified** | 需要标签与用户授权 |
| 写入后复读 | **未执行 / Not verified** | 同上 |
| 未知型号保护 | 静态检查通过 | 页数 0，写入路径返回只读错误 |

测试证据：

- 页面启动截图（构建产物内 `stage4-ntag.png`）；
- 真实 Client 执行 `hf mfu info` 在当前 HID 卡上未返回 UID/TYPE；
- GUI 未崩溃，状态提示明确。

---

## 5. 已知问题

1. **没有 NTAG/Ultralight 实物标签**，成功识别/读/写/验证均未硬件确认。
2. `hf mfu info` 的实际成功输出格式未在真实 NTAG 上观察；解析规则来自 v4.23346 源码中的打印格式，需在 Stage 6 用标签复核。
3. 当前未实现：
   - NDEF 读写；
   - dump 文件导入/导出；
   - UL-C / UL-AES 专用认证流程（仅通用 `-k`）；
   - 密码/PACK 专用编辑；
   - 锁定/OTP 写入。
4. 未知型号只读；不会猜测页数或写入范围。
5. `Read All` 对 NTAG216（231 页）会逐页执行，耗时较长；未使用批量 dump 解析。

---

## 6. 下一阶段计划

Stage 5：LF 波形显示。

- 先确认 v4.23346 是否能在无 Client Qt GUI 的情况下输出可绘制样本；
- 当前设备 firmware 无 LF，无法验证真实采样；
- 如果 Client 只把样本保存在内部 graph buffer，GUI 需要找到可导出的命令/文件格式；否则如实报告限制，不伪造波形。

---

**Stage 4 结论：通过（页面与保护策略完成；真实 NTAG/UL 读写未验证）。**
