# 源码集成报告

## 1. 目标

把独立模块 `deliverables/LF_Parser_Standalone/`（Stage 2）并入主项目，替换主项目里 Stage 1 的子集实现，且不产生重复类、重复实现或重复编译。

## 2. Stage 1 与 Stage 2 对比

| 项目 | Stage 1（主项目 `src/lf/`，原状） | Stage 2（独立模块） |
|---|---|---|
| 协议数 | 4（EM4100、HID Prox、AWID、Indala） | **22** |
| 注册表位置 | `lfparser.h` 内 | `lfparserregistry.h`（`lfparser.h` 兼容包含） |
| `ILfParser` | 存在 | 完全相同 |
| `LFParserRegistry` | 存在 | 相同接口，拆分到独立头文件 |
| `Lf::Protocol` | 5 个值（含 Unknown） | 23 个值（含 Unknown） |
| `LFParseResult` | 存在 | 字段完全一致 |
| T55xx 只读 | 无 | `t55xxparser.h/.cpp` |
| 测试 | 59 项 | 126 项（覆盖 Stage 1 全部协议与同名 fixtures） |
| 目录 | 扁平 `src/lf/*.cpp` | `include/lf/**` + `src/lf/**` |

结论：Stage 2 是 Stage 1 的**源码兼容超集**；同名类没有第二份实现。

## 3. 最终目录

```text
src/lf/
├─ parser/
│  ├─ include/lf/           公共头（<lf/...> 前缀）
│  │  ├─ lfparsetypes.h
│  │  ├─ lftextnormalizer.h
│  │  ├─ lfparser.h
│  │  ├─ lfparserregistry.h
│  │  ├─ t55xxparser.h
│  │  └─ parsers/*.h        22 个协议头
│  └─ src/lf/
│     ├─ lfparsetypes.cpp
│     ├─ lftextnormalizer.cpp
│     ├─ lfparserregistry.cpp
│     ├─ t55xxparser.cpp
│     └─ parsers/*.cpp      22 个协议实现 + parserhelpers.h
├─ (legacy_stage1 已移至 docs/LF_GUI_INTEGRATION/legacy_stage1/)
```

`src/lf/parser/src/lf/parsers/parserhelpers.h` 为内部头，由同目录 `.cpp` 直接包含。

## 4. `.pro` 变更

```qmake
# 公共头使用 <lf/...>
INCLUDEPATH += $$PWD/lf/parser/include

SOURCES += lf/parser/src/lf/lfparsetypes.cpp \
           lf/parser/src/lf/lftextnormalizer.cpp \
           lf/parser/src/lf/lfparserregistry.cpp \
           lf/parser/src/lf/t55xxparser.cpp \
           lf/parser/src/lf/parsers/<22 个协议>.cpp
HEADERS += lf/parser/include/lf/<...>.h
```

同时加入 `module/lfparsertab.cpp/.h`（GUI 页面）。

避免重复编译：
- 原 Stage 1 的 7 个扁平源文件不再出现在 `SOURCES`；
- 原文件已移到 `docs/LF_GUI_INTEGRATION/legacy_stage1/`（保留可回滚）；
- 全项目只保留一份 `LFParserRegistry`、一份 `LFParseResult`。

## 5. 测试工程

| 工程 | 源 | 结果 |
|---|---|---|
| `tests/lfparser_stage2/` | 独立模块 126 项测试 + 集成源码 | 126 passed |
| `tests/lfparser/`（保留） | Stage 1 的 59 项测试 + 集成源码 | 59 passed |

Stage 1 测试文件未删除；其 `.pro` 已指向集成后的源码（注册表需要全部 22 个解析器，故测试工程链接完整源集）。

## 6. 可回滚性

- 新增目录：`src/lf/parser/`、`src/module/lfparsertab.*`、`tests/lfparser_stage2/`、`docs/LF_GUI_INTEGRATION/`。
- 删除（已归档）：`src/lf/*.cpp|*.h` → `docs/LF_GUI_INTEGRATION/legacy_stage1/`。
- `.pro` 中新增/替换的段落集中在 INCLUDEPATH、SOURCES、HEADERS。
- 回滚：删除新增文件与 `.pro` 对应条目，并把 `legacy_stage1/` 移回 `src/lf/` 即可恢复 Stage 1 构建。
