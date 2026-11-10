# Stage 6 Report — 整体回归、打包与交付

**日期：** 2026-11-10  
**版本：** Proxmark3GUI v0.3.0  
**分支：** `upgrade/iceman-v4.23346`

---

## 1. 阶段目标

- 对全部页面/连接/命令路径做最终回归；
- 生成可独立运行的 Windows 目录；
- 清理构建警告；
- 完成交付文档；
- 明确区分已验证 / 未验证 / Experimental。

---

## 2. 最终构建

环境：

```text
Qt 5.15.19 (MSYS2 UCRT64, OpenSource)
MinGW-w64 GCC 16.2.0
qmake 3.1
GNU Make 4.4.1
```

命令：

```powershell
qmake-qt5 ..\src\Proxmark3GUI.pro -spec win32-g++ CONFIG+=release
mingw32-make -j4
```

结果：

- 编译、链接成功；
- 最终 `Proxmark3GUI.exe` 977,408 bytes；
- `build-final2.log` 无编译警告；
- 产品版本：`0.3.0.0`；
- 默认配置列表包含 `config_rrgv4.23346.json`。

版本提升：

```qmake
VERSION = 0.3.0
```

新增配置：

```text
config/config_rrgv4.23346.json
```

（基于 v4.23346 tag commit `dc0110c...`；命令集沿用已验证的 v4.16717 配置，未引入猜测参数。）

---

## 3. 独立 Windows 目录

输出：

```text
dist/Proxmark3GUI/
```

内容：

- `Proxmark3GUI.exe`；
- `Qt5Core/Gui/Widgets/SerialPort.dll`；
- `platforms/qwindows.dll`；
- `styles/qwindowsvistastyle.dll`；
- `imageformats/*.dll`；
- MSYS2 传递依赖：
  - ICU：`libicudt78/libicuin78/libicuuc78`；
  - PCRE：`libpcre2-16-0/libpcre2-8-0`；
  - 字体/图形：`freetype/harfbuzz/graphite2/png/jpeg`；
  - 其他：`glib/intl/iconv/brotli/bz2/zstd/zlib/double-conversion/md4c`；
- `GUIsettings.ini`（语言、Client Path、参数）。

规模：

```text
29 files, ~71.7 MB
```

### 验证

| 测试 | 结果 |
|---|---|
| PATH 仅 `C:\Windows\System32;C:\Windows` 启动 | 通过 |
| 中文目录 `dist\测试 路径\` 启动 | 通过 |
| 独立目录连接 v4.23346 Client | 通过 |
| 版本/型号显示 | `v4.23346-suspect / PM3 GENERIC / AT91SAM7S256 Rev D` |

---

## 4. 回归结果

| 页面/功能 | 结果 |
|---|---|
| Mifare 页面/表格/按钮 | 通过 |
| LF 页面 + Show Waveform 按钮 | 通过 |
| T55xx 原 UI + Block0 解码 | 通过 |
| NTAG/Ultralight 页面 | 通过 |
| RawCommand | 通过 |
| Settings + 配置列表 | 通过 |
| Connect / Disconnect / Reconnect | 通过 |
| Client 异常退出检测 | 通过 |
| 中文命令输出 | 通过 |
| Trace GUI round-trip | 通过（SHA-256 一致） |
| MIFARE `hf 14a info` / `hf mf chk` | 通过（只读） |
| LF/T55xx/NTAG 成功路径 | 未验证（硬件/标签限制） |

构建警告清理：

- `QButtonGroup::buttonToggled` → `idToggled`；
- `QWheelEvent::pos()` → `position().toPoint()`；
- `QDateTime::toTime_t()` → `toSecsSinceEpoch()`；
- 重名 `label_17` 已重命名。

---

## 5. 交付文档

已生成/更新：

- `AUDIT_STAGE1.md`
- `ICEMAN_COMPATIBILITY_MATRIX.md`
- `ARCHITECTURE_UPGRADE.md`
- `CHANGELOG.md`
- `TEST_REPORT.md`
- `BUILD_GUIDE.md`
- `README.md`
- `docs/STAGE0_REPORT.md` … `docs/STAGE6_REPORT.md`

---

## 6. 最终已知限制

1. 当前设备 firmware 无 LF：LF/T55xx 无法真实验证。
2. 当前卡为 HID SEOS/EMV：MIFARE Classic 成功读/写/攻击/仿真未验证。
3. 无 NTAG/Ultralight 标签：页面成功识别/读写未验证。
4. 未做写卡/锁卡/擦除/恢复/刷写测试。
5. 未建立自动化测试/Mock Client 工程（本次以真实 Client + 手工/脚本化回归为主）。
6. Raw/fire-and-forget 长命令仍不进入 FIFO。
7. 无持久日志文件。
8. T55xx 部分字段/选项块仅保留 UI，明确禁用。

---

## 7. Git 提交

```text
stage0: establish Qt5 build baseline
stage1: refactor PM3 client communication
stage2: repair legacy feature compatibility
stage3: restore experimental T55xx UI
stage4: add NTAG Ultralight module
stage5: add optional LF waveform view
stage6: finalize packaging and documentation
```

基线：

```text
main / baseline-v0.2.8
```

所有阶段均可回滚；`dist/` 与 `build*` 已加入 `.gitignore`。

---

## 8. 结论

**Stage 6 完成。**

- 源码可构建；
- 独立 Windows 目录可启动；
- 可连接指定 v4.23346 Client 并读取真实设备；
- 已有功能保留；
- 新增功能与实验性功能状态已明确；
- 未验证项没有被报告为硬件成功。
