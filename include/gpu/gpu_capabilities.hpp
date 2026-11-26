#pragma once

#include "core/error.hpp"

#include <d3d12.h>
#include <dxgi1_4.h>

#include <cstdint>
#include <string_view>

namespace raytime::gpu {

enum class GpuVendor {
    Amd,
    Nvidia,
    Intel,
    Microsoft,
    Other,
};

struct GpuAdapterDescription final {
    std::uint32_t vendorId = 0;
    std::uint32_t deviceId = 0;
    std::uint64_t dedicatedVideoMemoryBytes = 0;
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
    bool warp = false;
};

struct GpuCapabilities final {
    GpuVendor vendor = GpuVendor::Other;
    D3D_SHADER_MODEL shaderModel = D3D_SHADER_MODEL_5_1;
    D3D12_RESOURCE_BINDING_TIER resourceBindingTier =
        D3D12_RESOURCE_BINDING_TIER_1;
};

struct GpuLocalMemoryInfo final {
    bool available = false;
    std::uint64_t currentUsageBytes = 0;
    std::uint64_t budgetBytes = 0;
};

[[nodiscard]] GpuVendor identifyGpuVendor(std::uint32_t vendorId) noexcept;
[[nodiscard]] std::string_view gpuVendorName(GpuVendor vendor) noexcept;
[[nodiscard]] std::string_view shaderModelName(
    D3D_SHADER_MODEL shaderModel) noexcept;

[[nodiscard]] bool queryGpuCapabilities(
    ID3D12Device* device,
    std::uint32_t vendorId,
    GpuCapabilities& capabilities,
    core::Error& error);

[[nodiscard]] bool queryGpuLocalMemoryInfo(
    IDXGIAdapter1* adapter,
    GpuLocalMemoryInfo& memoryInfo,
    core::Error& error);

} // namespace raytime::gpu
