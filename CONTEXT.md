# Engine Domain Context

This file is the glossary for the engine's scene, asset, ECS, animation, and rendering concepts. Implementation decisions are recorded in `docs/architecture/ecs-rendering.md` and the ADR directory.

## Terms

- **Asset**: shared model, mesh, material, or texture data; it is not a scene instance.
- **RenderableAsset**: one complete drawable part consisting of a mesh bound to its material and textures.
- **RenderableAssetId**: stable identity of a RenderableAsset; a RenderInstance stores this identity.
- **ModelAsset**: an imported model description containing its node hierarchy and renderable parts.
- **Scene instance**: an ECS entity hierarchy created from a ModelAsset.
- **LocalTransform**: an entity's transform relative to its parent.
- **WorldTransform**: an entity's final transform in world space, including inherited parent transforms.
- **RenderExtraction**: the SceneManager-owned operation that interprets ECS scene state and produces a frame's render input.
- **RenderInstance**: CPU-side description of one visible renderable entity.
- **RenderSnapshot**: the read-only collection of visible RenderInstance values for one frame.
- **CameraEntity**: an ECS entity containing camera data and transform data.
- **PrimaryCamera**: the marker identifying the camera used for the current main view.
- **CameraView**: calculated view/projection data consumed by the Renderer.
- **Entity opacity**: per-instance runtime visibility multiplier; absent means 1.0.
- **Material opacity**: default opacity belonging to a material.
- **Texture alpha**: per-pixel alpha sampled from a texture.
- **Alpha Blend**: continuous color blending using final alpha.
- **Alpha Test**: binary pixel retention using alphaCutoff.
- **SkinInstanceId**: identity of one entity's current animation bone data.
- **AnimationRenderData**: read-only animation data exposed to rendering without exposing ECS or animation implementation details.
- **GpuInstanceData**: GPU-side dynamic data derived from a RenderInstance.
