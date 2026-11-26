#pragma once

#include "core/error.hpp"
#include "graphics/d3d12_command_queue.hpp"

#include <string>

#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

namespace raytime::gpu {

class D3D12Context final {
public:
    D3D12Context() = default;
    D3D12Context(const D3D12Context&) = delete;
    D3D12Context& operator=(const D3D12Context&) = delete;

    [[nodiscard]] bool initialize(bool useWarp, core::Error& error);
    [[nodiscard]] bool waitForGpu(core::Error& error);

    [[nodiscard]] ID3D12Device* device() const noexcept {
        return m_device.Get();
    }

    [[nodiscard]] graphics::D3D12CommandQueue& commandQueue() noexcept {
        return m_commandQueue;
    }

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
    graphics::D3D12CommandQueue m_commandQueue;
    std::wstring m_adapterName;
    D3D_FEATURE_LEVEL m_featureLevel = D3D_FEATURE_LEVEL_11_0;
    bool m_usedWarp = false;
};

} // namespace raytime::gpu
