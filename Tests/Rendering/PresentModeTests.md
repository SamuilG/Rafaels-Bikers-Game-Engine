# Presentation mode selection

Run from the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Tests/Rendering/run-present-mode-tests.ps1
```

This CPU-only suite uses the production `ChoosePresentMode` helper with controlled
surface capability sets. It needs Visual Studio C++ tools and the repository's
Vulkan headers, but does not create a window or require a Vulkan device.

It checks synchronized FIFO selection, immediate presentation priority when
VSync is disabled, mailbox fallback, FIFO fallback and an empty capability set.
The requested VSync flag and actual active swapchain mode are separate: requesting
unlimited presentation cannot guarantee it when a surface only supports FIFO.
Window creation defaults to synchronized FIFO, and successful swapchain recreation
publishes its newly selected actual mode. Deferred recreation retains the previous
active mode. Move construction/assignment transfer both presentation fields.

These selector tests do not exercise native swapchain recreation or measure frame
rate. Those need an editor run on a supported graphics device. Build logs and
results remain under `Intermediate/Diagnostics/present-mode`.
