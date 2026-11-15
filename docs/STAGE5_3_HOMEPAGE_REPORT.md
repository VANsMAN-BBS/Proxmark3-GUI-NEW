# Stage 5.3 报告：GitHub 仓库主页与 README 重构

日期：2026-11-15

## 1. 审计基线

- 分支：`upgrade/v0.4.1-polish`
- 起始 HEAD：`80b1e02ab65b960910e053b27aa4824a76f44fe4`
- 远程：`origin/upgrade/v0.4.1-polish` = `80b1e02`
- 工作区：干净
- 技术栈：Qt 5.15.19 / Qt Widgets / C++11 / qmake / MinGW-w64 GCC 16.2.0
- 目标 Client：Iceman/RRG v4.23346（Windows）
- 测试硬件：PM3 GENERIC / AT91SAM7S256 / 256 KB，当前无 NTAG 标签在天线

操作前已完整备份到：

```text
E:\Opencode_project_Backup_Stage5_3_20261115_2010
```

## 2. README 修改前结构

```text
# Proxmark3GUI v0.4.1
1. Features
   - Preserved / repaired
   - New
2. Verified status
3. Build
4. Run
5. Page guide
   5.1 MIFARE
   5.2 T55xx（作为一级页面）
   5.3 NTAG / Ultralight
   5.4 LF waveform
6. Known limitations
7. Documentation
8. Credits / License
```

主要问题：

- Features 声称存在持久日志，Known limitations 又写“没有持久日志”。
- NTAG 仍写“未验证”，与 Stage 3.3/3.4 的 NTAG215 实机结果冲突。
- T55XX 仍描述为一级页面，未反映 Stage 4 的 LF 子页结构。
- 页面指南缺少 LF Parser、Amiibo、NDEF/Quick Launch、eMRTD、Settings。
- 下载/发行包状态不明确。
- 没有中文介绍。
- 验证矩阵没有区分单元测试、构建、Client 集成和实机验证。
- 历史矩阵与后续阶段报告存在时间差。

## 3. README 修改后结构

```text
# Proxmark3GUI
Introduction
Screenshots
Features
  MIFARE
  LF
    LF Functions
    LF Parser
    T55XX
  NTAG / Ultralight
    Memory
    NDEF / Quick Launch
    Amiibo
  eMRTD
  RawCommand
  Settings
Compatibility and verification
Download and installation
Quick start
Build from source
Documentation
Safety and limitations
Credits and license
中文介绍
  - 功能概览
  - 兼容性与验证状态
  - 下载与运行
  - 构建
  - 安全与限制
  - 文档与许可
```

设计变化：

- 英文优先，中文完整独立章节。
- 只使用真实静态徽章：LGPL-2.1、Qt 5.15；没有伪造 CI、Release 或平台徽章。
- 使用 6 张当前软件真实截图。
- 功能介绍按实际导航分组，不再按“旧功能/新功能”混合排列。
- 将“单元测试、构建验证、Client 集成、实机验证、实验性、未验证”分开。
- 明确没有 GitHub Release 二进制包。
- 明确 GUI 不包含 Client、固件和完整 eMRTD Python 运行时。

## 4. 修正的事实矛盾

| 原 README | 实际情况 | 修正 |
|---|---|---|
| Features 有持久日志，Known limitations 说没有 | `Logger` 已实现，写入 `%LOCALAPPDATA%\Proxmark3GUI\logs\Proxmark3GUI.log`，并有回退路径 | 在 Features/Settings 中说明持久日志，删除“无日志”限制 |
| NTAG/Ultralight 未验证 | Stage 3.3 读取稳定性和 Stage 3.4 写入/清空/校验已在 NTAG215 测试标签上实机验证 | 改为“NTAG215 读写/清空/校验实机验证；NTAG213/216 未验证” |
| T55xx 是独立一级页面 | Stage 4 后 T55XX 位于 LF → T55XX | 功能与导航改为 LF 子页 |
| 页面指南缺少 LF Parser、Amiibo、NDEF/Quick Launch、eMRTD、Settings | 当前导航包含这些页面 | 按真实导航补齐 |
| 未说明发行包状态 | GitHub Releases 为空 | 明确“目前无官方二进制发行版，源码构建” |
| 未说明 Client/固件边界 | GUI 通过独立 `proxmark3.exe` 工作 | Introduction/Download/Quick Start 明确四部分依赖 |
| 兼容性写法过宽 | 只验证 v4.23346，未验证未来版本 | 明确基线，未来版本需重新验证 |
| Windows 能力可能被误读为跨平台 | 只有 Windows 构建/运行验证 | Linux/macOS 标记 Not verified |
| LF/T55xx 状态与测试记录不一致 | 当前 256 KB 测试固件无 LF | 统一标记 LF/T55XX 射频功能未实机验证 |
| QDarkStyleSheet 许可在 README 中不完整 | 仓库已有 `src/qdarkstyle/LICENSE.rst` | Credits 中链接 MIT/CC-BY-4.0 完整声明 |

eMRTD/Amiibo/NDEF/Quick Launch 均按当前代码和阶段报告重新描述：

- eMRTD：协议、FakeChip、UI 已测试；真实证件未验证。
- Amiibo：镜像库、导入/保存、写入/复读、`eload` + `sim`；不内置商业数据，模拟未在游戏机验证。
- NDEF/Quick Launch：NDEF 显示 + 实验性 URI 写入；手机兼容性未完全验证。
- NTAG：异步读取/写入、逐页复读、取消和页面保护按 Stage 3.3/3.4 描述；清空只验证了快照/计划/取消（3 页），未执行全 126 页清空。

## 5. 真实截图

来源：当前 `dist\Proxmark3GUI\Proxmark3GUI.exe` 实际运行，英文界面，100% 缩放，窗口 1600×1000，连接到 PM3。

处理后：

- 裁剪顶部本机 Client Path 区域，避免公开本机路径；
- 统一缩放到 1280 宽；
- 未修改 UI、未合成界面、未伪造卡片数据。

| 文件 | 内容 | 说明 |
|---|---|---|
| `docs/images/mifare.png` | MIFARE 页面 | 已连接 PM3，无个人卡数据 |
| `docs/images/lf-functions.png` | LF Functions | 显示 LF 子页导航和配置 |
| `docs/images/lf-parser.png` | LF Parser | 使用测试 fixture `em4100_standard.txt` 解析 EM4100 |
| `docs/images/ntag-quick-launch.png` | NDEF / Quick Launch | 显示实验性提示，无标签数据 |
| `docs/images/amiibo.png` | Amiibo 库 | 空库/导入界面，无用户 Amiibo 数据 |
| `docs/images/emrtd.png` | eMRTD | 无证件加载，无 MRZ/头像个人数据 |

尺寸：`1280×735`（T55XX 宽屏尝试未采用）。

未采用的截图：

1. **NTAG Memory**：截图时天线没有检测到标签，页面为空；不伪造识别结果。
2. **T55XX**：当前英文分组标题 `Clone to T55xx (Experimental)` 在现有布局宽度下被裁切；不展示缺陷截图，也不修改源码布局。
3. **Settings**：页面包含本机工作路径，未采用。

上述三项列为后续补拍/修复项。

## 6. 链接与格式验证

自动检查 README 中全部相对链接和图片：

- 相对目标：40 处
- 唯一目标：24 个
- 缺失文件：0

检查项：

- [x] `BUILD_GUIDE.md`
- [x] `CHANGELOG.md`
- [x] `ARCHITECTURE_UPGRADE.md`
- [x] `ICEMAN_COMPATIBILITY_MATRIX.md`
- [x] `TEST_REPORT.md`
- [x] `LICENSE`
- [x] `src/qdarkstyle/LICENSE.rst`
- [x] Stage 3.3/3.4/4/5.2/5.3 报告
- [x] LF/Amiibo/eMRTD 文档
- [x] 6 张 README 图片

Markdown：

- 未使用复杂 HTML 布局
- 未使用伪造徽章
- 中英文功能名称、版本和验证状态一致
- `git diff --check` 通过（`core.whitespace=cr-at-eol`）

## 7. GitHub About / Topics

因现有 GitHub 令牌只能推送代码，无法修改仓库设置（Stage 5.2 已实测 PATCH 返回 403），About 和 Topics 未修改。

建议手动填写：

**Description**

```text
A Qt 5 desktop GUI for Proxmark3, maintaining compatibility with the Iceman/RRG client. Features MIFARE, NTAG, Amiibo, LF and eMRTD tools, with a focus on maintainability and practical workflows.
```

**Topics**

```text
proxmark3
iceman
rfid
nfc
qt5
mifare
ntag
amiibo
emrtd
```

## 8. 修改文件

本阶段当前修改：

```text
M  README.md
A  docs/STAGE5_3_HOMEPAGE_REPORT.md
A  docs/images/amiibo.png
A  docs/images/emrtd.png
A  docs/images/lf-functions.png
A  docs/images/lf-parser.png
A  docs/images/mifare.png
A  docs/images/ntag-quick-launch.png
```

未修改：

- `src/`
- `tests/`
- `resources/commands/`
- qmake/构建配置
- 许可证
- NTAG/MIFARE/LF/eMRTD 业务逻辑
- 已发布历史

## 9. 构建与测试说明

本阶段只修改 README、截图和报告，未修改 `src/`、`tests/` 或构建配置，因此没有重新运行 Clean Build 或自动化测试；测试基线沿用 Stage 5.2（ntagmodel 67、amiibodata 22、lfparser 59、lfparser_stage2 126，Clean Build 0 warnings / 0 errors）。

## 10. 未完成事项

1. **PENDING**：GitHub About/Topics 需有仓库设置权限的用户在网页修改。
2. **PENDING**：默认分支仍为 `main`（Stage 5.2 已记录，需网页切换）。
3. **缺图**：NTAG Memory 需在实际检测到标签时补拍。
4. **缺图**：T55XX 需先修复英文实验性标题裁切或改在中文界面补拍。
5. **缺图**：Settings 需在无本机路径泄露的干净配置下补拍。
6. GitHub Releases 仍为空；本阶段不创建 Release。
7. 本阶段修改已按用户确认提交，并推送到 `upgrade/v0.4.1-polish`；推送结果以远程分支 HEAD 为准。

## 11. 风险与回滚

- README 只改文档，不影响软件行为。
- 新增图片均为当前程序真实截图，不覆盖已有素材。
- 如需回滚：

```text
git checkout -- README.md
rm -rf docs/images
```

提交后如需回滚，使用 `git revert <本次提交>`；禁止强制推送或重写已发布历史。

## 12. 提交与推送

提交信息：

```text
docs: redesign GitHub project homepage and README
```

- 目标分支：`upgrade/v0.4.1-polish`
- 远程：`origin` (`https://github.com/VANsMAN-BBS/Proxmark3-GUI-NEW.git`)

只提交本阶段文件：README、`docs/images/` 六张截图、本报告。未修改代码、测试、构建配置或许可证。推送不使用 force，不推送其他分支或标签。
