#pragma once

#include <string>

namespace raytime::core {

struct Error {
    std::string message;
    long resultCode = 0;
};

} // namespace raytime::core
