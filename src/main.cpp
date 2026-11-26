#include "gpu/d3d12_context.hpp"
#include "graphics/graphics_validation.hpp"

#include <iostream>
#include <string_view>

namespace {

void printUsage() {
    std::cout << "RayTime - Windows D3D12 initialization research framework\n"
                 "Usage: RayTime [--warp] [--validate]\n"
                 "  --warp      Select Microsoft's software D3D12 adapter.\n"
                 "  --validate  Run D3D12 resource, copy, and synchronization checks.\n";
}

const char* featureLevelName(D3D_FEATURE_LEVEL featureLevel) {
    switch (featureLevel) {
    case D3D_FEATURE_LEVEL_12_0:
        return "12_0";
    case D3D_FEATURE_LEVEL_11_0:
        return "11_0";
    default:
        return "unknown";
    }
}

} // namespace

int main(int argc, char* argv[]) {
    bool useWarp = false;
    bool validate = false;

    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--warp") {
            useWarp = true;
        } else if (argument == "--validate") {
            validate = true;
        } else if (argument == "--help" || argument == "-h") {
            printUsage();
            return 0;
        } else {
            std::cerr << "Unknown argument: " << argument << '\n';
            printUsage();
            return 2;
        }
    }

    raytime::gpu::D3D12Context context;
    raytime::core::Error error;
    if (!context.initialize(useWarp, error)) {
        std::cerr << "[error] " << error.message << '\n';
        return 1;
    }

    std::wcout << L"[D3D12] Adapter: " << context.adapterName() << L'\n';
    std::cout << "[D3D12] Feature level: "
              << featureLevelName(context.featureLevel()) << '\n';
    std::cout << "[D3D12] Adapter type: "
              << (context.usedWarp() ? "WARP software adapter" : "hardware")
              << '\n';

    if (validate) {
        if (!raytime::graphics::runGraphicsValidation(context, error)) {
            std::cerr << "[error] " << error.message << '\n';
            return 1;
        }
        std::cout << "[test] D3D12 resource, command-list, and queue validation passed.\n";
    } else {
        if (!context.waitForGpu(error)) {
            std::cerr << "[error] " << error.message << '\n';
            return 1;
        }
        std::cout << "[D3D12] Initialization and queue synchronization completed.\n";
    }
    return 0;
}
