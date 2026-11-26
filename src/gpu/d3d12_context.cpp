#include "gpu/d3d12_context.hpp"

#include "gpu/d3d12_error.hpp"

#include <Windows.h>

#include <array>
#include <iostream>
#include <utility>

namespace raytime::gpu {
namespace {

bool createDeviceForAdapter(
    IDXGIAdapter1* adapter,
    D3D_FEATURE_LEVEL& featureLevel,
    ID3D12Device** device) {
    constexpr std::array featureLevels{
        D3D_FEATURE_LEVEL_12_0,
        D3D_FEATURE_LEVEL_11_0,
    };

    for (const auto candidateLevel : featureLevels) {
        const HRESULT result =
            D3D12CreateDevice(adapter, candidateLevel, IID_PPV_ARGS(device));
        if (SUCCEEDED(result)) {
            featureLevel = candidateLevel;
            return true;
        }
    }
    return false;
}

} // namespace

bool D3D12Context::initialize(bool useWarp, core::Error& error) {
    error = {};

#if defined(_DEBUG)
    Microsoft::WRL::ComPtr<ID3D12Debug> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
        debugController->EnableDebugLayer();
        std::clog << "[D3D12] Debug layer enabled.\n";
    } else {
        std::clog << "[D3D12] Debug layer unavailable; install the Windows "
                     "Graphics Tools optional feature to enable it.\n";
    }
#endif

    UINT factoryFlags = 0;
#if defined(_DEBUG)
    factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
#endif
    HRESULT result = CreateDXGIFactory2(
        factoryFlags, IID_PPV_ARGS(&m_factory));
    if (FAILED(result) && factoryFlags != 0) {
        std::clog << "[DXGI] Debug factory unavailable; retrying without "
                     "debug flags.\n";
        result = CreateDXGIFactory2(0, IID_PPV_ARGS(&m_factory));
    }
    if (FAILED(result)) {
        return detail::setHresultError(error, "CreateDXGIFactory2", result);
    }

    if (useWarp) {
        result = m_factory->EnumWarpAdapter(IID_PPV_ARGS(&m_adapter));
        if (FAILED(result)) {
            return detail::setHresultError(
                error, "IDXGIFactory4::EnumWarpAdapter", result);
        }
    } else {
        for (UINT index = 0;; ++index) {
            Microsoft::WRL::ComPtr<IDXGIAdapter1> candidate;
            result = m_factory->EnumAdapters1(index, &candidate);
            if (result == DXGI_ERROR_NOT_FOUND) {
                break;
            }
            if (FAILED(result)) {
                return detail::setHresultError(
                    error, "IDXGIFactory4::EnumAdapters1", result);
            }

            DXGI_ADAPTER_DESC1 description{};
            result = candidate->GetDesc1(&description);
            if (FAILED(result)) {
                return detail::setHresultError(
                    error, "IDXGIAdapter1::GetDesc1", result);
            }
            if ((description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) {
                continue;
            }

            D3D_FEATURE_LEVEL supportedLevel = D3D_FEATURE_LEVEL_11_0;
            if (createDeviceForAdapter(candidate.Get(), supportedLevel, nullptr)) {
                m_adapter = std::move(candidate);
                m_featureLevel = supportedLevel;
                break;
            }
        }

        if (!m_adapter) {
            error.message =
                "No hardware adapter supports the required D3D12 feature level "
                "(11_0 or newer). Use --warp to test with the software adapter.";
            return false;
        }
    }

    DXGI_ADAPTER_DESC1 adapterDescription{};
    result = m_adapter->GetDesc1(&adapterDescription);
    if (FAILED(result)) {
        return detail::setHresultError(
            error, "IDXGIAdapter1::GetDesc1", result);
    }
    m_adapterName = adapterDescription.Description;
    m_vendorId = adapterDescription.VendorId;
    m_deviceId = adapterDescription.DeviceId;
    m_dedicatedVideoMemoryBytes =
        static_cast<std::uint64_t>(adapterDescription.DedicatedVideoMemory);

    if (!createDeviceForAdapter(
            m_adapter.Get(), m_featureLevel, m_device.ReleaseAndGetAddressOf())) {
        error.message = "D3D12CreateDevice failed for the selected adapter.";
        return false;
    }

    if (!m_commandQueue.initialize(m_device.Get(), error)) {
        return false;
    }

    m_usedWarp = useWarp;
    return true;
}

bool D3D12Context::waitForGpu(core::Error& error) {
    return m_commandQueue.waitForGpu(error);
}

void D3D12Context::shutdown() noexcept {
    m_commandQueue.reset();
    m_device.Reset();
    m_adapter.Reset();
    m_factory.Reset();
    m_adapterName.clear();
    m_vendorId = 0;
    m_deviceId = 0;
    m_dedicatedVideoMemoryBytes = 0;
    m_featureLevel = D3D_FEATURE_LEVEL_11_0;
    m_usedWarp = false;
}

} // namespace raytime::gpu
