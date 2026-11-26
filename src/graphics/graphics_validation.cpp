#include "graphics/graphics_validation.hpp"

#include "gpu/d3d12_context.hpp"
#include "graphics/d3d12_command_list.hpp"
#include "graphics/d3d12_resources.hpp"

#include <array>
#include <cstddef>
#include <iostream>

namespace raytime::graphics {

bool runGraphicsValidation(gpu::D3D12Context& context, core::Error& error) {
    constexpr std::size_t bufferSize = 4096;
    constexpr Texture2DDescription textureDescription{64, 64, 1};
    BufferDescription uploadDescription{
        bufferSize, MemoryUsage::Upload};
    BufferDescription deviceDescription{
        bufferSize, MemoryUsage::DeviceLocal};
    BufferDescription readbackDescription{
        bufferSize, MemoryUsage::Readback};

    D3D12Buffer uploadBuffer;
    D3D12Buffer deviceBuffer;
    D3D12Buffer readbackBuffer;
    D3D12Texture2D texture;
    if (!D3D12Buffer::create(
            context.device(), uploadDescription, uploadBuffer, error) ||
        !D3D12Buffer::create(
            context.device(), deviceDescription, deviceBuffer, error) ||
        !D3D12Buffer::create(
            context.device(), readbackDescription, readbackBuffer, error) ||
        !D3D12Texture2D::create(
            context.device(), textureDescription, texture, error)) {
        return false;
    }

    std::array<std::byte, bufferSize> expected{};
    std::array<std::byte, bufferSize> actual{};
    for (std::size_t index = 0; index < expected.size(); ++index) {
        expected[index] = static_cast<std::byte>((index * 37U) & 0xffU);
    }
    if (!uploadBuffer.write(expected, 0, error)) {
        return false;
    }
    if (deviceBuffer.write(expected, 0, error) || error.message.empty()) {
        error.message = "Device-local buffers must reject CPU writes.";
        return false;
    }
    if (uploadBuffer.write(
            std::span<const std::byte>(expected.data(), 1), bufferSize, error) ||
        error.message.empty()) {
        error.message = "Upload buffers must reject out-of-range writes.";
        return false;
    }

    D3D12CommandList commandList;
    if (!commandList.initialize(context.device(), error) ||
        !commandList.reset(error)) {
        return false;
    }
    if (context.commandQueue().executeAndWait(commandList, error) ||
        error.message.empty()) {
        error.message = "The command queue must reject a recording command list.";
        return false;
    }
    if (commandList.transition(
            uploadBuffer, ResourceState::GenericRead,
            ResourceState::CopyDestination, error) ||
        error.message.empty()) {
        error.message = "Upload buffers must reject incompatible state transitions.";
        return false;
    }
    if (!commandList.transition(
            deviceBuffer, ResourceState::Common,
            ResourceState::CopyDestination, error) ||
        !commandList.copyBuffer(
            uploadBuffer, deviceBuffer, bufferSize,
            uploadBuffer.initialState(), ResourceState::CopyDestination, error) ||
        !commandList.transition(
            deviceBuffer, ResourceState::CopyDestination,
            ResourceState::CopySource, error) ||
        !commandList.copyBuffer(
            deviceBuffer, readbackBuffer, bufferSize,
            ResourceState::CopySource, readbackBuffer.initialState(), error) ||
        !commandList.close(error) ||
        !context.commandQueue().executeAndWait(commandList, error) ||
        !readbackBuffer.read(actual, 0, error)) {
        return false;
    }

    if (actual != expected) {
        error.message = "D3D12 buffer copy round trip produced mismatched data.";
        return false;
    }

    std::clog << "[graphics] Created upload, device-local, and readback buffers "
                 "and a 64x64 RGBA8 texture.\n";
    std::clog << "[graphics] D3D12 buffer copy round trip passed.\n";
    return true;
}

} // namespace raytime::graphics
