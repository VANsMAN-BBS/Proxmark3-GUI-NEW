# LF GUI 集成 — 总览

**版本：** v0.4.1 工作区（未提交）
**分支：** `upgrade/v0.4.1-polish`（HEAD `245b184`）
**日期：** 2026-11-14

本目录记录“低频多协议解析”页面并入 Proxmark3GUI 的审计、实施与验证。
M1 保存权限问题单独记录在 `docs/BUG_AUDIT/M1_SAVE_PERMISSION_AUDIT.md`。

## 文档索引

| 文件 | 内容 |
|---|---|
| `SOURCE_INTEGRATION.md` | Stage 1 与 Stage 2 对比、最终目录、`.pro` 变更 |
| `GUI_STRUCTURE.md` | 子标签结构、原有功能保留方式 |
| `PARSER_UI_MAPPING.md` | 解析状态与界面字段映射 |
| `I18N_REPORT.md` | 新增/完成翻译统计 |
| `TEST_REPORT.md` | 126 + 59 项解析器测试、GUI 验证 |
| `HARDWARE_COMPATIBILITY.md` | 当前固件能力与界面表现 |
| `LF_GUI_INTEGRATION_FINAL_REPORT.md` | 交付总结 |
| `legacy_stage1/` | 被替换的 Stage 1 源文件（可回滚） |

## 一句话结果

- 独立模块（22 个解析器 + T55xx 只读）已完整并入 `src/lf/parser/`，主程序 clean build 通过、无新增警告。
- 原有低频页面**未改写**，作为 `LF → 低频功能` 子标签原样保留；新增 `LF → 低频多协议解析` 子标签。
- 解析器测试：**126 项（Stage 2）全部通过**；保留的 Stage 1 回归套件 **59 项全部通过**。
- 解析完全基于文本，当前无 LF 的裁剪固件下仍可验证整个解析流程（手动粘贴 / 导入文本）。
- M1 保存问题**只调查、未修复**。
