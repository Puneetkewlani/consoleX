#pragma once

#include "core/error.hpp"

#include <d3d12.h>

#include <string_view>

namespace raytime::raytracing {

struct DxrCapabilities final {
    D3D12_RAYTRACING_TIER tier = D3D12_RAYTRACING_TIER_NOT_SUPPORTED;

    [[nodiscard]] bool supported() const noexcept;
    [[nodiscard]] std::string_view tierName() const noexcept;
};

[[nodiscard]] bool queryDxrCapabilities(
    ID3D12Device* device,
    DxrCapabilities& capabilities,
    core::Error& error);

} // namespace raytime::raytracing
