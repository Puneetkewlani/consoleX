# Compatibility boundaries

RayTime is a source-only graphics/runtime research project, not an Xbox
emulator. It must not bypass console security, DRM, or game protections.

No Xbox console runtime interfaces, private SDKs, game loaders, protected
content handling, or proprietary shader translation are implemented. Future
runtime work is restricted to documented, lawful interfaces and synthetic or
authorized development inputs. Unknown behavior must be reported as
unsupported, not represented by a fake success path.

The D3D12 bootstrap proves only that a Windows D3D12 device and basic command
synchronization can be initialized. It does not demonstrate compatibility with
any Xbox title. The initial DXR phase reports the tier exposed by the selected
Windows D3D12 device. An unsupported tier means no DXR workload is submitted;
the project does not claim a software ray-tracing fallback.
