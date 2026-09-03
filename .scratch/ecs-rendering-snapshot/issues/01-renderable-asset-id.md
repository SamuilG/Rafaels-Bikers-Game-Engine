# 01 — 建立 RenderableAssetId 兼容资源表

**What to build:** 为每个可渲染模型部件建立稳定的 RenderableAssetId，使它能解析到绑定的 Mesh、Material 和 Texture，同时兼容当前资源索引。

**Blocked by:** None — can start immediately

**Status:** completed

- [x] 相同资源可以复用同一个 RenderableAssetId
- [x] 一个多部件模型可以为每个可渲染部件提供独立 ID
- [x] Renderer 可以通过 ID 找到完整可渲染资源
- [x] 现有 mesh/material 索引渲染行为保持不变
- [x] 缺失资源不会导致整帧渲染停止

## Comments

- Implemented: normalized CPU/GPU model caches, per-part RenderableAssetId registry, and safe missing-ID omission. Premake Debug x64 build passed.
