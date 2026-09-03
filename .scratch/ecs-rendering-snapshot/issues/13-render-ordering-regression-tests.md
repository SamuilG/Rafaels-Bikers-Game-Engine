# 13 — Render ordering regression tests

**Status:** completed

- [x] Formalize opaque-first and transparent back-to-front ordering.
- [x] Treat material alpha blend and runtime entity opacity as transparent.
- [x] Make `BuildRenderBatches` consume the tested ordering rule.

## Verification

`EngineSnapshotTests.exe` and the Debug x64 Engine build passed.
