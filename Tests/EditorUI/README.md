# Editor docking layout tests

From the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Tests/EditorUI/run-editor-layout-tests.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File Tests/EditorUI/run-editor-transform-tests.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File Tests/EditorUI/run-trigger-binding-tests.ps1
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
