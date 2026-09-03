# ECS, Scene, Assets, and Rendering Architecture

## 1. Goal and scope

This design defines the path from model import to rendering while keeping scene interpretation separate from GPU execution. It is intentionally a simple first architecture: one main camera, synchronous loading, no LOD, no material overrides, and no asynchronous asset state machine.

## 2. Frame execution order

```text
Input
  ↓
Gameplay
  ↓
PhysicsSystem::Update()
  ├── advance physics
  └── sync Physics → ECS LocalTransform
  ↓
AnimationSystem::Update()
  ↓
SceneManager::ProgressTransforms()
  ├── LocalTransform → WorldTransform
  └── apply parent-child hierarchy
  ↓
SceneManager::PrepareRenderFrame()
  ├── extract PrimaryCamera → CameraView
  └── RenderExtraction → RenderSnapshot
  ↓
Renderer::Render(CameraView, RenderSnapshot)
```

Gameplay currently includes the existing level update logic rather than requiring a new GameplaySystem. SceneManager operations remain separate because ECS-related work can be needed at different points in the frame.

## 3. Ownership

### AssetManager

Owns shared CPU resource descriptions and resource identity. It loads model files, retains CPU ModelAsset data for now, registers/reuses resources by normalized path, returns RenderableAssetId values for drawable parts, and keeps imported node descriptions available to SceneManager. Import-setting hashes, hot reload, reference counting, automatic unloading, and Loading/Ready/Failed states are deferred.

### SceneManager

Owns the ECS world and scene instances. It creates/destroys entities, creates model hierarchies, owns instance transforms and opacity, performs transform progression and RenderExtraction, and owns the current CameraView and RenderSnapshot. It does not inspect the Renderer GPU resource table.

### PhysicsSystem

Owns physics bodies and the EntityId-to-BodyId mapping. Gameplay sends input by EntityId. Physics owns the physical result and writes a physics vehicle root's LocalTransform after simulation. Missing-body requests are ignored with a warning. Body creation/destruction remains synchronous for now.

### AnimationSystem

Owns animation state and bone matrices. It updates animation and skin data before render preparation and exposes a lightweight read-only AnimationRenderData interface.

### Renderer

Owns Vulkan/GPU resources, GPU dynamic buffers, render passes, sorting, resource resolution, command recording, and submission. It receives CameraView and RenderSnapshot by read-only reference and never queries ECS.

## 4. Model import and scene instantiation

ModelLoader parses a model into an ImportedModel containing nodes with parentIndex, LocalTransform, and an optional RenderableAssetId. Each drawable mesh/material part receives its own RenderableAssetId. Empty/root nodes may have no ID and still preserve hierarchy.

SceneManager creates a model instance in two passes: first create all ECS entities, then establish parent relationships using parentIndex. It returns the root EntityId. The root need not have a MeshReference. Its initial transform is `spawnTransform × importedRootTransform`; child transforms remain relative to their parents.

## 5. Transform rules

```text
LocalTransform → Transform progression → WorldTransform
```

LocalTransform is not model vertex space. Only transform progression writes WorldTransform. Gameplay, Animation, and other systems modify owned LocalTransform or animation data. Renderer and RenderExtraction read WorldTransform.

Physics outputs a world-space result, but current physics vehicles are root entities, so their physics world transform can be written directly to LocalTransform. Parent physics can be added later with an explicit world-to-local conversion.

## 6. Camera

The main camera is an ECS entity with CameraComponent, PrimaryCamera, LocalTransform, and WorldTransform. SceneManager queries PrimaryCamera and creates CameraView. If no PrimaryCamera exists, scene rendering is skipped and a warning is recorded. CameraView and RenderSnapshot come from the same Scene state. Additional views are deferred.

## 7. Render extraction and snapshot

PrepareRenderFrame is called after all logic and transform work, immediately before Renderer. It extracts the primary camera, queries entities with MeshReference + LocalTransform + WorldTransform, computes a temporary WorldAABB from LocalAABB and WorldTransform, performs frustum culling, skips opacity values at or below 0.001, and produces one RenderInstance per visible renderable entity.

The snapshot is CPU-side data only:

```cpp
struct RenderInstance
{
    RenderableAssetId renderableAssetId;
    glm::mat4 worldTransform;
    float opacity;
    bool castShadow;
    std::optional<SkinInstanceId> skinInstanceId;
};

struct RenderSnapshot
{
    std::vector<RenderInstance> instances;
};
```

It contains no ECS components, Vulkan types, GPU handles, complete model data, EntityId, or WorldAABB. Missing required ECS data is an ECS data error; the entity is skipped with a warning. Missing GPU resources are handled by Renderer.

The snapshot is logically new each frame, while vector storage may be reused. SceneManager owns it. Renderer copies required data immediately into current frame resources and does not retain the snapshot.

## 8. Assets and IDs

```text
RenderableAssetId → RenderableAsset → Mesh + Material + Texture references
```

A multi-part model has multiple IDs. Identical resources reuse the same ID. CPU and GPU representations share the same logical identity, while GPU handles remain private to Renderer. MeshReference stores the ID in ECS; extraction copies it into RenderInstance; Renderer resolves it.

## 9. GPU data and frames in flight

Static model/material/texture data is uploaded when prepared. Renderer derives dynamic data per frame:

```cpp
struct GpuInstanceData
{
    glm::mat4 worldTransform;
    float opacity;
};
```

Renderer writes a continuous dynamic buffer instead of uploading each object independently. It owns and grows the buffer. Old buffers are released only after GPU completion. The first implementation uses two frames in flight, each with separate frame resources.

Renderer groups instances by RenderableAssetId. Opaque objects prioritize resource/material/pipeline grouping. Transparent objects are rendered after opaque objects and sorted far-to-near; grouping is attempted only when it does not violate depth order. An empty snapshot still clears/presents the frame but skips instance upload and object draws.

## 10. Material and transparency

Material owns BlendMode and alphaCutoff. Entity opacity defaults to 1.0 and is clamped to [0,1]. Texture alpha is sampled per pixel. Alpha Blend uses `textureAlpha × materialOpacity × entityOpacity` for continuous color blending.

Opaque: depth test on, depth write on, blending off.

Alpha Test: compare texture alpha with material alphaCutoff, discard below the threshold, depth test on, depth write on, blending off. It is used for trees and fences.

Alpha Blend: depth test on, depth write off, blending on, after opaque objects, far-to-near sorting. It is used for glass, smoke, fade, and invisibility. Entity opacity does not automatically fade Alpha Test materials.

## 11. Skeletal animation

Skinned entities use the same RenderSnapshot path as static entities. A skinned RenderInstance carries an optional SkinInstanceId, not bone matrices. AnimationSystem updates bones before render preparation. Renderer reads them through AnimationRenderData and uploads them. Missing SkinComponent, SkinInstanceId, or bone data causes the instance to be skipped with a warning.

## 12. Deferred work

- asynchronous asset loading and resource states;
- hot reload, reference counting, and automatic unloading;
- LOD;
- per-entity material overrides;
- multiple camera views;
- full removal of legacy RenderBatch paths;
- parented physics entities;
- command queues and multithreaded snapshot ownership.

## 13. Gameplay and physics interaction

Gameplay does not directly write the transform of a physics-controlled vehicle. It sends input using the vehicle's ECS EntityId:

```text
Gameplay → PhysicsSystem::ApplyInput(EntityId, input) → Jolt Body
```

PhysicsSystem maps EntityId to its private physics BodyId. Gameplay never stores or passes BodyId. If no Body exists, the input is ignored and a warning is recorded.

SceneManager coordinates physics entity lifetime. When creating a vehicle it creates the ECS entity and asks PhysicsSystem to create its body. When destroying one it asks PhysicsSystem to destroy the body first, then destroys the ECS entity. The exact deferred-command policy is intentionally not fixed yet; the current design favors immediate execution.

Physics vehicles are root entities. PhysicsSystem is their sole LocalTransform writer after simulation. Visual children such as wheels, lights, and cameras are not individually updated by PhysicsSystem; transform progression derives their WorldTransform from the vehicle root.

## 14. ECS component rules

`MeshReference` is the renderability marker and stores one `RenderableAssetId`. A renderable entity must also have LocalTransform and WorldTransform. Entities without MeshReference remain valid non-rendering ECS entities, including triggers, cameras, audio sources, logic entities, and collision-only entities.

`Opacity` is optional. Absence means 1.0. Runtime values are clamped to [0,1], and values at or below 0.001 are omitted from the current Snapshot without destroying the ECS entity.

`EntityId` is intentionally not currently included in RenderInstance. It may be added later for picking and debug mapping; it is not needed for the Renderer to resolve a RenderableAssetId.

## 15. Current-to-target migration

The existing `SceneManager::get_render_batches()` is the first implementation of RenderExtraction. Migration is incremental:

1. keep the current ECS query, frustum culling, and RenderBatch path working;
2. introduce RenderSnapshot and RenderInstance as the small Scene-to-Renderer interface;
3. add a compatibility mapping from the current mesh/material indices to RenderableAssetId;
4. have SceneManager fill the Snapshot after transform progression;
5. make Renderer consume the Snapshot while legacy RenderBatch consumers remain temporarily available;
6. remove direct Renderer-to-ECS queries and retire RenderBatch only after all special paths (portals, previews, skinning, shadows) are covered.

The compatibility step is required because the current implementation stores meshIndex and materialIndex separately, while the target RenderableAssetId identifies a complete mesh/material/texture unit. A mesh index must not be treated as a complete RenderableAssetId.

## 16. Error and fallback policy

The first design prefers explicit omission over hidden fallback behavior:

- no PrimaryCamera: skip scene rendering and warn;
- missing required ECS transform data: skip the entity and warn;
- missing GPU resource for a RenderableAssetId: Renderer skips the instance and warns, with repeated warnings rate-limited;
- invalid skin data: skip the skinned instance and warn;
- missing physics body during input: ignore the request and warn;
- no visible instances: still clear/present as required, but skip instance upload and object draw calls.

The design does not yet require a default missing-mesh material, automatic entity repair, or engine shutdown for these cases.
