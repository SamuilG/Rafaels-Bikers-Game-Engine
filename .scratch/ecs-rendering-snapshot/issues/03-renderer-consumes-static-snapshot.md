# 03 — Renderer 消费静态 RenderSnapshot

**What to build:** Renderer 接收 SceneManager 生成的静态 RenderSnapshot，并使用其中的 RenderableAssetId 和实例数据完成绘制，不再主动查询 ECS。

**Blocked by:** 02 — 静态实体生成 RenderSnapshot

**Status:** completed

- [x] Renderer 通过 RenderableAssetId 解析 GPU 资源
- [x] Renderer 通过只读输入接收 Snapshot
- [x] 静态模型可以按照 Snapshot 正常绘制
- [x] Renderer 不依赖 SceneManager 的 ECS 查询接口来绘制静态实例
- [x] 旧 RenderBatch 路径仍可作为兼容路径运行

## Comments

- Implemented: main static render path consumes RenderSnapshot and resolves asset IDs inside Renderer; legacy batches remain only for portal/preview compatibility. Premake Debug x64 build passed.
