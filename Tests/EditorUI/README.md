# Editor UI tests

From the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Tests/EditorUI/run-editor-layout-tests.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File Tests/EditorUI/run-editor-transform-tests.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File Tests/EditorUI/run-trigger-binding-tests.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File Tests/EditorUI/run-editor-theme-tests.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File Tests/EditorUI/run-camera-gizmo-tests.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File Tests/EditorUI/run-light-inspector-tests.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File Tests/EditorUI/run-entity-grouping-tests.ps1
```

The light inspector suite submits production controls to real ImGui. Mouse and
keyboard events edit intensity, cascade distances, receiver bias, strength and
filtering, choose the primary light and verify disabled shadow controls. English
and Chinese labels retain their IDs and fit the 300-pixel inspector.

Requires Visual Studio C++ tools. The script builds the vendored Dear ImGui core
and `EditorLayoutTests.cpp` into a separate console executable. It does not build
the Engine, use the runtime UI tests, or initialize a window or graphics backend.
Logs and build outputs stay under `Intermediate/Diagnostics/editor-layout`.

The camera gizmo suite exercises the production GLM geometry helper without a
window or Vulkan device. It checks real near/far frustum corners, pose/FOV/aspect,
RH_ZO projection, clipping against all six planes including segments behind the
eye, and rejection of invalid inputs. It also checks the default 5-metre display
guide, unchanged game lens parameters, full-range mode, short far planes, and
near planes beyond the requested preview depth. The default Bikers spawn view
must contain all twelve preview edges without clipping their endpoints, guarding
against the long rays and disconnected far edges of the original 1000-metre guide.
Outputs stay under
`Intermediate/Diagnostics/camera-gizmo`. The scene-only icon/line drawing and
View menu switches are verified in the running editor.

The tests run actual ImGui frames and inspect the resulting docking nodes and
window content rectangles. They verify defaults at 1280x720 and 1920x1080,
reserved tabs for initially closed panels, a custom layout persisted through an
ini save/load into a fresh context, explicit reset, and migration from the old
`DockSpaceOverViewport(0, ...)` layout. Ini persistence is exercised in memory;
the user's `imgui.ini` is never read or modified. Actual Chinese/English titles
come from the production Translator; switching language must preserve stable
window IDs and saved docking. The real panel submission order must leave Entity
Inspector, Assets and Scene as the default active tabs. Game is a peer of Scene
on the same rendering surface. A saved workspace without a Game window gains
the new tab without changing custom panels or split ratios; selecting either
viewport remains stable across frames. The viewport dock cannot split or undock
into two simultaneous render windows. Selecting any of the 12 Scene view modes
from the Game tab must activate Scene, cancel pending Game focus, and preserve
the selected mode in the production rendering route. Returning to Game uses
Default without clearing the Scene selection. A transient frame where both tabs
report visible cannot let Game override Scene. Hidden Scene diagnostics cannot
disable Game's configured portal passes/surfaces or change their depth/SSR inputs;
the active Scene diagnostic still suppresses its own portals. Zero and tiny initial viewports
must defer layout creation; restoring a usable size or reloading their ini must
produce the normal defaults. Existing custom layouts survive these unusable
sizes and ini saves, and a pending reset waits until the viewport is usable.

Captured gameplay input is tested against real ImGui buttons outside Game.
Unbounded virtual cursor coordinates, clicks and keyboard events cannot activate
editor widgets or steal viewport focus. Releasing capture discards queued input
and restores a fresh editor click without changing docking or replaying pedals.

User tab changes in the same context are preserved. Across restarts the saved
dock nodes and split sizes are restored, but tool visibility uses application
startup defaults; the initial window focus is managed by ImGui. The suite does
not claim to restore an optional tool's visibility or focus across restarts.

The separate transform suite uses real ImGuizmo code. The trigger suite compiles
the production TriggerSystem with only GLM and the C++ standard library. It links
no ParticleSystem, DebugRenderer, UI, physics or GPU code and uses no adapters.
Its 10 groups cover unbound triggers with no particles, enter/exit crossings,
transformed box and capsule geometry, one-shot completion with or without a
callback, disabled bindings, aggregate OR particle outputs, binding removal and
index compaction, and callbacks that add, clear, disable or replace triggers.
Callback additions begin on the next Update; clearing cancels the old evaluation;
disabling resets crossing state without an exit callback. Completed one-shots
retain their particle binding but immediately emit inactive output. These tests
verify the CPU outputs; game-side particle application and GPU rendering require
separate integration validation. Logs are in `Intermediate/Diagnostics/trigger-bindings`.

Integrate by calling `engine::editor_layout::DrawDockspace()` before editor
windows, replacing the previous `DockSpaceOverViewport` call. Use the header's
window-title constants for each panel. Calling `DrawDockspace(true)` restores the
default docking positions; panel visibility remains the application's concern.
Its boolean return value indicates that the viewport was usable: retain a reset
request while it returns false, then clear the request after a true result.

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

Run Tests/EditorUI/run-editor-play-tests.ps1 for six CPU transport regression groups.
They exercise deferred Play/Pause/Stop, blank-host rejection, Stop precedence,
failed-load retry, editor/in-game pause independence, and the production frame
scheduler while editing. GPU world retention and Scene/Game input routing require
the editor integration smoke test.
