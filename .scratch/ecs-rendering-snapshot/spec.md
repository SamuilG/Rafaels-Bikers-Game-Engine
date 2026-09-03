# ECS to Renderer Render Snapshot

Status: ready-for-agent

## Problem Statement

Gameplay, physics, animation, ECS transform progression, scene hierarchy, culling, and GPU rendering are currently coupled through direct SceneManager queries and legacy render batches. Renderer must understand mutable ECS data and scene rules, while the current mesh/material index representation does not express a complete renderable resource. This makes frame ordering, ownership, testing, and future GPU/threading changes difficult.

## Solution

Introduce a Scene-owned render extraction seam. After gameplay, physics, animation, and transform progression finish, SceneManager creates one immutable-for-consumption `RenderSnapshot` for the current frame. Renderer receives the snapshot and the current camera view by read-only input, resolves renderable assets, prepares dynamic GPU data, and submits GPU work without querying ECS.

The solution preserves the existing renderer incrementally through a compatibility path from legacy render batches to the new snapshot. It also defines a complete renderable asset as one mesh/material/texture unit identified by a stable RenderableAssetId.

## User Stories

1. As a gameplay system, I want to update ECS-owned state before rendering, so that the rendered frame reflects the final gameplay state.
2. As a physics system, I want to own physics bodies and synchronize physical root transforms, so that gameplay cannot overwrite authoritative physics results.
3. As an animation system, I want to update animation and skin data before extraction, so that rendered characters use the current pose.
4. As a SceneManager, I want to calculate final WorldTransforms from LocalTransforms and hierarchy, so that all scene instances have consistent world-space transforms.
5. As a SceneManager, I want to create model instances from imported node hierarchies, so that model parent-child relationships are preserved.
6. As a SceneManager, I want to return a model root EntityId, so that callers can move, rotate, scale, or destroy a complete model instance.
7. As a SceneManager, I want non-rendering root and helper entities to exist without MeshReference, so that hierarchy and gameplay objects are not forced to render.
8. As an AssetManager, I want to load and retain CPU model descriptions, so that animation, physics, debugging, and later resource operations can use them.
9. As an AssetManager, I want each drawable model part to have a stable RenderableAssetId, so that instances can refer to complete mesh/material/texture resources.
10. As an AssetManager, I want normalized paths to reuse already loaded resources, so that the same asset is not uploaded repeatedly.
11. As a SceneManager, I want MeshReference to identify a renderable asset, so that renderability is represented by ECS data.
12. As a SceneManager, I want to extract only entities with complete required render data, so that invalid ECS entities cannot reach rendering.
13. As a SceneManager, I want to cull entities using LocalAABB transformed by WorldTransform, so that invisible entities are excluded before rendering.
14. As a SceneManager, I want to skip nearly invisible entities, so that opacity-zero objects do not consume render work while remaining in ECS.
15. As a SceneManager, I want to extract the PrimaryCamera into a CameraView, so that culling and rendering use the same camera state.
16. As a Renderer, I want to consume a read-only RenderSnapshot, so that I do not depend on ECS, Flecs, or scene hierarchy implementation.
17. As a Renderer, I want to resolve RenderableAssetId to GPU resources, so that Scene code does not hold Vulkan handles.
18. As a Renderer, I want to derive GPU instance data from CPU render instances, so that GPU layout remains Renderer-owned.
19. As a Renderer, I want to update a continuous dynamic instance buffer, so that many transforms can be uploaded efficiently.
20. As a Renderer, I want separate frame resources for two frames in flight, so that CPU updates do not overwrite GPU data still in use.
21. As a Renderer, I want to grow dynamic buffers when required, so that increased visible instance counts do not silently drop objects.
22. As a Renderer, I want old GPU buffers released only after GPU completion, so that resource lifetime is safe.
23. As a Renderer, I want to group opaque instances by renderable asset, so that repeated mesh/material/pipeline changes are reduced.
24. As a Renderer, I want transparent instances rendered after opaque instances, so that blending is meaningful.
25. As a Renderer, I want transparent instances sorted far-to-near, so that alpha blending produces the expected result.
26. As a material author, I want to select Opaque, AlphaTest, or AlphaBlend, so that trees, fences, glass, smoke, and invisibility use appropriate rendering behavior.
27. As a material author, I want a per-material alpha cutoff, so that cutout textures can choose their own threshold.
28. As gameplay, I want entity opacity to default to 1 and change at runtime, so that fade and invisibility effects do not modify shared materials.
29. As an animation system, I want each skinned instance to identify its bone data with SkinInstanceId, so that multiple characters can share a mesh while having different poses.
30. As a Renderer, I want read-only AnimationRenderData, so that I can upload bone matrices without querying ECS or owning animation logic.
31. As a developer, I want missing camera, ECS data, GPU assets, physics bodies, or skin data to produce warnings and safe omission, so that one invalid object does not stop the frame.
32. As a maintainer, I want the existing RenderBatch path migrated incrementally, so that the architecture can improve without rewriting all portal, preview, shadow, and skinning behavior at once.

## Implementation Decisions

- Application/Engine explicitly controls the frame order: Input, Gameplay, Physics plus physics-to-ECS synchronization, Animation, Transform progression, PrepareRenderFrame, then Renderer.
- Gameplay currently remains the existing level update logic; a new GameplaySystem is not required.
- PhysicsSystem completes physics-to-ECS synchronization at the end of its update.
- Physics vehicles are root entities. PhysicsSystem is the sole writer of their LocalTransform after simulation.
- Gameplay influences physics through an EntityId-based ApplyInput interface, never through direct transform writes or Jolt BodyIds.
- SceneManager coordinates physics body creation/destruction while PhysicsSystem owns the EntityId-to-BodyId mapping.
- SceneManager owns ECS instances, hierarchy, CameraView, and RenderSnapshot. Renderer owns Vulkan resources, GPU handles, dynamic buffers, passes, sorting, and submission.
- ModelLoader returns imported nodes containing parent relationships, local transforms, and optional per-part RenderableAssetIds. SceneManager creates all entities first and links parents in a second pass.
- A model root entity may have no MeshReference. Only entities with MeshReference are renderable; cameras, triggers, audio sources, logic entities, and collision-only entities are valid non-rendering entities.
- The root spawn transform is combined with the imported root transform. Callers receive the root EntityId and control the complete model by changing the root LocalTransform.
- LocalTransform is relative to the parent. WorldTransform is the calculated world-space result. Only Transform progression writes WorldTransform.
- The main camera is an ECS CameraEntity marked PrimaryCamera. SceneManager creates CameraView from its CameraComponent and WorldTransform. Missing PrimaryCamera skips scene rendering with a warning.
- PrepareRenderFrame is a public SceneManager operation called externally immediately before Renderer. It creates CameraView and RenderSnapshot from the same scene state.
- RenderSnapshot contains one RenderInstance per visible renderable entity. It contains RenderableAssetId, WorldTransform, runtime opacity, cast-shadow state, and optional SkinInstanceId. It does not contain ECS components, Vulkan types, GPU handles, EntityId, complete models, or WorldAABB.
- RenderExtraction calculates WorldAABB temporarily from the asset LocalAABB and WorldTransform, then performs frustum culling. LOD is explicitly out of scope.
- Missing MeshReference, LocalTransform, or WorldTransform is invalid ECS data; extraction skips the entity and warns. Missing GPU assets are handled by Renderer, not SceneManager.
- RenderSnapshot is logically rebuilt each frame, while its storage may be reused. SceneManager owns it; Renderer copies required values immediately and does not retain it.
- A RenderableAsset is one complete drawable part: Mesh plus bound Material and Texture references. RenderableAssetId is the identity passed through MeshReference and RenderInstance.
- AssetManager retains CPU ModelAsset data and uses normalized paths for reuse. The current loading model is synchronous: GPU preparation completes before a renderable asset is handed to SceneManager. Asynchronous loading states are deferred.
- CPU and GPU representations share the same logical RenderableAssetId, but GPU handles remain private to Renderer.
- Renderer converts RenderInstance into GPU-side dynamic data containing WorldTransform and opacity. AssetId and cast-shadow decisions remain CPU-side.
- Renderer uses two frames in flight and one dynamic buffer per frame resource. It grows buffers as needed and retires old buffers only after GPU completion.
- Opaque instances use depth test and depth write. AlphaTest uses texture alpha and material alphaCutoff, discards below threshold, and keeps depth test/write without blending. AlphaBlend uses texture alpha multiplied by material opacity and entity opacity, enables blending, keeps depth test, disables depth write, and sorts far-to-near.
- Entity opacity defaults to 1, is clamped to [0,1], and values at or below 0.001 are excluded from the Snapshot. AlphaTest does not automatically become a fade effect from entity opacity.
- Skinned instances use the same Snapshot path and carry SkinInstanceId rather than bone matrices. AnimationSystem updates bones; Renderer reads them through AnimationRenderData. Invalid skin data skips the instance with a warning.
- The first migration seam is SceneManager render extraction to RenderSnapshot. The existing get_render_batches implementation remains as a compatibility adapter until portal, preview, shadow, static, and skinned paths are covered.

## Testing Decisions

- Tests must verify observable behavior at the SceneManager Snapshot seam and Renderer consumption seam, not private ECS query details or Vulkan implementation details.
- Scene extraction tests should cover hierarchy flattening to final WorldTransform, root-only movement, non-rendering entities, missing required components, opacity defaults/clamping/threshold, frustum culling, and missing PrimaryCamera.
- Asset tests should cover normalized-path reuse, per-part RenderableAssetId assignment, complete RenderableAsset association, and synchronous readiness before scene instance creation.
- Renderer-facing tests should cover AssetId resolution, missing-resource omission, opaque/AlphaTest/AlphaBlend classification, transparent ordering, and conversion to GPU instance data.
- Animation integration tests should cover distinct SkinInstanceIds, read-only AnimationRenderData access, and invalid skin data omission.
- Physics integration tests should cover EntityId-based input, root LocalTransform synchronization, child-following behavior, and missing-body warnings.
- Buffer tests should cover two frame resources, capacity growth, immediate Snapshot copying, and deferred old-buffer release after GPU completion. These tests may use a fake GPU resource owner rather than Vulkan calls.
- Existing RenderBatch/rendering utility tests and runtime checks are prior art for preserving current static, portal, shadow, preview, and skinning behavior during migration.

## Out of Scope

- Asynchronous streaming/loading, Loading/Ready/Failed resource states, hot reload, reference counting, and automatic unloading.
- LOD and distance-based asset selection.
- Per-entity material overrides or arbitrary material parameter overrides.
- Multiple camera views beyond the single PrimaryCamera.
- Parent-relative physics entities.
- Command queues, multithreaded Snapshot ownership, and a full render-thread scene replica.
- Replacing all legacy RenderBatch code in one change.
- Automatic default missing-mesh resources or automatic ECS repair.

## Further Notes

The complete domain glossary is in `CONTEXT.md`. The architectural rationale is in `docs/architecture/ecs-rendering.md` and ADR 0001. This Spec should be split into small implementation issues before coding; the first issue should establish the Snapshot seam while preserving the legacy renderer.
