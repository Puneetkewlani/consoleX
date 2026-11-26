# Runtime session

Phase 6 provides one explicit lifecycle for the project's supported Windows
subsystems. It initializes a D3D12 device and queue, captures GPU and DXR
capabilities in a value-only runtime snapshot, records whether DXC is
available, runs the existing synthetic
resource validation and copy-timing workload, compiles a project-owned compute
shader when DXC is available, and runs the existing synthetic DXR hit/miss
test when both DXC and adapter support are available.

Run the end-to-end synthetic path:

```powershell
.\build\Release\RayTime.exe --runtime-smoke .\shaders\phase3_smoke.hlsl --raytracing-shader .\shaders\phase4_smoke.hlsl
```

Select WARP for software-device testing, or specify DXC and a cache directory
when DXC is not on `PATH`:

```powershell
.\build\Release\RayTime.exe --runtime-smoke .\shaders\phase3_smoke.hlsl --raytracing-shader .\shaders\phase4_smoke.hlsl --warp
.\build\Release\RayTime.exe --runtime-smoke .\shaders\phase3_smoke.hlsl --raytracing-shader .\shaders\phase4_smoke.hlsl --dxc C:\path\to\dxc.exe --cache-dir C:\temp\RayTimeCache
```

DXC is optional for initializing the runtime. If it is unavailable, the smoke
path explicitly reports that shader compilation and DXR validation were
skipped while still running its D3D12 resource and timing checks. If the
adapter lacks DXR, only that workload is skipped. Other compiler initialization
or compilation failures are errors. The session flushes its D3D12 queue on
shutdown, then releases its device and queue resources.

The compute shader is compiled and validated by DXC but is not dispatched. The
ray-tracing shader is separately compiled and dispatched by the existing DXR
smoke test; that test verifies hit/miss colors from a synthetic triangle.
CTest includes a WARP runtime smoke test; when DXC is not on `PATH`, it still
checks the D3D12 resource and timing path and reports shader/DXR stages skipped.

The runtime does not load Xbox executables or implement Xbox system services,
private SDK APIs, protected-content handling, or proprietary shader
translation. Its "compatibility" interfaces are limited to documented Windows
graphics APIs and synthetic, project-owned input. A successful smoke test
demonstrates only those Windows operations; it does not demonstrate that any
Xbox title is compatible.
