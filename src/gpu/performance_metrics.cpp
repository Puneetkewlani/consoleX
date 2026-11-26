#include "gpu/performance_metrics.hpp"

#include "gpu/d3d12_error.hpp"
#include "graphics/d3d12_command_list.hpp"

#include <Windows.h>
#include <wrl/client.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace raytime::gpu {
namespace {

using Microsoft::WRL::ComPtr;

constexpr UINT64 copyBufferSize = 4U * 1024U * 1024U;
constexpr std::uint32_t copyCount = 16;
constexpr std::uint32_t timestampCount = 2;

bool createBuffer(
    ID3D12Device* device,
    D3D12_HEAP_TYPE heapType,
    UINT64 sizeBytes,
    D3D12_RESOURCE_STATES initialState,
    ComPtr<ID3D12Resource>& output,
    core::Error& error,
    const char* operation) {
    D3D12_HEAP_PROPERTIES heapProperties{};
    heapProperties.Type = heapType;
    heapProperties.CreationNodeMask = 1;
    heapProperties.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC description{};
    description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    description.Width = sizeBytes;
    description.Height = 1;
    description.DepthOrArraySize = 1;
    description.MipLevels = 1;
    description.Format = DXGI_FORMAT_UNKNOWN;
    description.SampleDesc.Count = 1;
    description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    const HRESULT result = device->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &description,
        initialState,
        nullptr,
        IID_PPV_ARGS(&output));
    if (FAILED(result)) {
        return detail::setHresultError(error, operation, result);
    }
    return true;
}

bool writeSourceBuffer(ID3D12Resource* resource, core::Error& error) {
    D3D12_RANGE noReadRange{0, 0};
    void* mapped = nullptr;
    const HRESULT result = resource->Map(0, &noReadRange, &mapped);
    if (FAILED(result)) {
        return detail::setHresultError(
            error, "ID3D12Resource::Map(benchmark source)", result);
    }

    auto* bytes = static_cast<std::byte*>(mapped);
    for (UINT64 index = 0; index < copyBufferSize; ++index) {
        bytes[index] = static_cast<std::byte>(index & 0xFFU);
    }
    const D3D12_RANGE writtenRange{
        0, static_cast<SIZE_T>(copyBufferSize)};
    resource->Unmap(0, &writtenRange);
    return true;
}

double elapsedMilliseconds(
    const LARGE_INTEGER& start,
    const LARGE_INTEGER& end,
    const LARGE_INTEGER& frequency) {
    return (static_cast<double>(end.QuadPart - start.QuadPart) * 1000.0) /
           static_cast<double>(frequency.QuadPart);
}

} // namespace

double GpuWorkloadMetrics::effectiveThroughputGigabytesPerSecond() const noexcept {
    if (gpuElapsedMilliseconds <= 0.0) {
        return 0.0;
    }
    return static_cast<double>(bytesCopied) /
           (gpuElapsedMilliseconds * 1'000'000.0);
}

bool runGpuCopyBenchmark(
    ID3D12Device* device,
    graphics::D3D12CommandQueue& commandQueue,
    GpuWorkloadMetrics& metrics,
    core::Error& error) {
    error = {};
    metrics = {};
    if (device == nullptr || commandQueue.nativeHandle() == nullptr) {
        error.message = "GPU copy benchmark requires an initialized D3D12 device and queue.";
        return false;
    }

    UINT64 timestampFrequency = 0;
    HRESULT result =
        commandQueue.nativeHandle()->GetTimestampFrequency(&timestampFrequency);
    if (FAILED(result)) {
        return detail::setHresultError(
            error, "ID3D12CommandQueue::GetTimestampFrequency", result);
    }
    if (timestampFrequency == 0) {
        error.message = "D3D12 command queue reported a zero timestamp frequency.";
        return false;
    }

    LARGE_INTEGER cpuFrequency{};
    if (!QueryPerformanceFrequency(&cpuFrequency)) {
        const DWORD windowsError = GetLastError();
        return detail::setHresultError(
            error,
            "QueryPerformanceFrequency",
            windowsError == ERROR_SUCCESS
                ? E_FAIL
                : HRESULT_FROM_WIN32(windowsError));
    }
    if (cpuFrequency.QuadPart <= 0) {
        error.message = "QueryPerformanceFrequency returned a non-positive frequency.";
        error.resultCode = static_cast<long>(E_FAIL);
        return false;
    }

    ComPtr<ID3D12Resource> source;
    if (!createBuffer(
            device,
            D3D12_HEAP_TYPE_UPLOAD,
            copyBufferSize,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            source,
            error,
            "CreateCommittedResource(benchmark upload)")) {
        return false;
    }
    if (!writeSourceBuffer(source.Get(), error)) {
        return false;
    }

    ComPtr<ID3D12Resource> destination;
    if (!createBuffer(
            device,
            D3D12_HEAP_TYPE_DEFAULT,
            copyBufferSize,
            D3D12_RESOURCE_STATE_COPY_DEST,
            destination,
            error,
            "CreateCommittedResource(benchmark destination)")) {
        return false;
    }

    D3D12_QUERY_HEAP_DESC queryHeapDescription{};
    queryHeapDescription.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    queryHeapDescription.Count = timestampCount;
    ComPtr<ID3D12QueryHeap> queryHeap;
    result = device->CreateQueryHeap(
        &queryHeapDescription, IID_PPV_ARGS(&queryHeap));
    if (FAILED(result)) {
        return detail::setHresultError(
            error, "ID3D12Device::CreateQueryHeap(timestamp)", result);
    }

    ComPtr<ID3D12Resource> timestampReadback;
    if (!createBuffer(
            device,
            D3D12_HEAP_TYPE_READBACK,
            sizeof(UINT64) * timestampCount,
            D3D12_RESOURCE_STATE_COPY_DEST,
            timestampReadback,
            error,
            "CreateCommittedResource(timestamp readback)")) {
        return false;
    }

    graphics::D3D12CommandList commandList;
    if (!commandList.initialize(device, error) ||
        !commandList.reset(error)) {
        return false;
    }
    ID3D12GraphicsCommandList* nativeCommandList = commandList.nativeHandle();
    nativeCommandList->EndQuery(
        queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
    for (std::uint32_t copyIndex = 0; copyIndex < copyCount; ++copyIndex) {
        nativeCommandList->CopyBufferRegion(
            destination.Get(),
            0,
            source.Get(),
            0,
            copyBufferSize);
    }
    nativeCommandList->EndQuery(
        queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
    nativeCommandList->ResolveQueryData(
        queryHeap.Get(),
        D3D12_QUERY_TYPE_TIMESTAMP,
        0,
        timestampCount,
        timestampReadback.Get(),
        0);

    if (!commandList.close(error)) {
        return false;
    }

    LARGE_INTEGER cpuStart{};
    LARGE_INTEGER cpuEnd{};
    if (!QueryPerformanceCounter(&cpuStart)) {
        const DWORD windowsError = GetLastError();
        return detail::setHresultError(
            error,
            "QueryPerformanceCounter",
            windowsError == ERROR_SUCCESS
                ? E_FAIL
                : HRESULT_FROM_WIN32(windowsError));
    }
    if (!commandQueue.executeAndWait(commandList, error)) {
        return false;
    }
    if (!QueryPerformanceCounter(&cpuEnd)) {
        const DWORD windowsError = GetLastError();
        return detail::setHresultError(
            error,
            "QueryPerformanceCounter",
            windowsError == ERROR_SUCCESS
                ? E_FAIL
                : HRESULT_FROM_WIN32(windowsError));
    }

    D3D12_RANGE readRange{0, sizeof(UINT64) * timestampCount};
    void* mapped = nullptr;
    result = timestampReadback->Map(0, &readRange, &mapped);
    if (FAILED(result)) {
        return detail::setHresultError(
            error, "ID3D12Resource::Map(timestamp readback)", result);
    }
    std::array<UINT64, timestampCount> timestamps{};
    std::memcpy(timestamps.data(), mapped, sizeof(timestamps));
    D3D12_RANGE noWriteRange{0, 0};
    timestampReadback->Unmap(0, &noWriteRange);
    if (timestamps[1] < timestamps[0]) {
        error.message = "D3D12 timestamp query returned a decreasing timestamp.";
        return false;
    }

    metrics.gpuElapsedMilliseconds =
        (static_cast<double>(timestamps[1] - timestamps[0]) * 1000.0) /
        static_cast<double>(timestampFrequency);
    metrics.cpuSubmitAndWaitMilliseconds =
        elapsedMilliseconds(cpuStart, cpuEnd, cpuFrequency);
    metrics.copyCount = copyCount;
    metrics.bytesCopied = copyBufferSize * copyCount;
    return true;
}

} // namespace raytime::gpu
