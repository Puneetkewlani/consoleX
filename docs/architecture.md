# Architecture and feasibility

## Intended layers

- **Core:** initialization, structured errors, logging, and shared lifetime rules.
- **GPU:** adapter enumeration and capability reporting through DXGI/D3D12.
- **Graphics:** a future backend-neutral command/resource model with a D3D12
  implementation. Translation must be limited to operations whose semantics
  are understood and testable.
- **Ray tracing:** an optional DXR implementation behind runtime feature checks.
  See [raytracing.md](raytracing.md) for the current capability and smoke-test
  scope.
- **Shader pipeline:** validation/compilation experiments using documented
  HLSL inputs and the external DXC compiler; this is not a general Xbox shader
  decompiler. See [shader_pipeline.md](shader_pipeline.md).
- **Runtime compatibility:** explicit interfaces for documented behavior only.
  Unsupported console-specific interfaces remain unsupported.

Phases 1-2 implement the Windows process entry point, DXGI adapter selection,
D3D12 device and direct-queue startup, basic resource wrappers, command-list
recording, buffer barriers/copies, and fence synchronization. The integration
path performs an upload-to-device-local-to-readback copy and checks the result.
Resource descriptors/binding, general command translation, presentation,
shader translation, general DXR scene rendering, and runtime emulation are not
implemented. Phase 3 adds experimental DXC compilation for ordinary HLSL
files and caching for sources without includes; it does not accept console
shader formats. Phase 4 adds a DXR capability query and a synthetic triangle
BLAS/TLAS plus ray dispatch smoke test. General scene rendering and console
shader formats remain unsupported.

## Technical feasibility

Windows 10/11 provides stable DXGI and Direct3D 12 APIs, making a native device
bootstrap, resource management, command submission, and DXR capability checks
feasible. A modular research framework can exercise synthetic workloads and
documented interfaces.

Compatibility with Xbox Series game software cannot be inferred from using
D3D12. Xbox software may depend on platform-specific runtime services,
toolchains, shader formats, memory rules, security boundaries, and proprietary
APIs unavailable to this source-only project. Without documented interfaces
and lawful development inputs, broad compatibility is not a credible claim.

## Major engineering challenges

- Defining accurate semantics before translating commands or resource layouts.
- Managing GPU synchronization, lifetime, and memory residency without leaks
  or use-after-free errors.
- Validating shader behavior across different compilers and hardware.
- Handling optional DXR features and driver variation without false capability
  claims.
- Profiling and diagnosing asynchronous CPU/GPU failures.
- Keeping all runtime interfaces documented and avoiding reliance on private
  console SDKs or protected content.

AMD GPUs are selected through the same vendor-independent DXGI/D3D12 path.
Vendor-specific reporting and any FidelityFX/FSR investigation must use
officially supported interfaces; no FSR support is claimed by this phase.
