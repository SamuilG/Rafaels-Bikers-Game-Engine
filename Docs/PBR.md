# PBR rendering

The default material combines Disney diffuse, isotropic GGX specular,
height-correlated Smith visibility and Schlick Fresnel. Static meshes, skinned
meshes, portal views and model thumbnails share `default.frag`.

## 1. Direct lighting

`Assets/Shaders/pbr_common.glsl` supplies GGX and Smith functions shared by
runtime shading and IBL baking. Material roughness is perceptual roughness:
`alpha = max(roughness * roughness, 0.001)`. The GGX distribution squares alpha
internally. The Smith function already includes `G / (4 NoL NoV)`; the runtime
does not divide by this factor a second time.

Disney diffuse and the existing metallic/roughness texture convention are
preserved: green is roughness and blue is metallic. Roughness is bounded to
0.02–1, metallic to 0–1, and the existing normal-variance roughness adjustment
remains. Directional, point and spot lights retain their shadows and per-light
specular multiplier.

Compared with the old Beckmann distribution and Cook–Torrance geometry factor,
GGX produces a longer highlight tail. Roughness controls the lobe width and
the environment reflection blur consistently.

## 2. Environment lighting

`Ibl.cpp` runs a one-time graphics-queue bake after the background cubemap is
loaded. It creates these linear RGBA16F textures:

| Resource | Default size | Purpose |
| --- | --- | --- |
| GGX-prefiltered cubemap | 128² × 6, 8 mip levels | Environment reflection at different roughness values |
| Diffuse cubemap | 32² × 6 | Cosine convolution storing irradiance / pi |
| DFG lookup table | 128² | Matching GGX/Smith/Schlick integration, indexed by NoV and roughness |

`IblBakeSettings` controls dimensions and sample counts. The defaults use 512
samples for the cubemap convolutions and 1024 for the DFG integral. Baking adds
startup work and approximately 1.2 MiB of texture storage, without a per-frame
convolution or dependence on the swapchain size. Resizing retains the textures
and updates scene descriptors.

Scene descriptor set 0 uses bindings 0–4 for scene UBO, shadow map, prefiltered
environment, diffuse environment and DFG lookup respectively. Background skybox
rendering retains its separate source cubemap and descriptor set.

Runtime specular IBL uses the split-sum approximation:

```text
lod      = roughness * (prefilterMipCount - 1)
specular = prefilteredEnvironment(R, lod) * (F0 * DFG.x + DFG.y)
```

The former roughness fade, nonmetal reflection multiplier and black-environment
fallback are removed. Rough materials receive blurred reflections; nonmetal
reflection strength follows Fresnel instead of an arbitrary attenuation.

An approximate multiple-scattering compensation scales direct and indirect
specular by `1 + F0 * (1 / E - 1)`, where `E = DFG.x + DFG.y`. This restores the
missing single-scattering energy for a white conductor under a uniform white
environment, including rough and grazing views. It is an approximation, not a
complete layered Disney BSDF or a full multiple-scattering simulation.

The current source remains the project's sRGB PNG skybox, decoded to linear
when sampled. Baking cannot recover HDR radiance absent from that source.
Source samples use LOD 0 because that cubemap currently has one mip level;
convolution noise for very small bright features can be reduced with higher
sample counts. HDR source loading, offline bake caching, anisotropy and clearcoat
are separate extensions.

## 3. HDR and display output

Scene and portal colors remain linear HDR. SSAO, SSR, Bloom and exposure precede
the single ACES-style fitted tone curve in `composite.frag`. Final sRGB targets
receive linear values for hardware encoding; UNORM presentation receives
explicit sRGB encoding. The editor's neutral backdrop keeps its authored color.
The composite pipeline declares RGBA16F color and D32 depth to match its actual
offscreen render attachments, independently of the swapchain's color format.
Existing SSAO multiplication and SSR mixing are retained; this change does not
make SSR a full BRDF-weighted reflection solution.

Default exposure is 0.35 (about 1.5 stops below 1.0), and Bloom strength is 1.2.
The editor Exposure and Bloom Strength controls still adjust these values.
Portal surfaces keep HDR radiance until this same composite pass. A new Play
scene creates its own portal mesh on first use; portal texture descriptors use
the renderer's persistent pool so scene material-pool exchanges cannot invalidate
them. Portal visibility follows the active viewport's render mode, including
the Game viewport when the Scene viewport uses a diagnostic mode.

Punctual GGX peaks at very low roughness can exceed the finite range of RGBA16F.
The scene shader bounds stored radiance to 65504 before Bloom extraction, avoiding
infinities while retaining the attachment's usable HDR range.

Model thumbnails bypass the composite pass. Fragment specialization constant 0
enables tone mapping and sRGB encoding for their RGBA8 UNORM output. Their cache
version is `thumb_v3_512_ggx`, and their point light now has a valid range and
specular multiplier. Old thumbnails are regenerated on demand.

## Validation

The application configurations share `Assets/Shaders/spirv`. Rebuild both Debug
and Release when changing the shader/descriptor interface. An old executable
with the three-binding scene layout cannot bind the new irradiance/DFG resources
required by the five-binding shader. Use `Bin/Debug-x64` or `Bin/Release-x64`
Editor/Demo; the historical `Bin/Engine.exe` is not a current build target.

Run from the repository root:

```powershell
Tests/Rendering/run-pbr-gpu-tests.ps1
Tests/Rendering/run-view-mode-gpu-tests.ps1
```

The PBR suite calls the production baker and runs the production PBR vertex /
fragment and composite shaders on a headless Vulkan graphics queue. It checks
constant-radiance preservation at every mip/face, diffuse normalization, bounded
DFG values, face identity, rough reflection broadening, the white-furnace test,
direct GGX tails, finite sharp highlights and Bloom output, per-light specular
controls, HDR output, thumbnail display conversion, portal HDR copying and
near-plane coverage, neutral midtone brightness in both presentation paths,
exposure order and editor
backdrop output. These are controlled GPU readbacks, not full-scene screenshots
or an interactive engine acceptance test.

The suite also loads the actual `StandardCubeMap.png` as an sRGB cubemap using
the production cross-face mapping and default bake resolutions. It verifies
nonblack irradiance in every direction and renders metal/nonmetal controls with
no direct lights: IBL enabled produces environment illumination and disabled
produces zero. This catches problems hidden by the synthetic white-environment
tests, including source loading, format decoding and toggle behavior.

Compiler logs and results stay in `Intermediate/Diagnostics/pbr-gpu`. Validation
layers are enabled when available; an unavailable layer is reported explicitly.
The suite and existing view-mode GPU regressions passed on an NVIDIA GeForce RTX
2060 SUPER. This environment did not expose the Khronos validation layer.

For scene comparison, inspect the same metal and nonmetal materials at roughness
0.1, 0.5 and 0.9, including grazing views. Check softened environment detail on
rough metal, broad highlight tails, emissive Bloom, portal rendering, model
thumbnails and scene/game viewport resizing. Lighting and exposure may need
artistic retuning because the previous double tone mapping and IBL attenuation
are no longer present.
