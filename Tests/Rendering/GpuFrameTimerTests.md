# GPU frame timer regression tests

Run the CPU contract tests from the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Tests/Rendering/run-gpu-frame-timer-tests.ps1
```

This suite includes the production `GpuFrameTimer.hpp` and supplies mocked Volk
entry points plus a non-owning `VulkanContext` test lifetime. It does not create
a device or a window. Nine groups cover:

- Timestamp conversion, 32/64-bit wraparound and valid-bit masking.
- Invalid periods/widths and non-finite results.
- Unsupported queues and invalid slot counts.
- Query pool creation failure and successful allocation.
- Begin/end pipeline stages, query indices and slot isolation.
- Submission gating, read-once samples, availability data and absence of
  `VK_QUERY_RESULT_WAIT_BIT`.
- Unavailable/error reads yielding no sample instead of zero or stale data.
- Discarding pending samples and resuming measurements.
- Pool reinitialization, explicit shutdown and destructor ownership.

For a real Vulkan device, run the separate headless smoke test:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Tests/Rendering/run-gpu-frame-timer-device-tests.ps1
```

It creates a graphics queue with timestamp support and an 8 MiB transfer buffer,
then measures eight buffer fills per submission using the same production timer.
Three frames exercise two query slots and slot reuse; each result must be finite,
positive and consumed once. A fourth submission checks sample discard. The test
harness waits on its submission fence for deterministic verification; the timer
does not add its own waits. This tests actual query reset/write/read behavior,
not the editor's full frame integration, rendering throughput or swapchain mode.

Both runners need Visual Studio C++ tools and use repository Vulkan/Volk headers.
The device test links the repository's Volk source and requires a Vulkan device;
neither test opens a window or modifies project content. Build logs and results
are under `Intermediate/Diagnostics/gpu-frame-timer` and
`Intermediate/Diagnostics/gpu-frame-timer-device`.

The initial device run passed on NVIDIA GeForce RTX 2060 SUPER (timestamp period
1 ns), with the transfer workload measuring approximately 0.17-0.18 ms. This
harness does not enable validation layers and does not report game frame time.
