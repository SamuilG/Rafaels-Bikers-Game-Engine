# View mode GPU regression tests

Run from the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Tests/Rendering/run-view-mode-gpu-tests.ps1
```

Requires Visual Studio C++ tools and a Vulkan graphics device supporting Vulkan
1.3 dynamic rendering, synchronization2, scalar block layout and non-solid
polygon fill. The script uses
the repository's `glslc.exe`, Vulkan headers, Volk loader, GLM and stb. It does not
need an installed Vulkan SDK, a window, surface, swapchain or a running Engine.
No settings, scene assets or production SPIR-V files are modified.

The test compiles the current production `debug.vert`, `skinned.vert`,
`debug_mip.frag`, `debug_depth.frag`, `debug_deriv.frag`, `overdraw.frag`,
`debug_albedo.frag`, `debug_wireframe.frag`, `debug_shadow.frag`, `fullscreen.vert`
and `debug_buffer.frag` into
`Intermediate/Diagnostics/view-modes/shaders`. It executes those shaders on the
GPU against a 64 x 64 RGBA16F color attachment and D32 depth attachment, and reads
the half-float pixels back to the CPU. Assertions cover:

- Mipmap magnification, distinct LOD 0/1/2 colors and clamping to available mips.
- Logarithmic camera depth and occlusion with both near/far submission orders.
- Real fragment derivatives on flat, horizontal-slope and vertical-slope planes.
- Additive overdraw without depth rejection, versus depth-passing accumulation
  with near-first and far-first draw orders for Overshading.
- Material alpha cutouts, object clipping, the scene portal clip plane at the
  real uniform offset, and alpha discard during accumulation.
- The production skeletal vertex shader, bone descriptor set, nonzero
  `boneBaseIndex` push constant and actual bone translation in debug output.
- Albedo as a known linear UNORM texture multiplied by the material base-color
  factor, independent of lighting and tone mapping.
- Actual `VK_POLYGON_MODE_LINE` wireframe rasterization: a triangle has visible
  edges and an empty interior, while its filled control covers that interior.
- Directional CSM visibility using a real four-layer D32 texture and comparison
  sampler, with known lit/shadowed halves and different cascade results. The
  production shadow fragment uses the same shadow-sampling include as lighting.
- The production full-screen buffer resolver for SSAO, SSR and Normals. Controlled
  float input textures exercise red-channel grayscale, reflected RGB multiplied
  by confidence, signed/non-unit world-normal decoding, and invalid/background
  normals. An SSR contribution above 1 verifies HDR values survive without tone
  mapping or exposure changes. These inputs test buffer interpretation, not the
  SSAO occlusion kernel, SSR ray marcher, or generation of the engine G-buffer.

The small harness reproduces the production debug pipeline contract: one color
attachment, no culling, `LESS_OR_EQUAL` depth test/write for normal diagnostics
and Overshading, no depth test/write for Overdraw, and `ONE + ONE` RGB blending
with `ONE + ZERO` alpha for accumulation. Wireframe enables the physical device's
`fillModeNonSolid` feature and uses line rasterization. The CSM sampler uses the
production `LESS` comparison and clamp-to-white-border policy. Scene uniforms use the 1568-byte CPU
layout and object push constants use the shared 128-byte layout. The harness
constructs its own pipelines; it does **not** instantiate `RenderSystem`, verify
the full engine's mode routing or post-processing bypass, or call its pipeline
factory. Those integration checks require the engine build and code review or
an engine run, independently of this shader/GPU suite.

`build.log`, `results.txt`, the test executable and diagnostic PNGs stay under
`Intermediate/Diagnostics/view-modes`. PNGs are actual offscreen GPU readbacks,
enlarged with nearest-neighbor sampling and encoded from linear RGB to sRGB for
viewing. They are controlled test geometry, not captures of the running game.
Numerical assertions use the original float pixels before display encoding;
the PNG display clips values above 1, while those HDR values remain checked.

This suite passed on an NVIDIA GeForce RTX 2060 SUPER with Vulkan 1.4.351.
The available driver environment did not expose `VK_LAYER_KHRONOS_validation`;
the successful run therefore verifies GPU output, not validation-layer silence.
