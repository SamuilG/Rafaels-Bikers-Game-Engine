# State and controller regressions

Run `powershell -NoProfile -ExecutionPolicy Bypass -File Tests/State/run-controller-tests.ps1` from the repository root with Visual Studio C++ tools installed.

This builds the real header-only PlayerController, CameraController and composition/view types using GLM and the C++ standard library. Output is written to `Intermediate/Diagnostics/state-controllers`.

The 13 groups cover player lifecycle and drive-reset notifications, simulation-owned death effects, camera/player isolation, speed feedback in every camera mode, persistent manual lens and orbit settings, free movement and zero-time updates, cinematic priority and recovery, portal cancellation/preemption, natural portal handoff and immediate teleport, session-reset/readonly-view boundaries, and fixed frame execution gates.

The final two groups call the production `FrameScheduler::BeginFrame` and `Execute`
with recording callbacks. A Victory request submitted during gameplay must leave
the current events/physics/feedback/scene/animation/presentation/audio pipeline
intact; it stops all simulation together on the following frame. Successful and
failed reloads are consumed once and suppress simulation throughout their
completion frame, including the frame reference used by rendering. Gameplay,
scene and renderer views created before a reload must keep referring to the same
application-owned frame after resets and assignments. Pause and Settings remain
independent reasons to stop simulation and are committed only at boundaries.

These are CPU tests; they do not run a Jolt vehicle, open a window or validate GPU rendering. Runtime UI integration is covered separately by `Tests/UI`.

Run `powershell -NoProfile -ExecutionPolicy Bypass -File Tests/State/run-scene-frame-tests.ps1`
for three additional groups using the real Flecs library and production
`UpdateSceneWorld` helper. They register ordinary simulation, interval-timer and
transform systems, verify that paused/menu/settings/reload frames run only
transform propagation without advancing either world clock, and verify that
simulation and interval timers resume together. This specifically guards against
`world.progress(0)` measuring wall time and `time_scale=0` advancing raw-time timers.
Logs are written to `Intermediate/Diagnostics/scene-frame`.
