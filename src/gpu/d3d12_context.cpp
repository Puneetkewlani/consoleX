#include "gpu/d3d12_context.hpp"

#include <Windows.h>

#include <array>
#include <iostream>
#include <limits>
#include <sstream>
#include <utility>

namespace raytime::gpu {
namespace {

std::string hresultMessage(const char* operation, HRESULT result) {
    std::ostringstream stream;
    stream << operation << " failed (HRESULT 0x" << std::hex
           << static_cast<unsigned long>(result) << ")";
    return stream.str();
}

bool setError(InitializationError& error, const char* operation, HRESULT result) {
    error.message = hresultMessage(operation, result);
    error.resultCode = static_cast<long>(result);
    return false;
}

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

bool D3D12Context::initialize(bool useWarp, InitializationError& error) {
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
        return setError(error, "CreateDXGIFactory2", result);
    }

    if (useWarp) {
        result = m_factory->EnumWarpAdapter(IID_PPV_ARGS(&m_adapter));
        if (FAILED(result)) {
            return setError(error, "IDXGIFactory4::EnumWarpAdapter", result);
        }
    } else {
        for (UINT index = 0;; ++index) {
            Microsoft::WRL::ComPtr<IDXGIAdapter1> candidate;
            result = m_factory->EnumAdapters1(index, &candidate);
            if (result == DXGI_ERROR_NOT_FOUND) {
                break;
            }
            if (FAILED(result)) {
                return setError(error, "IDXGIFactory4::EnumAdapters1", result);
            }

            DXGI_ADAPTER_DESC1 description{};
            result = candidate->GetDesc1(&description);
            if (FAILED(result)) {
                return setError(error, "IDXGIAdapter1::GetDesc1", result);
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
        return setError(error, "IDXGIAdapter1::GetDesc1", result);
    }
    m_adapterName = adapterDescription.Description;

    if (!createDeviceForAdapter(
            m_adapter.Get(), m_featureLevel, m_device.ReleaseAndGetAddressOf())) {
        error.message = "D3D12CreateDevice failed for the selected adapter.";
        return false;
    }

    D3D12_COMMAND_QUEUE_DESC queueDescription{};
    queueDescription.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    result = m_device->CreateCommandQueue(
        &queueDescription, IID_PPV_ARGS(&m_commandQueue));
    if (FAILED(result)) {
        return setError(error, "ID3D12Device::CreateCommandQueue", result);
    }

    result = m_device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator));
    if (FAILED(result)) {
        return setError(error, "ID3D12Device::CreateCommandAllocator", result);
    }

    result = m_device->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        m_commandAllocator.Get(),
        nullptr,
        IID_PPV_ARGS(&m_commandList));
    if (FAILED(result)) {
        return setError(error, "ID3D12Device::CreateCommandList", result);
    }

    result = m_commandList->Close();
    if (FAILED(result)) {
        return setError(error, "ID3D12GraphicsCommandList::Close", result);
    }

    result = m_device->CreateFence(
        0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence));
    if (FAILED(result)) {
        return setError(error, "ID3D12Device::CreateFence", result);
    }

    m_usedWarp = useWarp;
    return true;
}

bool D3D12Context::waitForGpu(InitializationError& error) const {
    error = {};
    if (!m_commandQueue || !m_fence) {
        error.message = "Cannot wait for GPU before D3D12 initialization.";
        return false;
    }
    if (m_fenceValue == std::numeric_limits<UINT64>::max()) {
        error.message = "D3D12 fence value exhausted.";
        return false;
    }

    const UINT64 targetValue = ++m_fenceValue;
    HRESULT result = m_commandQueue->Signal(m_fence.Get(), targetValue);
    if (FAILED(result)) {
        return setError(error, "ID3D12CommandQueue::Signal", result);
    }
    const UINT64 completedValue = m_fence->GetCompletedValue();
    if (completedValue == std::numeric_limits<UINT64>::max()) {
        HRESULT removalReason = m_device->GetDeviceRemovedReason();
        if (SUCCEEDED(removalReason)) {
            removalReason = DXGI_ERROR_DEVICE_REMOVED;
        }
        return setError(
            error, "D3D12 device removed", removalReason);
    }
    if (completedValue >= targetValue) {
        return true;
    }

    HANDLE eventHandle = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (eventHandle == nullptr) {
        const DWORD lastError = GetLastError();
        error.message = "CreateEventW failed with Windows error " +
                        std::to_string(lastError) + ".";
        error.resultCode = static_cast<long>(HRESULT_FROM_WIN32(lastError));
        return false;
    }

    result = m_fence->SetEventOnCompletion(targetValue, eventHandle);
    if (FAILED(result)) {
        CloseHandle(eventHandle);
        return setError(error, "ID3D12Fence::SetEventOnCompletion", result);
    }

    const DWORD waitResult = WaitForSingleObject(eventHandle, INFINITE);
    const DWORD waitError = waitResult == WAIT_FAILED ? GetLastError() : ERROR_SUCCESS;
    CloseHandle(eventHandle);
    if (waitResult != WAIT_OBJECT_0) {
        error.message = "Waiting for the D3D12 fence failed (wait result " +
                        std::to_string(waitResult) + ", Windows error " +
                        std::to_string(waitError) + ").";
        error.resultCode = waitResult == WAIT_FAILED
            ? static_cast<long>(HRESULT_FROM_WIN32(waitError))
            : static_cast<long>(E_FAIL);
        return false;
    }
    return true;
}

} // namespace raytime::gpu
