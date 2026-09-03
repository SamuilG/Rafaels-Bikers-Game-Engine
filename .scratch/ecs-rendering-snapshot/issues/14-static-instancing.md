# 14 — Static opaque instancing

**Status:** completed

- [x] Consecutive opaque/AlphaTest Snapshot instances with the same asset become one instanced draw.
- [x] Different assets, transparent instances, and legacy batches remain separate.
- [x] The main static render loop consumes the resulting draw groups.
- [x] Regression tests and Debug x64 Engine build pass.

## Verification

`EngineSnapshotTests.exe` and the Debug x64 Engine build passed.
