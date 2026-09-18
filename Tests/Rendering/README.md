# View mode GPU regression tests

Run from the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Tests/Rendering/run-view-mode-gpu-tests.ps1
```

Requires Visual Studio C++ tools and a Vulkan graphics device supporting Vulkan
1.3 dynamic rendering, synchronization2 and scalar block layout. The script uses
the repository's `glslc.exe`, Vulkan headers, Volk loader, GLM and stb. It does not
need an installed Vulkan SDK, a window, surface, swapchain or a running Engine.
No settings, scene assets or production SPIR-V files are modified.

The test compiles the current production `debug.vert`, `skinned.vert`,
`debug_mip.frag`, `debug_depth.frag`, `debug_deriv.frag` and `overdraw.frag` into
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

The small harness reproduces the production debug pipeline contract: one color
attachment, no culling, `LESS_OR_EQUAL` depth test/write for normal diagnostics
and Overshading, no depth test/write for Overdraw, and `ONE + ONE` RGB blending
with `ONE + ZERO` alpha for accumulation. Scene uniforms use the 1568-byte CPU
layout and object push constants use the shared 128-byte layout. The harness
constructs its own pipelines; it does **not** instantiate `RenderSystem`, verify
the full engine's mode routing or post-processing bypass, or call its pipeline
factory. Those integration checks require the engine build and code review or
an engine run, independently of this shader/GPU suite.

`build.log`, `results.txt`, the test executable and diagnostic PNGs stay under
`Intermediate/Diagnostics/view-modes`. PNGs are actual offscreen GPU readbacks,
enlarged with nearest-neighbor sampling and encoded from linear RGB to sRGB for
viewing. They are controlled test geometry, not captures of the running game.
Numerical assertions use the original float pixels before display encoding.

This suite passed on an NVIDIA GeForce RTX 2060 SUPER with Vulkan 1.4.351.
The available driver environment did not expose `VK_LAYER_KHRONOS_validation`;
the successful run therefore verifies GPU output, not validation-layer silence.
