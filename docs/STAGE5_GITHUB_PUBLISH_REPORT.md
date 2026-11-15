# Stage 5 / 5.1 报告：GitHub 首次发布与远程仓库同步

日期：2026-11-15

> 本报告在远程推送完成后更新。更新内容属于后续本地修改，**不包含在远程 `e021651` 提交中**。

## 1. 本地仓库审计

- 分支：`upgrade/v0.4.1-polish`
- 推送前 HEAD：`e0216517d43a34c192021b27a5f7b079dee67bbc`
- 推送分支历史：

```text
e021651 chore(license): add QDarkStyleSheet notices and ignore build outputs
1f0036c refactor(ui): consolidate LF, NTAG and navigation layouts
b222668 feat: add NTAG, Amiibo and LF functionality
245b184 fix: keep eMRTD protocol output UTF-8 on Windows pipes
```

- 推送前工作区：已跟踪文件干净，暂存区为空
- 构建输出由 `.gitignore` 忽略，未删除任何文件

## 2. 远程仓库初始状态

目标：

```text
https://github.com/VANsMAN-BBS/Proxmark3-GUI-NEW.git
```

推送前：

- 仓库公开、非 fork、默认分支 `main`
- 只有 `main` 一个分支
- `main`：`1bbdde0b8c757f257b9cbdf0a3269da98cf3f428`
- 无其他分支、无标签
- 初始化提交仅包含 LGPL-2.1 `LICENSE` 和 2 行通用 README
- 本地 `LICENSE` blob 与远程初始 `LICENSE` 一致：
  `8000a6faacf471c537530805ab29523c7732e11a`
- 目标分支 `upgrade/v0.4.1-polish` 不存在

## 3. origin 配置

```text
origin  https://github.com/VANsMAN-BBS/Proxmark3-GUI-NEW.git (fetch)
origin  https://github.com/VANsMAN-BBS/Proxmark3-GUI-NEW.git (push)
```

未修改其他 remote。

## 4. 敏感信息审计

扫描范围：

- HEAD 工作树
- 全部 26 个可达提交
- 历史路径对象

结果：

- GitHub Token / PAT / AWS key / Google API key / Slack token：未发现
- 私钥 / SSH key / PGP private key：未发现
- 密码、API key、access token 赋值：未发现
- `PRIVATE KEY` 文本：未发现
- 用户 NTAG dump、Amiibo 用户 BIN、测试 JSON、临时日志：HEAD 与历史均未发现
- 已跟踪 `.exe/.dll/.o`：未发现
- 历史数据类文件仅：
  - `config/config_*.json`（Client 命令配置，公开资源）
  - `i18n/languages.ini`（语言映射）

非敏感但需后续处理：

- `tests/amiibodata/tst_amiibodata.cpp:243,245` 含本机路径
  `E:/Opencode_project/...`
  不是密钥或用户数据，但属于本机绝对路径。

## 5. 用户数据与构建产物

- 仓库没有跟踪任何 `.bin` 文件。
- 没有跟踪 NTAG/Amiibo 用户数据。
- 没有跟踪 `dist/`、`data/`、根 `build*`。
- tests/deliverables 构建输出未被提交、未被删除，已通过 `.gitignore` 忽略。

## 6. 许可证审计

### 6.1 主项目

- 根 `LICENSE`：GNU LGPL-2.1，包含 “or (at your option) any later version”，实际为 **LGPL-2.1-or-later**
- 本地 `LICENSE` 与远程初始化 `LICENSE` 完全一致
- README 声明主项目 LGPL-2.1；Proxmark3 Client 作为独立进程、GPLv3，不分发 Client 二进制
- LF Parser 审计文档确认：`src/lf/`、`tests/lfparser*` 为独立实现，未复制 Phosphor（GPL-3.0）或 Iceman Client 代码
- 未引入 GPL-3.0 代码，未修改主项目许可证类型

### 6.2 QDarkStyleSheet 合规修复

已新增：

- `src/qdarkstyle/LICENSE.rst`
  - MIT License（代码）
  - CC-BY-4.0（图片）
  - 上游版权归属
- `src/qdarkstyle/README.md`
  - 补充 Colin Duquesnoy / Daniel Cosmo Pizetta 版权归属
  - 说明代码 MIT、图片 CC-BY-4.0
  - 指向 `LICENSE.rst`

该修复不改变主项目 LGPL-2.1-or-later，也不改变功能代码。

## 7. .gitignore

新增最小规则：

```text
/deliverables/LF_Parser_Standalone/build*/
/tests/*/build/
/tests/lfparser_stage2/stage2-tests.log
```

验证：

- 构建目录和测试日志被忽略
- `tests/**/*.cpp`、`*.pro`、fixtures 不被忽略
- `deliverables` 源码/文档不被忽略
- 未删除任何文件

## 8. 远程推送

### 8.1 推送前状态

- 本地 HEAD：`e0216517d43a34c192021b27a5f7b079dee67bbc`
- 目标分支：`upgrade/v0.4.1-polish`
- 远程目标分支：不存在
- 远程 `main`：`1bbdde0`（保持不变）

### 8.2 实际命令

```text
git push -u origin upgrade/v0.4.1-polish
```

认证使用本机已配置的 Git Credential Manager。未将任何 Token、密码或凭据写入命令、脚本、日志或报告。

### 8.3 推送结果

成功：

```text
* [new branch]      upgrade/v0.4.1-polish -> upgrade/v0.4.1-polish
branch 'upgrade/v0.4.1-polish' set up to track 'origin/upgrade/v0.4.1-polish'.
```

- 只推送了 `upgrade/v0.4.1-polish`
- 未推送 `main`
- 未推送其他分支或标签
- 未使用 force / force-with-lease / --all / --tags

## 9. 推送后验证

命令：

```text
git ls-remote origin
git fetch origin
git rev-parse HEAD
git rev-parse origin/upgrade/v0.4.1-polish
git rev-list --left-right --count HEAD...origin/upgrade/v0.4.1-polish
```

结果：

```text
1bbdde0 refs/heads/main
e0216517d43a34c192021b27a5f7b079dee67bbc refs/heads/upgrade/v0.4.1-polish
local  HEAD: e0216517d43a34c192021b27a5f7b079dee67bbc
remote HEAD: e0216517d43a34c192021b27a5f7b079dee67bbc
0       0
```

- 本地与远程目标分支完全一致
- 本地分支已跟踪 `origin/upgrade/v0.4.1-polish`
- 远程 `main` 未变化
- 没有其他远程分支或标签

## 10. GitHub 页面检查

通过 GitHub API 和远程 Git 引用检查：

1. 代码可访问：目标分支指向 `e021651`。
2. `README.md` 正常：远程 blob `ecc4ac0e805c529e8adf3e432c8bae5b8314c87b`，10590 字节。
3. `LICENSE`：GitHub 识别为 **LGPL-2.1**。
4. `src/qdarkstyle/LICENSE.rst` 存在，19175 字节，blob `e22b6873534d628e98f7c0f1ef0d899c624f07d2`。
5. Stage 2–3.4 与 Stage 4 内容存在：
   - `src/module/ntag/`
   - `src/lf/parser/`
   - `src/module/amiibo/`
   - `tests/`
   - `docs/STAGE*`、LF/Amiibo 文档
6. `.gitignore` 存在且已包含新增构建输出规则。
7. 远程树共 1017 个条目；未发现 `.exe/.dll/.o/.log/.dmp/.bin`、`build/`、`dist/` 等构建产物。
8. 目标分支最新提交为 `e021651`。
9. 仓库默认分支仍为 `main`，未修改。

## 11. 推送后的本地修改

本报告在推送后更新，因此本地工作区当前有一项未提交修改：

```text
M docs/STAGE5_GITHUB_PUBLISH_REPORT.md
```

该修改**未包含在远程 `e021651` 中**，属于后续文档更新。本地 HEAD 仍为 `e021651`，与远程一致。

## 12. Stage 5.2 发布后修复

### 12.1 Git Credential Helper

原始问题：

- 全局 `credential.helper` 直接写入含空格的 GCM 路径，Git 通过 shell 调用时被拆分。
- GCM 可执行文件目录和 `C:\msys64\usr\bin` 未出现在当前进程 PATH 中，GCM 无法定位 `git.exe`。
- 多个因素叠加导致 `git push` 等待认证或 helper 报错。

修复：

- 用户级 PATH 已包含 `C:\msys64\usr\bin` 和
  `C:\Users\van\AppData\Local\Programs\Git Credential Manager`。
- 全局配置改为 GCM 官方短名：

```text
git config --global credential.helper manager
```

- GCM 版本：`3.0.2+0d95999b8fb39bd1045770fe1d69f6a2b07101d7`
- Git 版本：`2.56.0`
- 使用 `git credential fill` 验证：返回 username/password，未打印任何凭据内容。

结果：**已修复**。

### 12.2 Git 作者身份

- 仓库级 `user.name`：`VANsMAN-BBS`
- 仓库级 `user.email`：`68843740+VANsMAN-BBS@users.noreply.github.com`
- 已由用户确认使用该 GitHub noreply 身份。
- 已发布历史未修改。

结果：**已配置，仅影响后续提交**。

### 12.3 Amiibo 测试本机路径

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

该测试只验证 `requiresAsciiClientPath()` 对 ASCII/非 ASCII 字符的判断，不访问文件系统，因此无需新增 fixture。

验证：

- 从 `tests/amiibodata/build/release` 运行：22 passed
- 从 `tests/` 目录运行：22 passed
- 从项目根目录运行：22 passed
- 测试文件已不再包含本机绝对路径

结果：**已修复**。

### 12.4 GitHub 默认分支

- GitHub API 显示当前账号对仓库 `permissions.admin = true`
- 尝试 PATCH `default_branch = upgrade/v0.4.1-polish`
- GitHub 返回 **403**：

```text
Resource not accessible by personal access token
```

现有凭据可推送代码，但无权修改仓库设置，因此默认分支仍为 `main`。

结果：**PENDING**。

需要用户在 GitHub 网页执行：

```text
Repository → Settings → Branches → Default branch → 切换到 upgrade/v0.4.1-polish → Update
```

### 12.5 Stage 5.2 验证

- Clean Build：rc=0，0 warnings，0 errors
- 自动化测试：
  - ntagmodel：67 passed
  - amiibodata：22 passed
  - lfparser：59 passed
  - lfparser_stage2：126 passed
- `git diff --check`：通过（i18n 保持仓库 CRLF 风格）
- 未修改已发布历史、许可证、Amiibo 原始数据、NTAG 写入保护或 Stage 3.3/3.4 逻辑

## 13. 当前状态

- 本地分支：`upgrade/v0.4.1-polish`
- 已发布远程 HEAD：`095fea615a54d533c368d7f5553e42561a987d86`
- Stage 5.2 代码修复已提交并推送：`095fea615a54d533c368d7f5553e42561a987d86`
- 默认分支：仍为 `main`（PENDING）
- 已发布历史作者仍显示 `Audit Agent <audit@local>`；未重写历史

## 14. 结论

- Stage 5.1 推送成功，本地与远程 `e021651` 一致。
- Stage 5.2 已修复凭据链路、作者身份和 Amiibo 测试路径。
- GitHub 默认分支切换因令牌权限不足未完成，标记 PENDING。
- 未上传构建产物、用户数据或认证凭据。
