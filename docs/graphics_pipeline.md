# Graphics pipeline

## Current phase

Phases 1-2 do not render a frame. They select a DXGI adapter (or explicitly
requested WARP adapter), create a D3D12 device and direct queue, and provide
RAII wrappers for buffers and basic RGBA8 2D textures. The command-list wrapper
supports resource-state barriers and buffer copies; the queue serializes
submission and fence waiting.

The `--validate --warp` path creates upload, device-local, and readback buffers
plus a 64x64 RGBA8 texture. It writes a synthetic byte pattern, records a copy
through device-local memory, waits for completion, and checks the readback
bytes. This validates allocation, mapping, barriers, command submission, and
synchronization on WARP; it is not a rendering test.

## Planned pipeline

The intended later flow is:

1. Accept a documented, explicitly supported input operation.
2. Validate its arguments and resource state transitions.
3. Map it to a backend-neutral command/resource representation.
4. Record equivalent native D3D12 work and submit it with explicit
   synchronization.
5. Report unsupported operations rather than silently dropping or approximating
   them.

Descriptor heaps, broad texture formats and operations, shader compilation,
frame presentation, and rendering correctness remain unimplemented. State
transitions are explicit; callers must supply the actual before-state and keep
resources alive through queue completion. The initial queue API blocks until
submitted work completes and is intended for serialized single-queue use.
