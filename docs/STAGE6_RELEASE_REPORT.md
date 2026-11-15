# Stage 6 报告：Proxmark3 GUI v0.4.1 发行准备

日期：2026-11-15

## 1. Git 初始状态

- 分支：`upgrade/v0.4.1-polish`
- HEAD：`96c8ab26901517c9e52fb4f034d04a9d0e0660a0`
- 本地与 `origin/upgrade/v0.4.1-polish` 一致
- 工作区：干净
- 远程默认分支：`upgrade/v0.4.1-polish`（GitHub API 已确认）
- 原初始化 `main`：已删除
- 远程 Tag：无
- GitHub Release：无
- 本地 Tag：`baseline-v0.2.8`、`upgrade-v0.3.0`、`upgrade-v0.3.1`、`upgrade-v0.4.0`、`upgrade-v0.4.1`；无 `v0.4.1`

备份：

```text
E:\Opencode_project_Backup_Stage6_20261115_2100
```

包含 `.git`、工作区、未跟踪文件和构建产物；关键文件哈希已校验。

## 2. 版本元数据

| 项目 | 值 |
|---|---|
| qmake `VERSION` | 0.4.1 |
| EXE FileVersion | 0.4.1.0 |
| EXE ProductVersion | 0.4.1.0 |
| README 版本 | v0.4.1 |
| CHANGELOG | V0.4.1 |
| 应用 `applicationVersion` | 0.4.1（来自 qmake VERSION） |

版本号一致，无需修改。

EXE 元数据中的 CompanyName 仍为原作者 `wh201906`，属于原始项目归属信息；本阶段未修改。

## 3. Release Clean Build

独立构建目录：

```text
E:\Opencode_project\Proxmark3GUI-master\build-release-v041
```

命令：

```powershell
qmake-qt5.exe ..\src\Proxmark3GUI.pro -spec win32-g++ CONFIG+=release
mingw32-make.exe -j4
```

结果：

- 返回码：0
- 编译器 warnings：0
- 编译器 errors：0
- qmake 首次配置输出一个资源对象查找提示（`release\Proxmark3GUI_resource_res.o`），构建过程正常生成，非编译错误
- 架构：PE32+ / x86-64
- EXE：`build-release-v041\release\Proxmark3GUI.exe`
- 大小：1,946,624 bytes
- SHA-256：`84F058536ADE09C87AFD29D4E9EFCD82F997AA66C82269D533844FB75200170E`

## 4. 发行目录

```text
artifacts/release/v0.4.1/win-x64/
├── Proxmark3-GUI-v0.4.1-win-x64/
│   ├── Proxmark3GUI.exe
│   ├── Qt5Core/Gui/Widgets/Network/SerialPort DLLs
│   ├── platforms/qwindows.dll
│   ├── styles/qwindowsvistastyle.dll
│   ├── imageformats/qgif/qico/qjpeg.dll
│   ├── bearer/qgenericbearer.dll
│   ├── MinGW/MSYS2 runtime DLLs
│   ├── emrtd/（独立 Python 3.12 运行时）
│   ├── licenses/（Qt、MSYS2、QDarkStyle、Python、PyCryptodome、Pillow）
│   ├── LICENSE
│   ├── THIRD-PARTY-NOTICES.txt
│   └── README.txt
├── Proxmark3-GUI-v0.4.1-win-x64.zip
└── SHA256SUMS.txt
```

发行目录解压后大小：约 **124.1 MB**
文件数：1099

`.gitignore` 已新增 `/artifacts/`，避免发行 ZIP/EXE/DLL 被误提交。

未包含：

- `.git`、源码、`.o/.obj/.pdb`
- 日志、`GUIsettings.ini`、用户 dump/卡片数据
- Proxmark3 Client 和固件

## 5. 依赖清单

### Qt

- Qt5Core.dll
- Qt5Gui.dll
- Qt5Widgets.dll
- Qt5Network.dll
- Qt5SerialPort.dll
- `platforms/qwindows.dll`
- `styles/qwindowsvistastyle.dll`
- `imageformats/qgif.dll`、`qico.dll`、`qjpeg.dll`
- `bearer/qgenericbearer.dll`（windeployqt 部署的可选网络 bearer 插件）

### MinGW / MSYS2

- libgcc_s_seh-1.dll
- libstdc++-6.dll
- libwinpthread-1.dll
- ICU：libicudt78 / libicuin78 / libicuuc78
- PCRE2、zlib、libpng、libjpeg、FreeType、HarfBuzz、Graphite2
- GLib、gettext-runtime/libintl、libiconv、bzip2、zstd、Brotli
- double-conversion、md4c

### 外部运行时

- `emrtd/python.exe` + Python 3.12.10
- `Crypto`（PyCryptodome 4.3.0）
- `PIL`（Pillow 13.0.0）
- `emrtd_pm3`

验证：

```text
python 3.12.10
pycryptodome 4.3.0
pillow 13.0.0
emrtd_pm3 import OK
```

在 PATH 仅为 `C:\Windows\System32;C:\Windows` 时通过。

## 6. 许可证与声明

- 主项目 LICENSE：LGPL-2.1-or-later
- QDarkStyleSheet：MIT + CC-BY-4.0，完整文本在 `licenses/QDarkStyleSheet-LICENSE.rst`
- Qt：LGPL-3.0/GPL/商业，Qt 许可证文本在 `licenses/msys2-qt5-base`、`msys2-qt5-serialport`
- MinGW GCC runtime：GPLv3 + GCC Runtime Library Exception
- MSYS2 组件：各 `licenses/msys2-*` 目录
- Python：`licenses/Python-LICENSE.txt`
- PyCryptodome：`licenses/PyCryptodome-LICENSE.rst` + sublicenses
- Pillow：`licenses/Pillow-LICENSE.txt`
- `THIRD-PARTY-NOTICES.txt` 汇总说明

未捆绑 Proxmark3 Client 或固件。

## 7. ZIP 与 SHA256

```text
File: Proxmark3-GUI-v0.4.1-win-x64.zip
Size: 52,499,142 bytes
SHA-256: 211BD1F39A74D34198F8514DEAE21400B23F0309B8A654AEF6140CF8CBEFB9DE
Built UTC: 2026-11-15T13:16:56Z
```

`SHA256SUMS.txt` 已生成。

重新解压 ZIP 校验：

- ZIP 内 `Proxmark3GUI.exe` 与发行目录 EXE SHA-256 一致
- ZIP 可正常解压
- 根目录、README、THIRD-PARTY-NOTICES、licenses、emrtd、plugins 完整

## 8. 独立环境测试

### 8.1 已完成的隔离启动测试

从 ZIP 解压到项目外目录：

```text
C:\Users\van\AppData\Local\Temp\opencode\stage6-zip-extract\Proxmark3-GUI-v0.4.1-win-x64
```

测试条件：

- PATH 仅 `C:\Windows\System32;C:\Windows`
- 清空 `QT_PLUGIN_PATH` / `QT_QPA_PLATFORM_PLUGIN_PATH`
- 使用独立空的 `USERPROFILE`、`APPDATA`、`LOCALAPPDATA`、`TEMP`
- 不从源码目录启动

结果：

- 主窗口启动正常
- MIFARE、LF、NTAG/UL、eMRTD、RawCommand、Settings 页面切换正常
- 进程保持响应
- 正常关闭
- 日志在独立 profile 下创建

### 8.2 独立 Windows 账户 / 虚拟机测试

**PASSED（用户确认）**

用户在独立 Windows 环境完成发行包测试并确认通过（2026-11-15）。
具体环境细节由用户掌握；本报告不虚构未提供的系统信息。

### 8.3 硬件只读冒烟

从 ZIP 解压目录启动发行包，显式选择 COM14：

- Client：Iceman v4.23346
- 结果：连接成功，日志解析
  `Iceman/master/v4.23346-suspect ... / PM3 GENERIC / AT91SAM7S256 Rev D`
- Client 正常退出，exit=0
- 未执行写卡、擦除、UID 修改、密钥恢复、克隆、仿真或固件刷写

## 9. Git Tag / GitHub Release

### 9.1 Tag

Annotated Tag 已创建并推送：

```text
Tag:          v0.4.1
Tag object:   4d3fa5b3eb781f692b478aee000c791229663187
Target commit:96c8ab26901517c9e52fb4f034d04a9d0e0660a0
Message:      Proxmark3 GUI v0.4.1
```

Tag 指向实际用于构建发行包的提交 `96c8ab2`。

### 9.2 GitHub Release

```text
Release ID: 446105736
URL: https://github.com/VANsMAN-BBS/Proxmark3-GUI-NEW/releases/tag/v0.4.1
Title: Proxmark3 GUI v0.4.1
Draft: false
Pre-release: false
```

资产：

```text
Proxmark3-GUI-v0.4.1-win-x64.zip
  size: 52,499,142 bytes
  https://github.com/VANsMAN-BBS/Proxmark3-GUI-NEW/releases/download/v0.4.1/Proxmark3-GUI-v0.4.1-win-x64.zip

SHA256SUMS.txt
  size: 212 bytes
  https://github.com/VANsMAN-BBS/Proxmark3-GUI-NEW/releases/download/v0.4.1/SHA256SUMS.txt
```

### 9.3 下载复验

从 GitHub Release 下载 ZIP 后：

```text
local SHA-256:      211BD1F39A74D34198F8514DEAE21400B23F0309B8A654AEF6140CF8CBEFB9DE
downloaded SHA-256: 211BD1F39A74D34198F8514DEAE21400B23F0309B8A654AEF6140CF8CBEFB9DE
size: 52,499,142 bytes
```

GitHub API 返回的 asset digest 与本地 SHA-256 一致，Release 为非 Draft、非 Pre-release。

Release 页面、说明和资产可通过 GitHub API/页面正常访问。发布时网络曾出现短暂 `github.com` 连接重置；API 与资产下载随后验证成功。

### 9.4 默认分支

```text
upgrade/v0.4.1-polish
```

未修改默认分支；原初始化 `main` 已删除。

## 10. Release Notes

已提交：

```text
docs/RELEASE_NOTES_v0.4.1.md
```

Release 描述使用该文件内容，包含 Overview、Highlights、Compatibility、
Installation、Known limitations、License。

## 11. README 更新

Release 创建成功后，README 已更新：

- v0.4.1 GitHub Release 链接
- Windows x64 ZIP 下载
- SHA256SUMS 下载
- SHA-256 与大小
- eMRTD 运行时已随包提供
- 源码构建说明仍保留

README 更新属于 Tag 之后的普通文档提交，不属于 `v0.4.1` Tag 指向的构建提交；这不移动 Release Tag。

## 12. 风险与回滚

- 本地发行包位于 `artifacts/release/v0.4.1/win-x64/`，`.gitignore` 已排除 `/artifacts/`。
- 若需撤回 Release：在 GitHub 删除 Release 和远程 Tag `v0.4.1`，再删除对应本地 Tag；不得强制推送分支。
- 源码分支历史未重写。
- 若重新构建，必须重新生成 ZIP/SHA256 并上传新资产，不能复用旧 SHA。
- 不建议删除已发布资产后继续使用同一 Tag，除非确认没有用户已下载。

## 13. 结论

- Release Clean Build：**通过**
- 依赖收集：**完成**
- 许可证/第三方声明：**完整**
- ZIP/SHA256：**完成并通过下载复验**
- 隔离 profile 启动：**通过**
- 独立 Windows 环境：**用户确认通过**
- 硬件只读连接：**通过（COM14）**
- Git Tag：**v0.4.1，指向 96c8ab2**
- GitHub Release：**已创建（非 Draft / 非 Pre-release）**
- 资产上传：**ZIP + SHA256SUMS 成功**
- README 下载入口：**已更新**
- 默认分支：**upgrade/v0.4.1-polish**
- `main` 初始化分支：**已删除**

Proxmark3 GUI v0.4.1 已完成正式发布。
