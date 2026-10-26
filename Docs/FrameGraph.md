# Runtime Frame Graph

Open **Window > Frame Graph** in the editor. The panel captures the last
recorded graphics frame for the active Scene or Game viewport.

- **Graph** displays blue Pass nodes and orange RT content versions. A write
  creates a new `vN` of the same physical image; reads connect to the version
  they actually consume. This keeps reused shadow maps, Portal scratch images
  and in-place overlays understandable without cyclic resource nodes.
- Use the wheel to zoom at the cursor and middle-drag to pan. **Fit graph**
  frames the complete graph. Click a Pass or RT for its access list, versions,
  format, dimensions, sample count, image identity and first/last use.
  **Focus selection** centers a selected item at a readable zoom level.
- **RT lifetime** shows execution-ordered Pass columns and physical RT rows.
  Blue cells read, orange cells write and purple cells load and write. The gray
  interval connects an RT's first and last use in this frame; it is not an
  allocation/free interval. Hover a cell or Pass header for details.
- **Render targets** lists metadata and nominal image sizes. **Unused RTs**
  includes registered targets skipped by the current path. Search filters this
  list and the lifetime rows, and dims nonmatching graph nodes.
- **Ordering edges** adds write-after-read and write-after-write constraints.
  Horizontal placement reflects dependency depth; Pass numbers retain actual
  command recording order. It does not indicate concurrent GPU execution.
- **Freeze capture** retains a value-only snapshot while the scene continues.
  It remains safe across resizing and AA resource recreation. Unfreeze to
  capture again. Capturing is disabled while the panel or editor is closed.

The capture includes directional/spot shadow layers, recursive Portal views,
Forward MRT, the shared MSAA scratch attachments and fixed-function resolves,
SSR/SSAO (including their clear-only disabled paths), Bloom, Composite, TAA
motion/history initialization/resolve/copy, overlays, viewport output, UI and
presentation. Geometry and buffer diagnostic early-outs record their actual
paths. TAA histories retain their previous-frame origin. MSAA resolve is part
of the MRT rendering instance, so its scratch and resolved images belong to
the same Pass. Particle/debug stages are logical stages within rendering
instances and can contain no draws.

`FrameGraph::Compile()` derives content versions, RAW/WAR/WAW constraints,
per-frame use intervals and unwritten-read diagnostics from access declarations
at recording sites. The graph currently serves inspection and validation of
the existing graphics command stream: it does not schedule passes, allocate or
alias images, generate Vulkan barriers, or measure individual Pass GPU times.
Its scope is render targets in the frame; uploaded model textures, IBL baking,
thumbnail jobs, buffers and arbitrary texture descriptors used by ImGui are
outside this capture. The displayed memory total counts used engine images
once (array layers separately), excludes the external swapchain, and estimates
uncompressed texel bytes without allocation padding, tiling or heap overhead.

## Verification

```powershell
Tests/Rendering/run-frame-graph-tests.ps1
Tests/Rendering/run-frame-graph-ui-tests.ps1
Tests/Rendering/run-anti-aliasing-integration-tests.ps1
```

The CPU suite covers physical image identity, independent array layers,
content versions, all three ordering hazards, read/write accesses, imported
history, presentation, intervals, memory estimates, repeat compilation,
snapshot isolation and invalid/unwritten uses.

The standalone UI suite exercises the production panel's three tabs, selection,
empty and reset states with a graph fixture. It software-rasterizes actual ImGui
draw data to PNGs under `Intermediate/Diagnostics/frame-graph-ui` for layout
review; those fixture images are not captures of the running game.

The Vulkan integration suite opens the actual panel and verifies captured
paths for MSAA 1x/2x/4x/8x and TAA, diagnostics, a Portal, resizing, camera cuts,
freezing through a resize and resumed capture, also after mounting an older
project's asset directory. It checks presentation and graph validation.
