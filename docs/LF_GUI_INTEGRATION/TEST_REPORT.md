# 测试报告

## 1. 解析器测试（源码级，无硬件）

| 套件 | 位置 | 用例 | 结果 |
|---|---|---|---|
| Stage 2 全量 | `tests/lfparser_stage2/`（链接 `src/lf/parser/`） | **126** | **126 passed / 0 failed** |
| Stage 1 回归（保留） | `tests/lfparser/`（链接同一份集成源码） | **59** | **59 passed / 0 failed** |

运行方式（Qt 5.15，MSYS2 UCRT64）：

```powershell
$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"
cd tests\lfparser_stage2\build; mingw32-make -j4; .\release\tst_lfparser_stage2.exe
cd tests\lfparser\build;        mingw32-make -j4; .\release\tst_lfparser.exe
```

覆盖（来自 Stage 2 套件）：

- 22 个解析器注册与身份；
- EM4100 多变体、HID 多格式与 Ambiguous、AWID 26/50、Indala 有/无 Raw；
- FDX-B、Gallagher、G-Prox-II、IO Prox、Keri、Nedap、NexWatch、Noralsy、PAC、Paradox、Presco、Pyramid、SecuraKey、Viking、Visa2000、Motorola、IDTECK、Jablotron；
- 混合协议文本、无卡文本、固件不支持文本；
- T55xx detect（有/无密码）、block0（ASK/PSK2/扩展/非法）；
- 空输入、格式优先级、summary 稳定性。

## 2. GUI 测试（真实窗口 + UI Automation）

| 用例 | 输入 | 结果 |
|---|---|---|
| LF 容器与子标签 | — | 顶层 `LF`，子标签 `低频功能` / `低频多协议解析` |
| 原有低频控件保留 | — | 搜索、读取、调谐、嗅探、显示波形、获取配置、重置 均在 |
| EM4100 | `em4100_standard.txt` | Identified，Hex/Decimal/Raw 等字段正确 |
| HID 冲突 | `hid_conflict.txt` | Partial，`Format: Ambiguous` + 候选格式说明 |
| Indala 缺 Raw | `indala_64_no_raw.txt` | Identified，FC 123 / CN 1337 |
| AWID 26 | `awid_26.txt` | Identified，FC 50 / CN 1234 |
| 无卡文本 | `no_card.txt` | 未找到受支持的协议 |
| T55xx | `t55xx/detect_plain.txt` | 只读页显示 Chip/Block0/Modulation/Bit rate/Offset/Inverted |
| 空输入 | 空 | “输入为空。” |
| 英文界面 | — | 全部用例通过 |
| 中文界面 | — | 子标签与原有低频页面中文显示正常 |

## 3. 构建

| 项目 | 结果 |
|---|---|
| 主程序 clean build | 通过（rc=0） |
| 新增编译警告 | **0** |
| 解析器以 `-Wall -Wextra` 编译 | 无警告 |

## 4. 未验证 / 限制

1. **真实 LF 硬件未验证**：当前固件为裁剪掉 LF 的 256 KB 版本，所有 LF 命令均不可用；本次仅验证文本解析与界面。
2. 未做真实 LF 卡片读取 / 调谐 / 嗅探的回归（硬件不支持）。
3. GUI 自动化中的中文按钮点击受 PowerShell 脚本编码限制，改用英文界面完成功能测试；中文界面以截图确认。
4. 未新增 C++ GUI 单元测试框架（沿用项目既有约定），GUI 验证通过 UI Automation 完成。
