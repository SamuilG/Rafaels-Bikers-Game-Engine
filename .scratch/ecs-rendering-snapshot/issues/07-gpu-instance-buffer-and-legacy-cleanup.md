# 07 — 动态 GPU 实例数据和旧路径收尾

**What to build:** Renderer 将 RenderSnapshot 转换为 GPU 动态实例数据，支持两帧并行、安全扩容，并在所有新路径稳定后移除旧的直接 ECS 查询依赖。

**Blocked by:** 03 — Renderer 消费静态 RenderSnapshot；05 — 加入 Opaque、AlphaTest 和 AlphaBlend；06 — 加入骨骼动画 Snapshot 引用

**Status:** completed

- [x] Renderer 将 WorldTransform 和 opacity 写入连续动态实例数据
- [x] 两个 Frame In Flight 使用互不覆盖的帧资源
- [x] 动态 Buffer 容量不足时可以安全扩容
- [x] 旧 Buffer 等 GPU 完成使用后才释放
- [x] Renderer 按 RenderableAssetId 组织不透明实例
- [x] Snapshot 在 Renderer 读取后即可复用，Renderer 不保存其引用
- [x] 编辑器预览与阴影路径完成运行时兼容验证
- [x] 旧的 Renderer-to-ECS 查询和遗留 RenderBatch 依赖被明确保留为非主路径

## Comments

- Implemented: two frame-local instance buffers and matching descriptor sets. Static opaque and transparent snapshot batches now pass their `RenderSnapshot` index as `firstInstance`; `default.vert` reads `WorldTransform` from the GPU SSBO. Shadow rendering uses the CPU transform copied from the current Snapshot. Portal and preview batches retain the legacy push-constant transform path. Portal transition clones and editor preview batches explicitly clear `instanceIndex`, preventing a transformed compatibility batch from indexing frame-local Snapshot data. Buffer growth waits for the GPU and refreshes both descriptor sets. Premake Debug x64 build and explicit `default.vert` SPIR-V compilation passed.
- Runtime validation passed: the Models browser opens without freezing, and the verified scene paths remain stable.
