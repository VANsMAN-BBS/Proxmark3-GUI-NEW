# LF 测试与验证方案

**目标：** 在"不修改业务代码、不写真实卡"的前提下，建立可重复、可审计的三层验证体系。

---

## 0. 测试分层与原则

| 层 | 名称 | 依赖 | 可自动 | 本阶段能否执行 |
|---|---|---|---|---|
| **A** | 纯软件测试 | 无 | ✅ | ✅ **可以**（合成输出） |
| **B** | Client 集成测试 | `proxmark3.exe`（可离线） | 部分 | ✅ **可以**（离线模式） |
| **C** | 真实硬件测试 | 含 LF 的固件 + 真实卡 | ❌ | ❌ **当前固件不支持，全部标记未验证** |

**原则：**

1. 任何"通过"都必须指明层级。A 层通过 ≠ 能用。
2. 没有真实卡的用例一律写 **未验证**，不得标"已支持"。
3. 测试数据（Client 输出样本）必须带来源（Client 版本 + 固件版本 + 是否真实卡）。

---

## A. 纯软件测试

### A.1 测试框架建议

- 现有 `.pro` 无 `testlib`。建议**新建独立测试工程** `tests/lf_tests.pro`：
  ```qmake
  QT += core testlib
  CONFIG += console c++11 testcase
  SOURCES += ../src/module/lfparser.cpp ../src/module/lfcommands.cpp tst_lf.cpp
  ```
- 不修改主 `.pro`，避免影响发布构建。
- 也可先用 `QTest::qExec` 单文件验证，再接入。

### A.2 用例矩阵：解析器

| ID | 输入 | 期望 | 说明 |
|---|---|---|---|
| A-P-001 | `[+] EM 410x ID 0F00112233` | EM4100 / id=`0F00112233` | 基础 |
| A-P-002 | `[+] EM 410x ID 0f00112233` | 同上（大写归一） | 大小写 |
| A-P-003 | `[-] No known 125/134 kHz tags found!` | `NoCard` | 无卡 |
| A-P-004 | `unknown command:: 0x0228` | `FirmwareUnsupported` | 固件无 LF |
| A-P-005 | 空字符串 | `NoCard` / `Indeterminate` | 空输入 |
| A-P-006 | 只有 `[=] Checking for known tags...` | `NoCard` | 不完整 |
| A-P-007 | `FC: 65  CN: 29334` + `raw: 200078BE5E1E` | HID / FC=65 / CN=29334 / raw | HID 基础 |
| A-P-008 | `FC: 65, CN: 29334`（逗号） | 同上 | 分隔符变体 |
| A-P-009 | `FC/65/CN/29334`（斜杠） | 同上 | 分隔符变体 |
| A-P-010 | `HID Prox` 但无 FC/CN/raw | 解析失败/Unknown | 不完整 |
| A-P-011 | `IO Prox - XSF(01)65:01337, Raw: 007859603059cdaf` | IOProx / VN=01 / FC=101(hex 65) / CN=1337 | hex→dec |
| A-P-012 | `IO PROX....... 00785960` | IOProx / raw 回退 | 8 位 ID 路径 |
| A-P-013 | `Animal ID......... 999-123456789012` | FDX-B / 999 / 123456789012 | 多行 |
| A-P-014 | `Indala` + `Raw:` | Indala / raw | 有 raw |
| A-P-015 | `INDALA 224.... 00A1B2C3`（无 `Raw:`/`ID:`） | **应识别**（当前 Phosphor 会失败） | 格式回归 |
| A-P-016 | `KERI - Internal ID: 12345, Raw: E000...` | Keri / type=i / cn=12345 | |
| A-P-017 | `Descrambled MS - FC: 1 Card: 12544` | Keri / type=m / FC=1 / CN=12544 | |
| A-P-018 | `GALLAGHER - Region: 1 Facility: 22 Card No.: 3333 Issue Level: 1` | 4 字段 | 单行 |
| A-P-019 | Gallagher 四行分开 | 4 字段 | 多行 |
| A-P-020 | `NEDAP (64b) - ID: 12345 subtype: 1 customer code: 101 / 0x065 Raw: ...` | st=1 / cc=101 / id=12345 | |
| A-P-021 | 单行出现 `EM 410x` 与 `HID Prox` | 按定义顺序命中第一个 | 优先级 |
| A-P-022 | 超长输出（1 MB） | 不崩溃，性能可接受 | 压力 |
| A-P-023 | 含 ANSI 颜色码 | 剥离后正常解析 | ANSI |
| A-P-024 | 含中文/UTF-8 | 不崩溃 | 编码 |
| A-P-025 | `[!!]` 错误行 + 卡型行 | 判定为失败而非成功 | 错误优先 |

### A.3 用例矩阵：命令构造器

| ID | 输入 | 期望 |
|---|---|---|
| A-C-001 | EM4100 + `0F00112233` | `lf em 410x clone --id 0F00112233` |
| A-C-002 | HID + raw | `lf hid clone -r <raw>`（优先 raw） |
| A-C-003 | HID + FC/CN 无 raw | `lf hid clone -w H10301 --fc 65 --cn 29334` |
| A-C-004 | HID + format `34-bit` | **应使用正确 format 或拒绝**（当前会静默降级） |
| A-C-005 | IOProx + FC/CN/VN | `lf io clone --vn 1 --fc 101 --cn 1337` |
| A-C-006 | AWID 无 FC/CN | 返回错误（无 raw 回退） |
| A-C-007 | UID 含 `;` | 拒绝（注入防护） |
| A-C-008 | UID 含换行/空格 | 拒绝 |
| A-C-009 | UID 超长（>200） | 拒绝 |
| A-C-010 | 非可克隆类型（COTAG/Hitag） | 返回错误 |
| A-C-011 | EM4305 + 不支持 `--em` 的 11 种新卡型 | 拒绝 |

### A.4 位宽 / 校验 / 编码（若将来做纯算法）

> 当前设计**不在 GUI 内做解码**，因此这些测试主要针对"若引入可选算法模块"的场景。

| ID | 内容 | 说明 |
|---|---|---|
| A-B-001 | EM4100 10 hex nibble → 40 bit + 行/列奇偶 | 与 Client 输出对拍 |
| A-B-002 | EM4100 位宽边界（40 bit / 64 bit 帧） | |
| A-B-003 | HID 26/34/35/37/40/48 bit 帧 | 位宽映射 |
| A-B-004 | Manchester 编码（1=高→低 / 0=低→高） | 正反相 |
| A-B-005 | Manchester 解码非法序列（`00`/`11`） | 错误处理 |
| A-B-006 | FSK1/FSK2/FSK1a/FSK2a 频率比 | 若自研 |
| A-B-007 | PSK1/2/3 相位跳变 | 若自研 |
| A-B-008 | CRC8/CRC16 已知向量 | 若自研 |
| A-B-009 | 空数据 / 截断帧 / 只有前导 | 必须返回"失败"而不是猜测 |
| A-B-010 | 多帧粘连 | 只取第一帧并标记 |

**注意：** 若坚持"解码交给 Client"，A.4 只保留"对拍"性质的最小集合。

---

## B. Client 集成测试

### B.1 离线模式（可立即执行）

`proxmark3.exe` 在无端口时进入 **OFFLINE mode**，`lf search` 与 `lf <proto> demod` 标记为 `available offline: yes`。

**已实测可用的流程：**

```powershell
cd C:\Users\van\AppData\Local\Temp\opencode\pm3off
# 生成合成样本（文本，每行一个 -127..127 的整数）
node gen.js
# 脚本：加载 + 解码
@("data load -f em4100.txt","lf search -1u") | Out-File -Encoding ascii run.txt
proxmark3.exe --incognito -s run.txt
```

**本次实测结果（必须如实记录）：**

| 试验 | 命令 | 结果 |
|---|---|---|
| 空 GraphBuffer | `data save` | `[!] Graphbuffer is empty, nothing to save` |
| 加载文本样本 | `data load -f t1.txt` | `[+] loaded 3200 samples` |
| 时钟检测 | `data detectclock --ask` | `Auto-detected clock rate: 128, Best Starting Position: 81` |
| ASK/Manchester 解码 | `data rawdemod --am` | `ASK/Manchester - clock 128 - decoded bitstream`（位流与合成位有 1 bit 偏移） |
| 自动解调判定 | `data autodemod --dry-run -v` | `decision PSK / raw at clock 50.00`（合成基带被误判） |
| 卡型识别 | `lf search -1u` | **未识别出 EM4100**（`No known 125/134 kHz tags found`） |
| 协议专用解码 | `lf em 410x demod [--clk 32/64] [-i]` | **无输出**（未找到前导） |

**结论：**

1. **离线测试通路成立**：可以加载合成样本并运行 Client 的真实解码器。
2. **但"合成基带波形"不足以触发 EM4100 识别**——需要更接近真实 ASK 包络的样本，或直接使用真实卡的 `.pm3` 抓包。
3. 因此 **B 层的"卡型识别"用例目前只能标记为"通路已验证、样本待补"**。

### B.2 离线用例

| ID | 用例 | 期望 |
|---|---|---|
| B-O-001 | `data load` 不存在的文件 | 明确错误，不崩溃 |
| B-O-002 | `data load` 空文件 | 明确错误 |
| B-O-003 | `data print` 无 DemodBuffer | `[!] DemodBuffer is empty` |
| B-O-004 | `lf search -1`（空 buffer） | `No known 125/134 kHz tags found` |
| B-O-005 | `lf search -1` + 真实卡抓包 | 识别出卡型（**需要样本**） |
| B-O-006 | `lf em 410x demod` + 真实 EM4100 抓包 | 输出 `EM 410x ID …`（**需要样本**） |
| B-O-007 | 帮助导出 `-t` / `--fulltext` | 可解析出命令清单（本次已验证） |

### B.3 在线用例（需设备，当前固件下大部分会失败）

| ID | 用例 | 当前预期 |
|---|---|---|
| B-C-001 | `lf search`（无卡） | `unknown command`（固件无 LF） |
| B-C-002 | `lf search`（有卡） | 同上 |
| B-C-003 | 命令不存在 | `unknown command:: 0x0228` |
| B-C-004 | 端口被占用 | 客户端报错/超时 |
| B-C-005 | 命令超时（模拟） | GUI 自动重启 Client |
| B-C-006 | Client 崩溃 | GUI 捕获并提示重连 |
| B-C-007 | 输出格式变化（人为注入无关行） | 解析器不误判 |
| B-C-008 | 长命令（`lf tune`）运行中按 Stop | Client 重启，UI 复位 |
| B-C-009 | 长命令（`lf read -@`）运行中再发命令 | 被拒绝并提示"请先 Stop" |
| B-C-010 | 连续两条命令 | FIFO 顺序正确，无交错 |
| B-C-011 | 命令含 `;` | 被拒绝（注入防护） |
| B-C-012 | 非 ASCII 端口名 | 被拒绝 |

### B.4 超时矩阵（建议值）

| 命令 | 建议超时 | 理由 |
|---|---|---|
| `lf search` | 30 s | 弱耦合/坏卡 |
| `lf t55xx detect` | 15 s | |
| `lf t55xx chk` | **不自动执行** | 不可控 |
| `lf t55xx dump` | 60 s | 多块读取 |
| `lf t55xx wipe` | 30 s | |
| clone | 30 s | |
| `lf tune` / `lf read` / `lf sniff` | detached | 无固定超时 |

---

## C. 真实硬件测试（当前全部未验证）

> **前置条件：固件必须支持 LF。** 当前 `fullimage.elf` 不支持，因此下表**全部为"未验证"**。

### C.1 前置检查

| ID | 检查 | 方法 | 期望 |
|---|---|---|---|
| C-P-001 | 固件是否含 LF | `lf search`（无卡） | 不返回 `unknown command` |
| C-P-002 | FPGA LF 镜像 | `hw version` / `hw status` | 能切到 LF 模式 |
| C-P-003 | LF 天线谐振 | `lf tune` | 电压在合理范围（记录基线） |
| C-P-004 | 采样配置 | `hw status` | 有 `LF Sampling config` 段 |

### C.2 只读测试

| ID | 卡型 | 命令 | 期望 | 状态 |
|---|---|---|---|---|
| C-R-001 | 空场 | `lf search` | `No known 125/134 kHz tags found` | 未验证 |
| C-R-002 | EM4100 | `lf search` | `EM 410x ID <10hex>` | 未验证 |
| C-R-003 | HID Prox | `lf search` | `FC/CN` + `raw` | 未验证 |
| C-R-004 | AWID | `lf search` | `AWID …` | 未验证 |
| C-R-005 | Indala | `lf search` | `Indala …` | 未验证 |
| C-R-006 | T5577（空白） | `lf t55xx detect` | `Chip type T55x7` | 未验证 |
| C-R-007 | T5577（有数据） | `lf t55xx detect` + `lf search` | 检测到 + 识别出卡型 | 未验证 |
| C-R-008 | 多卡区分 | 依次放卡 | 每次卡型正确、不串型 | 未验证 |
| C-R-009 | 距离/角度 | 移动卡片 | 识别率记录 | 未验证 |
| C-R-010 | 复读稳定性 | 同一卡 10 次 | 10/10 一致 | 未验证 |

### C.3 写入测试（高风险，需单独批准）

| ID | 内容 | 前置 | 状态 |
|---|---|---|---|
| C-W-001 | EM4100 → T5577 | C-R-002 通过 | 未验证 |
| C-W-002 | HID → T5577（raw） | C-R-003 通过 | 未验证 |
| C-W-003 | AWID → T5577 | C-R-004 通过 | 未验证 |
| C-W-004 | 写后 `lf search` 复验 | — | 未验证 |
| C-W-005 | 密码保护 T5577 的擦除 + 写入 | 用户提供密码 | 未验证 |
| C-W-006 | 错误数据写入（负向） | — | 未验证 |

> **本阶段明确禁止执行 C.3。**

---

## D. 回归与持续验证

| 项 | 做法 |
|---|---|
| 解析回归 | 每个 Client/固件升级后，重新采集样本并跑 A-P-* |
| 命令回归 | 用 `-t` / `--fulltext` 导出帮助，与配置 JSON 对比命令是否存在 |
| 能力回归 | 每次连接设备后跑一次 L1 探测，记录到日志 |
| 发布前 | A 层全绿 + B-O 通路通过 + 真实硬件清单逐项勾选 |

---

## E. 测试数据管理

建议目录：

```
tests/lf/
├── fixtures/
│   ├── em4100_0F00112233.txt      # Client 原始输出
│   ├── hid_fc65_cn29334.txt
│   ├── no_card.txt
│   ├── firmware_unsupported.txt
│   └── ...
├── graph/
│   ├── em4100_real.pm3            # 真实抓包（需设备）
│   └── synthetic_em4100.txt
└── README.md                      # 每个样本的来源/版本/是否真实卡
```

**每个 fixture 必须标注：**

- 采集日期
- Client 版本（如 `v4.23346-suspect`）
- 固件版本
- 设备型号
- 是否真实卡（`real` / `synthetic`）
- 采集命令

---

## F. 通过标准（Definition of Done）

一个 LF 卡型只有同时满足以下条件，才可以在文档/UI 中标记为"支持"：

1. ✅ A 层解析用例通过（≥2 个变体）。
2. ✅ A 层命令构造用例通过。
3. ✅ B 层至少 1 个真实 Client 输出样本通过。
4. ✅ C 层真实卡识别通过（**若硬件不可用，必须继续标记"未验证"**）。
5. ✅ 写入类功能额外要求 C 层写入 + 复验通过。
6. ✅ 相关文案已进 i18n。
7. ✅ 风险门控与确认流程已实现。

---

## G. 本次审计已执行的测试记录

| 编号 | 内容 | 结果 |
|---|---|---|
| T-01 | 两项目 Git/版本核实 | ✅ 通过（见 README） |
| T-02 | Phosphor 与上游 blob 对比 | ✅ 3 个文件一致 |
| T-03 | Client 全量命令导出（`-t`/`--fulltext`） | ✅ 1,827 / 22,676 行 |
| T-04 | 固件字符串扫描 | ✅ 2,046 条，LF 关键字 0 命中 |
| T-05 | Client 字符串扫描 | ✅ 76,225 条，LF 命令齐全 |
| T-06 | 离线 GraphBuffer 加载 | ✅ 通过 |
| T-07 | 离线 ASK/Manchester 解调 | ✅ 部分（clock 128，位流有 1 bit 偏移） |
| T-08 | 离线 EM4100 识别 | ❌ 未成功（合成基带不足） |
| T-09 | 主项目构建/运行 | ⏸ 未执行（本阶段不构建） |
| T-10 | 真实 LF 硬件测试 | ⛔ 不可执行（固件无 LF） |
