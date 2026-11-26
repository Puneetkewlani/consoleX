#include "graphics/d3d12_command_queue.hpp"

#include "gpu/d3d12_error.hpp"
#include "graphics/d3d12_command_list.hpp"

#include <Windows.h>

#include <limits>
#include <string>

namespace raytime::graphics {
namespace {

class ScopedHandle final {
public:
    explicit ScopedHandle(HANDLE handle) noexcept : m_handle(handle) {}
    ~ScopedHandle() {
        if (m_handle != nullptr) {
            CloseHandle(m_handle);
        }
    }

    ScopedHandle(const ScopedHandle&) = delete;
    ScopedHandle& operator=(const ScopedHandle&) = delete;

    [[nodiscard]] HANDLE get() const noexcept {
        return m_handle;
    }

private:
    HANDLE m_handle;
};

} // namespace

bool D3D12CommandQueue::initialize(ID3D12Device* device, core::Error& error) {
    error = {};
    if (device == nullptr) {
        error.message = "Cannot initialize a D3D12 command queue without a device.";
        return false;
    }

    D3D12_COMMAND_QUEUE_DESC description{};
    description.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    HRESULT result = device->CreateCommandQueue(
        &description, IID_PPV_ARGS(&m_queue));
    if (FAILED(result)) {
        return gpu::detail::setHresultError(
            error, "ID3D12Device::CreateCommandQueue", result);
    }

    result = device->CreateFence(
        0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence));
    if (FAILED(result)) {
        return gpu::detail::setHresultError(
            error, "ID3D12Device::CreateFence", result);
    }

    m_device = device;
    m_fenceValue = 0;
    return true;
}

bool D3D12CommandQueue::executeAndWait(
    D3D12CommandList& commandList,
    core::Error& error) {
    error = {};
    if (!m_queue || !commandList.isInitialized()) {
        error.message = "Cannot submit an uninitialized D3D12 command list or queue.";
        return false;
    }
    if (commandList.isRecording()) {
        error.message = "Close the D3D12 command list before submitting it.";
        return false;
    }

    ID3D12CommandList* lists[] = {commandList.nativeHandle()};
    m_queue->ExecuteCommandLists(1, lists);
    return waitForGpu(error);
}

bool D3D12CommandQueue::waitForGpu(core::Error& error) {
    error = {};
    if (!m_queue || !m_fence || !m_device) {
        error.message = "Cannot wait for GPU before command queue initialization.";
        return false;
    }
    if (m_fenceValue == std::numeric_limits<UINT64>::max()) {
        error.message = "D3D12 fence value exhausted.";
        return false;
    }

    const UINT64 targetValue = ++m_fenceValue;
    HRESULT result = m_queue->Signal(m_fence.Get(), targetValue);
    if (FAILED(result)) {
        return gpu::detail::setHresultError(
            error, "ID3D12CommandQueue::Signal", result);
    }

    const UINT64 completedValue = m_fence->GetCompletedValue();
    if (completedValue == std::numeric_limits<UINT64>::max()) {
        HRESULT removalReason = m_device->GetDeviceRemovedReason();
        if (SUCCEEDED(removalReason)) {
            removalReason = DXGI_ERROR_DEVICE_REMOVED;
        }
        return gpu::detail::setHresultError(
            error, "D3D12 device removed", removalReason);
    }
    if (completedValue >= targetValue) {
        return true;
    }

    ScopedHandle eventHandle(CreateEventW(nullptr, FALSE, FALSE, nullptr));
    if (eventHandle.get() == nullptr) {
        const DWORD lastError = GetLastError();
        error.message = "CreateEventW failed with Windows error " +
                        std::to_string(lastError) + ".";
        error.resultCode = static_cast<long>(HRESULT_FROM_WIN32(lastError));
        return false;
    }

    result = m_fence->SetEventOnCompletion(targetValue, eventHandle.get());
    if (FAILED(result)) {
        return gpu::detail::setHresultError(
            error, "ID3D12Fence::SetEventOnCompletion", result);
    }

    const DWORD waitResult = WaitForSingleObject(eventHandle.get(), INFINITE);
    const DWORD waitError = waitResult == WAIT_FAILED ? GetLastError() : ERROR_SUCCESS;
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

} // namespace raytime::graphics
