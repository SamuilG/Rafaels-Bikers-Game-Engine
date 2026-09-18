# State and controller regressions

Run `powershell -NoProfile -ExecutionPolicy Bypass -File Tests/State/run-controller-tests.ps1` from the repository root with Visual Studio C++ tools installed.

This builds the real header-only PlayerController, CameraController and composition/view types using GLM and the C++ standard library. Output is written to `Intermediate/Diagnostics/state-controllers`.

The 10 groups cover player lifecycle and drive-reset notifications, simulation-owned death effects, camera/player isolation, speed feedback in every camera mode, persistent manual lens and orbit settings, free movement and zero-time updates, cinematic priority and recovery, portal cancellation/preemption, natural portal handoff and immediate teleport, and session-reset/readonly-view boundaries.

These are CPU tests; they do not run a Jolt vehicle, open a window or validate GPU rendering. Runtime UI integration is covered separately by `Tests/UI`.
