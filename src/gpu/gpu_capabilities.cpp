#include "gpu/gpu_capabilities.hpp"

#include "gpu/d3d12_error.hpp"

#include <array>
#include <wrl/client.h>

namespace raytime::gpu {

GpuVendor identifyGpuVendor(std::uint32_t vendorId) noexcept {
    switch (vendorId) {
    case 0x1002:
        return GpuVendor::Amd;
    case 0x10DE:
        return GpuVendor::Nvidia;
    case 0x8086:
        return GpuVendor::Intel;
    case 0x1414:
        return GpuVendor::Microsoft;
    default:
        return GpuVendor::Other;
    }
}

std::string_view gpuVendorName(GpuVendor vendor) noexcept {
    switch (vendor) {
    case GpuVendor::Amd:
        return "AMD";
    case GpuVendor::Nvidia:
        return "NVIDIA";
    case GpuVendor::Intel:
        return "Intel";
    case GpuVendor::Microsoft:
        return "Microsoft";
    case GpuVendor::Other:
        return "Other";
    }
    return "Unknown";
}

std::string_view shaderModelName(D3D_SHADER_MODEL shaderModel) noexcept {
    switch (shaderModel) {
    case D3D_SHADER_MODEL_5_1:
        return "5.1";
    case D3D_SHADER_MODEL_6_0:
        return "6.0";
    case D3D_SHADER_MODEL_6_1:
        return "6.1";
    case D3D_SHADER_MODEL_6_2:
        return "6.2";
    case D3D_SHADER_MODEL_6_3:
        return "6.3";
    case D3D_SHADER_MODEL_6_4:
        return "6.4";
    case D3D_SHADER_MODEL_6_5:
        return "6.5";
    case D3D_SHADER_MODEL_6_6:
        return "6.6";
    case D3D_SHADER_MODEL_6_7:
        return "6.7";
    default:
        return "unknown";
    }
}

bool queryGpuCapabilities(
    ID3D12Device* device,
    std::uint32_t vendorId,
    GpuCapabilities& capabilities,
    core::Error& error) {
    error = {};
    if (device == nullptr) {
        error.message = "Cannot query GPU capabilities without a D3D12 device.";
        return false;
    }

    D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
    HRESULT result = device->CheckFeatureSupport(
        D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options));
    if (FAILED(result)) {
        return detail::setHresultError(
            error,
            "ID3D12Device::CheckFeatureSupport(D3D12_OPTIONS)",
            result);
    }

    constexpr std::array shaderModels{
        D3D_SHADER_MODEL_6_7,
        D3D_SHADER_MODEL_6_6,
        D3D_SHADER_MODEL_6_5,
        D3D_SHADER_MODEL_6_4,
        D3D_SHADER_MODEL_6_3,
        D3D_SHADER_MODEL_6_2,
        D3D_SHADER_MODEL_6_1,
        D3D_SHADER_MODEL_6_0,
        D3D_SHADER_MODEL_5_1,
    };
    D3D_SHADER_MODEL supportedShaderModel = D3D_SHADER_MODEL_5_1;
    bool shaderModelQueried = false;
    for (const D3D_SHADER_MODEL candidate : shaderModels) {
        D3D12_FEATURE_DATA_SHADER_MODEL shaderModelData{};
        shaderModelData.HighestShaderModel = candidate;
        result = device->CheckFeatureSupport(
            D3D12_FEATURE_SHADER_MODEL,
            &shaderModelData,
            sizeof(shaderModelData));
        if (SUCCEEDED(result)) {
            supportedShaderModel = shaderModelData.HighestShaderModel;
            shaderModelQueried = true;
            break;
        }
        if (result != E_INVALIDARG) {
            return detail::setHresultError(
                error,
                "ID3D12Device::CheckFeatureSupport(SHADER_MODEL)",
                result);
        }
    }
    if (!shaderModelQueried) {
        error.message =
            "The D3D12 device did not accept any supported shader-model query.";
        error.resultCode = static_cast<long>(E_INVALIDARG);
        return false;
    }

    capabilities.vendor = identifyGpuVendor(vendorId);
    capabilities.shaderModel = supportedShaderModel;
    capabilities.resourceBindingTier = options.ResourceBindingTier;
    return true;
}

bool queryGpuLocalMemoryInfo(
    IDXGIAdapter1* adapter,
    GpuLocalMemoryInfo& memoryInfo,
    core::Error& error) {
    error = {};
    memoryInfo = {};
    if (adapter == nullptr) {
        error.message = "Cannot query GPU memory information without a DXGI adapter.";
        return false;
    }

    Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter3;
    HRESULT result = adapter->QueryInterface(IID_PPV_ARGS(&adapter3));
    if (result == E_NOINTERFACE) {
        return true;
    }
    if (FAILED(result)) {
        return detail::setHresultError(
            error, "QueryInterface(IDXGIAdapter3)", result);
    }

    DXGI_QUERY_VIDEO_MEMORY_INFO nativeMemoryInfo{};
    result = adapter3->QueryVideoMemoryInfo(
        0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &nativeMemoryInfo);
    if (result == DXGI_ERROR_UNSUPPORTED) {
        return true;
    }
    if (FAILED(result)) {
        return detail::setHresultError(
            error, "IDXGIAdapter3::QueryVideoMemoryInfo(local)", result);
    }

    memoryInfo.available = true;
    memoryInfo.currentUsageBytes = nativeMemoryInfo.CurrentUsage;
    memoryInfo.budgetBytes = nativeMemoryInfo.Budget;
    return true;
}

} // namespace raytime::gpu
