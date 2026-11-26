# DXR capability and smoke test

Phase 4 begins with a runtime query of `D3D12_FEATURE_D3D12_OPTIONS5` on the
selected D3D12 device. RayTime reports the returned ray-tracing tier and
distinguishes an unsupported tier from DXR-capable device tiers. Unrecognized
nonzero tier values are printed numerically to remain diagnosable with newer
runtime/SDK combinations.

Run the probe on the preferred hardware adapter:

```powershell
.\build\Release\RayTime.exe --raytracing-capabilities
```

Or explicitly query WARP:

```powershell
.\build\Release\RayTime.exe --raytracing-capabilities --warp
```

The tier query reports the D3D12 capability exposed by that device; it does not
prove that the selected adapter uses hardware acceleration or indicate
performance. WARP is a software adapter and is useful for exercising the query
path, not as evidence of hardware DXR performance.

This phase does not yet create bottom-level or top-level acceleration
structures for arbitrary scenes or provide a general renderer. The DXR smoke
test builds one bottom-level and one top-level acceleration structure from a
synthetic triangle, creates a DXR state object and shader table, dispatches one
hit ray and one miss ray, and checks the GPU readback colors. Its HLSL is
compiled as a `lib_6_3` library by DXC.

Run the end-to-end synthetic test:

```powershell
.\build\Release\RayTime.exe --validate-dxr .\shaders\phase4_smoke.hlsl --dxc <path-to-dxc.exe>
```

DXC may be omitted when it is on `PATH`; `--warp` selects WARP for testing when
that adapter exposes the required DXR operations. If the selected device has
no DXR tier, the command is skipped and submits no ray-tracing work. A software
ray-tracing fallback is not implemented.
