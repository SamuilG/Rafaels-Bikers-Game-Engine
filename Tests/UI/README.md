# Runtime UI flow regression tests

Run from the repository root with Visual Studio C++ tools installed:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Tests/UI/run-game-flow-tests.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File Tests/UI/run-runtime-ui-flow-tests.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File Tests/UI/run-runtime-ui-flow-tests.ps1 -GameOnly
```

The first script compiles the real, dependency-free `GameFlowController` into a
console executable under `Intermediate/Diagnostics/game-flow`. It tests commands,
independent pause/settings reasons, rejected transitions and revision stability,
single-consumer reload requests, completion guards, failures and retries. It does
not reproduce the state machine in a test-only implementation.

The UI script builds a separate console executable under
`Intermediate/Diagnostics/runtime-ui-flow/{editor,game}`. It does not build or
launch the Engine application and does not require previously built libraries.

The UI executable uses the production `GameFlowController`, `RuntimeUiController`,
`GameUIEventRouter`, `UIManager`, widgets, serializer, theme loader and animations. It loads the actual
`Assets/ui` screens. Button tests compute their layout, find an exposed point with
the production hit tester, and send mouse-down/mouse-up events through UIManager.
Animation time advances without sleeping. Keyboard events use the runtime facade
and UIManager's real event registry. Gameplay also requests the controller directly
and synchronizes the resulting screens without a synthetic UI event.

Coverage includes settings opened from all three sources, cancellation, apply,
reset, failed display changes, repeated opens, rapid back actions, pause/resume,
result screens, consumed scene-reload requests, failed-load retry and exit buttons,
temporary screen cleanup, editor entry, quit, background preload and a missing
destination asset. Both immediate visibility and the stack
after animation updates are checked.

The fixture synchronizes the initial MainMenu with `SyncGameFlowUi()`. Reloads use
the same host protocol as Application: `TakeReloadRequest()`, then
`CompleteReload(success)`, then UI synchronization. No legacy state flags or
reset-only UI events are used. The host success/failure is simulated; this suite
does not load or destroy an actual scene and does not test GPU recovery.

`HeadlessUiAdapters.cpp` replaces only logging/toasts and the draw backend. The
display callbacks model host acceptance/rejection. No audio system is attached;
audio adapter calls abort so the suite cannot silently claim audio coverage.
Flecs is linked because existing UI headers reference its builtin constants, but
the tests create no ECS world, Vulkan instance, window or audio device. GPU output,
real fullscreen/resolution changes and actual audio volume remain integration
checks for the Engine application. Build diagnostics and test results are saved
beside each test executable.
