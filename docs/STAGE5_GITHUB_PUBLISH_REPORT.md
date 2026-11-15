# Stage 5 报告：GitHub 首次发布与远程仓库同步

日期：2026-11-15

## 1. 本地仓库审计

- 分支：`upgrade/v0.4.1-polish`
- 起始 HEAD：`1f0036ceb02d9b249fa0d6b32452094da49ca70c`
- 当前本地历史：

```text
(本报告所在提交) chore(license): add QDarkStyleSheet notices and ignore build outputs
1f0036c refactor(ui): consolidate LF, NTAG and navigation layouts
b222668 feat: add NTAG, Amiibo and LF functionality
245b184 (tag: upgrade-v0.4.1) fix: keep eMRTD protocol output UTF-8 on Windows pipes
```

- 已跟踪工作区：干净
- 暂存区：空
- 未跟踪构建输出已通过 `.gitignore` 忽略，未删除任何文件

## 2. 远程仓库状态

目标：

```text
https://github.com/VANsMAN-BBS/Proxmark3-GUI-NEW.git
```

检查结果：

- 仓库公开、非 fork、默认分支 `main`
- 远程只有 `main` 一个分支
- 远程 `main`：`1bbdde0b8c757f257b9cbdf0a3269da98cf3f428`
- 无其他分支、无标签
- 初始化提交仅包含：
  - `LICENSE`：GNU LGPL-2.1，504 行
  - `README.md`：2 行通用说明
- 本地 `LICENSE` blob 与远程初始 `LICENSE` 完全一致：
  `8000a6faacf471c537530805ab29523c7732e11a`
- 本地 `README.md` 是完整项目 README，与远程 2 行 README 不同
- 目标分支 `upgrade/v0.4.1-polish` 在远程不存在

结论：远程没有代码成果；推送目标为**新分支**，不会覆盖 `main` 的 README/LICENSE，不需要 force。

## 3. origin 配置

已配置：

```text
origin  https://github.com/VANsMAN-BBS/Proxmark3-GUI-NEW.git (fetch)
origin  https://github.com/VANsMAN-BBS/Proxmark3-GUI-NEW.git (push)
```

未配置其他 remote，未修改 remote URL。

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
  这不是密钥或用户数据，但属于本机绝对路径，建议后续改为 fixture 相对路径。
- 邮箱匹配均为占位符 `name@example.com` 或 `@2x.png` 文件名误报，未发现个人邮箱。

## 5. 用户数据与构建产物

- 仓库没有跟踪任何 `.bin` 文件。
- 没有跟踪 NTAG/Amiibo 用户数据。
- 没有跟踪 `dist/`、`data/`、根 `build*`。
- 原有的 tests/deliverables 构建输出未被提交、未被删除，现已通过 `.gitignore` 忽略。

## 6. 许可证审计

### 6.1 主项目

- 根 `LICENSE`：GNU LGPL-2.1，包含 “or (at your option) any later version”，实际为 **LGPL-2.1-or-later**
- 本地 `LICENSE` 与远程初始化 `LICENSE` 完全一致
- README 声明主项目 LGPL-2.1；Proxmark3 Client 作为独立进程、GPLv3，不分发 Client 二进制
- LF Parser 审计文档确认：`src/lf/`、`tests/lfparser*` 为独立实现，未复制 Phosphor（GPL-3.0）或 Iceman Client 代码
- 未引入 GPL-3.0 代码，未修改主项目许可证类型

### 6.2 QDarkStyleSheet 合规修复

原仓库包含 QDarkStyleSheet 的样式和 209 个图片资源，但只有一行来源说明，缺少 MIT/CC-BY-4.0 许可文本。

根据上游 commit `6ff5fdf` 的 `LICENSE.rst`，已新增：

- `src/qdarkstyle/LICENSE.rst`
  - MIT License（代码）
  - CC-BY-4.0（图片）
  - 上游版权归属
- `src/qdarkstyle/README.md`
  - 补充 Colin Duquesnoy / Daniel Cosmo Pizetta 版权归属
  - 说明代码 MIT、图片 CC-BY-4.0
  - 指向 `LICENSE.rst`

该修复不改变主项目 LGPL-2.1-or-later，也不改变任何功能代码。

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

## 8. 推送命令与当前结果

计划命令：

```text
git push -u origin upgrade/v0.4.1-polish
```

已尝试执行，但本机 Git 没有配置任何凭据：

- `credential.helper`：空
- `GH_TOKEN` / `GITHUB_TOKEN`：未设置
- SSH key：不存在
- SSH agent：未运行
- Git 命令在等待认证时超时，被终止

因此本次**没有完成 push**，远程没有任何变化：

```text
1bbdde0  HEAD
1bbdde0  refs/heads/main
```

目标分支 `upgrade/v0.4.1-polish` 仍不存在于远程。

未执行：

- `git push --force`
- `git push --force-with-lease`
- `git push --all`
- `git push --tags`
- 分支/标签修改

## 9. 推送后验证（待执行）

认证成功后应执行：

```text
git push -u origin upgrade/v0.4.1-polish
git ls-remote origin refs/heads/upgrade/v0.4.1-polish
git status --short
git branch -vv
git log -5 --oneline --decorate
```

预期：

- 远程分支 HEAD 等于本地 HEAD
- `b222668`、`1f0036c` 和本合规提交均存在
- 远程 `main` 仍保持 `1bbdde0`，不被覆盖
- 不推送其他分支和标签

## 10. 未解决问题

1. 本机缺少 GitHub 认证信息，无法完成推送。
2. `tests/amiibodata/tst_amiibodata.cpp` 含本机绝对路径，建议后续改为相对 fixture。
3. 远程 `main` 仍是通用 README；是否将默认分支切换到代码分支需用户后续决定。
4. 是否需要把 `deliverables/` 的独立 LF Parser 作为单独发布物，需用户后续决定。

本地提交和许可证修复已完成，等待配置凭据后推送。
