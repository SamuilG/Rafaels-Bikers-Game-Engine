# 08 — RenderSnapshot 回归测试

**What to build:** 为不依赖 Vulkan 窗口的 `RenderSnapshot → GpuInstanceData` 契约建立可独立运行的回归测试，保护静态实例数据、双帧槽位和 legacy batch 隔离规则。

**Status:** completed

- [x] Snapshot 的 `WorldTransform` 和 `opacity` 被完整复制到 GPU 数据布局
- [x] GPU 数据布局大小与 `default.vert` 的 std430 布局匹配
- [x] Snapshot batch 使用自身索引作为 `firstInstance`
- [x] portal / preview compatibility batch 不会索引 Snapshot GPU 数据
- [x] 两帧资源槽位选择保持稳定
- [x] 测试可作为独立 ConsoleApp 构建和运行，不创建 Vulkan 窗口

## Verification

`Bin/Tests/EngineSnapshotTests.exe` passed. Premake Debug x64 Engine build also passed.

## Follow-up

SceneManager hierarchy, culling, opacity threshold, and missing-camera behaviour require a separate ECS extraction test seam. They are intentionally not coupled to this GPU-data contract test target.
