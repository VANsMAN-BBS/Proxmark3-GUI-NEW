# Architecture

## 1. Purpose

Convert Proxmark3 LF **text** into structured data. No waveform processing, no
client execution, no hardware access.

```
Client text
   │
   ▼
LFTextNormalizer        ANSI / CRLF / NUL / BOM, right-trim
   │
   ▼
LFParserRegistry        detect() score per parser, pick one
   │
   ▼
ILfParser::parse()      one protocol parser
   │
   ▼
LFParseResult           protocol, status, confidence, fields, warnings
```

T55xx output is handled by a **separate** namespace (`LfT55xx`) and is not part
of the card registry.

## 2. Public API

| Header | Contents |
|---|---|
| `lf/lfparsetypes.h` | `Lf::Protocol`, `Lf::Status`, `Lf::Confidence`, `Lf::Optional<T>`, `LFParseResult`, `LfExtraField` |
| `lf/lftextnormalizer.h` | `LFTextNormalizer` |
| `lf/lfparser.h` | `ILfParser` (and includes the registry for compatibility) |
| `lf/lfparserregistry.h` | `LFParserRegistry` |
| `lf/parsers/*.h` | one class per protocol |
| `lf/t55xxparser.h` | `LfT55xx::parseDetect`, `parseInfo`, `parseBlock0` |

### 2.1 Result model

```cpp
struct LFParseResult {
    Lf::Protocol protocol;
    Lf::Status status;                 // Identified / Partial / Unidentified / InvalidInput
    Lf::Confidence confidence;         // None / Low / Medium / High
    QString displayName;
    QString format;
    QStringList formatCandidates;
    Lf::Optional<int> bitLength;
    Lf::Optional<quint32> facilityCode;
    Lf::Optional<quint64> cardNumber;
    QString rawData;                   // uppercase hex, no separators
    QString hexId;
    Lf::Optional<quint64> decimalId;
    QList<LfExtraField> extraFields;   // protocol specific values
    QStringList warnings;
    QStringList unparsedFields;
    QString sourceText;
    int matchScore;
};
```

Rules:

- Missing optional values stay absent; they are never 0.
- Protocol-specific values go to `extraFields`, not new struct members.
- `valid()` is expressed by `status`; `hasIdentity()` answers "do we have at
  least one identity value".

## 3. Dispatch

Each parser implements:

```cpp
int detect(const QString &normalizedText, QStringList *evidence) const;
```

Score scale:

| Score | Meaning |
|---|---|
| 100 | protocol-specific structural signature |
| 85–95 | strong labelled line |
| 50–80 | marker / weak structure |
| 0 | no evidence |

`LFParserRegistry::parse`:

1. Normalize; empty → `InvalidInput`.
2. Collect candidates with score > 0.
3. Sort by score, then deterministic protocol priority.
4. **Parse only the best candidate.**
5. If it returns `Unidentified`, convert to `Partial` (protocol was detected).
6. Equal top score from different protocols → add ambiguity warning.

No fallback to another protocol after a parse failure.

### 3.1 Priority

Very specific protocols (IDTECK, Keri, NexWatch, Motorola, G-Prox-II, Pyramid,
Paradox, IO Prox, FDX-B, Presco, NEDAP, Gallagher, Noralsy, SecuraKey,
Jablotron, Visa2000, Viking, PAC) precede the generic four
(EM4100, HID Prox, AWID, Indala). This preserves the Stage 1 tie behaviour
while ensuring IDTECK-like output wins over an incidental HID line.

## 4. Normalizer

`LFTextNormalizer` removes only transport noise:

- UTF-8 BOM, ANSI CSI/OSC, NUL
- `\r\n` / `\r` → `\n`
- right-trim each line

It does **not** collapse internal spaces (HID alignment matters) or modify hex
payloads. `normalizeHex()` uppercases and strips separators, returning empty on
invalid input.

## 5. T55xx separation

```cpp
namespace LfT55xx {
DetectInfo parseDetect(const QString &text);
DetectInfo parseInfo(const QString &text);
Block0     parseBlock0(const QString &hex);
}
```

- `Block0` decodes modulation (bits 12–16), extend (17), data bit rate
  (18–23/18–20), max block (5–7), flag bits 0–4 and master-key bits 8–11.
- Bits 0–4 and 8–11 are marked **unverified** with a warning.
- No write/erase/config command is produced anywhere.

## 6. Threading and lifetime

- Parsers are stateless and share no mutable state.
- `QRegularExpression` objects are function-local statics (thread-safe init).
- The registry is a process-wide singleton; `parse()` is const and re-entrant.
- No Qt event loop is required; the example uses `QCoreApplication` only for
  argument handling.

## 7. Extension

Add a protocol:

1. Create `include/lf/parsers/xparser.h` and `src/lf/parsers/xparser.cpp`.
2. Implement `protocol`, `name`, `detect`, `parse`.
3. Add the enum value to `Lf::Protocol` and `Lf::protocolName`.
4. Register it in `LFParserRegistry` and assign a priority.
5. Add fixtures and tests.
6. Add sources to the qmake and CMake lists.
