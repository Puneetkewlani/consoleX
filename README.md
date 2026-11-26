# ConsoleX

### An Experimental Xbox Series Game Compatibility Layer for Windows

<p align="center">
  <strong>Bringing Console Gaming Technologies to Windows</strong>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/Language-C%2B%2B20-blue">
  <img src="https://img.shields.io/badge/Platform-Windows%2010%2F11-blue">
  <img src="https://img.shields.io/badge/Graphics-Direct3D%2012-orange">
  <img src="https://img.shields.io/badge/Ray%20Tracing-DXR-purple">
  <img src="https://img.shields.io/badge/Status-Experimental-yellow">
  <img src="https://img.shields.io/badge/License-MIT-green">
</p>

---

## Overview

**ConsoleX** is an experimental, source-only Windows compatibility-layer project focused on exploring Xbox Series X|S game compatibility through Windows-native graphics technologies.

The project aims to translate supported console graphics operations into **Direct3D 12**, while exploring hardware-accelerated ray tracing, shader translation, GPU resource management, and AMD Radeon optimization.

With a modular architecture, ConsoleX is designed to provide a foundation for investigating compatibility between console-specific graphics workloads and the Windows graphics ecosystem.

The long-term vision is to explore running supported Xbox Series game workloads in a Windows environment without relying on a traditional full-system emulation approach.

> **Note:** ConsoleX is an experimental research project. Full Xbox Series X|S game compatibility is not yet implemented or guaranteed.

## Core Objectives

- Explore Xbox Series X|S graphics compatibility on Windows.
- Translate supported graphics operations into Direct3D 12.
- Investigate hardware-accelerated ray tracing using DXR.
- Develop shader translation and compilation systems.
- Implement GPU resource and memory management.
- Explore AMD Radeon GPU compatibility.
- Investigate AMD FSR 4.1.2 integration on compatible hardware.
- Maintain a modular, source-only architecture.

## Technology Stack

| Technology | Purpose |
|---|---|
| C++20 | Core development |
| Direct3D 12 | Graphics translation backend |
| DXGI | GPU and display management |
| DXR | Ray-tracing support |
| HLSL | Shader development |
| DXC | Shader compilation |
| CMake | Build system |
| Windows SDK | Windows integration |
| AMD GPUOpen | GPU optimization research |

## Architecture

```text
                  ConsoleX
                     |
           Compatibility Runtime
                     |
            Graphics Abstraction
                     |
         +-----------+-----------+
         |           |           |
     Graphics     Shader      GPU Resource
    Translation  Translation  Management
         |           |           |
         +-----------+-----------+
                     |
                Direct3D 12
                     |
           +---------+---------+
           |                   |
        DXR Engine         AMD Backend
           |                   |
      Ray Tracing        GPU Optimization
                               |
                        FSR Integration
```

## Project Structure

```text
ConsoleX/
│
├── CMakeLists.txt
├── README.md
├── LICENSE
│
├── docs/
│   ├── architecture.md
│   ├── graphics_pipeline.md
│   ├── shader_pipeline.md
│   └── compatibility.md
│
├── include/
│   ├── core/
│   ├── graphics/
│   ├── shader/
│   ├── raytracing/
│   ├── runtime/
│   └── gpu/
│
├── src/
│   ├── core/
│   ├── graphics/
│   ├── raytracing/
│   ├── runtime/
│   ├── gpu/
│   ├── shader/
│   └── main.cpp
│
├── shaders/
├── tests/
├── tools/
└── examples/
```

## Development Roadmap

### Phase 1 — Windows Foundation
- Windows application initialization.
- Direct3D 12 device creation.
- DXGI adapter detection.
- Basic rendering pipeline.

### Phase 2 — Graphics Translation
- Graphics abstraction layer.
- GPU resource management.
- Command queue management.
- Rendering operation translation.

### Phase 3 — Shader Translation
- Experimental HLSL compilation and DXIL container checks through DXC.
- Content-addressed caching for shaders without `#include` dependencies.
- No Xbox shader format or translation support is claimed.

### Phase 4 — Ray Tracing
- DXR integration.
- Acceleration structures.
- Ray-generation and hit shaders.
- Hardware capability detection.

### Phase 5 — AMD Optimization
- AMD Radeon GPU detection.
- GPU performance profiling.
- FSR integration research.
- Rendering optimization.

### Phase 6 — Compatibility Runtime
- Runtime interface abstractions.
- Supported API translation.
- Compatibility testing.
- Integration of developed subsystems.

## System Requirements

**Development environment**

- Windows 10 or Windows 11 (64-bit).
- Visual Studio 2022.
- C++20 compiler.
- Windows SDK.
- CMake.
- DirectX 12-compatible GPU.
- DXC (`dxc.exe`) is optional and required only for shader compile commands.

**Recommended hardware**

- AMD Radeon GPU with DirectX 12 support.
- Hardware ray-tracing support for DXR experiments.
- Sufficient system RAM and GPU memory.

## Building

Clone the repository:

```bash
git clone https://github.com/YOUR_USERNAME/ConsoleX.git

cd ConsoleX
```

Configure the project:

```bash
cmake -S . -B build -A x64
```

Build:

```bash
cmake --build build --config Release
```

Run the executable:

```bash
.\build\Release\RayTime.exe
```

Run the synthetic shader compiler/cache check (requires DXC on `PATH`):

```powershell
.\build\Release\RayTime.exe --validate-shader .\shaders\phase3_smoke.hlsl --stage compute
```

The CTest shader checks validate the compute, vertex, and pixel profiles. DXC
is optional for the rest of the application; shader tests are skipped when it
is not available.

## Design Philosophy

ConsoleX follows four main principles:

- **Source-only:** Development and implementation remain centered around inspectable source code.
- **Modularity:** Every major subsystem should be independently maintainable.
- **Performance:** Use native Windows graphics capabilities wherever possible.
- **Transparency:** Clearly document implemented, experimental, and unsupported features.

## Current Limitations

- Xbox-specific runtime interfaces require further research and implementation.
- Console-specific executable and system interfaces are not automatically compatible with Windows.
- Proprietary SDK functionality cannot be assumed to be available.
- FSR integration depends on supported hardware and software interfaces.
- Full commercial game compatibility is a long-term research objective.

## Disclaimer

ConsoleX is an independent experimental software project. It is not affiliated with, endorsed by, or associated with Microsoft, Xbox, or AMD.

Xbox is a trademark of Microsoft Corporation. AMD and Radeon are trademarks of Advanced Micro Devices, Inc.

This project does not distribute proprietary Xbox SDKs, game files, firmware, or copyrighted game content.

## License

This project is intended to be released under the MIT License.

---

**ConsoleX — Exploring the boundary between console gaming and Windows compatibility.**
