# ADR 0001: Scene-to-Renderer Render Snapshot

## Status

Accepted

## Decision

SceneManager performs render extraction after gameplay, physics, animation, and transform progression. It creates a `RenderSnapshot` containing only the data required to draw visible instances. Renderer receives the snapshot by read-only reference and does not query ECS.

```text
ECS → SceneManager extraction → RenderSnapshot → Renderer → GPU
```

Each `RenderInstance` contains a `RenderableAssetId`, world transform, runtime opacity, shadow flag, and an optional `SkinInstanceId`. The ID resolves to a complete renderable part: mesh, material, and textures.

## Consequences

- Scene rules and ECS interpretation stay in SceneManager.
- Vulkan resources and GPU buffer lifetime stay in Renderer.
- Renderer does not depend on Flecs or scene hierarchy types.
- The snapshot is a CPU-side frame description; it does not copy complete model data.
- Animation bone matrices remain on the animation data path and are referenced by SkinInstanceId through a read-only AnimationRenderData interface.

## Deferred work

- Asynchronous asset loading and `Loading/Ready/Failed` states.
- LOD selection.
- Material overrides per entity.
- Multi-camera rendering.
- Full replacement of legacy `RenderBatch` paths.

The complete set of related decisions, including model import, hierarchy, transforms, camera, transparency, GPU buffers, and asset ownership, is documented in [`docs/architecture/ecs-rendering.md`](../architecture/ecs-rendering.md).
