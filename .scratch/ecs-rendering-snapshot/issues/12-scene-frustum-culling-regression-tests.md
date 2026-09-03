# 12 — Scene frustum-culling regression tests

**Status:** completed

- [x] Move the backend-independent frustum and AABB math to Scene.
- [x] Keep Renderer’s old include path as a compatibility wrapper.
- [x] Cover transformed bounds, culling, and padding in `EngineSnapshotTests`.

## Verification

`EngineSnapshotTests.exe` and the Debug x64 Engine build passed.
