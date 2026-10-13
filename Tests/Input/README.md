# Mouse capture regression tests

Run from the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Tests/Input/run-mouse-capture-tests.ps1
```

The suite compiles the production InputSystem with GLFW test doubles. It checks
middle-button edges, capture persistence, cursor-warp suppression, active
viewport bounds/ownership, menu and Pause/Stop release, Shift+F1 and Escape,
focus loss and return, neutralized held controls, and shutdown. It does not test
native OS confinement; GLFW_CURSOR_DISABLED supplies that in the running host.

The separate EditorUI layout suite executes the production CapturedInput helper
against real ImGui widgets, checking that captured virtual input cannot click
editor buttons or replay after release.

In the editor, click a running Game canvas to capture. Shift+F1 or the middle
mouse button releases it; Escape also preserves the game's pause command. UI
menus, editor pause/stop, leaving the owning viewport and Alt+Tab release the
mouse. Captured gameplay keys/buttons remain independent of ImGui.
