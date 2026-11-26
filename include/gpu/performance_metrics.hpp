#pragma once

#include "core/error.hpp"
#include "graphics/d3d12_command_queue.hpp"

#include <d3d12.h>

#include <cstdint>

namespace raytime::gpu {

struct GpuWorkloadMetrics final {
    double gpuElapsedMilliseconds = 0.0;
    double cpuSubmitAndWaitMilliseconds = 0.0;
    std::uint64_t bytesCopied = 0;
    std::uint32_t copyCount = 0;

    [[nodiscard]] double effectiveThroughputGigabytesPerSecond() const noexcept;
};

[[nodiscard]] bool runGpuCopyBenchmark(
    ID3D12Device* device,
    graphics::D3D12CommandQueue& commandQueue,
    GpuWorkloadMetrics& metrics,
    core::Error& error);

} // namespace raytime::gpu
