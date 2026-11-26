# Graphics pipeline

## Current phase

Phase 1 does not render or translate graphics commands. It selects a DXGI
adapter (or explicitly requested WARP adapter), creates a D3D12 device, and
initializes the direct queue, allocator, command list, and fence. The
`--validate --warp` path submits an empty queue signal and waits for completion
as an integration smoke test.

## Planned pipeline

The intended later flow is:

1. Accept a documented, explicitly supported input operation.
2. Validate its arguments and resource state transitions.
3. Map it to a backend-neutral command/resource representation.
4. Record equivalent native D3D12 work and submit it with explicit
   synchronization.
5. Report unsupported operations rather than silently dropping or approximating
   them.

Resource heaps, barriers, descriptors, texture formats, shader compilation,
frame presentation, and rendering correctness remain unimplemented.
