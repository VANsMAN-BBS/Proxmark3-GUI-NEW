# 硬件验证矩阵

**版本：** v0.4.1

**日期：** 2026-11-15

**设备：** Proxmark3（`VID_9AC4` / `PID_4B8F`），`PM3 GENERIC`，`AT91SAM7S256 Rev D`，256 KB 固件

**Client：** `Iceman/master/v4.23346-suspect`（Iceman/RRG v4.23346 基线）

> “模拟”指使用软件模拟芯片/合成数据；“真实”指真实证件/标签/游戏机。  
> 未验证项目不得视为已完成。

| 功能 | Client 命令 | 设备 | 标签/卡片 | 测试结果 | 验证方式 |
|---|---|---|---|---|---|
| 连接 / 版本识别 | `hw version` | PM3 | — | 通过 | 真实 |
| Client 崩溃恢复 | —（外部终止进程） | PM3 | — | 通过 | 真实 |
| 命令超时恢复 | Raw `hf tune` → 30 s | PM3 | — | 通过（自动重启） | 真实 |
| 长命令 detached | `lf tune` | PM3 | — | 通过（Stop 重启） | 真实 |
| Raw FIFO 串行化 | `hw status` / `hw ping` | PM3 | — | 通过 | 真实 |
| `hf 14a info` | `hf 14a info` | PM3 | HID SEOS/EMV | 命令已下发；无响应结果 | 真实（无对应卡响应） |
| MIFARE Classic 读/写 | `hf mf *` | PM3 | 无 Classic 卡 | 未验证 | — |
| LF / T55xx | `lf *` | PM3 | 固件无 LF | 未验证 | — |
| eMRTD SELECT AID | `hf 14a apdu` | PM3 | 护照/通行证 | 未验证（无证件；非 eMRTD 卡返回无卡） | 真实环境 + 模拟 |
| eMRTD BAC | `hf 14a apdu` | PM3 | 护照/通行证 | 未验证（协议流程已用 FakeChip 验证） | 模拟 |
| DG1 解析 | — | PM3 | 护照/通行证 | 通过（合成数据） | 模拟 |
| DG2 人像显示 | — | PM3 | 护照/通行证 | 通过（合成图像） | 模拟 |
| DG11 / DG12 | — | PM3 | 护照/通行证 | 通过（合成数据） | 模拟 |
| 串口交接（GUI ↔ Python） | — | PM3 | — | 通过（始终 1 个 client） | 真实 |
| 读取后自动重连 | — | PM3 | — | 通过 | 真实 |
| NTAG215 识别 | `hf mfu info` | PM3 | NTAG215 测试标签 | 通过 | 真实 |
| NTAG215 读取 | `hf mfu dump` | PM3 | NTAG215 测试标签 | 通过 | 真实 |
| NTAG215 写入 | `hf mfu restore` / `hf mfu wrbl` | PM3 | NTAG215 测试标签 | 通过（单页/多页） | 真实 |
| NTAG215 写后校验 | `hf mfu dump` | PM3 | NTAG215 测试标签 | 通过 | 真实 |
| NTAG215 整卡清空（126 页） | 清空流程 | PM3 | NTAG215 测试标签 | 仅快照/计划/取消（3 页） | 真实 |
| Amiibo 模拟 | `hf mfu eload` + `hf mfu sim -t 7` | PM3 | 无 NTAG215 / 游戏机 | 未验证（Client 命令存在） | — |
| 合成 NTAG215 数据加载/保存 | — | — | — | 通过 | 合成 |
| 在线目录 | HTTP GET | — | — | 未配置时离线可用；公共 API 构建时不可用 | — |

## 说明

1. 当前 256 KB 固件无 LF 功能，LF/T55xx 无法验证。
2. 当前卡片为 HID SEOS/EMV，不是 MIFARE Classic、NTAG215 或 eMRTD。
3. 无真实护照/港澳通行证，eMRTD 的 BAC/DG 解析仅用软件模拟芯片验证。
4. NTAG215 读取、写入与写后校验已在测试标签上验证；Amiibo 模拟未在游戏机上验证。
5. 所有“通过（模拟）”项目仅证明协议与界面流程，不代表真实证件/标签成功。
