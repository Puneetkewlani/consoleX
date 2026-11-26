#pragma once

#include "core/error.hpp"
#include "gpu/d3d12_context.hpp"
#include "gpu/gpu_capabilities.hpp"
#include "gpu/performance_metrics.hpp"
#include "raytracing/dxr_capabilities.hpp"
#include "shader/dxc_shader_compiler.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace raytime::runtime {

struct RuntimeConfiguration final {
    bool useWarp = false;
    std::filesystem::path dxcExecutable;
    std::filesystem::path shaderCacheDirectory;
};

struct RuntimeCapabilities final {
    std::wstring adapterName;
    std::uint32_t vendorId = 0;
    std::uint32_t deviceId = 0;
    std::uint64_t dedicatedVideoMemoryBytes = 0;
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
    bool usedWarp = false;
    gpu::GpuCapabilities gpu;
    gpu::GpuLocalMemoryInfo localVideoMemory;
    raytracing::DxrCapabilities raytracing;
    bool shaderCompilerAvailable = false;
    std::string shaderCompilerStatus;
};

struct RuntimeSmokeReport final {
    bool graphicsValidationPassed = false;
    gpu::GpuWorkloadMetrics gpuWorkload;
    bool shaderCompiled = false;
    bool shaderCacheHit = false;
    std::size_t shaderDxilBytes = 0;
    std::string shaderStatus;
    bool raytracingValidated = false;
    std::string raytracingStatus;
};

class RuntimeSession final {
public:
    RuntimeSession() = default;
    RuntimeSession(const RuntimeSession&) = delete;
    RuntimeSession& operator=(const RuntimeSession&) = delete;

    [[nodiscard]] bool initialize(
        const RuntimeConfiguration& configuration,
        core::Error& error);
    [[nodiscard]] bool runSyntheticSmokeTest(
        const std::filesystem::path& computeShaderSource,
        const std::filesystem::path& raytracingShaderSource,
        bool debugShaders,
        RuntimeSmokeReport& report,
        core::Error& error);
    [[nodiscard]] bool shutdown(core::Error& error);

    [[nodiscard]] bool initialized() const noexcept {
        return m_initialized;
    }

    [[nodiscard]] const RuntimeCapabilities& capabilities() const noexcept {
        return m_capabilities;
    }

private:
    gpu::D3D12Context m_graphicsContext;
    shader::DxcShaderCompiler m_shaderCompiler;
    RuntimeCapabilities m_capabilities;
    bool m_initialized = false;
};

} // namespace raytime::runtime
