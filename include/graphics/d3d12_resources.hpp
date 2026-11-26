#pragma once

#include "core/error.hpp"
#include "graphics/graphics_types.hpp"

#include <d3d12.h>
#include <wrl/client.h>

#include <cstddef>
#include <span>

namespace raytime::graphics {

class D3D12Buffer final {
public:
    D3D12Buffer() = default;
    D3D12Buffer(const D3D12Buffer&) = delete;
    D3D12Buffer& operator=(const D3D12Buffer&) = delete;
    D3D12Buffer(D3D12Buffer&&) noexcept = default;
    D3D12Buffer& operator=(D3D12Buffer&&) noexcept = default;

    [[nodiscard]] static bool create(
        ID3D12Device* device,
        const BufferDescription& description,
        D3D12Buffer& output,
        core::Error& error);

    [[nodiscard]] bool write(
        std::span<const std::byte> data,
        std::uint64_t offset,
        core::Error& error);
    [[nodiscard]] bool read(
        std::span<std::byte> data,
        std::uint64_t offset,
        core::Error& error) const;

    [[nodiscard]] ID3D12Resource* nativeHandle() const noexcept {
        return m_resource.Get();
    }

    [[nodiscard]] std::uint64_t sizeBytes() const noexcept {
        return m_description.sizeBytes;
    }

    [[nodiscard]] MemoryUsage memoryUsage() const noexcept {
        return m_description.memoryUsage;
    }

    [[nodiscard]] ResourceState initialState() const noexcept;

private:
    Microsoft::WRL::ComPtr<ID3D12Resource> m_resource;
    BufferDescription m_description;
};

class D3D12Texture2D final {
public:
    D3D12Texture2D() = default;
    D3D12Texture2D(const D3D12Texture2D&) = delete;
    D3D12Texture2D& operator=(const D3D12Texture2D&) = delete;
    D3D12Texture2D(D3D12Texture2D&&) noexcept = default;
    D3D12Texture2D& operator=(D3D12Texture2D&&) noexcept = default;

    [[nodiscard]] static bool create(
        ID3D12Device* device,
        const Texture2DDescription& description,
        D3D12Texture2D& output,
        core::Error& error);

    [[nodiscard]] ID3D12Resource* nativeHandle() const noexcept {
        return m_resource.Get();
    }

    [[nodiscard]] const Texture2DDescription& description() const noexcept {
        return m_description;
    }

private:
    Microsoft::WRL::ComPtr<ID3D12Resource> m_resource;
    Texture2DDescription m_description;
};

} // namespace raytime::graphics
