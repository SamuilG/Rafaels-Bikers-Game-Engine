# Runtime UI flow regression tests

Run from the repository root with Visual Studio C++ tools installed:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Tests/UI/run-runtime-ui-flow-tests.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File Tests/UI/run-runtime-ui-flow-tests.ps1 -GameOnly
```

The script builds a separate console executable under
`Intermediate/Diagnostics/runtime-ui-flow/{editor,game}`. It does not build or
launch the Engine application and does not require previously built libraries.

The executable uses the production `RuntimeUiController`, `GameUIEventRouter`,
`UIManager`, widgets, serializer, theme loader and animations. It loads the actual
`Assets/ui` screens. Button tests compute their layout, find an exposed point with
the production hit tester, and send mouse-down/mouse-up events through UIManager.
Animation time advances without sleeping. Keyboard/gameplay events are dispatched
through UIManager's real event registry.

Coverage includes settings opened from all three sources, cancellation, apply,
reset, failed display changes, repeated opens, rapid back actions, pause/resume,
result screens, queued scene reloads, temporary screen cleanup, editor entry,
quit and a missing destination asset. Both immediate visibility and the stack
after animation updates are checked.

`HeadlessUiAdapters.cpp` replaces only logging/toasts and the draw backend. The
display callbacks model host acceptance/rejection. No audio system is attached;
audio adapter calls abort so the suite cannot silently claim audio coverage.
Flecs is linked because existing UI headers reference its builtin constants, but
the tests create no ECS world, Vulkan instance, window or audio device. GPU output,
real fullscreen/resolution changes and actual audio volume remain integration
checks for the Engine application. Build diagnostics and test results are saved
beside each test executable.
