# Editor UI tests

From the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Tests/EditorUI/run-editor-layout-tests.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File Tests/EditorUI/run-editor-transform-tests.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File Tests/EditorUI/run-trigger-binding-tests.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File Tests/EditorUI/run-editor-theme-tests.ps1
```

Requires Visual Studio C++ tools. The script builds the vendored Dear ImGui core
and `EditorLayoutTests.cpp` into a separate console executable. It does not build
the Engine, use the runtime UI tests, or initialize a window or graphics backend.
Logs and build outputs stay under `Intermediate/Diagnostics/editor-layout`.

The tests run actual ImGui frames and inspect the resulting docking nodes and
window content rectangles. They verify defaults at 1280x720 and 1920x1080,
reserved tabs for initially closed panels, a custom layout persisted through an
ini save/load into a fresh context, explicit reset, and migration from the old
`DockSpaceOverViewport(0, ...)` layout. Ini persistence is exercised in memory;
the user's `imgui.ini` is never read or modified. Actual Chinese/English titles
come from the production Translator; switching language must preserve stable
window IDs and saved docking. The real panel submission order must leave Entity
Inspector and Assets as the default active tabs.

User tab changes in the same context are preserved. Across restarts the saved
dock nodes and split sizes are restored, but tool visibility uses application
startup defaults; the initial window focus is managed by ImGui. The suite does
not claim to restore an optional tool's visibility or focus across restarts.

The separate transform suite uses real ImGuizmo code. The trigger suite compiles
the production TriggerSystem and exercises particle-binding removal/compaction,
including disabled and completed one-shot triggers. It creates uninitialized
ParticleSystem objects solely for real CPU trigger processing; unexpected GPU
cleanup or debug drawing fails the test. The four link-only graphics adapters
abort if called; no trigger logic is replaced with a test implementation.

Integrate by calling `engine::editor_layout::DrawDockspace()` before editor
windows, replacing the previous `DockSpaceOverViewport` call. Use the header's
window-title constants for each panel. Calling `DrawDockspace(true)` restores the
default docking positions; panel visibility remains the application's concern.

The theme suite applies the production `EditorTheme.hpp` to real ImGui and
ImGuizmo contexts, checks palette/borders and text contrast, exercises a mouse
hover, and software-renders actual ImGui draw data using the project's font.
Its `Intermediate/Diagnostics/editor-theme/preview.png` is a labeled theme
fixture, not a capture of the running Vulkan engine. It does not modify ini files.
The preview models linear RGB blending and sRGB output, including the production
palette conversion, so colors do not acquire an extra output gamma transform.
The specified secondary text color is retained exactly (4.48:1 against paper);
primary text is checked at 4.5:1 or better in normal, hovered, and selected states.

In the engine, F1 enters the paper-themed editor. View > Neutral Viewport
Background and View > Viewport Grid control the editor backdrop and the
depth-tested XZ reference grid. Disable the background to preview the scene sky
and camera effects. Game view retains its existing style and authored UI colors.
