# NTAG / Amiibo 协同升级报告

**日期：** 2026-11-14  
**基线：** `upgrade/v0.4.1-polish` @ `245b184`（含未提交的 LF/异步改动）  
**范围：** NTAG 型号识别、完整读取、内存浏览器、NDEF；Amiibo 功能保留与状态一致性

---

## 1. 审计结果

| 项目 | 现状 |
|---|---|
| NTAG 页面 | `src/module/ntagtab.{h,cpp}`：`hf mfu info` + 逐页 `rdbl`/`wrbl`，4 列表格 |
| Amiibo 页面 | `src/module/amiibopage.{h,cpp}`：库/详情/操作/在线目录；写/复读已异步化（上一阶段） |
| 通信层 | 全部经 `Util::execCMDWithOutput` → `ClientSession` FIFO；无第二 Client |
| 型号识别 | 仅解析 `hf mfu info` 的 `TYPE:` 字符串；克隆卡显示 `NTAG UNKNOWN` 时无法识别 |
| 页数 | `pagesForType()` 与 `Total memory` 正则；未知型号为 0 |
| 完整读取 | 逐页 `rdbl`；客户端在未知型号下把 rdbl 限制到第 41 页 |
| NDEF | 无 |

问题清单：

1. 克隆 NTAG21x 的 `TYPE` 为 `NTAG UNKNOWN`，页面无法得到页数/布局。
2. `rdbl -b 42` 返回 `Max block is 41/0x29`，逐页读取丢失尾部页。
3. 未区分“未读取/读取失败/不可用”。
4. 无 CC/TLV/NDEF 展示，无 GET_VERSION。

---

## 2. Max block 41 的根因

来自实际 Client 行为与分析文档（`PM3-NTAG-三卡读取分析-20261114.md`）：

- `hf mfu rdbl` 在**型号未知**时把可读块上限设为 **41**（客户端保守默认），不是硬件上限；
- `hf mfu dump` 支持 `-p <起始页> -q <页数>`，可读取超过 41 页；dump 文件为 **56 字节头 + 0x38 起每页 4 字节**，头部 `0x0B` 为最大页号；
- 因此修复方式是：**先识别型号 → 计算页数 → 用 dump 分块读取**，而不是把 231 硬编码，也不是把 rdbl 上限改大。

---

## 3. 实现

### 3.1 纯逻辑模块 `src/module/ntag/ntagmodel.{h,cpp}`

- `parseVersion()`：兼容带/不带前导 `0x00` 的 GET_VERSION 响应；解析 vendor/type/subtype/major/minor/storage/protocol。
- `identify()`：
  - Storage Size `0x0F/0x11/0x13` → NTAG213/215/216；
  - CC size `0x12/0x3E/0x6D` 作为辅助/回退；
  - 客户端 `TYPE` 字符串作为最后回退；
  - 置信度：GET_VERSION 与 CC 一致 → **Exact**；仅 CC/类型 → **Probable**；无证据 → **Unknown**；冲突时给出警告文本。
- `capacityFor()`：页数、用户区、动态锁页、CFG0/CFG1/PWD/PACK 按型号生成。
- `parseCC()`：magic/version/size、NDEF 容量（size×8）、读写/多块读位。
- `parseDump()`：校验 56 字节头、4 字节对齐、头部最大页与文件页数一致性。
- `parseNdef()`：TLV（NULL/终止/0x03，含 3 字节长度）→ NDEF 记录（MB/ME/CF/SR/IL/TNF、类型长度、载荷长度、ID）→ Text（UTF‑8/UTF‑16BE）、URI（前缀表）、MIME、External；失败保留错误与原始数据。

### 3.2 NTAG 页面

- **Detect**：`hf mfu info` + `hf 14a apdu -k -d 6000000000`（GET_VERSION）+ CC 提取 → 型号/置信度/页数/容量；
- 新增信息行：Model、GET_VERSION、CC、Capacity；
- 表格改为 6 列：Page / Region / Data (hex) / ASCII / Access / Status；
- **Read All**：按 32 页分块 `hf mfu dump -p .. -q .. -f ..`，逐块更新“正在读取第 x..y 页”，解析后填表；失败页标记 `Read failed`；无型号时回退逐页 rdbl 并提示“客户端 rdbl 上限为第 41 页”；
- 新增 **NDEF** 标签页：CC 摘要 + TLV/NDEF 解析结果，失败显示原因与原始 hex；
- 原有 Read Selected / Read Page / Write Page / Write Selected / Verify Page 保持不变（列索引已同步更新）。

### 3.3 Amiibo 页面

未改动数据模型与操作流程；逐项保留：

导入、导出、Hex 预览、Detect、Read、Write、Verify、写入确认、写后复读、模拟、示例数据、在线目录。  
上一阶段的“写入/复读异步 + 取消操作”保留。

未新增 UID 冒充、Lock、OTP、PWD 修改。

### 3.4 统一规范（本阶段实际落地部分）

- 两页均使用“状态标签 + 表格 + 只读文本视图”的相同交互模式；
- 状态值统一为：未读取 / 已读取 / 读取失败 / 不可用；
- Hex 展示统一为空格分隔大写、每页 4 字节；
- 新增文案全部进入 `zh_CN.ts`。

未引入新的 UI 框架或公共抽象层。

---

## 4. Amiibo 页 224 字节上限的根因与修复（2026-11-14 追加）

用户反馈：Amiibo 页最多只能读取/写入 224 字节。

根因：

1. `AmiiboPage::readBackInto()` 使用 `hf mfu dump -f <file>`，**没有 `-q`**。Client 在型号未知时默认只读约 42 页 → 文件 `56 + 42×4 = 224` 字节，与现象完全一致。
2. 写入/模拟把**无头的 540 字节镜像**直接交给 `hf mfu restore -f` / `eload -f`。这两个命令的帮助明确要求 **dump file（bin/eml/json）**，即 56 字节头 + 页数据；无头文件会被按错误偏移解析，导致写入耗时 140 s 且复读失败。

修复（`src/module/amiibopage.cpp`）：

- 读取：先 `hf 14a apdu -k -d 6000000000` 取 GET_VERSION，经 `Ntag::identify()` 得到页数（Amiibo 默认 135）；执行 `hf mfu dump -p 0 -q <pages> -f <file>`；用 `Ntag::parseDump()` 解析并**去掉 56 字节头**，得到 540 字节镜像。
- 写入 / 模拟：新增 `buildTagDump()`，生成 56 字节头（复制 GET_VERSION、`0x0B = pages-1`）+ 540 字节页数据，再交给 `restore` / `eload`。
- 未启用 `-s` / `-e` / `-r` 等特殊写入选项（不写 UID、不写 version/signature、不改密码）。

验证：编译通过；Client 帮助输出确认 restore/eload 接受 dump 文件；实机读取时读卡区无标签，未能完成 540 字节实测。

---

## 5. Amiibo 容量与复读修复（2026-11-14 第二次追加）

用户反馈：NTAG216 也只能读到 540 字节；写入后自动复读只有 224 字节。

根因：

1. `readBackInto()` 在 GET_VERSION 失败时把页数固定为 135，并在拼接时用 `qMin(pages, dump.pages)` **截断**，因此 216 的 231 页被砍成 135 页（540 B）。
2. 写入后的异步复读 `startAsyncVerify()` 仍是旧命令 `hf mfu dump -f <file>`，**没有 `-p/-q`** → 客户端回到默认 42 页（224 B）；且 `finishAsyncVerify()` 直接把整个文件当页数据，未解析 56 字节头。

修复：

| 位置 | 改动 |
|---|---|
| `detectTagPages()` | GET_VERSION → `Ntag::identify`；失败则用 `hf mfu info` 的 TYPE/Total memory；再失败默认 135 |
| `readBackInto()` | 按检测页数 `dump -p 0 -q <pages>`，**保留全部返回页**（180/540/924），失败重试一次 |
| `startAsyncVerify()` | 使用同一页数与 `-p/-q` |
| `finishAsyncVerify()` | `Ntag::parseDump()` 去头；Stage 2 起只比对可写用户页（4..userLast），UID/CC/锁/配置不参与，避免换卡 UID 不同导致误报 |
| `writeTag()` | 写前确认标签容量 ≥ 镜像；`buildTagDump()` 生成 56 字节头 + 页数据 |
| `formatOf()` / `importFile()` | 支持 180（213）/924（216）整卡镜像 |

---

## 6. Stage 2：Amiibo 原始 BIN 模拟修复

### 6.1 根因

本轮读/写架构调整后，模拟入口不再使用导入文件的原始字节：

1. `startEmulation()` 直接拿 `entry.data`（已被结构化流程当成“页负载”）重新拼接 56 字节头。只要导入文件本身是 dump（56 字节头 + 页），就会被**二次包头**；若导入的是 540 字节镜像，虽然结果碰巧正确，但“原始字节”已经不再被保留。
2. `importFile()` 只接受 180/540/924/1024/2048 长度，**拒绝合法的 dump 文件**（596/236/980 字节），用户只能先手工去头，进一步丢失原始文件。
3. 数据模型只有 `data` 一个字段，无法区分“原始 BIN”与“页负载”，任何解析/展示都可能覆盖原始数据。

### 6.2 双数据模式

新增纯逻辑模块 `src/module/amiibo/amiibodata.{h,cpp}`：

| 模式 | 判定 | 保存内容 | 模拟输入 |
|---|---|---|---|
| `RawImage` | 180/540/924/1024/2048 字节且不符合 dump 头 | `raw == payload` | `makeDump(payload)`：只加 56 字节头，负载逐字节不变 |
| `DumpFile` | `0x0B + 1` 页且 `size == 56 + pages*4` | `raw` 完整文件、`payload` 原始顺序页、`header` | **原样使用 `raw`**，不重新包头 |
| `Invalid` | 其他长度/截断 | 仅返回错误 | 拒绝导入 |

`AmiiboEntry` 同时保存：

- `data`：页负载（用于写卡、校验、十六进制预览）；
- `raw`：导入文件完整字节；
- `rawHash`：`raw` 的 SHA-256；
- `kind` / `pages` / `header`。

解析、页面切换、十六进制预览只读 `data`，不会写回 `raw`。保存时若存在 `raw` 则原样导出，保证“导入 → 保存”字节一致。

### 6.3 模拟流程

```
导入文件
  → Amiibo::classify()
      ├─ DumpFile：raw 原样保存，payload 仅用于显示/写入
      └─ RawImage：raw/payload 相同
  → 选择条目
  → startEmulation()
      ├─ DumpFile：simFile = raw（原始字节，零转换）
      └─ RawImage：simFile = makeDump(payload)（只加头，负载不变）
  → 校验 classify(simFile).payload == entry.data
  → 校验 rawHash（DumpFile）
  → 写入临时文件 → hf mfu eload -f → hf mfu sim -t 7
```

写卡使用同一份 `data` 生成 dump（`dumpForEntry()`），并再次验证 `classify(dump).payload == entry.data`；不一致时拒绝写入。模拟/写入前都会记录 SHA-256 与 `payload-match=yes` 日志。

### 6.4 写入校验（Stage 2 修正）

`hf mfu restore` 在未启用 `-s` / `-e` / `-r` 时不会写 UID/BCC/CC/锁/配置/PWD/PACK。因此换卡写入后，UID 必然与源镜像不同，旧版“540 字节全量前缀比对”会误报失败。

现在 `finishAsyncVerify()` 按镜像长度选择 213/215/216 布局，只比较 `4..userLast` 的可写用户页；UID/CC/锁/配置/安全页不参与失败判定。结果仍保留页号、预期值、实际值与失败数量，无法读取的用户页会标记为“不可校验”，不会当作通过。

### 6.5 自动化测试

`tests/amiibodata`（20 项）覆盖：

- 180/540/924 原始镜像分类、dump 文件分类；
- **不会把 540 字节原始镜像误判为 dump**（偏移 `0x0B` 恰为 `0xE1`，或长度上恰好符合 121 页 dump）；
- 空文件、未知长度、截断 dump 拒绝；
- SHA-256 稳定性与单比特变化检测；
- `makeDump()` 不截断、不补零、保留版本、拒绝非 4 字节对齐/超过 256 页；
- 原始 BIN 与 dump 两条路径的哈希闭环；
- 重复解析不改变原始字节。

---

## 7. Stage 2：NTAG 一键清空

### 7.1 页面分类与清空范围

`Ntag::classifyPage()` 按型号布局分类（不按页码猜测）：

| 页码 | 类型 | 清空策略 |
|---|---|---|
| 0 | Manufacturer | 保留 |
| 1 | UID | 保留 |
| 2 | Lock（静态锁） | 保留 |
| 3 | Config（CC） | 保留 |
| `lockPage` | Lock（动态锁） | 保留 |
| `cfg0/cfg1` | Config | 保留 |
| `pwd/pack` | Security | 保留 |
| 4..`userLast` | Writable | 仅清空当前非 0 的页 |
| 其他 | NotApplicable | 不处理 |

`Ntag::planErase()` 的拒绝条件：

- 型号未知、布局未知、`Confidence::Unknown`；
- 未读到完整页表（任一用户页缺失）；
- Capability Container 明确报告只读。

清空前强制完整读取；只对“当前非 0”的可写用户页写入 `00 00 00 00`，已经是 0 的页跳过以减少磨损。UID/CC/OTP/锁/配置/PWD/PACK **从不写入**。

### 7.2 确认与结果

确认框显示型号、UID、待清空页范围与页数、跳过页数、保留页、锁定位警告、不可逆风险；默认按钮为“否”。

执行后重新读取并逐页判断：

| 结果 | 条件 |
|---|---|
| 完全清空 | 全部目标页读回 `00 00 00 00` |
| 部分清空 | 部分目标页清空，部分失败/不可读 |
| 清空失败 | 目标页均未清空 |
| 无法确认 | 无法重新读取 |

写入失败的页面会单独列出，并标注“已锁定或写保护”，不会尝试绕过。报告明确说明：软件**不保证**把被锁定的 NTAG 恢复为出厂状态。

---

## 8. Stage 2：NTAG 写入校验

### 8.1 页面分类

`Ntag::classifyPage()` 输出：`Writable / ReadOnly / Manufacturer / UID / Lock / Config / Security / NotApplicable / Unknown`，全部基于已识别型号与内存布局。

### 8.2 校验规则

`Ntag::verifyWrite()` 只接收**本次写入的目标页**及写入值：

- Writable 且已读取：逐页比较，一致 `Match`，不一致 `Mismatch`；
- Writable 但未读取：`Unchecked`，不会算作通过；
- Manufacturer/UID/Lock/Config/Security：只读页面，读到则 `Preserved`，读不到则 `NotApplicable`，**不计入失败**；
- 不在布局内的页：`NotApplicable`。

状态判定：

| 状态 | 条件 |
|---|---|
| Passed | 所有目标可写页一致，且无不可校验/只读例外 |
| Passed with Exceptions | 所有目标可写页一致，但存在只读/不可校验页 |
| Failed | 至少一个目标可写页不一致 |
| Incomplete | 有目标可写页未读到，或没有可校验目标 |

### 8.3 展示

`QMessageBox` 摘要 + 可展开明细表：

```
Page  Class     Expected   Actual     Result
4     Writable  D1 01 55.. D1 01 55.. match
5     Writable  ...        ...        MISMATCH
0     Manufacturer --     04 11 22.. preserved
```

明细保留实际读到的原始值和失败原因；不会为了“通过”而修改预期数据，也不会把未读页标记为通过。单页“Verify Page”和写入后的自动复读都走同一套逻辑。

---

## 9. Stage 2：快捷启动（Quick Launch）

### 9.1 页面

NTAG 页面新增第三个子页“快捷启动（NDEF URI）”：

- 启动类型：网页链接、电话、短信、邮箱、自定义 URI、Wi-Fi（明确不支持）；
- 动态输入框：URL/电话/邮箱/URI、短信正文、邮件主题/正文；
- 实时预览：最终 URI、NDEF 消息、所需空间（TLV + 终止符）、可用容量、占用比例、是否可容纳、十六进制消息与按页补齐的 TLV；
- 写入按钮仅在“格式有效 + 容量足够 + 已识别可写 CC”时启用。

### 9.2 NDEF 编码

纯逻辑模块 `src/module/ntag/ndefbuilder.{h,cpp}`：

- URI Record：`TNF=1`、`type='U'`、URI Identifier Code 使用标准前缀表（如 `https://` = 0x04、`tel:` = 0x05、`mailto:` = 0x06、`sms:` 无标准前缀 → 0x00）；
- 短记录（payload ≤ 255）：`MB=ME=SR=1`，1 字节长度；
- 长记录（payload > 255）：`SR=0`，4 字节大端长度；
- TLV：`03 <len> <message> FE`，长度 < 255 用 1 字节，否则 `FF + 2 字节大端`；
- 写入区域按 4 字节页补齐，只写覆盖 TLV 的用户页；CC、UID、锁、OTP、配置、PWD/PACK 不写入。

短信正文/邮件主题与正文使用 percent-encoding；`tel:` 只打开拨号界面，`sms:`/`mailto:` 只打开对应应用，不承诺自动发送；浏览器是否自动打开取决于手机设置。

### 9.3 容量

可用容量优先使用 CC 的 NDEF 容量；无有效 CC 时拒绝写入。页面实时显示：

- NTAG213：144 B（CC 0x12）
- NTAG215：496 B（CC 0x3E）
- NTAG216：872 B（CC 0x6D）

所需空间包含 TLV 头与终止符；超出容量时写入按钮禁用，并在预览中说明。

### 9.4 写入与验证

写前：检测/识别/完整读取、检查 CC 可写、检查锁定位（静态/动态任一非 0 即拒绝并说明原因）、检查用户页范围、确认框显示型号/UID/URI/页范围/兼容性提示。

写入：仅写发生变化的目标页；每页写入后立即读回；任一步失败即停止并报告。

写后：重新读取目标页，独立解析 NDEF，比较 URI 与 TLV 原始字节；结果分为“已验证 / 校验失败 / 不完整”，并明确 CC 与系统页未修改。

---

## 10. 测试

| 项目 | 结果 |
|---|---|
| `tests/ntagmodel`（49 项） | **49 passed / 0 failed** |
| `tests/amiibodata`（20 项） | **20 passed / 0 failed** |
| 覆盖 | GET_VERSION、213/215/216、CC、dump、区域、NDEF 解析；页面分类、清空计划、锁定页识别、校验四态、只读页例外；NDEF URL/tel/sms/mailto/自定义/长记录/Wi-Fi 拒绝；213/215/216 容量边界 |
| 主程序构建 | rc=0，无新增警告 |
| 翻译 | 928 finished / 0 unfinished（中英） |
| UI 冒烟 | 快捷启动可生成 `https://example.com/reset?id=42` 的正确 NDEF；无标签时写入禁用；Amiibo 导入 540 原始镜像/596 dump 后模式、长度、SHA-256 正确 |
| 真实标签 | **未验证**：测试时读卡区无标签（`hf mfu info` 与 dump 均 64 ms 返回） |

---

## 11. 已知限制

1. 真实 NTAG 清空、写入校验、快捷启动写入未在本次会话中用硬件验证（无标签）。
2. 锁定位只做“是否已设置”的判断；不推断每个锁定位对应的具体页，写入失败的页按“已锁定/写保护”报告，不绕过。
3. 快捷启动不实现 Wi-Fi NDEF；不会生成未经验证的猜测格式。
4. `tel:`/`sms:`/`mailto:` 的行为由手机决定，GUI 只描述“打开对应界面”。
5. NDEF 容量使用 CC 值（496/872 等）；与厂商标称用户区（504/888）不同，采用更保守的 CC 值。
6. 被清空的页面不会自动重建 CC 或 NDEF TLV；CC 保持原值，用户区为空白。

---

## 12. Stage 3：开工审计（2026-11-14）

- Git CLI 在当前环境不可用（`git` 不在 PATH），因此无法重新读取分支/HEAD；未执行任何 `git` 命令，未重置工作区、未提交、未移动标签。
- 以上一阶段源码为准重新审计：`AmiiboPage` 仍保留 Detect/Read/Write/Verify/异步写校验/在线目录；`NtagTab` 仍使用 6 列只读文本网格和逐页 `rdbl/wrbl`。
- 审计发现上一阶段 Amiibo 模拟仍会走 `dumpForEntry()` 生成临时 dump；这不是“原始文件直通”。
- 审计 Client 源码 `client/src/cmdhfmf.c`、`cmdhfmfu.c`、`fileutils.c`：
  - `hf mfu eload` 调用 `pm3_load_dump()` 后执行 `convert_mfu_dump_format()`；
  - Client 自身识别 **new / old / plain** 三种 MFU dump；plain 540 字节镜像在 BCC0/BCC1 正确时可直接加载；
  - 不需要 GUI 加 56 字节头。
- `hf mfu rdbl/wrbl` 的 `--force` 只跳过 Client 的地址上限检查（见第 13 节）。

---

## 13. Stage 3：Amiibo 原始 BIN 直通

### 13.1 根因

本轮不再让 GUI 生成或改写模拟文件。旧实现的 `dumpForEntry()` 会对无头镜像补 56 字节头，再交给 `eload`；这与 Client 已支持的 plain 直通路径重复，且当分类/长度判断不一致时就会表现为“无头文件无法模拟”。同时旧 Amiibo 页把标签读写、异步复读、在线目录都混在同一个页面，数据来源不唯一。

真实证据（直接运行 Client）：

```
hf mfu eload -f E:/.../amiibo plain 540.bin
[+] Loaded 540 bytes from binary file ...
[=] Detected plain mfu dump format
[+] Plain mfu dump format was converted to 135 blocks
[?] Hint: Try `hf mfu sim -t 7` to simulate an Amiibo.
```

Client 自己完成格式识别；GUI 不得再次包头。

### 13.2 调用链

```
导入文件 → QFile::readAll() → rawBytes + SHA-256 + 原始路径
→ Start:
   重新读取原文件
   → 大小与 SHA-256 必须与导入时一致，否则拒绝
   → clientPathFor():
       ASCII 路径：直接使用原文件
       非 ASCII 路径：复制完全相同的字节到 ASCII 临时文件并校验 SHA-256
   → hf mfu eload -f "<path>"（Client 自行识别 plain/new/old）
   → 检查 Client 输出（Loaded/Detected；任何 fail/error 都中止）
   → hf mfu sim -t 7（detached）
→ Stop:
   emit stopRequested() → ClientSession 重启 Client，结束模拟
```

不执行：二次包头、去头、补头、重排、截断、补零、结构化重建。

### 13.3 实际传递给 Client 的文件

| 测试 | 原始路径 | 大小 | SHA-256 | 结果 |
|---|---|---:|---|---|
| 有效 plain 镜像 | `amiibo plain 540.bin` | 540 | `f0a8ac48677831bb6692a01ef58cc2f9fae7688828d5fc60ffa42345f54ae59e` | `Detected plain mfu dump format`，模拟启动/停止成功 |
| 无效无头镜像 | `bad-headerless.bin` | 540 | `f2aa60be482e3bf774563c877fd1736d673a7ff26ad6967c8f595e86b84f7e2a` | Client 返回 `Failed to detected mfu dump format`，GUI 原样显示，未启动模拟 |
| 中文/空格路径 | `测试 Amiibo 540.bin` | 540 | 同上 540 镜像 | Windows Client 以窄字符串打开路径失败；GUI 会复制**逐字节相同**的临时文件再调用 `eload` |

中文路径的 GUI 文件对话框自动化未能成功输入中文，因此该分支的**端到端实机调用未完成验证**；复制逻辑与哈希一致性由 `tests/amiibodata` 覆盖。

### 13.4 移除的 Amiibo 功能

- Detect / Read / Write / Verify / 异步写校验 / Cancel；
- 540 字节结构镜像编辑、库列表、在线目录、图片、导入后保存；
- 所有 `hf mfu restore`、`eload` 之外的标签操作入口。

保留：导入、路径/大小/SHA-256、输入检查、启动/停止模拟、Client 原始输出。

### 13.5 Amiibo 自动化测试

`tests/amiibodata` 增加到 **22 项**：尺寸/格式分类、原始哈希、无损 dump 包装、错误长度、540 镜像不误判、按字节复制、非 ASCII 路径检测、Client 文件复制哈希一致。

---

## 14. Stage 3：NTAG 通用 Hex 内存浏览器

### 14.1 数据模型

新增 `src/module/ntag/ntagmemory.{h,cpp}`：

- `MemoryPage`：页码、原始 4 字节、编辑后 4 字节、读取状态、页面类型、可写性、写入状态、校验状态、错误。
- `MemoryImage`：按识别出的 `Capability` 建立页表；`setPageData()` 保存原始快照；`editByte()`/`setBytes()` 只修改编辑副本；`dirtyPages()`、`revertPage()`、`revertAll()`；写入前先验证整段范围，任一只读/未读页都会拒绝整次修改。
- `parseHexBytes()`：支持 `E1 10 3E 00`、`E1103E00`、逗号/冒号/连字符分隔、`0x` 前缀；拒绝奇数位和非法字符。
- `buildDump()`：导出 56 字节头 + 页数据；不接受非 4 字节页或超过 256 页。

### 14.2 页面布局

- 顶部：连接/检测状态、TYPE、型号/置信度、UID、ATQA、SAK、总页数（4 字节/页）、已读取页、可写页、锁定位。
- 中部：9 列 Hex 表格（Page、B0-B3、ASCII、页面类型、访问、状态），右侧“选中页”详情。
- 工具栏：Detect / Read All / Refresh / Read Selected / Read Page / Undo / Redo / Discard Edits / Export / Import / Write Page / Write Selected / Write Dirty / Verify Page / Verify Written / Erase。
- 状态图例以文字列出：已读取、已修改、已写入、已验证、读取失败、未读取；颜色只作为辅助。

### 14.3 编辑交互

- 双击字节单元格输入两位十六进制；非法输入立即拒绝并恢复。
- Ctrl+C 复制选中字节；Ctrl+V 从当前字节开始粘贴，可跨页；粘贴前先验证全部目标页。
- Ctrl+Z / Ctrl+Y 撤销重做（保存受影响页的前后快照）。
- 读取/刷新/清空/快捷启动前若存在未提交修改，必须确认放弃。
- 只读页（UID/CC/Lock/Config/PWD/PACK）不可编辑；PWD/PACK 默认显示 `**`，可勾选“显示 PWD/PACK 数据”。
- 写入只使用明确的目标页；写入前确认，写入后逐页复读并生成四态报告。

---

## 15. Stage 3：NTAG216 真实硬件测试

### 15.1 标签身份与基准

- UID：`53 24 60 F3 53 00 01`
- ATQA：`00 44`；SAK：`00`
- `hf mfu info`：TYPE `NTAG UNKNOWN`（兼容芯片）
- dump 头版本：`00 53 04 02 01 00 13 03`（storage `0x13` = NTAG216）
- CC：`E1 10 6D 00`（872 B NDEF）
- 页数：231；用户页 4..225（222 页，888 B）
- 静态锁：`00 00`；动态锁：`00 00 00`
- 基准镜像：980 字节，SHA-256 `c8c489b4230ea499699547e6452d50305f87af4df864ce4deb514e341d350283`
- 记录目录：`E:\Opencode_project\PM3-HW-Test-Stage3\`（baseline216.bin/json、各阶段 dump、fulltest.log）

GET_VERSION（APDU `60`）在该兼容芯片上无返回，型号由 dump 头 + CC 判定为 NTAG216（Probable）。

### 15.2 写入失败根因

Client 的 `hf mfu wrbl/rdbl` 会根据自动识别的标签类型计算 `maxblockno`。该标签被识别为 `NTAG UNKNOWN`，Client 上限为 **41**：

```
hf mfu rdbl -b 42
[!] block number to large. Max block is 41/0x29
```

因此 GUI 之前对 42..225 的所有写入在**进入射频命令之前**就被 Client 拒绝，看起来像“部分页面无法写入”。这不是标签质量或时序问题。

修复：当目标页 > 41 时，GUI 在已识别布局有效且页在 `userLast` 内的前提下追加 `--force`：

```
hf mfu rdbl -b 42 --force      → 42/0x2A | F3 B0 09 C1
hf mfu wrbl -b 42 -d A1B2C3D4 --force → Write ( ok )
```

`--force` 只跳过 Client 的地址上限检查，不改变实际页地址，也不绕过锁定/OTP/配置。

### 15.3 真实写入结果

| 测试 | 结果 |
|---|---|
| 单页 42 无 `--force` | 失败：Client 上限 41 |
| 单页 42 带 `--force` | 写入并复读 `A1 B2 C3 D4` 成功 |
| 页面 4 / 42 / 100 / 225 | 全部写入、复读一致 |
| GUI 四页编辑 + Write Dirty | 校验 **Passed 4/4** |
| 全用户区 4..225 全零（222 页） | 全部成功，dump 确认全零 |
| 16 个代表页 × 5 种模式 × 3 轮（240 页写入） | 全部成功，dump 与最后一轮一致 |
| 恢复基准用户区（222 页） | 全部成功 |
| 恢复后全片 dump | 与基准**逐字节相同**，SHA-256 一致 |
| 写入/复读错误/超时 | 0 |

合计 684 次 `wrbl`（全零 222 + 模式 240 + 恢复 222），无失败。锁定/配置页 226..230 在所有阶段保持不变。

### 15.4 写入策略与恢复

- 只写 `dirty` 且已读取、可写的页面；未修改页不写。
- 每页写入后立即 `rdbl --force` 复读；失败自动重试一次（40 ms），仍失败则**停止后续页面**，保留之前成功页并报告。
- 成功后再次全量读取，按页比较写入前快照，输出 Passed / Passed with Exceptions / Failed / Incomplete。
- 实测失败原因为 Client 地址上限，修复后未再出现写入失败；重试/停止逻辑在本次硬件测试中未被触发。

---

## 16. Stage 3：Quick Launch 实验性

- 标题改为“快捷启动（NDEF URI）— 实验性”，页面顶部红色提示。
- 明确：真实手机兼容性未充分验证、不是稳定功能、暂停新增 NDEF 类型、只修复崩溃/影响其他页面的严重问题。
- 现有 NDEF 编码与写入代码保留，不作为稳定功能宣传。

---

## 17. Stage 3：测试与构建

| 项目 | 结果 |
|---|---|
| `tests/ntagmodel` | **59 passed / 0 failed** |
| `tests/amiibodata` | **22 passed / 0 failed** |
| `tests/lfparser` | 59 passed / 0 failed |
| `tests/lfparser_stage2` | 126 passed / 0 failed |
| 主程序 Clean Build | **rc=0，0 warning，0 error** |
| 翻译 | **945 finished / 0 unfinished** |
| 真实 NTAG216 | 识别、全量读取、GUI 编辑/写入/校验、全用户区写入、模式写入、恢复全部完成 |
| Amiibo 有效 plain 镜像 | eload + sim 启动/停止成功 |
| Amiibo 无效无头镜像 | Client 原始错误原样显示，未模拟 |

---

## 18. Stage 3：未验证事项

1. 中文路径的 GUI 文件对话框自动化未成功输入中文；GUI 复制到 ASCII 临时文件的端到端实机调用未完成验证（复制/哈希逻辑有单元测试）。
2. 写失败重试与“失败后停止”未在硬件上触发（修复后无失败）；仅有代码路径和单元模型覆盖。
3. Quick Launch 的手机触碰行为未验证，仍为实验性。
4. NTAG213/215 未在本轮实机测试；`--force` 根因与 216 相同，但未逐型号复测。
5. GET_VERSION 在该兼容芯片上不可用，型号判定为 Probable。


