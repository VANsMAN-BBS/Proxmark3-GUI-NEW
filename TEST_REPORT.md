# 测试报告

验证对象：Proxmark3 GUI v0.4.1（Windows x64 / Qt 5.15.19 / MinGW-w64 GCC 16.2.0）。

> 本报告只列当前有效的验证结论。“通过（模拟）”表示使用软件模拟芯片或合成数据，不代表真实卡片/证件成功。未验证项目不得视为已完成。

## 1. 测试环境

| 项目 | 值 |
|---|---|
| OS | Windows 64-bit |
| Qt | 5.15.19（MSYS2 UCRT64，OpenSource） |
| 编译器 | MinGW-w64 GCC 16.2.0（UCRT64） |
| Client | Iceman/RRG v4.23346（外部提供，不随 GUI 分发） |
| 设备 | Proxmark3 `PM3 GENERIC`，AT91SAM7S256，256 KB 固件（无 LF） |
| 卡片 | NTAG215 测试标签；HID SEOS/EMV（非 MIFARE Classic） |

## 2. 自动化测试

| 测试套件 | 用例数 | 结果 |
|---|---|---|
| ntagmodel | 67 | 通过 |
| amiibodata | 22 | 通过 |
| lfparser | 59 | 通过 |
| lfparser_stage2 | 126 | 通过 |
| emrtd pytest | 47 | 通过 |

## 3. 构建与启动

| 项目 | 结果 |
|---|---|
| qmake Release clean build | 通过，0 warning / 0 error |
| 独立目录启动（PATH 仅 System32/Windows） | 通过 |
| Qt 插件与 MSYS2 运行时齐全 | 通过 |
| eMRTD Python 运行时导入 | 通过 |
| 100% / 125% / 150% 缩放 | 通过（修复 T55xx 标签裁切） |
| 多显示器 / 跨 DPI | 未验证 |

## 4. 真实硬件验证

| 项目 | 结果 |
|---|---|
| 连接、版本与型号识别 | 通过（只读） |
| `hw version` / `hw status` | 通过（只读） |
| `hf 14a info` | 通过（只读） |
| `hf mf chk --1k` | 通过（只读） |
| Trace Load/Save | 通过（只读） |
| NTAG215 识别 / 读取 / 写入 / 写后校验 | 通过（测试标签） |
| NTAG215 整卡清空（126 页） | 未执行（仅验证快照/计划/取消，3 页） |
| MIFARE Classic 读/写/攻击/仿真 | 未验证（无 Classic 卡） |
| LF / T55xx 射频操作 | 未验证（测试固件无 LF） |
| Amiibo 模拟（游戏机） | 未验证 |
| 真实 eMRTD 证件（BAC/DG） | 未验证（FakeChip 与合成数据通过） |
| 写卡/锁卡/擦除/恢复/仿真 | 未执行 |

## 5. 客户端与界面

| 项目 | 结果 |
|---|---|
| 启动/断开/重连、无残留进程 | 通过 |
| Client 异常退出后的状态与提示 | 通过 |
| 命令超时自动重启 | 通过 |
| 长命令 detached（Stop 重启） | 通过 |
| FIFO 命令串行化、内部哨兵隐藏 | 通过 |
| UTF-8 / ANSI / CR 输出处理 | 通过 |
| MIFARE / LF / NTAG / eMRTD / RawCommand / Settings 页面切换 | 通过 |
| 中文 / 英文界面 | 通过 |
| 主题、字体设置 | 保留（未逐项回归） |

## 6. 重要限制

1. 256 KB 测试固件无 LF，LF/T55xx 射频路径无法在该固件上验证。
2. 无 MIFARE Classic 卡，仅验证了命令下发、表格与解析流程。
3. eMRTD 协议流程用 FakeChip 验证；没有真实护照/通行证。
4. Amiibo 写入与复读仅在 NTAG215 测试标签验证，未在游戏机上验证。
5. 自动化测试通过不等同于真实硬件验证。
