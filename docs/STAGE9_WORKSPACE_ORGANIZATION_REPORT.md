# Stage 9 — 工作区整理报告

日期：2026-11-16

分支：`upgrade/v0.4.1-polish`

远程：仅本地提交，**未 Push**

## 1. 整理前状态

- 根目录存在 11 个 `build-*` 目录、`dist/`、旧 `artifacts/release/`。
- 构建中间文件、Debug/Release 输出、测试构建、旧发布包混在项目根目录。
- Git 未跟踪任何构建产物；全部由 `.gitignore` 忽略。
- 整理前备份：外部目录，7,827 个文件，720.3 MB。

主要体积：

| 区域 | 大小 |
|---|---:|
| 根目录旧 build/ | 214.0 MB |
| `dist/` | 260.4 MB |
| `artifacts/`（v0.4.1） | 174.2 MB |
| 源码 / 测试 / 资源 | 约 8 MB |

## 2. 整理后目录

```text
build/
├── README.md
├── legacy/                      旧 Stage 0-8 构建树（可删除）
└── qt515-mingw64/
    ├── debug/                   Debug 构建
    ├── release/                 Release 构建
    └── tests/<suite>/           测试构建
artifacts/
├── README.md
├── publish/win-x64/v0.4.1/      正式发布 ZIP + SHA256SUMS + 解压包
├── test/{reports,logs,coverage}/
├── screenshots/
└── temp/
```

根目录现在只保留源码、文档、项目文件与 `build/`、`artifacts/` 两个工作目录。

## 3. 文件移动

| 原位置 | 新位置 |
|---|---|
| `build-final`、`build-release-v041`、`build-runtime`、`build-stage0`、`build-stage8`、`build-sym`、`build-test-*`、`build-tests-felica` | `build/legacy/<原名>` |
| `dist/` | `build/legacy/dist/` |
| `tests/<suite>/build/` | `build/legacy/tests/<suite>/` |
| `deliverables/.../build*` | `build/legacy/deliverables/` |
| `artifacts/release/v0.4.1/win-x64/` | `artifacts/publish/win-x64/v0.4.1/` |

未删除任何正式发布文件、测试源码或用户数据。旧构建树暂存于 `build/legacy/`，可在新构建验证后自行删除。

## 4. 发布资产校验

v0.4.1 移动后重新计算 SHA256：

```text
211BD1F39A74D34198F8514DEAE21400B23F0309B8A654AEF6140CF8CBEFB9DE
```

与发布记录及 GitHub Release 一致。ZIP 大小仍为 52,499,142 字节。未重新打包、未覆盖。

## 5. 构建命令

```powershell
.\scripts\build.ps1                          # Debug
.\scripts\build.ps1 -Configuration Release   # Release
.\scripts\build.ps1 -Configuration Release -Clean
.\scripts\clean-build.ps1                    # 删除 qt515-mingw64 构建
```

脚本自动定位 qmake / mingw32-make；缺失时退出码 2。

## 6. 测试命令

```powershell
.\scripts\test.ps1
.\scripts\test.ps1 -SkipBuild
```

- 构建：`build/qt515-mingw64/tests/<suite>/`
- 报告：`artifacts/test/reports/<timestamp>/`
- 日志：`artifacts/test/logs/`

## 7. 发布打包命令

```powershell
.\scripts\package-release.ps1 -Version vX.Y.Z -StagingDir <已准备的发布目录>
```

脚本生成 ZIP、`SHA256SUMS.txt`、`MANIFEST.txt`，写入 `artifacts/publish/win-x64/<version>/`；已存在版本默认拒绝覆盖（退出码 3）。

## 8. Clean Build

```text
qmake:  C:\msys64\ucrt64\bin\qmake-qt5.exe (Qt 5.15.19)
make:   C:\msys64\ucrt64\bin\mingw32-make.exe (GCC 16.2.0)
Release: build/qt515-mingw64/release/release/Proxmark3GUI.exe
         2,256,896 bytes, warnings 0, errors 0
Debug:   build/qt515-mingw64/debug/debug/Proxmark3GUI.exe
         41,026,997 bytes, warnings 0, errors 0
```

两套构建使用独立目录，无旧对象文件复用。

## 9. 回归结果

| 套件 | 结果 |
|---|---|
| FeliCa | 17 passed |
| ntagmodel | 67 passed |
| amiibodata | 22 passed |
| lfparser | 59 passed |
| lfparser_stage2 | 126 passed |
| eMRTD pytest | 47 passed |
| Git audit | 17 passed |

全部通过；测试构建与报告均写入新目录。

## 10. GUI 冒烟

从新 Release 构建启动：

- MIFARE、LF、NTAG/UL、eMRTD、FeliCa、RawCommand、Settings 全部可打开；
- 进程保持响应；
- 正常退出。

未执行写卡或硬件操作。

## 11. Git Push Audit

- `core.hooksPath=.githooks` 仍有效。
- 策略新增 `allow_exact_paths`：`build/README.md`、`artifacts/README.md`。
- 手动审核 `origin..HEAD`：44 个文件，ALLOW 44，BLOCK 0，REVIEW 0。
- Git audit 自测：17/17 通过。
- `.gitignore` 未放松真实数据或二进制拦截规则。

## 12. 本地提交

```text
091a10b chore(workspace): organize build, artifacts and workspace scripts
```

本报告单独提交。

## 13. 远程状态

- 本地领先 `origin/upgrade/v0.4.1-polish` 6 个提交。
- 未执行 Push。
- `v0.4.1` Tag 未移动，GitHub Release 未改变。

## 14. 未解决项

- `build/legacy/` 中的旧构建树未删除；确认新流程稳定后可删除。
- `scripts/translate-*.py` 仍含本机绝对路径（Stage 8 遗留，未处理）。
- `package-release.ps1` 已用小样本验证归档/哈希/防覆盖逻辑，但未用完整 124 MB 发布目录重新打包，以避免覆盖既有资产。
- 完整发布目录的 Qt/MinGW 部署仍依赖既有 Stage 6 打包流程；本阶段未重写部署清单生成器。
