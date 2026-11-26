# GPU capability and workload reporting

Phase 5 begins with a vendor-independent adapter report using DXGI identity
and D3D12 feature queries. Run it for the default hardware adapter:

```powershell
.\build\Release\RayTime.exe --gpu-capabilities
```

Use `--warp` to inspect the Microsoft software adapter path:

```powershell
.\build\Release\RayTime.exe --gpu-capabilities --warp
```

The report includes the adapter name, PCI vendor/device IDs, dedicated video
memory, D3D feature level, highest shader model accepted by the current D3D12
runtime, resource-binding tier, and DXR tier. AMD identification is a PCI
vendor-ID match (`0x1002`); it is not a driver validation or a guarantee about
which AMD features a particular device supports.

When `IDXGIAdapter3::QueryVideoMemoryInfo` is available, the report also shows
the current process's local-memory usage and budget. This is aggregate DXGI
accounting, not per-resource allocation tracking; unsupported adapters are
reported as unavailable.

The current report does not measure GPU utilization, clocks, power, or
temperature. Such telemetry is not provided by a single vendor-independent
D3D12 query and needs a supported platform-specific API or external tool. No
AMD-specific telemetry API is included in this phase yet.

## Synthetic copy benchmark

Run the D3D12 timestamp-query sample on the selected adapter:

```powershell
.\build\Release\RayTime.exe --benchmark-gpu
```

It copies a 4 MiB upload buffer to a device-local buffer 16 times. Output
includes the GPU timestamp interval around the copy commands, CPU time spent
submitting and waiting for the fence, and effective logical copy throughput.
This intentionally small smoke benchmark is repeatable but is not a calibrated
bandwidth test, a rendered frame-time measurement, or a GPU-utilization
measurement. WARP results describe its software implementation, not GPU
hardware performance.

## FSR status

FSR integration is not implemented. RayTime does not bundle, download, or
assume access to proprietary or separately distributed SDK components. Any
future investigation must use officially supported interfaces and compatible
hardware/software, and must report availability accurately; it must not claim
FSR 4.1.2 support based solely on detecting an AMD vendor ID.
