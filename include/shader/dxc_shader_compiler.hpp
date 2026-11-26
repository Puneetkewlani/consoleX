#pragma once

#include "core/error.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace raytime::shader {

inline constexpr long dxcUnavailableResultCode = -2147024894L;

enum class ShaderStage {
    Vertex,
    Pixel,
    Compute,
};

struct ShaderCompileDescription {
    std::filesystem::path sourcePath;
    ShaderStage stage = ShaderStage::Compute;
    std::string entryPoint = "main";
    bool debugInformation = false;
    std::vector<std::filesystem::path> includeDirectories;
};

struct CompiledShader {
    std::vector<std::byte> dxil;
    std::string diagnostics;
    bool cacheHit = false;
};

class DxcShaderCompiler final {
public:
    [[nodiscard]] bool initialize(
        const std::filesystem::path& dxcExecutable,
        const std::filesystem::path& cacheDirectory,
        core::Error& error);

    [[nodiscard]] bool compile(
        const ShaderCompileDescription& description,
        CompiledShader& output,
        core::Error& error) const;

private:
    std::filesystem::path m_dxcExecutable;
    std::filesystem::path m_cacheDirectory;
    std::wstring m_compilerFingerprint;
};

[[nodiscard]] bool validateShaderCache(
    const DxcShaderCompiler& compiler,
    const ShaderCompileDescription& description,
    core::Error& error);

} // namespace raytime::shader
