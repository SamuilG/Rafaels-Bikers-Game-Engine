# Frame timing and presentation

Open **Window > Diagnostics** to inspect timings. **Render Settings > Presentation >
VSync** requests synchronized presentation (enabled by default). Disable it to
request an unlimited mode. The actual mode is reported in Diagnostics; the driver,
desktop compositor, or external limiter can still constrain the observed FPS.
Immediate presentation may tear. If no unlimited mode is supported, the panel
reports the synchronized fallback instead of claiming that VSync was disabled.

CPU and GPU histories independently retain up to 120 valid measurements. During
warmup, averages use only the available samples. GPU results are read from recently
completed frames, so their sample count and time interval can differ from CPU
samples. Missing or invalid results are not recorded as zero-duration frames.

| Display | Meaning |
| --- | --- |
| FPS / frame total | Reciprocal of the average measured frame wall time, not an estimate of GPU rendering speed. |
| CPU work | Frame wall time minus fence wait, acquisition, and the presentation call. Includes updates, UI, command recording, submission, and other CPU/driver work. This is not CPU utilization. |
| GPU execution | Device timestamp interval around the frame's graphics commands, including rendering, editor UI and device-side stalls within that interval. Does not measure screen scanout or the complete presentation queue latency. |
| Fence wait | CPU time waiting to reuse GPU rendering resources from a completed frame. Queued frames and presentation back-pressure can contribute; this is not a substitute for GPU execution time. |
| Acquire wait | CPU time inside swapchain image acquisition, including any wait for an available image. |
| Present call | CPU time inside the presentation API call, including driver overhead and any blocking. It is not GPU execution time. |
| Presentation wait | Acquire wait + present call; already part of frame total. This includes driver overhead and is not display latency. |

CPU and GPU work overlap. **Do not add CPU work and GPU execution** to calculate
the frame time. A stable 60 FPS with low CPU and GPU durations and significant
acquire/present overhead is consistent with synchronization, but these measurements
alone do not identify driver settings or display latency. VSync back-pressure can
also surface in the fence wait rather than in acquire/present; compare the modes
instead of diagnosing GPU load from a large fence wait alone. Compare performance with a **Release** build and the
same scene, viewport size, camera, and rendering settings.

Unsupported GPU timestamps show `N/A`; supported queries without a completed
result show an awaiting-sample message. Profiling must not introduce an extra
per-frame GPU wait merely to obtain a number. Startup, scene reload, Play/Stop and swapchain transition
frames are excluded from steady-state CPU timing. Histories reset on editor transport
and swapchain transitions. VSync changes made during Play are retained after Stop.
Scene and Game tabs share the
renderer: the GPU time describes the currently rendered view, not two independent
viewport renders.

CPU history regressions:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Tests/Rendering/run-frame-performance-tests.ps1
```

This suite tests warmup, independent delayed GPU samples, repeated ring rollover,
invalid sample rejection, and reset/reuse without requiring a Vulkan device.

`Tests/Rendering/run-present-mode-tests.ps1` checks device-mode selection and
fallback. `run-gpu-frame-timer-tests.ps1` checks the production query lifecycle
with controlled Vulkan responses; `run-gpu-frame-timer-device-tests.ps1` exercises
real device timestamps without opening a window. GPU results are consumed only
after the renderer's existing per-frame fence has completed, without a query WAIT flag.
