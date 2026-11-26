#pragma once

#include <string>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

namespace raytime::gpu {

struct InitializationError {
    std::string message;
    long resultCode = 0;
};

class D3D12Context final {
public:
    D3D12Context() = default;
    D3D12Context(const D3D12Context&) = delete;
    D3D12Context& operator=(const D3D12Context&) = delete;

    [[nodiscard]] bool initialize(bool useWarp, InitializationError& error);
    [[nodiscard]] bool waitForGpu(InitializationError& error) const;

    [[nodiscard]] const std::wstring& adapterName() const noexcept {
        return m_adapterName;
    }

    [[nodiscard]] D3D_FEATURE_LEVEL featureLevel() const noexcept {
        return m_featureLevel;
    }

    [[nodiscard]] bool usedWarp() const noexcept {
        return m_usedWarp;
    }

private:
    template <typename T>
    using ComPtr = Microsoft::WRL::ComPtr<T>;

    ComPtr<IDXGIFactory4> m_factory;
    ComPtr<IDXGIAdapter1> m_adapter;
    ComPtr<ID3D12Device> m_device;
    ComPtr<ID3D12CommandQueue> m_commandQueue;
    ComPtr<ID3D12CommandAllocator> m_commandAllocator;
    ComPtr<ID3D12GraphicsCommandList> m_commandList;
    ComPtr<ID3D12Fence> m_fence;
    std::wstring m_adapterName;
    D3D_FEATURE_LEVEL m_featureLevel = D3D_FEATURE_LEVEL_11_0;
    mutable UINT64 m_fenceValue = 0;
    bool m_usedWarp = false;
};

} // namespace raytime::gpu
