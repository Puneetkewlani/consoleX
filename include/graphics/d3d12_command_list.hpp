#pragma once

#include "core/error.hpp"
#include "graphics/d3d12_resources.hpp"
#include "graphics/graphics_types.hpp"

#include <d3d12.h>
#include <wrl/client.h>

namespace raytime::graphics {

class D3D12CommandList final {
public:
    D3D12CommandList() = default;
    D3D12CommandList(const D3D12CommandList&) = delete;
    D3D12CommandList& operator=(const D3D12CommandList&) = delete;

    [[nodiscard]] bool initialize(ID3D12Device* device, core::Error& error);
    [[nodiscard]] bool reset(core::Error& error);
    [[nodiscard]] bool transition(
        D3D12Buffer& buffer,
        ResourceState before,
        ResourceState after,
        core::Error& error);
    [[nodiscard]] bool copyBuffer(
        const D3D12Buffer& source,
        D3D12Buffer& destination,
        std::uint64_t sizeBytes,
        ResourceState sourceState,
        ResourceState destinationState,
        core::Error& error);
    [[nodiscard]] bool close(core::Error& error);

    [[nodiscard]] bool isInitialized() const noexcept {
        return m_commandList.Get() != nullptr;
    }

    [[nodiscard]] bool isRecording() const noexcept {
        return m_recording;
    }

    [[nodiscard]] ID3D12GraphicsCommandList* nativeHandle() const noexcept {
        return m_commandList.Get();
    }

private:
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_allocator;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_commandList;
    bool m_recording = false;
};

} // namespace raytime::graphics
