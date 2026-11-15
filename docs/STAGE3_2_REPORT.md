# Stage 3.2 报告：Amiibo 多格式模拟兼容性与批量导入

日期：2026-11-15  
项目：`E:\Opencode_project\Proxmark3GUI-master`

## 1. 开工审计

- Git 可用：`C:\msys64\usr\bin\git.exe`。
- 分支：`upgrade/v0.4.1-polish`
- HEAD：`245b184`
- 工作区：Stage 2/3/3.1 的未提交修改和未跟踪文件仍在；本轮未 reset/checkout/clean/rebase，未提交，未移动标签。
- 修改前基线：Stage 3.1 报告、Amiibo 页面/模型、NTAG 页面、`ClientSession`、主题 QSS 均已重新核对；基线 Clean Build 为 0 warning。
- 当前 Client：`E:\Opencode_project\pm3-256-iceman-v4.23346\proxmark3.exe`；源码：`pm3-256-iceman-v4.23346-src`。

## 2. 当前 Client 对 MFU dump 的实际支持

根据 `client/src/cmdhfmf.c`、`cmdhfmfu.c`、`fileutils.c`：

- `hf mfu eload` 参数：`-f <file>`、`-q <blocks>`、`-v`；内部调用 `CmdHF14AMfELoad(..., --ul)`。
- `CmdHF14AMfELoad` 使用 `pm3_load_dump()` 读文件，再调用 `convert_mfu_dump_format()`。
- 支持格式：
  - **new**：56 字节 `mfu_dump_t` 头 + 页数据；
  - **old**：48 字节旧头 + 页数据；
  - **plain**：无头页数据。
- plain 识别条件是 `data[3] == 0x88 ^ data[0]^data[1]^data[2]` 且 `data[8] == data[4]^data[5]^data[6]^data[7]`；**不是 540 字节就自动接受**。
- 识别失败输出 `Failed to detected mfu dump format` / `Failed convert on load`；即使随后打印 `Done!`，加载也已失败。
- `hf mfu sim -t 7` 只使用 emulator memory：`mfu_dump_t` 头（版本/页数/签名/计数器）在偏移 0，页数据从偏移 56 开始；页数取头中 `pages` 字段。
- `hf mf eclr` 可清空 emulator memory；`hf mf esetblk -b <block> -d <16 bytes>` 可写 16 字节块；`hf mfu esetblk --blk <page> -d <pages>` 可写从页号开始的整页数据（内部偏移 +14 页）。
- 没有发现独立于 `eload` 的“raw simulation / legacy simulation”命令；不能虚构模式。

因此新增的“直接页加载”不是新 Client 命令，而是组合现有官方命令：`hf mf eclr` + `hf mf esetblk`（仅 emulator 头）+ `hf mfu esetblk`（原始页）+ `hf mfu sim -t 7`。

## 3. eload 与 sim 的关系

`eload` 只负责把 dump 转成 emulator memory；`sim` 不读原文件，只读 emulator memory。  
因此加载阶段必须以 `Detected ... mfu dump format` 为成功证据，不能以 `Loaded`/`Upload`/`Done!` 判断。

## 4. 新增模拟兼容策略

UI：模拟区新增“加载方式”下拉框，默认“自动兼容”。

| 模式 | 实现 | 说明 |
|---|---|---|
| 自动兼容 | 先 Standard；失败后若本地预检通过，再 Direct | 默认 |
| 标准 MFU 加载 | `hf mfu eload -f <path>` | 保留原链路；要求 Client 明确输出格式识别 |
| 直接页加载（兼容） | `hf mf eclr` → `hf mf esetblk` 头 → `hf mfu esetblk` 页（16 页/批）→ sim | 只写 emulator memory，不修改 BIN；要求 540 B、CC `E1 10 12/3E/6D 00`、UID 非空 |

每模式记录实际命令、完整 Client 输出、成功/失败原因；失败后不会启动 sim。写入失败模式会 `hf mf eclr` 清理，避免部分状态被误用。
加载方式通过 `Amiibo/loadMode` 记住，默认 `auto`。

## 5. Ace 文件根因与结果

文件：

`E:\Opencode_project\amiibo\Animal Crossing\Ace (1b000103d205, Decrypted).bin`

- 大小：540 B
- SHA-256：`0bc10c7949f022cc92d10bb79d9e77a7297dce4528ab19ba8204be936b3f7553`
- 仅有 9 个非零字节：偏移 `0x02,0x03,0x1DC,0x1DD,0x1DF,0x1E0..0x1E3`
- page0：`00 00 0F E0`；page1：全零；page2：全零；page3（CC）：`00 00 00 00`
- BCC0：期望 `0x87`，实际 `0xE0`；BCC1：期望 `0x00`，实际 `0x00`

结论：

1. `eload` 失败的直接原因是 plain 格式 BCC0 校验失败。
2. 更深层原因是文件内容不是有效 NTAG 镜像：CC 缺失、UID 基本为空，绝大多数页为 0。
3. 标准模式失败；直接页加载预检拒绝（无有效 CC/UID），因此**所有已实现策略均失败**。
4. 没有伪造成功，也没有修改原文件。
5. 对 `E:\Opencode_project\amiibo` 下 932 个 BIN 做静态检查：按 plain BCC+CC 规则 **0 个通过**，全部呈相同稀疏/零填充模式。这不是单一文件格式差异，更像是整个文件集合的内容损坏/占位；需要用户重新获取有效 BIN。

## 6. 不同 BIN 兼容性结果

| 文件 | 大小 | SHA-256 | 标准 | 自动/直接 | 结果 |
|---|---:|---|---|---|---|
| `amiibo plain 540.bin` | 540 | `f0a8ac48...f54ae59e` | 成功 | 直接页加载命令级成功 | 可模拟 |
| `amiibo-badbcc.bin`（有效内容，故意改 BCC0） | 540 | `14928de9...6fce21` | 失败：格式识别 | 自动回退 Direct 成功 | 可模拟；BIN 未变 |
| `Ace (...Decrypted).bin` | 540 | `0bc10c79...3f7553` | 失败 | Direct 预检拒绝 | 无法模拟（内容无效） |
| 导入后修改的文件 | 541 | — | — | 大小/SHA-256 检查拒绝 | 不启动 |
| `E:\Opencode_project\amiibo` 全库 932 个 | 540 | — | 0 个通过 BCC/CC 静态检查 | 不适用 | 内容疑似损坏 |

## 7. 多文件导入

- 使用 `QFileDialog::getOpenFileNames()`。
- 单次可选择多个 `.bin`；过滤器 `*.bin`，可切换所有文件。
- 每个文件独立读取/计算 SHA-256/创建条目；一个失败不影响其他文件。
- 多文件导入后显示汇总对话框：成功/跳过/失败 + 可展开详细结果。
- 已用两个路径验证：一次导入 plain 与 badBCC，库中新增 2 条。

## 8. 文件夹递归导入

- 新增 `Import folder...`。
- 使用 `QDirIterator` 递归扫描 `*.bin`/`*.BIN`。
- 不读取其他扩展名；不移动、不删除、不覆盖源文件。
- 每 200 个文件更新状态并处理事件，避免扫描阶段完全卡死。
- 测试目录：`PM3-HW-Test-Stage3\batch`
  - `a.bin`、`sub/b.bin`、`dup.bin`（同 SHA）、`sub/deep/c.bin`（badBCC）、`empty.bin`、`a.png`、`notes.txt`
  - 第一次：成功 4，失败 1（空文件），PNG/文本被忽略。
  - 第二次：跳过 4，失败 1；库中条目数不增加。
- 另做了 60 个 BIN 的嵌套目录导入；扫描/导入完成，但 UIA 只暴露约 39 个可见行（列表虚拟化），**没有用 UI 行数精确核对 60 条**。

## 9. 重复文件规则

- 规范化路径（`canonicalFilePath`，回退 `absoluteFilePath`，小写）相同 → 跳过。
- 相同 SHA-256、不同路径 → 保留为不同条目，并在详细结果中提示“same SHA-256 as ...”。
- 不按文件名判断重复。
- 不自动覆盖已有条目。
- 单文件失败不终止批次。

## 10. 批量导入测试结果

- 多选 2 个文件：成功 2。
- 4 个有效 BIN + 1 个空 BIN 的递归目录：成功 4、失败 1；再次导入跳过 4、失败 1。
- 空文件夹：提示未找到 BIN。
- 同一 SHA-256 的 3 个路径：全部保留，详情提示重复哈希。
- 源文件 SHA-256 在导入前后保持不变。
- sidecar PNG：库条目选中时沿用 Stage 3.1 的 `<basename>.png` 加载逻辑。

## 11. 修改文件

- `src/module/amiibopage.h/.cpp`
  - 加载方式下拉框与三种策略；
  - Standard/Direct 加载函数、格式/错误判断；
  - 多文件选择、递归文件夹导入、批量汇总、重复处理；
  - 加载方式持久化；日志增强。
- `i18n/zh_CN.ts`、`en_US.ts`、`zh_CN.qm`、`en_US.qm`
  - 新增 41 条翻译；最终 1020 finished / 0 unfinished。
- `scripts/translate-stage3_2.py`
- `docs/STAGE3_2_REPORT.md`

未修改 NTAG、LF、ClientSession、CommandQueue、主题、在线目录接口。

## 12. 自动化测试与构建

| 项目 | 结果 |
|---|---|
| `tests/ntagmodel` | **62 passed / 0 failed** |
| `tests/amiibodata` | **22 passed / 0 failed** |
| `tests/lfparser` | **59 passed / 0 failed** |
| `tests/lfparser_stage2` | **126 passed / 0 failed** |
| Clean Build | **rc=0，0 warning，0 error** |
| 翻译 | **1020 finished / 0 unfinished** |

## 13. 真实硬件验证情况

- Proxmark3 设备存在（COM14）；`hf mf eclr` / `hf mf esetblk` / `hf mfu esetblk` / `hf mfu eview` 命令级验证通过。
- 直接页加载后 `hf mfu sim -t 7` 成功进入模拟（Client 输出 “Press pm3 button or <Enter> to abort simulation”）；未用真实手机/读卡器验证 Amiibo 识别。
- 未检测到 NTAG216 实体标签（`hf mfu info` / `hf 14a info` 无响应），因此没有进行标签读写或真实 Amiibo 读取验证。
- 本轮未执行任何 NTAG 写入，Stage 3 基准镜像未被覆盖。

## 14. 未验证与遗留

1. Ace 无法通过任何现有 Client 原生策略恢复；文件内容疑似损坏，需要重新获取有效 BIN。
2. 直接页加载在真实 Amiibo 读卡器/手机上的行为未验证；仅验证了模拟命令启动。
3. 932 个库文件全部无法通过 BCC/CC 静态检查，未逐个用 GUI 模拟（内容明显稀疏）。
4. 60 文件批量导入的精确条目数未用 UI 读取（列表虚拟化），只确认扫描/导入完成；4+1 与 2 文件用例精确验证。
5. 在线目录/图片功能未重新测试（本轮未改其接口）；沿用 Stage 3.1 已验证结果。
6. 未执行 NTAG 写入回归（标签不在场）。
