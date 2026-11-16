# Stage 8.1 — FeliCa 记录数量与多语言详情

日期：2026-11-16  
基线：`7dbd45f`（Stage 9），分支 `upgrade/v0.4.1-polish`  
**未提交、未推送。**

## 1. 19 条限制的根因

读取链路：

`Read Card → hf felica reader/info → hf felica raw (0x008B/0x090F/0x108F/0x10CB) → 15+5 分块 → CjrcProfile::parse → UI`

- `0x090F` 在 CJRC 交通卡上是固定 **20 块**的环形缓冲。
- 读取端已完整读取 0–14、15–19，无分页遗漏。
- 解析端跳过全 0 块；卡片只要有一个空槽，就得到 **19 条有效记录**。
- 另存在一个冗余上限 `if (transactions.size() >= 20) break;`，已移除。
- 读取第 20 块（索引 20）会返回 `A8`（Block Number 越界），无法取得第 21 条。

结论：19 条来自**卡片数据（20 槽中有 1 个空槽）**，不是 GUI 截断。软件无法恢复已被环形缓冲覆盖的历史。

## 2. 修改前后差异

| 项目 | 修改前 | 修改后 |
|---|---|---|
| 解析上限 | 硬编码 20 条 | 按实际块数解析，无额外上限 |
| 重复记录 | 可能重复计入 | 相同 16 字节记录去重 |
| 空槽 | 跳过 | 跳过 |
| 原始块 | 保留 | 保留，未改写 |
| 详情语言 | 直接显示日文 | 按界面语言重新渲染 |
| 站名 | 仅日文 | 日文原名 + 可选中文/英文 |

## 3. 实测记录数量

| 卡片 | 读取状态 | 原始块 | 有效交易 | 说明 |
|---|---|---:|---:|---|
| 实体 PASMO | 4/4 服务成功 | 26 | **20** | 20 槽全部有数据 |
| Apple Wallet 虚拟 Suica | 未测试 | — | — | 当前无可用设备/卡，不能伪造结论 |

实体 PASMO 本次读取 20 条；此前显示 19 条是某次读取存在 1 个空槽。两者均未超过服务固定的 20 槽上限。

## 4. 修改文件

| 文件 | 理由 |
|---|---|
| `src/module/felica/cjrc_profile.cpp` | 移除 20 条硬上限；相同记录去重 |
| `src/module/felica/transit_types.h` | 新增 `LabelLanguage` 枚举 |
| `src/module/felica/station_table.h/.cpp` | 新增日文站名的中/英映射加载与查询 |
| `src/module/felica/code_tables.h/.cpp` | 终端/交易/支付/改札标签的中英文按代码查询 |
| `src/module/felicatab.h/.cpp` | 详情/表格按语言渲染；站名本地化 |
| `resources/felica/station_names_i18n.csv` | 新增站名映射数据 |
| `resources/felica/felica.qrc` | 打包站名映射资源 |
| `tests/felica/tst_felica.cpp` | 新增 4 项测试 |

## 5. 站名映射

- 文件：`resources/felica/station_names_i18n.csv`
- 字段：`ja,zh,en,source,verified`
- 当前 5,867 条：人工核实 18 条 + Wikidata 5,849 条；覆盖码表中 5,865/5,989 个名称。剩余 123 条是票种/企划券/一日券等非站名条目。
- 规则：`日文原名（中文名）` / `日文原名 (English Name)`；无映射时只显示日文原名。
- 历史站名/多候选仍按来源表分别显示（`suica-viewer` / `solidx-legacy`），不合并。
- 未覆盖的站名、公司名、线路名仍显示日文原文；这是当前已知限制。

示例：

```text
中文：鐘ケ淵（钟渊）
英文：鐘ケ淵 (Kanegafuchi)
```

## 6. 字段展示

| 字段 | 中文 | English |
|---|---|---|
| 终端 0x16 | 自动检票机 | Automatic ticket gate |
| 交易 0x01 | 自动检票机出场（支付车费） | Automatic gate exit (fare payment) |
| 支付 0x00 | 现金 / 无 | Cash / None |
| 改札 0x02 | 入场 / 出场 | Entry / Exit |

未映射的代码使用本地化的“未知…”文本，不再混入日文。原始 16 字节保持原样，JSON/CSV 仍使用原有日文字段，兼容旧数据。

## 7. 示例记录回归

输入：

```text
16 01 00 02 34 C1 9D 05 9D 02 FB 07 00 00 30 00
```

自动测试确认：

- `terminal=0x16`、`transaction=0x01`、`payment=0x00`、`gate=0x02`；
- 入口候选包含 `鐘ケ淵`；
- 出口候选包含 `とうきょうスカイツリー` 与 `業平橋`；
- 来源同时包含 `suica-viewer` 与 `solidx-legacy`；
- 原始字节逐字节不变；
- 中文/英文站名映射正确。

## 8. 测试

| 套件 | 结果 |
|---|---|
| FeliCa | **21 passed**（原 17 + 4） |
| ntagmodel | 67 passed |
| amiibodata | 22 passed |
| lfparser | 59 passed |
| lfparser_stage2 | 126 passed |
| eMRTD pytest | 47 passed |
| Git audit | 17 passed |

## 9. Clean Build

```text
Qt 5.15.19 / MinGW GCC 16.2.0
build/qt515-mingw64/release/release/Proxmark3GUI.exe
2,279,424 bytes, warnings 0, errors 0
```

## 10. 未解决 / 限制

- Apple Wallet 虚拟 Suica 未测试；无法确认其记录条数。
- 实体 PASMO 固定 20 槽，无法读取更多；这不是软件缺陷。
- Wikidata 同步在中/英标签缺失时回退日文原名（zh 缺 25 条、en 缺 6 条）。
- 公司名/线路名仍为日文。
- 稀有终端/交易代码使用本地化通用文本，不保留日文专名。

## 11. Git 状态

```text
HEAD:    7dbd45f
分支:    upgrade/v0.4.1-polish (ahead 7, 未 Push)
工作区:  本阶段修改未提交
Tag:     v0.4.1 -> 96c8ab2（未移动）
Release: 未改变
```

按任务要求，本阶段只做本地修改与验证，不提交、不推送。
