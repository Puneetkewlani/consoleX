#include "graphics/d3d12_resources.hpp"

#include "gpu/d3d12_error.hpp"
#include "d3d12_state.hpp"

#include <cstring>
#include <utility>

namespace raytime::graphics {
namespace {

bool getHeapType(
    MemoryUsage memoryUsage,
    D3D12_HEAP_TYPE& heapType,
    ResourceState& initialState,
    core::Error& error) {
    switch (memoryUsage) {
    case MemoryUsage::DeviceLocal:
        heapType = D3D12_HEAP_TYPE_DEFAULT;
        initialState = ResourceState::Common;
        return true;
    case MemoryUsage::Upload:
        heapType = D3D12_HEAP_TYPE_UPLOAD;
        initialState = ResourceState::GenericRead;
        return true;
    case MemoryUsage::Readback:
        heapType = D3D12_HEAP_TYPE_READBACK;
        initialState = ResourceState::CopyDestination;
        return true;
    }

    error.message = "Unsupported D3D12 buffer memory usage.";
    return false;
}

D3D12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE heapType) {
    D3D12_HEAP_PROPERTIES properties{};
    properties.Type = heapType;
    properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    properties.CreationNodeMask = 1;
    properties.VisibleNodeMask = 1;
    return properties;
}

bool validateRange(
    std::uint64_t capacity,
    std::uint64_t offset,
    std::uint64_t length,
    core::Error& error) {
    if (offset > capacity || length > capacity - offset) {
        error.message = "Buffer access range exceeds its allocation.";
        return false;
    }
    return true;
}

} // namespace

bool D3D12Buffer::create(
    ID3D12Device* device,
    const BufferDescription& description,
    D3D12Buffer& output,
    core::Error& error) {
    error = {};
    if (device == nullptr) {
        error.message = "Cannot create a D3D12 buffer without a device.";
        return false;
    }
    if (description.sizeBytes == 0) {
        error.message = "D3D12 buffer size must be greater than zero.";
        return false;
    }

    D3D12_HEAP_TYPE heapType{};
    ResourceState initialState{};
    if (!getHeapType(description.memoryUsage, heapType, initialState, error)) {
        return false;
    }

    D3D12_RESOURCE_DESC resourceDescription{};
    resourceDescription.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDescription.Width = description.sizeBytes;
    resourceDescription.Height = 1;
    resourceDescription.DepthOrArraySize = 1;
    resourceDescription.MipLevels = 1;
    resourceDescription.Format = DXGI_FORMAT_UNKNOWN;
    resourceDescription.SampleDesc.Count = 1;
    resourceDescription.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    const auto properties = heapProperties(heapType);
    const HRESULT result = device->CreateCommittedResource(
        &properties,
        D3D12_HEAP_FLAG_NONE,
        &resourceDescription,
        detail::toNativeState(initialState),
        nullptr,
        IID_PPV_ARGS(&resource));
    if (FAILED(result)) {
        return gpu::detail::setHresultError(
            error, "ID3D12Device::CreateCommittedResource(buffer)", result);
    }

    output.m_resource = std::move(resource);
    output.m_description = description;
    return true;
}

bool D3D12Buffer::write(
    std::span<const std::byte> data,
    std::uint64_t offset,
    core::Error& error) {
    error = {};
    if (!m_resource) {
        error.message = "Cannot write to an uninitialized D3D12 buffer.";
        return false;
    }
    if (m_description.memoryUsage != MemoryUsage::Upload) {
        error.message = "CPU writes are supported only for upload buffers.";
        return false;
    }
    if (!validateRange(m_description.sizeBytes, offset, data.size(), error)) {
        return false;
    }
    if (data.empty()) {
        return true;
    }

    D3D12_RANGE noReadRange{0, 0};
    void* mapped = nullptr;
    const HRESULT result = m_resource->Map(0, &noReadRange, &mapped);
    if (FAILED(result)) {
        return gpu::detail::setHresultError(
            error, "ID3D12Resource::Map(upload)", result);
    }

    std::memcpy(
        static_cast<std::byte*>(mapped) + static_cast<std::size_t>(offset),
        data.data(),
        data.size());
    const D3D12_RANGE writtenRange{
        static_cast<SIZE_T>(offset),
        static_cast<SIZE_T>(offset + data.size()),
    };
    m_resource->Unmap(0, &writtenRange);
    return true;
}

bool D3D12Buffer::read(
    std::span<std::byte> data,
    std::uint64_t offset,
    core::Error& error) const {
    error = {};
    if (!m_resource) {
        error.message = "Cannot read from an uninitialized D3D12 buffer.";
        return false;
    }
    if (m_description.memoryUsage != MemoryUsage::Readback) {
        error.message = "CPU reads are supported only for readback buffers.";
        return false;
    }
    if (!validateRange(m_description.sizeBytes, offset, data.size(), error)) {
        return false;
    }
    if (data.empty()) {
        return true;
    }

    const D3D12_RANGE readRange{
        static_cast<SIZE_T>(offset),
        static_cast<SIZE_T>(offset + data.size()),
    };
    void* mapped = nullptr;
    const HRESULT result = m_resource->Map(0, &readRange, &mapped);
    if (FAILED(result)) {
        return gpu::detail::setHresultError(
            error, "ID3D12Resource::Map(readback)", result);
    }

    std::memcpy(
        data.data(),
        static_cast<const std::byte*>(mapped) + static_cast<std::size_t>(offset),
        data.size());
    D3D12_RANGE noWriteRange{0, 0};
    m_resource->Unmap(0, &noWriteRange);
    return true;
}

ResourceState D3D12Buffer::initialState() const noexcept {
    switch (m_description.memoryUsage) {
    case MemoryUsage::Upload:
        return ResourceState::GenericRead;
    case MemoryUsage::Readback:
        return ResourceState::CopyDestination;
    case MemoryUsage::DeviceLocal:
        return ResourceState::Common;
    }
    return ResourceState::Common;
}

bool D3D12Texture2D::create(
    ID3D12Device* device,
    const Texture2DDescription& description,
    D3D12Texture2D& output,
    core::Error& error) {
    error = {};
    if (device == nullptr) {
        error.message = "Cannot create a D3D12 texture without a device.";
        return false;
    }
    if (description.width == 0 || description.height == 0 ||
        description.mipLevels == 0) {
        error.message = "D3D12 texture dimensions and mip count must be nonzero.";
        return false;
    }

    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    switch (description.format) {
    case PixelFormat::Rgba8Unorm:
        format = DXGI_FORMAT_R8G8B8A8_UNORM;
        break;
    default:
        error.message = "Unsupported D3D12 texture pixel format.";
        return false;
    }

    D3D12_RESOURCE_DESC resourceDescription{};
    resourceDescription.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resourceDescription.Width = description.width;
    resourceDescription.Height = description.height;
    resourceDescription.DepthOrArraySize = 1;
    resourceDescription.MipLevels = description.mipLevels;
    resourceDescription.Format = format;
    resourceDescription.SampleDesc.Count = 1;
    resourceDescription.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    const auto properties = heapProperties(D3D12_HEAP_TYPE_DEFAULT);
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    const HRESULT result = device->CreateCommittedResource(
        &properties,
        D3D12_HEAP_FLAG_NONE,
        &resourceDescription,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(&resource));
    if (FAILED(result)) {
        return gpu::detail::setHresultError(
            error, "ID3D12Device::CreateCommittedResource(texture)", result);
    }

    output.m_resource = std::move(resource);
    output.m_description = description;
    return true;
}

} // namespace raytime::graphics
