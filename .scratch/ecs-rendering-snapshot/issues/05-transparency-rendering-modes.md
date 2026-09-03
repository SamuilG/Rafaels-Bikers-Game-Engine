# 05 — 加入 Opaque、AlphaTest 和 AlphaBlend

**What to build:** Renderer 根据材质模式和实体运行时 opacity 正确处理不透明、像素裁剪透明和连续透明材质。

**Blocked by:** 03 — Renderer 消费静态 RenderSnapshot

**Status:** completed

- [x] Opaque 使用深度测试和深度写入
- [x] AlphaTest 按纹理 Alpha 和材质 alphaCutoff 逐像素 discard，并保持深度写入
- [x] AlphaBlend 开启颜色混合、开启深度测试、关闭深度写入
- [x] 材质拥有 BlendMode 和独立 alphaCutoff
- [x] Entity opacity 缺省为 1、限制在合法范围，并支持淡入淡出
- [x] AlphaBlend 的最终透明度正确组合纹理、材质和实体透明度
- [x] 不透明实例先绘制，透明实例后绘制
- [x] 透明实例优先按距离从远到近排序

## Comments

- Implemented: Opaque/AlphaTest/AlphaBlend routing, material alphaCutoff, entity-opacity composition, opaque-first ordering, and transparent far-to-near sort. Shader project and Debug x64 build passed.
