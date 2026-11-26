#pragma once

#include <cstdint>

namespace raytime::graphics {

enum class MemoryUsage : std::uint8_t {
    DeviceLocal,
    Upload,
    Readback,
};

enum class ResourceState : std::uint8_t {
    Common,
    CopySource,
    CopyDestination,
    GenericRead,
};

enum class PixelFormat : std::uint8_t {
    Rgba8Unorm,
};

struct BufferDescription {
    std::uint64_t sizeBytes = 0;
    MemoryUsage memoryUsage = MemoryUsage::DeviceLocal;
};

struct Texture2DDescription {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint16_t mipLevels = 1;
    PixelFormat format = PixelFormat::Rgba8Unorm;
};

} // namespace raytime::graphics
