#pragma once

#include "core/error.hpp"

#include <filesystem>

struct ID3D12Device;

namespace raytime::graphics {
class D3D12CommandQueue;
}

namespace raytime::shader {
class DxcShaderCompiler;
}

namespace raytime::raytracing {

[[nodiscard]] bool runDxrSmokeTest(
    ID3D12Device* device,
    graphics::D3D12CommandQueue& commandQueue,
    const shader::DxcShaderCompiler& shaderCompiler,
    const std::filesystem::path& shaderPath,
    bool debugShaders,
    core::Error& error);

} // namespace raytime::raytracing
