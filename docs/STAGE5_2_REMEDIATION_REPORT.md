# Stage 5.2 报告：发布后问题修复与仓库规范化

日期：2026-11-15

## 1. 初始 Git 状态

- 项目：`E:\Opencode_project\Proxmark3GUI-master`
- 分支：`upgrade/v0.4.1-polish`
- 起始 HEAD：`e0216517d43a34c192021b27a5f7b079dee67bbc`
- 远程：`origin/upgrade/v0.4.1-polish` = `e021651`
- 工作区：已跟踪修改仅 `docs/STAGE5_GITHUB_PUBLISH_REPORT.md`
- 暂存区：空
- 未跟踪构建输出由 `.gitignore` 忽略，未删除

操作前已完整备份到项目目录之外：

```text
E:\Opencode_project_Backup_Stage5_2_20261115_1930
```

已校验关键源码、测试、报告和 `.git` 元数据与备份一致。

## 2. Git Credential Helper

### 2.1 原始问题

- 全局配置：

```text
credential.helper=C:/Users/van/AppData/Local/Programs/Git Credential Manager/git-credential-manager.exe
```

路径含空格，Git 通过 shell 调用时被拆分。
- Git Credential Manager 可执行文件目录和 `C:\msys64\usr\bin` 未出现在当前进程 PATH 中。
- GCM 从 PowerShell 直接运行时报：

```text
Failed to locate 'git.exe' executable on the path.
```

- 多个因素叠加导致 push 等待认证或 helper 报错。

### 2.2 修复

用户级 PATH 已包含：

```text
C:\msys64\usr\bin
C:\Users\van\AppData\Local\Programs\Git Credential Manager
```

全局配置改为 GCM 官方短名：

```text
git config --global credential.helper manager
```

验证：

- Git：`2.56.0`
- GCM：`3.0.2+0d95999b8fb39bd1045770fe1d69f6a2b07101d7`
- `git-credential-manager --version` 正常
- `git credential fill` 返回 username/password，未输出凭据内容

说明：当前自动化 shell 的 PATH 是旧进程环境，推送时显式补充了 GCM 目录；新建 Windows 终端会读取更新后的用户 PATH。

结果：**已修复**。

## 3. Git 作者身份

用户确认使用 GitHub noreply 身份。

仓库级配置：

```text
user.name  = VANsMAN-BBS
user.email = 68843740+VANsMAN-BBS@users.noreply.github.com
```

- 只影响后续提交
- 未修改已发布历史
- 新提交 `095fea6` 已使用该身份

结果：**已配置**。

## 4. Amiibo 测试路径修复

文件：`tests/amiibodata/tst_amiibodata.cpp`

修改前：

```cpp
QStringLiteral("E:/Opencode_project/amiibo plain 540.bin")
QStringLiteral("E:/Opencode_project/测试 Amiibo 540.bin")
```

修改后：

```cpp
QStringLiteral("amiibo plain 540.bin")
QStringLiteral("测试 Amiibo 540.bin")
```

原因：该测试只验证 `Amiibo::requiresAsciiClientPath()` 对 ASCII/非 ASCII 字符的判断，不访问文件系统，因此不需要 fixture 或绝对路径。

结果：**已修复**，测试文件不再包含本机绝对路径。

## 5. 测试结果

| 测试 | 结果 |
|---|---:|
| `tests/ntagmodel` | **67 passed / 0 failed** |
| `tests/amiibodata`（release 目录） | **22 passed / 0 failed** |
| `tests/amiibodata`（`tests/` 目录） | **22 passed / 0 failed** |
| `tests/amiibodata`（项目根目录） | **22 passed / 0 failed** |
| `tests/lfparser` | **59 passed / 0 failed** |
| `tests/lfparser_stage2` | **126 passed / 0 failed** |

未删除测试用例，未修改测试断言。

## 6. Clean Build

- 返回码：**0**
- warnings：**0**
- errors：**0**
- 技术栈：Qt 5.15 / MinGW / qmake
- 未新增依赖

## 7. GitHub 默认分支

当前默认分支仍为 `main`。

尝试通过 GitHub API 修改：

```text
PATCH /repos/VANsMAN-BBS/Proxmark3-GUI-NEW
{"default_branch":"upgrade/v0.4.1-polish"}
```

结果：

```text
HTTP 403
Resource not accessible by personal access token
```

- 仓库读取成功，`permissions.admin = true`
- 现有凭据可推送代码，但无权限修改仓库设置
- 未删除 `main`，未覆盖 `main`，未修改远程分支历史

结果：**PENDING**。

需要用户在 GitHub 网页执行：

```text
Repository → Settings → Branches → Default branch
→ 选择 upgrade/v0.4.1-polish → Update
```

## 8. Stage 5.2 提交

代码/测试/报告提交：

```text
095fea615a54d533c368d7f5553e42561a987d86
chore: normalize repository configuration and test paths
```

文件：

```text
docs/STAGE5_GITHUB_PUBLISH_REPORT.md
tests/amiibodata/tst_amiibodata.cpp
```

本报告属于推送后的独立文档提交，其 SHA 见：

```text
git log -1 --oneline
```

未修改 `e021651` 及其之前的历史提交。

## 9. 推送结果

推送到：

```text
origin upgrade/v0.4.1-polish
```

结果成功：

```text
e021651..095fea6  upgrade/v0.4.1-polish -> upgrade/v0.4.1-polish
```

未使用 force、force-with-lease、--all 或 --tags。

## 10. 本地与远程一致性

代码提交 `095fea6` 推送后验证：

```text
local  HEAD: 095fea615a54d533c368d7f5553e42561a987d86
remote HEAD: 095fea615a54d533c368d7f5553e42561a987d86
ahead/behind: 0 0
```

本报告所在的独立文档提交推送后，应再次满足：

```text
git rev-list --left-right --count HEAD...origin/upgrade/v0.4.1-polish
0 0
```

## 11. 剩余未完成事项

1. **默认分支仍为 `main`**，需用户在 GitHub 网页切换（PENDING）。
2. 已发布历史作者仍显示 `Audit Agent <audit@local>`；未重写历史。后续提交已使用正式身份。
3. 自动化 shell 的旧 PATH 需要重开终端才能自然继承新的用户 PATH；当前推送通过显式 PATH 完成。
4. 本报告是推送后的独立文档提交，不包含在 `095fea6` 中。

## 12. 风险与回滚

### 12.1 风险

- 修改用户 PATH 和全局 `credential.helper` 会影响 Git 凭据行为；已使用 GCM 官方短名，未删除任何凭据。
- 默认分支尚未切换，GitHub 首页仍显示初始化 `main`。
- 测试路径修改只影响字符串输入，不改变业务逻辑或测试期望。

### 12.2 回滚

凭证配置：

```text
git config --global credential.helper "C:/Users/van/AppData/Local/Programs/Git Credential Manager/git-credential-manager.exe"
```

测试路径：

```text
git revert 095fea6
```

默认分支：无需回滚，未修改。

历史提交：未重写，不需要回滚。

## 13. 结论

- 凭据 helper：**已修复**
- Git 作者身份：**已配置**
- Amiibo 测试路径：**已修复**
- 默认分支：**PENDING（令牌无仓库设置写权限）**
- Clean Build：**通过**
- 自动化测试：**全部通过**
- Stage 5.2 代码提交 `095fea6`：**已推送**
- 本地与远程代码 HEAD：**一致**
