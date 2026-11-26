#pragma once

#include "core/error.hpp"

#include <d3d12.h>
#include <wrl/client.h>

namespace raytime::graphics {

class D3D12CommandList;

class D3D12CommandQueue final {
public:
    D3D12CommandQueue() = default;
    D3D12CommandQueue(const D3D12CommandQueue&) = delete;
    D3D12CommandQueue& operator=(const D3D12CommandQueue&) = delete;

    [[nodiscard]] bool initialize(ID3D12Device* device, core::Error& error);
    void reset() noexcept;
    [[nodiscard]] bool executeAndWait(
        D3D12CommandList& commandList,
        core::Error& error);
    [[nodiscard]] bool waitForGpu(core::Error& error);

    [[nodiscard]] ID3D12CommandQueue* nativeHandle() const noexcept {
        return m_queue.Get();
    }

private:
    Microsoft::WRL::ComPtr<ID3D12Device> m_device;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_queue;
    Microsoft::WRL::ComPtr<ID3D12Fence> m_fence;
    UINT64 m_fenceValue = 0;
};

} // namespace raytime::graphics
