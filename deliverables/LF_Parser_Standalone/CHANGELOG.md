# CHANGELOG

## Stage 2 (2026-11-14) — standalone package

### Added

- 18 new protocol parsers:
  `IOProx`, `FDX-B`, `Paradox`, `Keri`, `Viking`, `Pyramid`, `NexWatch`,
  `Presco`, `NEDAP`, `G-Prox-II`, `Gallagher`, `PAC/Stanley`, `Noralsy`,
  `Jablotron`, `SecuraKey`, `Visa2000`, `Motorola`, `IDTECK`.
- `LfT55xx` read-only module: `parseDetect`, `parseInfo`, `parseBlock0`,
  modulation/bit-rate/max-block decoding, reserved-bit warnings.
- Protocol enum extended to 22 values.
- Registry registers all 22 parsers with a deterministic priority order.
- Standalone layout: `include/lf`, `src/lf`, `tests`, `examples`, `docs`.
- CMake build files (library, tests, example).
- 67 new tests (total 126).
- 28 new fixtures under `tests/fixtures/<protocol>/`.
- Documentation set.

### Changed

- `lfparser.h` now declares `ILfParser` only; `LFParserRegistry` moved to
  `lfparserregistry.h`. `lfparser.h` still includes the registry for backwards
  compatibility with Stage 1 snippets.
- All public includes use the `<lf/...>` form.
- Parsers live in `src/lf/parsers/` and `include/lf/parsers/`.

### Preserved

- All 59 Stage 1 tests, unchanged in behaviour.
- Stage 1 dispatch semantics (score based, no cascade).
- Stage 1 data model (`Optional<T>`, `LFParseResult`, status/confidence).

## Stage 1 (2026-11-14) — initial extraction

- `EM4100`, `HID Prox`, `AWID`, `Indala` parsers.
- Text normalizer, result model, evidence-scored registry.
- 59 QtTest cases.
- Fixed HID silent H10301 fallback and Indala optional-Raw handling.
