# Stage 8.2 — Octopus 只读余额支持

日期：2026-11-16，基于 `7dbd45f` + Stage 8.1 未提交修改。  
未提交、未推送。

## 1. 修改前架构

- FeliCa 读取/解析以 CJRC（`0x0003`，Suica/PASMO）为主。
- `TransitCardProfile` 存在通用接口，但没有 Octopus 实现。
- 余额模型只含单一金额；UI 只显示 `¥`。
- 读取器只针对 CJRC 的 4 个服务，遇到非 CJRC 会拒绝。

## 2. 实现方式

新增 `OctopusBalanceParser`（独立、可测试），并在 `FelicaReader` 中加最小分支：

1. `hf felica info` 解析 System Code；
2. 出现 `0x8008` → 卡片类型 `Octopus (Hong Kong)`，profile `octopus`；
3. 不再检查/读取 CJRC 服务；
4. 只入队一个任务：Service `0x0117`，Block 0；
5. 复用现有 `hf felica raw -s -c` 与 15 块调度器；
6. 无认证、无扫描、无写操作。

## 3. 解码

```text
raw = uint32_be(data[0..3])
require length >= 4
require raw >= 500
available = (raw - 500) / 10.0
deposit   = 500 / 10.0 = 50.0
total     = raw / 10.0
```

内部使用整数十分位（deci），UI 固定一位小数：

- 样例 `00 00 02 A3 ...` → raw 675 → HKD 17.5 / deposit 50.0 / total 67.5。
- raw < 500、长度不足、全 0 → 无效，不显示旧余额。

## 4. UI

- Octopus 余额主显示：`HKD 17.5`（大字体）。
- 次级两行：`含押金金额：HKD 67.5`、`其中押金：HKD 50.0`（英文对应）。
- 币种使用 `HKD`；PASMO/Suica 仍为 `¥`。
- 读取失败时清空余额和押金行。
- 中英文通过 Qt 翻译；无日文 UI。
- 未新增 Octopus 专用页面。

## 5. 数据模型

`BalanceInfo` 增加：

```text
hasDeposit / deposit / totalWithDeposit
```

`amount` 在 Octopus 中为可用余额（十分位）。JSON/CSV 结构未改动（按任务允许暂不扩展），原始 Block 保留在 `rawBlocks` 与 `balance.raw`。

## 6. 测试

```text
FeliCa            22 passed（原 21 + 1 Octopus）
ntagmodel         67 passed
amiibodata        22 passed
lfparser          59 passed
lfparser_stage2  126 passed
eMRTD pytest      47 passed
Git audit         17 passed
```

Octopus 测试覆盖：样例 675、raw < 500、长度不足、全 0、边界 500、常量 System/Service。

## 7. Clean Build

```text
Qt 5.15.19 / GCC 16.2.0
build/qt515-mingw64/release/release/Proxmark3GUI.exe
2,362,368 bytes, warnings 0, errors 0
```

## 8. 实机验证

本阶段没有重新插入八达通实卡，未重复实机读取。解码与识别逻辑使用用户提供的已验证数据作为固定样例；UI 未用实卡截图验证。PASMO 实机读取未重复测试，但读取器仅在识别到 `0x8008` 时走新分支。

## 9. 未解决 / 假设

- 只支持新版押金偏移 500；旧版 350 不做自动回退。
- 不读取卡号或交易记录。
- 多 System 卡若同时含 `0x8008` 与 `0x0003`，当前优先 Octopus；该组合未实测。
- JSON 未增加 Octopus 专用字段。

## 10. Git

```text
HEAD:    7dbd45f
分支:    upgrade/v0.4.1-polish
工作区:  Stage 8.1 + 8.2 修改未提交
Tag:     v0.4.1 -> 96c8ab2（未移动）
Release: 未改变
```
