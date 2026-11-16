# Stage 8 — FeliCa 交通卡集成与本地 Git 推送审核

日期：2026-11-16
分支：`upgrade/v0.4.1-polish`
远程状态：仅本地提交，**未推送到 GitHub**
Tag `v0.4.1` 与 GitHub Release 未改变。

## 1. 原项目架构

- Qt 5.15 / Qt Widgets / qmake / C++11，Windows x64。
- 唯一的 `ClientSession` 持有 `proxmark3.exe` 进程、FIFO 命令队列和 `rem` 完成哨兵。
- 页面通过 `executeCommand()` + `commandFinished` 异步取回输出，不阻塞 GUI 线程。
- 现有 MIFARE、LF、NTAG/UL、eMRTD、RawCommand、Settings 页面均未改写。

新增页面复用同一 `ClientSession`，没有新增串口或进程管理。

## 2. FeliCa 页面

新增一级标签 **FeliCa**（位于 eMRTD 之后），包含：

- 卡片信息：类型、IDm、PMm、IC、系统码、读取时间；
- 余额面板；
- 交易记录表格（可排序，支持选中查看原始字节）；
- 改札记录表格；
- 服务读取状态表；
- 原始数据表（service / block / hex）；
- Read Card、Cancel、Export JSON、Export CSV、Clear View；
- “JSON 中包含原始数据”选项。

中英文界面均已接入 Qt 翻译；中文 `.qm` 已重新生成。

## 3. PM3 能力验证

Client：Iceman `v4.23346-suspect`，PM3 GENERIC / AT91SAM7S256 / 256 KB。

已验证可用命令：

| 命令 | 结果 |
|---|---|
| `hf felica reader` | 可用，返回 IDm |
| `hf felica info` | 可用，返回 PMM / IC / 制造日期 / 系统 |
| `hf felica raw -s -c <frame>` | 可用，只读 |
| `hf felica scsvcode` | 可用，可枚举服务 |
| `hf felica dump --no-auth` | 可用（未作为运行时依赖） |

单次 Read Without Encryption 上限 15 块；16 块返回 `FF A2`。实现按 15 + 5 读取 20 条履历。

## 4. 支持范围

- CJRC 系统 `0x0003` 的 public 服务：`0x008B`、`0x090F`、`0x108F`、`0x10CB`。
- 只读：不写块、不改余额、不仿真、不克隆、不尝试密钥。
- 加密服务（发行信息、定期、自动充值等）不读取，界面显示为未读取。
- 未来 Octopus 通过 `TransitCardProfile` 接口、独立币种/解析器接入，通用模型不写死日元或日本站码。

## 5. 字段解析

- 日期：`byte4–5` 大端位域（年/月/日）。
- 物販时刻：`byte6–7`，5 bit 时 + 6 bit 分 + 5 bit 秒×2。
- 改札时刻：`0x108F` 的 `byte8–9` 按十六进制字面量（`0x16 0x21` → `16:21`）。
- 余额：取引履历 `byte10–11` 小端；属性信息 `byte11–12` 小端；两者偏移不同。
- 取引番号：取引履历 `byte13–14` 大端；属性信息 `byte14–15` 大端。
- 区域码：`byte15` 高 2 bit / 次 2 bit。
- 交易类型：`byte1 & 0x7F`。
- 金额：由相邻记录余额差推算；最旧一条标记为未知（`?`），不伪造 0。
- 交通记录没有卡内时刻时，仅在日期、运费、出站匹配改札记录时借用 `0x108F` 时刻，并以 `*` 标记为派生值。

## 6. 站码与歧义

- 主表：`resources/felica/station_codes.csv`（soltia48/suica-viewer，MIT，6,958 条，十六进制）。
- 交叉版本候选：`resources/felica/station_codes_legacy.csv`（仅 182 个冲突码的 211 行，SolidX/SuicaTools 未声明许可，仅用于展示候选）。
- 解析结果保留来源表、候选列表与歧义标志；界面显示主候选并在选中行展示全部候选。
- 示例 `2A/0D` 同时给出西大井、武蔵小杉、新川崎，不静默选择其一。
- POS ID 没有公开商户映射；只显示 `0xNNNN` 与终端类型，`0xC9/0xCA` 显示为规范外终端。

## 7. 界面

本机使用真实 PASMO 卡片截图校验（中文界面）：余额、20 条交易、3 条改札、服务状态、原始数据均正确显示。

截图包含真实卡片标识与个人出行记录，**未提交到仓库**。

## 8. 测试

| 套件 | 结果 |
|---|---|
| FeliCa（新增） | 17 passed |
| ntagmodel | 67 passed |
| amiibodata | 22 passed |
| lfparser | 59 passed |
| lfparser_stage2 | 126 passed |
| emrtd pytest | 47 passed |
| Git audit（新增） | 17 passed |

FeliCa 测试覆盖：20 条履历、余额、端序、15+5 分块、站码三候选、未知 POS、改札时刻、最后入场、截断/空数据、JSON/CSV 导出结构和 BOM。

全部测试使用合成记录；未提交真实卡片数据。

## 9. 实机结果

真实 PASMO（public 服务只读）：

- 余额：157 JPY；
- 交易履历：20 条；
- 改札记录：3 条，时刻 16:14 / 16:21 / 17:56；
- 最后入场记录：有；
- 读取服务：4/4 成功，原始块 26 个；
- 15 + 5 分块读取通过；
- 读取过程未写卡、未认证、未修改任何数据。

IDm 等卡片标识未写入本报告。

## 10. JSON / CSV 导出

- JSON：`schema_version 1.0`，包含 card、balance、transactions、gate_history、services、read_status、warnings；原始块仅在勾选时写入。
- CSV：交易与改札分别输出，UTF-8，带 BOM，便于 Excel 显示日文。
- 金额为整数；未知金额输出 `?`；站码候选与来源保留；不生成商户名。
- 序列化由独立 `FelicaExport` 实现并通过单元测试；保存对话框已在 GUI 中确认打开。

## 11. Octopus 扩展接口

- `TransitCardProfile` 定义 profileId、显示名、币种、服务列表和解析入口。
- 通用模型使用 `qint64` 最小货币单位与 `currency` 字符串。
- 卡片识别不依赖固定 UID；服务定义可扩展；站名/商户解析独立于导出。
- 未实现 Octopus 读取。

## 12. Git 推送审核

- `.git-push-policy.json`：路径、扩展名与内容规则。
- `.githooks/pre-push`：调用审核脚本；BLOCK/REVIEW 均拒绝推送。
- `scripts/git-push-audit.ps1`：范围审计、首次推送审计、内容扫描、退出码 0/1/2。
- `scripts/setup-git-hooks.ps1`：安全安装，不覆盖已有自定义 `core.hooksPath`，备份旧 hook。
- `scripts/audit-git-content.ps1`：手动审核入口。
- `docs/GIT_PUSH_POLICY.md`：策略与用法。
- 未知文件默认 REVIEW；审核只阻止未来推送，不清理历史。

## 13. 本地提交

```text
fcd87e6 feat(felica): add FeliCa transit reader, parser and page
55b824e test(felica): add parser, chunking and export tests
55ec092 security(git): add local push audit policy and hook
```

本报告单独提交。

## 14. 尚未完成

- 香港八达通读取（仅预留接口）。
- 加密服务、定期券、自动充值（需要发卡方密钥，不在范围）。
- 真实 eMRTD 证件、MIFARE Classic、LF 射频未在本阶段验证。
- GUI 导出保存对话框的自动化尚未完成；序列化本身已测试。
- `scripts/translate-*.py` 的本机绝对路径未清理（按 Stage 8 范围保留）。

## 15. 远程同步状态

本地分支领先 `origin/upgrade/v0.4.1-polish` 4 个提交（含本报告）。
未执行任何 Push；`v0.4.1` Tag 与 GitHub Release 资产未改变。
