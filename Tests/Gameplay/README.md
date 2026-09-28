# Gameplay presentation regressions

These CPU suites exercise production gameplay data and frame scheduling without a GPU or window:

```powershell
powershell -ExecutionPolicy Bypass -File Tests/Gameplay/run-hud-tests.ps1
powershell -ExecutionPolicy Bypass -File Tests/Gameplay/run-vehicle-presentation-tests.ps1
powershell -ExecutionPolicy Bypass -File Tests/Gameplay/run-frame-presentation-tests.ps1
```

- HUD: speed units, distance/time accumulation without rendering, paused snapshots, session resets, actual UI bindings, and demo energy/lap placeholders.
- Vehicle presentation: generic render-effect outputs derived from gameplay, including speed/death/portal state.
- Frame presentation: the real `FrameScheduler`, `TriggerSystem`, `HudController`, `VehiclePresentation`, and `UserState` lifecycle together. Renderer early return and disabled presentation must not suppress trigger or HUD/effect updates. Paused/settings/terminal frames must freeze simulation. Reload completion must reset session output, preserve user/editor preferences, and skip simulation. A trigger's victory request must affect the next frame without interrupting the current frame's later stages.

Frame integration substitutes small callbacks for physics, scene transforms, animation and rendering. It validates production scheduling and state transitions, not GPU rendering, Jolt vehicle integration, or in-window interaction.

Compiler and result logs are written to `Intermediate/Diagnostics/hud-data`, `vehicle-presentation`, and `frame-presentation` respectively.
