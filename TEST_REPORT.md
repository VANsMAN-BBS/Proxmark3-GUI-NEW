# TEST_REPORT.md

**版本：** Proxmark3GUI v0.3.0  
**日期：** 2026-11-10  
**分支：** `upgrade/iceman-v4.23346`  
**目标 Client：** Iceman v4.23346  
**测试设备：** Proxmark3 `PM3 GENERIC`，AT91SAM7S256，256K flash，COM14  
**测试原则：** 仅执行只读/本地命令；未写卡、未锁卡、未擦除、未刷写、未执行仿真。

---

## 1. 测试环境

| 项目 | 值 |
|---|---|
| OS | Windows 64-bit |
| Qt | 5.15.19（MSYS2 UCRT64，OpenSource） |
| qmake | Qt 5.15.19 / QMake 3.1 |
| 编译器 | MinGW-w64 GCC 16.2.0（UCRT64） |
| make | GNU Make 4.4.1 |
| Client | `pm3-256-iceman-v4.23346\proxmark3.exe` |
| Client 版本 | `Iceman/master/v4.23346-suspect 2026-09-18 ... 9ce8a4b6c` |
| 固件 | `PM3 GENERIC`，AT91SAM7S256，256K；无 LF |
| 设备 | VID `0x9AC4` / PID `0x4B8F`，序列号 `ICEMAN` |
| 当前卡 | HID SEOS / EMV（非 MIFARE Classic，非 NTAG/UL） |

---

## 2. A. 静态与构建验证

| 项目 | 结果 | 证据 |
|---|---|---|
| qmake 读取工程 | 通过 | Makefile 生成 |
| 完整编译/链接 | 通过 | `Proxmark3GUI.exe` **977,408 bytes（最终 clean build）** |
| 新增代码编译警告 | 无 | `build-final/clean-build.log` 无 `warning:` |
| 资源嵌入 | 通过 | config/language/qdarkstyle qrc 编译 |
| 配置文件加载 | 通过 | 默认选择 `config_rrgv4.23346.json` |
| 独立依赖打包 | 通过 | dist 29 files / ~71.7 MB，含 Qt + ICU + 插件 |
| 无 MSYS2 PATH 启动 | 通过 | PATH 仅为 System32/Windows 时正常启动 |
| 中文安装路径启动 | 通过 | `dist\测试 路径\Proxmark3GUI.exe` 正常启动 |
| 安全边界 | 未执行写/刷写 | 测试记录无写操作 |

---

## 3. B. Client 集成验证

### 3.1 连接/版本

| 测试 | 结果 | 证据 |
|---|---|---|
| 启动指定 Client | 通过 | 状态栏 `Iceman/master/v4.23346-suspect` |
| 解析 Firmware | 通过 | `/ PM3 GENERIC` |
| 解析硬件 | 通过 | `/ AT91SAM7S256 Rev D` |
| `PM3:Connected` | 通过 | 状态栏 |
| Disconnect | 通过 | `PM3:Not Connected`，无残留进程 |
| 再次 Connect | 通过 | 重新连接成功 |
| Client 异常退出 | 通过 | 状态先变为 Not Connected，再显示错误 |
| 内部哨兵隐藏 | 通过 | Raw 无 `remark: PM3GUI_` / 握手 `rem PM3GUI_` |
| 命令队列 | 通过 | 普通命令按 FIFO；`hf mf chk` 27 s 期间不串命令 |

### 3.2 UTF-8 / 输出

| 测试 | 结果 | 证据 |
|---|---|---|
| 中文命令参数 | 通过 | Raw 显示 `rem 中文测试` / `remark: 中文测试` |
| ANSI/CR | 通过 | Client 输出无控制码污染；CR 归一化 |
| 多行输出 | 通过 | `hw version`、`hf 14a info` 完整 |
| 输出过长 | 部分 | 无持久日志/上限；当前测试输出有限 |

### 3.3 MIFARE

| 测试 | 结果 | 说明 |
|---|---|---|
| `hf 14a info` UID/ATQA/SAK | 通过 | `2F7B5454 / 0004 / 20`（HID SEOS） |
| Set Parameter 预填/校验 | 通过 | 未提交写卡 |
| `hf mf chk --1k` | 通过 | 27 s 完成，16 行表格，无超时 |
| MIFARE Classic 读块/写块 | 未验证 | 当前卡不是 MF Classic |
| Nested/StaticNested/Hardnested | 未验证 | 同上 |
| MIFARE 仿真 | 参数已验证 | `hf mf sim --help` 确认 `-x`；未执行仿真 |
| 文件 IO | 静态/代码验证 | 边界检查已加固；未做完整 UI round-trip |

### 3.4 Trace

| 测试 | 结果 | 证据 |
|---|---|---|
| Client round-trip | 通过 | 3070 bytes，SHA-256 一致 |
| GUI Load | 通过 | `Recorded Activity (TraceLen = 3070 bytes)` |
| GUI Save（buffer） | 通过 | 确认执行 `trace save -1 -f tmp...` |
| GUI round-trip | 通过 | 保存 3070 bytes，SHA-256 与源一致 |
| 设备 trace | 未执行 | 无写；未做 HF sniff trace |

### 3.5 LF

| 测试 | 结果 | 说明 |
|---|---|---|
| `lf read -v` | 不可用 | 当前 firmware 无 LF，返回 not available |
| `lf sniff -v` | 不可用 | 同上 |
| `hw status` LF 段 | 不存在 | 256K firmware 裁剪 LF |
| LF Config 解析 | 未验证 | 无 LF 输出，GUI 保持默认值；不宣称成功 |
| Show Waveform（合成数据） | 通过 | `data load` 200 points → `data save` → 波形显示 |
| 真实 LF 波形 | 未验证 | Not verified on physical hardware（firmware lacks LF） |

### 3.6 T55xx

| 测试 | 结果 | 说明 |
|---|---|---|
| 原 UI 显示 | 通过 | 三个配置分组 + 克隆组 |
| Block0 hex/bin | 通过 | `00107060` → 正确二进制 |
| Block0 字段解码 | 通过 | FSK2a / RF50 / MaxBlock 3 |
| 未验证控件禁用 | 通过 | 灰显 + tooltip |
| `lf t55xx detect/info/config` | 未验证 | 当前固件无 LF |
| EM410x clone | 未验证 | 无 LF/标签，未写 |

### 3.7 NTAG / Ultralight

| 测试 | 结果 | 说明 |
|---|---|---|
| 页面显示 | 通过 | 独立标签页 |
| Detect 在非 NTAG 卡 | 通过 | 明确提示 UID/TYPE 缺失，无崩溃 |
| 成功识别 NTAG/UL | 未验证 | 无标签 |
| 页面读写/复读 | 未验证 | 未执行写 |
| 页数/保护映射 | 静态检查 | 源码常量 + `cfg0`；未知型号禁写 |

### 3.8 设置/页面

| 测试 | 结果 |
|---|---|
| Mifare / LF / T55xx / NTAG/Ultralight / RawCommand / Settings 页切换 | 通过 |
| Client Path / Port / Refresh / Connect / Disconnect | 通过 |
| 客户端路径历史 | 通过（设置文件） |
| 主题/语言/字体设置 | 保留（未逐项回归） |
| 关闭窗口/退出 | 通过（Stop/Disconnect 后无残留 Client） |

---

## 4. C. 真实硬件验证

| 项目 | 状态 |
|---|---|
| 连接/版本/型号 | **已通过（只读）** |
| `hw version` / `hw status` | **已通过（只读）** |
| `hf 14a info` | **已通过（只读）** |
| `hf mf chk --1k` | **已通过（只读；无写）** |
| Trace Load/Save | **已通过（只读文件操作）** |
| MIFARE Classic 读写 | **Not verified**（卡不匹配） |
| LF Read/Sniff/T55xx | **Not verified on physical hardware**（firmware 无 LF） |
| NTAG/Ultralight | **Not verified**（无标签） |
| 写卡/锁卡/擦除/恢复/仿真 | **未执行**（按纪律） |

---

## 5. 测试结论

- **可以构建、启动、独立运行、连接指定 Client 并读取真实设备版本。**
- 核心通信层修复经过真实 Client/设备验证。
- Trace 和 MIFARE 信息/检查路径经过真实设备验证（只读）。
- LF/T55xx/NTAG 成功路径受硬件/标签限制，明确标记未验证。
- 没有任何把模拟结果冒充真实硬件验证的结论。

---

## 6. 未完成/未验证清单

1. MIFARE Classic 成功读/写/攻击/仿真；
2. LF Read/Sniff 真实采样与 T55xx 设备操作；
3. NTAG/Ultralight 成功识别、读写、锁定/OTP；
4. 中文文件路径的 Client 文件读写（仅 GUI 路径启动已验证）；
5. 长时间/大输出日志与内存上限；
6. 多显示器/高 DPI/不同主题的完整回归；
7. 自动化单元测试与 Mock Client（本次未建立测试工程）。
