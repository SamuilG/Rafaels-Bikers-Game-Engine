# 02 — 静态实体生成 RenderSnapshot

**What to build:** SceneManager 从 ECS 提取拥有完整渲染数据的静态实体，生成当前帧的 RenderSnapshot。

**Blocked by:** 01 — 建立 RenderableAssetId 兼容资源表

**Status:** completed

- [x] 每个可见静态实体生成一个 RenderInstance
- [x] RenderInstance 包含 RenderableAssetId、WorldTransform、opacity 和阴影状态
- [x] 缺少 MeshReference、LocalTransform 或 WorldTransform 的实体被安全跳过并记录警告
- [x] LocalAABB 根据 WorldTransform 转换后用于视锥体剔除
- [x] WorldAABB 不进入 RenderSnapshot
- [x] 空 Snapshot 不会导致错误

## Comments

- Implemented: ECS extraction applies WorldTransform AABB culling, clamps opacity, and emits compact RenderInstance data. Premake Debug x64 build passed.
