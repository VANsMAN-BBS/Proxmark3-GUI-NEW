# BUILD_GUIDE.md

## v0.4.1 追加：应用图标

图标资源位于 `resources/icons/`（SVG + PNG + ICO）。修改设计后运行：

```powershell
python scripts\make-icons.py
```

脚本同时生成 `app-icon.svg`、7 个尺寸的 PNG、512×512 `app-icon.png` 和多帧 `app-icon.ico`。
`.pro` 已包含：

```qmake
RESOURCES += ../resources/icons/icons.qrc
win32: RC_ICONS = ../resources/icons/app-icon.ico
```

修改后需重新运行 qmake 并构建，EXE 图标与窗口图标才会更新。

## v0.4.0 追加：eMRTD 独立运行时

发布目录需要额外的 Python 运行时（约 50 MB）：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\package-emrtd-runtime.ps1 `
    -PythonHome "$env:LOCALAPPDATA\Programs\Python\Python312" `
    -Destination "build-runtime\emrtd"

robocopy build-runtime\emrtd dist\Proxmark3GUI\emrtd /MIR
```

- 需要本机 Python 3.12 + `pycryptodome` + `Pillow`（仅构建机需要，最终用户不需要）。
- 运行时只包含标准库、`Crypto`、`PIL` 与 `emrtd_pm3`。
- GUI 查找顺序：`PM3GUI_EMRTD_EXE` → `<GUI>/emrtd/python.exe` → `<GUI>/../build-runtime/emrtd/python.exe`。
- 新增 `QT += network`（在线目录，可选）；`windeployqt` 会带上 `Qt5Network.dll`。

**项目：** Proxmark3GUI v0.3.0  
**目标平台：** Windows 64-bit  
**技术栈：** Qt 5.15.x + Qt Widgets + qmake + MinGW-w64  
**目标 Client：** Iceman v4.23346

---

## 1. 环境要求

本次实际验证通过的组合：

| 组件 | 版本 |
|---|---|
| Windows | 10/11 64-bit |
| Qt | **5.15.19**（MSYS2 UCRT64，OpenSource） |
| qmake | Qt 5.15.19 / QMake 3.1 |
| 编译器 | MinGW-w64 GCC **16.2.0**（UCRT64） |
| make | GNU Make 4.4.1（`mingw32-make`） |
| Qt 模块 | Core / Gui / Widgets / **SerialPort** |
| Client | Iceman v4.23346（Windows 64-bit） |

> 项目固定使用 Qt 5.15.x、qmake、Qt Widgets；不迁移 Qt6/CMake。

---

## 2. 安装 Qt 5.15 + QtSerialPort（MSYS2 UCRT64）

本次使用 MSYS2 的开源 Qt5 包。安装路径：

```text
C:\msys64\ucrt64
```

包：

```text
mingw-w64-ucrt-x86_64-qt5-base
mingw-w64-ucrt-x86_64-qt5-serialport
mingw-w64-ucrt-x86_64-qt5-tools
mingw-w64-ucrt-x86_64-make
mingw-w64-ucrt-x86_64-gcc
mingw-w64-ucrt-x86_64-gdb        (可选)
```

命令（MSYS2 UCRT64 shell 或 `C:\msys64\usr\bin\bash.exe`）：

```bash
pacman -S --needed --noconfirm \
  mingw-w64-ucrt-x86_64-qt5-base \
  mingw-w64-ucrt-x86_64-qt5-serialport \
  mingw-w64-ucrt-x86_64-qt5-tools \
  mingw-w64-ucrt-x86_64-make \
  mingw-w64-ucrt-x86_64-gcc
```

关键可执行文件：

```text
C:\msys64\ucrt64\bin\qmake-qt5.exe
C:\msys64\ucrt64\bin\mingw32-make.exe
C:\msys64\ucrt64\bin\g++.exe
C:\msys64\ucrt64\bin\windeployqt-qt5.exe
```

### 关于官方 Qt 5.15.2

本次先尝试了 `aqtinstall` 的 `5.15.2 win64_mingw81`，下载到的是 **Enterprise** 构建，`qmake` 要求 `licheck.exe` 许可证，未绕过。  
因此改用 MSYS2 的开源 Qt 5.15.19；它仍属于 Qt 5.15.x，符合项目约束。

---

## 3. 构建

在项目根目录：

```powershell
$env:PATH = "C:\msys64\ucrt64\bin;" + $env:PATH
mkdir build
cd build

qmake-qt5 ..\src\Proxmark3GUI.pro -spec win32-g++ CONFIG+=release
mingw32-make -j4
```

产物：

```text
build\release\Proxmark3GUI.exe
```

如果之前构建过，建议：

```powershell
mingw32-make clean
qmake-qt5 ..\src\Proxmark3GUI.pro -spec win32-g++ CONFIG+=release
mingw32-make -j4
```

### 注意

- 不要混用 MSVC 编译的 Qt 和 MinGW 编译器。
- 本次使用 Qt 5.15.19 + MinGW/UCRT64，二者必须来自同一 MSYS2 环境。
- `src/Proxmark3GUI.pro` 已包含 `QT += serialport`。

---

## 4. 生成可独立运行的 dist

### 4.1 Qt 部署

在 `release` 目录：

```powershell
windeployqt-qt5.exe --release --no-translations --no-angle --no-opengl-sw --compiler-runtime Proxmark3GUI.exe
```

这会复制：

```text
Qt5Core.dll / Qt5Gui.dll / Qt5Widgets.dll / Qt5SerialPort.dll
platforms\qwindows.dll
styles\qwindowsvistastyle.dll
imageformats\...
libgcc_s_seh-1.dll / libstdc++-6.dll / libwinpthread-1.dll
```

### 4.2 复制 MSYS2 传递依赖

`windeployqt` 不会复制 MSYS2 的 ICU/PCRE/字体等依赖。缺少时启动会以 `0xC0000135`（DLL not found）退出。

本次实际复制的依赖：

```text
libbrotlicommon.dll
libbrotlidec.dll
libbz2-1.dll
libdouble-conversion.dll
libfreetype-6.dll
libglib-2.0-0.dll
libgraphite2.dll
libharfbuzz-0.dll
libiconv-2.dll
libicudt78.dll
libicuin78.dll
libicuuc78.dll
libintl-8.dll
libjpeg-8.dll
libmd4c.dll
libpcre2-16-0.dll
libpcre2-8-0.dll
libpng16-16.dll
libzstd.dll
zlib1.dll
```

可以用 `ldd` 递归检查：

```bash
# MSYS2 bash，在 dist 目录
ldd ./*.dll ./platforms/*.dll ./styles/*.dll ./imageformats/*.dll \
  | grep 'not found'
```

应无输出。

### 4.3 dist 结构

```text
dist/
└─ Proxmark3GUI/
   ├─ Proxmark3GUI.exe
   ├─ GUIsettings.ini
   ├─ Qt5*.dll
   ├─ platforms/qwindows.dll
   ├─ styles/qwindowsvistastyle.dll
   ├─ imageformats/*.dll
   └─ 依赖 DLL...
```

本次已验证：

- 只保留 `C:\Windows\System32;C:\Windows` 的 PATH，GUI 仍可启动；
- 放在中文路径 `dist\测试 路径\` 下仍可启动；
- 通过 GUI 连接真实 v4.23346 Client 成功。

---

## 5. 运行与配置

1. 启动 `dist\Proxmark3GUI\Proxmark3GUI.exe`。
2. 首次可选择语言（English / 中文）。
3. 顶部 `Client Path` 选择：
   ```text
   <你的路径>\proxmark3.exe
   ```
4. `Port` 选择 Proxmark3 串口（如 `COM14`）。
5. `Settings → Client`：
   - Start arguments：`<port> -f`（GUI 会自动补 `-f`，但建议保留）；
   - Config file：默认 `config_rrgv4.23346.json`；
   - 如 Client 需要环境变量，设置 Preload script；
   - 工作目录默认 `../data`。
6. 点击 `Connect`，状态栏应显示：
   ```text
   HW Version: Iceman/master/v4.23346-suspect ... / PM3 GENERIC / ...
   PM3: Connected
   ```

### 指定 Client

本次目标：

```text
E:\Opencode_project\pm3-256-iceman-v4.23346\proxmark3.exe
```

其他 v4.23346 Windows 构建只要具备相同命令/输出即可；不承诺其他版本。

---

## 6. 已知构建/运行问题

1. **Qt 5.15.2 aqt Enterprise 包不能用**：不要绕过 `licheck.exe`；使用开源 Qt 5.15.19。
2. **windeployqt 不复制 MSYS2 传递 DLL**：必须补 `libicu*`、`libpcre2*` 等，否则 `0xC0000135`。
3. **Qt6 不可直接编译**：项目仍使用 `QTextCodec`、`setIniCodec` 等 Qt5 API；本次不迁移。
4. **Qt5 EOL**：长期维护需评估 Qt6 迁移，但不属于本次范围。
5. **固件无 LF**：不影响构建，但 LF/T55xx 功能无法在当前设备验证。
6. **未提供安装器**：当前为绿色目录形式，解压/复制即可运行。

---

## 7. 目录说明

```text
src/
├─ core/          ClientSession / ClientInfo
├─ common/        Util / event filter（旧 PM3Process 保留但不再编译）
├─ module/        Mifare / LF / T55xx / NtagTab
├─ ui/            MainWindow / dialogs / waveform
└─ qdarkstyle/    theme
config/           JSON command configs
i18n/             translations
docs/             stage reports + assets
dist/             (ignored) standalone build output
```
