# 04 — 接入场景层级、Transform 和主相机

**What to build:** 导入模型的根节点、子节点和 PrimaryCamera 通过 SceneManager 形成一致的场景状态，并在渲染前生成匹配的 CameraView 和 RenderSnapshot。

**Blocked by:** 02 — 静态实体生成 RenderSnapshot

**Status:** completed

- [x] ImportedNode 的父子关系可以恢复为 ECS 层级
- [x] 模型根实体可以没有 MeshReference
- [x] 修改根实体 LocalTransform 会带动所有子实体（非物理模型）
- [x] Transform progression 在动画之后统一生成 WorldTransform
- [x] PrimaryCamera 的 CameraComponent 和 WorldTransform 可以生成 CameraView
- [x] CameraView 和 RenderSnapshot 来自同一场景更新
- [x] 缺少 PrimaryCamera 时安全跳过场景渲染并记录警告

## Comments

- Imported glTF nodes and non-physics renderable parts are connected with ECS `ChildOf` relationships under a non-rendering model root. Compound physics models deliberately remain unparented because their parts are physics world-space outputs; parent-relative physics remains out of scope.
- `SceneManager::Update()` now only performs scene/physics synchronization. `SceneManager::ProgressTransforms()` runs once in Renderer preparation after AnimationSystem has completed, then PrimaryCamera and RenderSnapshot are extracted from that final ECS state. Missing-camera diagnostics are rate-limited and result in a clear/present frame with no scene extraction.
- Premake Debug x64 build passed. Runtime validation is still required for root movement, camera motion, and a missing-camera scene.
- Runtime validation passed: camera following, character animation, physics bike movement, and portal rendering all remained correct with the final transform progression order.
