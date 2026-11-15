# LF Parser Standalone

A dependency-free (Qt5 Core only) C++11 library that turns Proxmark3 client **LF text output** into structured card data.

- 22 card protocol parsers (EM4100, HID Prox, AWID, Indala, IO Prox, FDX-B, Paradox, Keri, Viking, Pyramid, NexWatch, Presco, NEDAP, G-Prox-II, Gallagher, PAC/Stanley, Noralsy, Jablotron, SecuraKey, Visa2000, Motorola, IDTECK)
- Separate read-only T55xx / T5577 status and block0 decoder
- Evidence-scored dispatch (no `contains` chains, no cross-protocol fallback)
- 126 QtTest cases, all passing, zero compiler warnings
- No main-project, hardware, firmware or Phosphor dependency

**License:** LGPL-2.1-or-later (same as Proxmark3GUI). See `LICENSE_NOTES.md`.

---

## 1. Requirements

| Item | Value |
|---|---|
| C++ | C++11 or newer |
| Qt | Qt 5.12+ (only `QtCore`; tests need `QtTest`) |
| Build | qmake **or** CMake 3.16+ |
| OS | Windows (verified), Linux/macOS expected to work |
| Hardware | **none** — pure text parsing |

Verified environment: Qt 5.15.19, MinGW-w64 GCC 16.2.0 (MSYS2 UCRT64), Windows 11 x64.

---

## 2. Build with qmake (verified)

```powershell
$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"   # adjust to your Qt/MinGW
mkdir build
cd build
qmake-qt5 ..\tests\lfparser_tests.pro
mingw32-make -j4
release\tst_lfparser.exe
```

`tests/lfparser_tests.pro` builds the library sources and the test binary together, so no separate library install step is required.

## 3. Build with CMake

```powershell
cmake -S . -B build-cmake -DCMAKE_PREFIX_PATH=C:/Qt/5.15.2/mingw81_64
cmake --build build-cmake -j4
ctest --test-dir build-cmake --output-on-failure
```

`LF_PARSER_BUILD_TESTS` and `LF_PARSER_BUILD_EXAMPLES` can be set to `OFF`.

> Note: the CMake files are provided and syntactically complete, but the only
> CMake available in the authoring environment was the MSYS POSIX build, which
> does not offer the `MinGW Makefiles` generator. The qmake build is the
> verified path; CMake should be validated once in your toolchain.

---

## 4. Use in another Qt project

Add the sources and include path:

```qmake
INCLUDEPATH += path/to/LF_Parser_Standalone/include
SOURCES += \
    path/to/LF_Parser_Standalone/src/lf/lfparsetypes.cpp \
    path/to/LF_Parser_Standalone/src/lf/lftextnormalizer.cpp \
    path/to/LF_Parser_Standalone/src/lf/lfparserregistry.cpp \
    path/to/LF_Parser_Standalone/src/lf/t55xxparser.cpp \
    path/to/LF_Parser_Standalone/src/lf/parsers/*.cpp
QT += core
```

Then:

```cpp
#include <lf/lfparser.h>

const LFParseResult result = LFParserRegistry::instance().parse(clientOutput);

if (result.status == Lf::Status::Identified) {
    qInfo("protocol=%s format=%s",
          qPrintable(Lf::protocolName(result.protocol)),
          qPrintable(result.format));
}
if (result.facilityCode.isPresent())
    qInfo("FC=%u", result.facilityCode.value());
```

A complete example is in `examples/parse_text_example.cpp`.

---

## 5. What it does and does not do

**Does**

- Parse `lf search` and protocol reader text.
- Distinguish Identified / Partial / Unidentified / InvalidInput.
- Keep missing fields absent (`Lf::Optional<T>`), never 0.
- Report warnings and unparsed details.
- Parse T55xx detect/info text and decode block0 bits.

**Does not**

- Decode waveforms (Manchester/FSK/PSK stay in the client/firmware).
- Execute Proxmark3 commands.
- Write, erase, lock, simulate or clone cards.
- Depend on any UI, process manager or hardware.

---

## 6. Layout

```
include/lf/            public headers
src/lf/                implementation
src/lf/parsers/        one class per protocol
tests/                 QtTest project, fixtures, reports
examples/              minimal CLI example
docs/                  architecture, matrix, formats, reference, test report
```

Documentation index:

| File | Contents |
|---|---|
| `docs/ARCHITECTURE.md` | data model, dispatch, flow |
| `docs/CARD_SUPPORT_MATRIX.md` | every audited card type and its status |
| `docs/CLIENT_OUTPUT_FORMATS.md` | client formats and field meanings |
| `docs/PARSER_REFERENCE.md` | per-parser inputs, outputs, limits |
| `docs/TEST_REPORT.md` | coverage and results |
| `docs/INTEGRATION_GUIDE.md` | copy/registration/build integration |
| `LICENSE_NOTES.md` | provenance and GPL risk review |
| `CHANGELOG.md` | Stage 1 → Stage 2 changes |
