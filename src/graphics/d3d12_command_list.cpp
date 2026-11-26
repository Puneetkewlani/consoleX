#include "graphics/d3d12_command_list.hpp"

#include "gpu/d3d12_error.hpp"
#include "d3d12_state.hpp"

#include <cstdint>

namespace raytime::graphics {

bool D3D12CommandList::initialize(ID3D12Device* device, core::Error& error) {
    error = {};
    if (device == nullptr) {
        error.message = "Cannot initialize a D3D12 command list without a device.";
        return false;
    }

    HRESULT result = device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_allocator));
    if (FAILED(result)) {
        return gpu::detail::setHresultError(
            error, "ID3D12Device::CreateCommandAllocator", result);
    }

    result = device->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        m_allocator.Get(),
        nullptr,
        IID_PPV_ARGS(&m_commandList));
    if (FAILED(result)) {
        return gpu::detail::setHresultError(
            error, "ID3D12Device::CreateCommandList", result);
    }

    result = m_commandList->Close();
    if (FAILED(result)) {
        return gpu::detail::setHresultError(
            error, "ID3D12GraphicsCommandList::Close", result);
    }
    m_recording = false;
    return true;
}

bool D3D12CommandList::reset(core::Error& error) {
    error = {};
    if (!m_commandList || !m_allocator) {
        error.message = "Cannot reset an uninitialized D3D12 command list.";
        return false;
    }
    if (m_recording) {
        error.message = "Cannot reset a D3D12 command list while it is recording.";
        return false;
    }

    HRESULT result = m_allocator->Reset();
    if (FAILED(result)) {
        return gpu::detail::setHresultError(
            error, "ID3D12CommandAllocator::Reset", result);
    }
    result = m_commandList->Reset(m_allocator.Get(), nullptr);
    if (FAILED(result)) {
        return gpu::detail::setHresultError(
            error, "ID3D12GraphicsCommandList::Reset", result);
    }
    m_recording = true;
    return true;
}

bool D3D12CommandList::transition(
    D3D12Buffer& buffer,
    ResourceState before,
    ResourceState after,
    core::Error& error) {
    error = {};
    if (!m_recording) {
        error.message = "D3D12 resource transitions require a recording command list.";
        return false;
    }
    if (buffer.nativeHandle() == nullptr) {
        error.message = "Cannot transition an uninitialized D3D12 buffer.";
        return false;
    }
    if (!detail::isAllowedState(buffer.memoryUsage(), before) ||
        !detail::isAllowedState(buffer.memoryUsage(), after)) {
        error.message = "The requested resource state is incompatible with the buffer heap.";
        return false;
    }
    if (before == after) {
        return true;
    }

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = buffer.nativeHandle();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = detail::toNativeState(before);
    barrier.Transition.StateAfter = detail::toNativeState(after);
    m_commandList->ResourceBarrier(1, &barrier);
    return true;
}

bool D3D12CommandList::copyBuffer(
    const D3D12Buffer& source,
    D3D12Buffer& destination,
    std::uint64_t sizeBytes,
    ResourceState sourceState,
    ResourceState destinationState,
    core::Error& error) {
    error = {};
    if (!m_recording) {
        error.message = "Buffer copies require a recording D3D12 command list.";
        return false;
    }
    if (source.nativeHandle() == nullptr || destination.nativeHandle() == nullptr) {
        error.message = "Cannot copy between uninitialized D3D12 buffers.";
        return false;
    }
    if (source.nativeHandle() == destination.nativeHandle()) {
        error.message = "A D3D12 buffer copy requires distinct source and destination buffers.";
        return false;
    }
    if (sizeBytes == 0 || sizeBytes > source.sizeBytes() ||
        sizeBytes > destination.sizeBytes()) {
        error.message = "D3D12 buffer copy size must fit both buffers and be nonzero.";
        return false;
    }
    const bool sourceCanCopy =
        sourceState == ResourceState::CopySource ||
        sourceState == ResourceState::GenericRead;
    if (!sourceCanCopy ||
        !detail::isAllowedState(source.memoryUsage(), sourceState) ||
        destinationState != ResourceState::CopyDestination ||
        !detail::isAllowedState(destination.memoryUsage(), destinationState)) {
        error.message = "D3D12 buffer copy requires a copy-source and copy-destination state.";
        return false;
    }

    m_commandList->CopyBufferRegion(
        destination.nativeHandle(), 0, source.nativeHandle(), 0, sizeBytes);
    return true;
}

bool D3D12CommandList::close(core::Error& error) {
    error = {};
    if (!m_commandList || !m_recording) {
        error.message = "Cannot close a D3D12 command list that is not recording.";
        return false;
    }

    const HRESULT result = m_commandList->Close();
    if (FAILED(result)) {
        return gpu::detail::setHresultError(
            error, "ID3D12GraphicsCommandList::Close", result);
    }
    m_recording = false;
    return true;
}

} // namespace raytime::graphics
