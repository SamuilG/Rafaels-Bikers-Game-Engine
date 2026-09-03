# 06 — 加入骨骼动画 Snapshot 引用

**What to build:** 蒙皮实体进入与静态实体相同的 RenderSnapshot 流程，并通过 SkinInstanceId 和只读 AnimationRenderData 使用当前骨骼姿态。

**Blocked by:** 03 — Renderer 消费静态 RenderSnapshot

**Status:** completed

- [x] 蒙皮实体可以进入 RenderSnapshot
- [x] RenderInstance 只保存 SkinInstanceId，不复制骨骼矩阵
- [x] AnimationSystem 在渲染准备前完成骨骼数据更新
- [x] Renderer 通过 AnimationRenderData 获取骨骼数据
- [x] 多个实例可以共享 RenderableAssetId 但拥有不同骨骼姿态
- [x] 无效 SkinComponent、SkinInstanceId 或骨骼数据会安全跳过并记录警告

## Comments

- Implemented: skinned snapshot instances reference SkinInstanceId while AnimationRenderData owns palettes; Renderer uploads from read-only frame data. Premake Debug x64 build passed.
