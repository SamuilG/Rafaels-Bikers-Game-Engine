# Anti-aliasing

Open **Render Settings > Anti-aliasing** in the editor:

- **MSAA**: Off, 2x, 4x or 8x. Unsupported levels are disabled. The renderer
  checks framebuffer limits, RGBA16F/D32 image sample counts and depth resolve
  support. Requests outside the supported set fall back to a supported lower level.
- **Temporal Anti-aliasing (TAA)**: independent of MSAA; both can be enabled.
- **TAA history weight**: 0.05–0.98. Higher values accumulate more history and
  respond more slowly. The default is 0.9.

Both techniques default to off and are session preferences. They survive
scene reload and Play/Stop along with the other rendering preferences.
Diagnostic views bypass TAA and projection jitter; geometry diagnostics retain
their single-sample pipelines. Buffer diagnostics use the resolved PBR buffers.

## MSAA

Static geometry, skinned geometry, skyboxes and portal surfaces use pipelines
matching the selected sample count. The main and portal views share scratch
multisample attachments because their commands execute sequentially on one
graphics queue. Each view resolves its three RGBA16F MRT attachments and D32
depth into its own existing single-sample targets. SSR, SSAO and later depth
tests continue to read those targets. Shadows and post-processing stay single
sampled. Changing MSAA rebuilds only the affected scene pipelines and attachments.
The editor composite unpremultiplies resolved surface radiance, tone maps it,
and blends coverage with the neutral paper background in linear space, avoiding
dark outlines at partially covered edges in both display-encoding paths.

Depth uses MIN resolve when available, otherwise SAMPLE_ZERO. Synchronization
includes color-output writes for depth resolve as required by the
[Vulkan render-pass resolve specification](https://docs.vulkan.org/spec/latest/chapters/renderpass.html#renderpass-resolve-operations).
Sample count and window-size changes wait for outstanding work before releasing
attachments; there is no extra per-frame GPU wait.

## TAA

The main camera uses a 16-frame Halton (2,3) subpixel sequence. CPU visibility
and directional-shadow fitting use the stable camera before applying jitter.
TAA resolves the composite scene before particles, debug overlays, speed/death
effects and UI. Overlay draws use the unjittered camera. The final output remains
on the existing display-encoding path.

A separate motion pass preserves the existing material, shadow and thumbnail
shader interfaces. It compares raster depth with the resolved scene depth to
select visible geometry. Current and previous entity transforms reproject rigid
objects; current and previous bone matrices also reproject animated objects.
Skybox reprojection uses world direction and camera rotation, without translation
parallax. Per-frame upload buffers are reused only after their existing frame
fence completes, and non-coherent uploads are flushed.

The resolve rejects history outside the previous image or with mismatching
previous linear depth. A 3x3 YCoCg neighborhood and variance bounds constrain
history colors; high motion reduces history weight. Two history images retain
resolved color and linear depth in alpha. History is copied before overlays so
text, editor lines and particles cannot accumulate into the next frame.

History resets on AA/effect setting changes, Scene/Game or diagnostic transitions,
scene changes, Play/Stop/reload, resolution/aspect changes, abrupt projection changes,
large camera translations and camera cuts. A fresh frame copies current color.
Smooth gameplay FOV changes use reprojection and retain history.

Portal interiors, crossing clones and dither-fading surfaces are reactive:
history reuse is disabled where reliable previous surface correspondence is
unavailable. Portal views still receive MSAA. Particle/debug/UI overlays do not
receive TAA. This implementation adds a motion geometry pass and two full-size
history images; it does not promise an FPS improvement.

## Validation

Generate projects with `premake5.exe vs2026`, then build Editor/Engine. New
shaders are included by the existing shader target. Production material and
scene-UBO interfaces remain unchanged.

Editor and Demo bind `Assets/Shaders/spirv` to the installed engine directory
before mounting project assets. Opening an older saved project therefore uses
the current engine shaders, including TAA, without requiring new shader copies
in that project. Explicit shader paths used by GPU tests remain independent.

```powershell
Tests/Rendering/run-anti-aliasing-gpu-tests.ps1
Tests/Rendering/run-anti-aliasing-integration-tests.ps1
```

The headless GPU suite executes production AA resources and shaders. It checks
actual subpixel coverage, all MRT resolves, depth resolve, first-frame behavior,
history accumulation, reset, rigid/bone motion and disocclusion, with TAA alone
and combined with MSAA. Validation is enabled when the layer is available;
an unavailable layer is reported explicitly.

The integration suite links the Release engine and creates a hidden editor
window. It renders moving geometry while cycling sample counts and TAA, switches
diagnostics, renders a portal, resizes the window, moves the camera and disables
AA again. It repeats after mounting an older project's asset directory with
missing TAA shaders and stale material/composite shader copies, verifying that
current engine shaders load and actual presentation continues. These automated checks
are not an interactive UI acceptance test or a performance benchmark.
