#pragma once

#include "graphics/graphics_types.hpp"

#include <d3d12.h>

namespace raytime::graphics::detail {

inline bool isAllowedState(MemoryUsage memoryUsage, ResourceState state) {
    switch (state) {
    case ResourceState::Common:
    case ResourceState::CopySource:
    case ResourceState::CopyDestination:
    case ResourceState::GenericRead:
        break;
    default:
        return false;
    }

    switch (memoryUsage) {
    case MemoryUsage::DeviceLocal:
        return true;
    case MemoryUsage::Upload:
        return state == ResourceState::GenericRead;
    case MemoryUsage::Readback:
        return state == ResourceState::CopyDestination;
    }
    return false;
}

inline D3D12_RESOURCE_STATES toNativeState(ResourceState state) {
    switch (state) {
    case ResourceState::Common:
        return D3D12_RESOURCE_STATE_COMMON;
    case ResourceState::CopySource:
        return D3D12_RESOURCE_STATE_COPY_SOURCE;
    case ResourceState::CopyDestination:
        return D3D12_RESOURCE_STATE_COPY_DEST;
    case ResourceState::GenericRead:
        return D3D12_RESOURCE_STATE_GENERIC_READ;
    }
    return D3D12_RESOURCE_STATE_COMMON;
}

} // namespace raytime::graphics::detail
