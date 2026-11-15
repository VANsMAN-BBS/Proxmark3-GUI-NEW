# 许可证与依赖审计

> **免责声明：** 本文是工程层面的许可证分析，**不是法律意见**。涉及最终分发、商用或闭源时，必须由法务复核。

---

## 1. 两个项目的许可证（实测）

| 项目 | 许可证 | 证据 |
|---|---|---|
| **Proxmark3GUI**（主项目） | **LGPL-2.1**（第 479 行含 "or (at your option) any later version" → 实为 **LGPL-2.1-or-later**） | `LICENSE` |
| **Phosphor** | **GPL-3.0**（`Cargo.toml` 写 `GPL-3.0-only`） | `LICENSE`、`Cargo.toml:6` |
| **Iceman proxmark3**（Client/固件） | **GPL-3.0** | GitHub API `license.spdx_id = GPL-3.0` |
| **Qt 5.15** | LGPL-3.0 / GPL-2.0+ / 商业 | 发行包内为动态 DLL |

### 1.1 兼容性判定

| 组合 | 是否可直接合并 |
|---|---|
| LGPL-2.1-or-later 代码 + GPL-3.0 代码 | **可以，但整个组合作品必须按 GPL-3.0 分发**（因为 LGPL-2.1 §3 允许升级到 GPL，而 GPL-3.0 代码不能降级） |
| GPL-3.0 代码 + LGPL-2.1-only 代码 | **不可以**（GPL-2.1-only 与 GPL-3.0 不兼容） |
| 主项目（LGPL-2.1-or-later）+ 直接复制 Phosphor 代码 | **技术上可行，但会强制整个 Proxmark3GUI 变为 GPL-3.0** |

**结论：把 Phosphor 源码复制进主项目 = 主项目必须整体改为 GPL-3.0。**
这对一个已有独立作者、已有发布历史的 LGPL 项目是**重大变更**，需要所有版权人同意。

> 因此本次审计的建议是：**不复制代码，只参考行为并独立重写。**

---

## 2. Phosphor 的依赖清单

### 2.1 Rust（`src-tauri/Cargo.toml`）

**直接依赖 15 个：**

| crate | 版本 | 常用许可证 | 用途 |
|---|---|---|---|
| `tauri` | 2.10.2 | MIT / Apache-2.0 | 应用框架 |
| `tauri-build` | 2.5.5 | MIT / Apache-2.0 | 构建脚本 |
| `tauri-plugin-opener` | 2.5.3 | MIT / Apache-2.0 | 打开外部链接 |
| `tauri-plugin-shell` | 2.3.5 | MIT / Apache-2.0 | 启动子进程（**关键**：用于调 `proxmark3.exe`） |
| `serde` / `serde_json` | 1.0.x | MIT / Apache-2.0 | 序列化 |
| `rusqlite`（bundled） | 0.31.0 | MIT | 本地数据库（**bundled 会编译 SQLite，SQLite 为 Public Domain**） |
| `regex` | 1.12.3 | MIT / Apache-2.0 | **全部 LF 解析** |
| `tokio` | 1.49.0 | MIT | 异步运行时 |
| `log` / `env_logger` | 0.4 / 0.11 | MIT / Apache-2.0 | 日志 |
| `thiserror` | 1.0.69 | MIT / Apache-2.0 | 错误类型 |
| `chrono` | 0.4.43 | MIT / Apache-2.0 | 时间戳 |

**传递依赖：`Cargo.lock` 共 531 个 package。**

### 2.2 前端（`package.json` + `package-lock.json`）

**直接依赖 9 个：**

| 包 | 版本 | 许可证（公开信息） |
|---|---|---|
| `@tauri-apps/api` | ^2 | MIT / Apache-2.0 |
| `@tauri-apps/plugin-opener` | ^2 | MIT / Apache-2.0 |
| `@tauri-apps/plugin-shell` | ^2 | MIT / Apache-2.0 |
| `@xstate/react` | ^6.0.0 | MIT |
| `react` / `react-dom` | ^19.1.0 | MIT |
| `xstate` | ^5 | MIT |

**devDependencies 7 个**：`@tailwindcss/vite`、`@tauri-apps/cli`、`@types/react*`、`@vitejs/plugin-react`、`tailwindcss`、`typescript`、`vite`（均 MIT / Apache-2.0 / ISC 类）。

**传递依赖：`node_modules/*` 条目 171 个。**

### 2.3 需要进一步核查的点

| 点 | 风险 | 说明 |
|---|---|---|
| 531 个 Rust crate 的许可证组合 | 中 | 绝大多数为 MIT/Apache-2.0/BSD，但可能存在 MPL-2.0 / Zlib / Unicode-DFS 等；**建议对实际使用到的子集做 `cargo license` 扫描**（本机无 Rust 工具链，未执行） |
| 171 个 npm 包 | 低-中 | 前端依赖**不会被移植**，仅作参考，因此风险可忽略 |
| `rusqlite` bundled SQLite | 低 | SQLite 为 Public Domain |
| 字体 / 图标 | 低 | `app-icon.png` 等为项目自有资源，**不得复制** |
| `src/assets/audio/phosphor.mp3` | 低 | 1.4 MB 音频资源，**不得复制** |

---

## 3. 五种"复用方式"及其义务

| 方式 | 具体行为 | 许可证义务 | 本次建议 |
|---|---|---|---|
| **A. 仅参考公开行为** | 阅读 README/界面/命令用法，独立设计 | **无** | ✅ 允许 |
| **B. 参考命令调用方式** | 知道"用 `lf hid clone -w … --fc … --cn …`" | **无**（命令语法是事实/接口，非表达性代码） | ✅ 允许 |
| **C. 重新实现相同功能** | 自己写解析器/状态机，不复制表达式 | **无**（独立创作） | ✅ **推荐** |
| **D. 直接复制源代码** | 把 `.rs` / `.ts` / `.tsx` 拷进主项目 | **必须遵守 GPL-3.0** → 主项目整体 GPL-3.0 + 保留版权声明 + 提供源码 | ❌ 不建议 |
| **E. 移植算法或代码片段** | 把 Rust 函数改写成 C++，逻辑逐行对应 | 法律上通常仍视为**衍生作品** → 同 D | ❌ 不建议 |

### 3.1 灰色地带：正则表达式与命令字符串

| 内容 | 性质 | 风险判断 |
|---|---|---|
| 单个正则，例如 `EM 410x ID\s*[-:]?\s*([0-9A-Fa-f]{10})` | 高度功能性、表达空间极小 | **低**，但并非零风险 |
| 22 条 clone 命令模板 | 直接来自 Client 帮助文档 | **低**（属于接口事实） |
| `parse_lf_search` 的**整体决策树**（顺序、回退、优先级） | 有创作性（结构选择） | **中**：逐行照搬结构 + 命名会被认定为复制 |
| `T5577Status` 结构体字段 | 功能性 | 低 |
| 卡型显示名/描述文案 | 表达性 | **中**：不要照抄英文描述句 |

**建议做法（降低风险）：**

1. 解析规则以**数据**形式重写（JSON 配置），不复制 Rust 代码结构。
2. 函数命名、注释、文件组织**完全按主项目风格**重写。
3. 决策顺序按**本项目 Client 实际输出**重新设计，而不是照搬 Phosphor 的 22 段 if 链。
4. 在源码/文档中记录"参考了 Phosphor 的公开行为（GPL-3.0），未复制代码"，作为审计痕迹。

---

## 4. 逐文件复用建议

| Phosphor 文件/内容 | 建议 | 理由 |
|---|---|---|
| `pm3/connection.rs` | **不参考** | 子进程模型与主项目 `ClientSession` 完全不同，参考无价值 |
| `pm3/command_builder.rs` 的命令字符串 | **参考**（重写为 JSON/常量） | 命令语法来自 Client 帮助，属事实 |
| `pm3/command_builder.rs` 的 `build_clone_command` 分支逻辑 | **重新设计** | 分支结构有创作性；且存在已知缺陷 |
| `pm3/output_parser.rs` 的单个正则 | **重新编写** | 功能性强，但仍应独立编写并加注释来源 |
| `pm3/output_parser.rs` 的决策树 | **重新设计** | 见 §3.1 |
| `pm3/output_parser.rs` 的 225 个测试 | **不复制**，但**参考测试用例类型** | 测试数据（真实输出样本）本身是 PM3 的输出，不是 Phosphor 的创作；但测试代码是 |
| `cards/types.rs` 枚举 | **参考**（卡型列表是行业事实） | 独立命名 |
| `state.rs` 状态机 | **参考思想**，用 C++ 重写 | 状态机模式是通用设计 |
| `commands/write.rs` 的 T5577 流程 | **参考步骤顺序**，重写 | 步骤顺序由硬件/协议决定 |
| 前端 `*.tsx` / `*.ts` | **不移植** | 技术栈不同 |
| `app-icon.png`、`audio/*.mp3`、`icons/*` | **禁止复制** | 明确的美术作品 |
| `README.md` 文案 | **禁止照抄** | 表达性文本 |

---

## 5. 主项目自身的许可证义务（既有，非本次新增）

| 义务 | 现状 | 说明 |
|---|---|---|
| LGPL-2.1 源码提供 | 已有 Git 仓库 | 分发二进制时需提供对应源码或书面offer |
| Qt LGPL-3.0 动态链接 | `dist/Proxmark3GUI/*.dll` | 动态链接 + 可替换 DLL，满足 LGPL 要求 |
| 允许用户重新链接 | DLL 形式 | 已满足 |
| 版权声明 | `LICENSE` 存在 | 新增文件不应删除既有声明 |

**LF 移植不会改变这些义务**（纯新增 C++ 代码，仍在 LGPL 体系内）。

---

## 6. 关于 Iceman Client 的 GPL-3.0

**关键事实：主项目不包含 Client 二进制。**

`dist/Proxmark3GUI/` 内只有 Qt DLL 与 `Proxmark3GUI.exe`，**没有 `proxmark3.exe`**（实测目录列表）。
Client 由用户自行安装/指定路径，主项目通过 `QProcess` 以**独立进程**方式调用。

**法律意义：**

- 进程边界 = 独立程序（arm's length），**不构成衍生作品**；
- 因此 GUI 的 LGPL-2.1 与 Client 的 GPL-3.0 **不冲突**；
- 这与 Phosphor 不同：Phosphor 把 `proxmark3.exe` 作为 **sidecar 一起分发**（`tauri.conf.json` 的 `externalBin`），所以它必须整体 GPL-3.0（它确实是）。

**对移植的影响：**

- 主项目**不要**把 Client 打进安装包，否则会引入 GPL-3.0 分发义务；
- 现有 `deploy/deploy.py` 与 `dist/` 结构已经避免这一点，应保持。

---

## 7. 外部协议/数据格式的授权问题

| 对象 | 是否有版权 | 说明 |
|---|---|---|
| EM4100 / HID / Indala 等**协议本身** | 否 | 协议是事实/标准，不可版权 |
| Wiegand 位格式（H10301 等） | 否 | 行业标准 |
| T5577 数据手册的位定义 | 否（事实），但**手册文本**受版权 | 不要复制手册表格文本，按事实重写 |
| Client 的编码实现代码 | 是（GPL-3.0） | 不复制；通过命令行调用即可 |
| Phosphor 的卡型描述文案 | 是 | 不照抄 |

---

## 8. 最终建议

### 8.1 可以直接复用（无许可证负担）

1. **命令名称与参数语法**（来自 Client 帮助，属接口事实）。
2. **协议字段的语义**（FC/CN/raw 的含义）。
3. **PM3 的真实输出样本**（用于测试夹具；输出是设备产生的数据）。
4. **设计思想**（状态机分步、先检测后写入、写后复验）。

### 8.2 建议重新实现（必须）

1. `parse_lf_search` 的决策树与所有正则。
2. `build_clone_command` 的分支逻辑。
3. `WizardMachine` / `wizardMachine.ts` 的状态机。
4. 所有 UI 组件、文案、图标。

### 8.3 需要保留版权声明的部分

如果最终**确实**复制了任何 Phosphor 片段（不建议），必须：

- 在主项目 `LICENSE` 中追加 GPL-3.0 说明；
- 在每个受影响文件头部加 `SPDX-License-Identifier: GPL-3.0-only` 与来源；
- 提供完整源码；
- 接受整个 Proxmark3GUI 变为 GPL-3.0。

### 8.4 需要进一步法律审查的部分

1. 若计划**商业分发**或**闭源插件**：LGPL-2.1 + GPL-3.0 的组合必须重新评估。
2. 若计划把 `proxmark3.exe` 打进安装包：需按 GPL-3.0 履行分发义务（提供源码/offer）。
3. 531 个 Rust crate 中若存在 GPL/AGPL 依赖（未扫描），仅影响 Phosphor 自身，不影响主项目；但若将来引入任何 Rust 组件需重新审计。
4. 若从 T5577 数据手册摘录位定义表，需确认手册的再分发条款。

---

## 9. 一句话结论

> **Phosphor 是 GPL-3.0，主项目是 LGPL-2.1-or-later。代码级复制会把主项目拖入 GPL-3.0。**
> **本次移植必须采用"参考行为 + 独立重写"路径；不需要引入 Phosphor 的任何依赖；不复制任何资源文件与文案。**
