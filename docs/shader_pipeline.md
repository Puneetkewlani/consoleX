# Shader compilation experiments

## Phase 3 scope

RayTime accepts ordinary HLSL source files and invokes an explicitly selected
or PATH-discovered `dxc.exe` process. Phase 3 supports `vs_6_0`, `ps_6_0`, and
`cs_6_0`. Phase 4 adds the `lib_6_3` profile used by the synthetic DXR
validation shader; library compilation does not pass an entry point. DXC's
normal validator remains enabled; RayTime does not pass `-Vd`. Successful
output must have a DXIL container (`DXBC`) signature before it is returned.

This is not Xbox shader translation. No proprietary console bytecode is
parsed, decompiled, or translated. Compiler installation is optional for the
rest of the program; shader commands return a CTest skip code when `dxc.exe`
cannot be found.

## Cache behavior

Cache entries are named by a SHA-256 digest covering the source path and bytes,
entry point, target profile, debug-information setting, include search paths,
and DXC executable path/size/last-write fingerprint. Cache writes use a
temporary file followed by a replace operation. A cache hit is accepted only
after checking the DXIL container signature.

Files containing `#include` compile normally but bypass the cache. Header
dependency tracking is not implemented yet, so caching those files could
return stale bytecode. The compiler captures bounded stdout/stderr diagnostics
and returns them with successful builds; nonzero DXC exit codes are errors.

The synthetic smoke shader exposes compute, vertex, and pixel entry points.
CTest compiles and validates all three profiles; the compute test also verifies
that a repeated compile is served from the cache. A separate Phase 4 test
compiles the ray-tracing library profile.

## Commands

```powershell
.\build\Debug\RayTime.exe --compile-shader .\shaders\phase3_smoke.hlsl --stage compute
.\build\Debug\RayTime.exe --validate-shader .\shaders\phase3_smoke.hlsl --stage compute
```

Use `--dxc <path>` when DXC is not on `PATH`, `--cache-dir <path>` to override
the default `%LOCALAPPDATA%\RayTime\ShaderCache`, `--entry <name>` to select a
different entry point, and `--debug-shaders` to embed debug information.
