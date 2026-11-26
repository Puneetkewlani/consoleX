#include "raytracing/dxr_capabilities.hpp"

#include "gpu/d3d12_error.hpp"

namespace raytime::raytracing {

namespace {

// DXR tier 1.2 has the value 12; older SDK headers may omit its enumerator.
constexpr unsigned int tier1_2 = 12U;

} // namespace

bool DxrCapabilities::supported() const noexcept {
    return static_cast<unsigned int>(tier) >=
           static_cast<unsigned int>(D3D12_RAYTRACING_TIER_1_0);
}

std::string_view DxrCapabilities::tierName() const noexcept {
    switch (static_cast<unsigned int>(tier)) {
    case static_cast<unsigned int>(D3D12_RAYTRACING_TIER_NOT_SUPPORTED):
        return "not supported";
    case static_cast<unsigned int>(D3D12_RAYTRACING_TIER_1_0):
        return "1.0";
    case static_cast<unsigned int>(D3D12_RAYTRACING_TIER_1_1):
        return "1.1";
    case tier1_2:
        return "1.2";
    default:
        return "unknown";
    }
}

bool queryDxrCapabilities(
    ID3D12Device* device,
    DxrCapabilities& capabilities,
    core::Error& error) {
    error = {};
    if (device == nullptr) {
        error.message = "Cannot query DXR capabilities without a D3D12 device.";
        return false;
    }

    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options{};
    const HRESULT result = device->CheckFeatureSupport(
        D3D12_FEATURE_D3D12_OPTIONS5, &options, sizeof(options));
    if (FAILED(result)) {
        return gpu::detail::setHresultError(
            error, "ID3D12Device::CheckFeatureSupport(D3D12_OPTIONS5)", result);
    }

    capabilities.tier = options.RaytracingTier;
    return true;
}

} // namespace raytime::raytracing
