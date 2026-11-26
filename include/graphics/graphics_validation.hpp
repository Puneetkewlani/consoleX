#pragma once

#include "core/error.hpp"

namespace raytime::gpu {
class D3D12Context;
}

namespace raytime::graphics {

[[nodiscard]] bool runGraphicsValidation(
    gpu::D3D12Context& context,
    core::Error& error);

} // namespace raytime::graphics
