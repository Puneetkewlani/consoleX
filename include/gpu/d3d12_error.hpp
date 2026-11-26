#pragma once

#include "core/error.hpp"

#include <Windows.h>

#include <sstream>

namespace raytime::gpu::detail {

inline bool setHresultError(
    core::Error& error,
    const char* operation,
    HRESULT result) {
    std::ostringstream stream;
    stream << operation << " failed (HRESULT 0x" << std::hex
           << static_cast<unsigned long>(result) << ")";
    error.message = stream.str();
    error.resultCode = static_cast<long>(result);
    return false;
}

} // namespace raytime::gpu::detail
