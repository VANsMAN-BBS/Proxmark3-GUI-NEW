# Integration Guide

How to move this module into another Qt project (for example, back into
Proxmark3GUI for Stage 3).

---

## 1. Copy the folder

Copy the whole `LF_Parser_Standalone` directory anywhere. It has no relative
dependency on Proxmark3GUI, Phosphor, a PM3 client, firmware or hardware.

Required subdirectories:

```
include/   public headers
src/       implementation
```

`tests/`, `examples/`, `docs/`, `build/` are optional at runtime.

---

## 2. Include headers

Public includes always use the `lf/` prefix:

```cpp
#include <lf/lfparser.h>          // ILfParser + registry (compatibility)
#include <lf/lfparserregistry.h>  // registry only
#include <lf/t55xxparser.h>       // T55xx read-only API
```

Add the `include` directory to the compiler include path.

---

## 3. Add sources

### 3.1 qmake

```qmake
QT += core

INCLUDEPATH += $$PWD/third_party/LF_Parser_Standalone/include

LF_PARSER_DIR = $$PWD/third_party/LF_Parser_Standalone

SOURCES += \
    $$LF_PARSER_DIR/src/lf/lfparsetypes.cpp \
    $$LF_PARSER_DIR/src/lf/lftextnormalizer.cpp \
    $$LF_PARSER_DIR/src/lf/lfparserregistry.cpp \
    $$LF_PARSER_DIR/src/lf/t55xxparser.cpp \
    $$LF_PARSER_DIR/src/lf/parsers/em4100parser.cpp \
    $$LF_PARSER_DIR/src/lf/parsers/hidproxparser.cpp \
    $$LF_PARSER_DIR/src/lf/parsers/awidparser.cpp \
    $$LF_PARSER_DIR/src/lf/parsers/indalaparser.cpp \
    $$LF_PARSER_DIR/src/lf/parsers/ioproxparser.cpp \
    $$LF_PARSER_DIR/src/lf/parsers/fdxbparser.cpp \
    $$LF_PARSER_DIR/src/lf/parsers/paradoxparser.cpp \
    $$LF_PARSER_DIR/src/lf/parsers/keriparser.cpp \
    $$LF_PARSER_DIR/src/lf/parsers/vikingparser.cpp \
    $$LF_PARSER_DIR/src/lf/parsers/pyramidparser.cpp \
    $$LF_PARSER_DIR/src/lf/parsers/nexwatchparser.cpp \
    $$LF_PARSER_DIR/src/lf/parsers/prescoparser.cpp \
    $$LF_PARSER_DIR/src/lf/parsers/nedapparser.cpp \
    $$LF_PARSER_DIR/src/lf/parsers/gproxiiparser.cpp \
    $$LF_PARSER_DIR/src/lf/parsers/gallagherparser.cpp \
    $$LF_PARSER_DIR/src/lf/parsers/pacparser.cpp \
    $$LF_PARSER_DIR/src/lf/parsers/noralsyparser.cpp \
    $$LF_PARSER_DIR/src/lf/parsers/jablotronparser.cpp \
    $$LF_PARSER_DIR/src/lf/parsers/securakeyparser.cpp \
    $$LF_PARSER_DIR/src/lf/parsers/visa2000parser.cpp \
    $$LF_PARSER_DIR/src/lf/parsers/motorolaparser.cpp \
    $$LF_PARSER_DIR/src/lf/parsers/idteckparser.cpp
```

`src/lf/parsers/parserhelpers.h` is an internal header; it is found because the
`.cpp` files include it from their own directory.

### 3.2 CMake

```cmake
add_subdirectory(third_party/LF_Parser_Standalone)
target_link_libraries(your_target PRIVATE lfparser)
```

or copy `CMakeLists.txt` knowledge:

```cmake
target_include_directories(your_target PRIVATE .../LF_Parser_Standalone/include)
target_sources(your_target PRIVATE .../LF_Parser_Standalone/src/lf/...cpp)
```

---

## 4. Call the parser

```cpp
#include <lf/lfparser.h>

void handleClientOutput(const QString &text)
{
    const LFParseResult result = LFParserRegistry::instance().parse(text);

    switch (result.status) {
    case Lf::Status::Identified:
        // protocol + identity available
        break;
    case Lf::Status::Partial:
        // protocol recognised, fields incomplete: show warnings
        break;
    case Lf::Status::Unidentified:
        // no supported protocol in this text
        break;
    case Lf::Status::InvalidInput:
        // empty input
        break;
    }

    for (const QString &warning : result.warnings)
        qWarning("LF parser: %s", qPrintable(warning));
}
```

Useful getters:

```cpp
Lf::protocolName(result.protocol);   // "HID Prox"
Lf::statusName(result.status);       // "identified"
Lf::confidenceName(result.confidence);
result.facilityCode.isPresent();     // never use value() without checking
result.cardNumber.valueOr(0);
```

T55xx:

```cpp
const LfT55xx::DetectInfo chip = LfT55xx::parseDetect(text);
if (chip.detected) {
    // chip.chipType, chip.passwordSet, chip.block0
}
const LfT55xx::Block0 block = LfT55xx::parseBlock0(QStringLiteral("00148040"));
```

---

## 5. Registering a new parser

1. Add an enum value to `Lf::Protocol` and a case in `Lf::protocolName`.
2. Create `include/lf/parsers/xparser.h` and `src/lf/parsers/xparser.cpp`.
3. Register in `LFParserRegistry::LFParserRegistry()`.
4. Add a priority in `protocolPriority()` if it needs to win ties.
5. Add sources to qmake/CMake.
6. Add a fixture and tests.

---

## 6. Handling unknown cards

- `Unidentified` means "no supported protocol signature"; do not guess.
- `Partial` with a protocol means "recognised but fields incomplete"; show the
  warnings.
- `Ambiguous` appears in `format` for conflicting HID candidates; do not pick
  a format automatically.

Recommended UI behaviour:

| Status | UI |
|---|---|
| Identified | show protocol, format, identity fields |
| Partial | show protocol + warnings, disable clone actions |
| Unidentified | show raw text only |
| InvalidInput | no card panel |

---

## 7. Things not to do

- Do not feed waveform/sample text into this module; it expects client output.
- Do not use `facilityCode` for FDX-B country or Gallagher region; they are
  exposed as extra fields for a reason.
- Do not assume `rawData` exists for all protocols.
- Do not turn any parsed value into a write command inside this module; it has
  no write API by design.

---

## 8. License in the host project

The module is LGPL-2.1-or-later. If the host is Proxmark3GUI, no change is
needed. If the host has a different license, review `LICENSE_NOTES.md` before
copying.
