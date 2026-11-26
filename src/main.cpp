#include "gpu/d3d12_context.hpp"
#include "gpu/gpu_capabilities.hpp"
#include "gpu/performance_metrics.hpp"
#include "graphics/graphics_validation.hpp"
#include "raytracing/dxr_capabilities.hpp"
#include "raytracing/dxr_validation.hpp"
#include "shader/dxc_shader_compiler.hpp"

#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string_view>

namespace {

void printUsage() {
    std::cout << "RayTime - Windows D3D12 initialization research framework\n"
                 "Usage:\n"
                 "  RayTime [--warp] [--validate]\n"
                 "  RayTime --gpu-capabilities [--warp]\n"
                 "  RayTime --benchmark-gpu [--warp]\n"
                 "  RayTime --raytracing-capabilities [--warp]\n"
                 "  RayTime --validate-dxr <shader> [--warp] [DXC options]\n"
                 "  RayTime --compile-shader <file> [shader options]\n"
                 "  RayTime --validate-shader <file> [shader options]\n"
                 "  --warp      Select Microsoft's software D3D12 adapter.\n"
                 "  --validate  Run D3D12 resource, copy, and synchronization checks.\n"
                 "  --gpu-capabilities  Report adapter vendor and D3D12 capabilities.\n"
                 "  --benchmark-gpu  Time a synthetic GPU copy workload.\n"
                 "  --raytracing-capabilities  Query the selected adapter's DXR tier.\n"
                 "  --validate-dxr  Dispatch rays against a synthetic triangle and check readback.\n"
                 "Shader options: --stage vertex|pixel|compute|library, --entry <name>,\n"
                 "                --dxc <path>, --cache-dir <path>, --debug-shaders\n";
}

bool parseShaderStage(
    std::string_view value,
    raytime::shader::ShaderStage& stage) {
    if (value == "vertex") {
        stage = raytime::shader::ShaderStage::Vertex;
        return true;
    }
    if (value == "pixel") {
        stage = raytime::shader::ShaderStage::Pixel;
        return true;
    }
    if (value == "compute") {
        stage = raytime::shader::ShaderStage::Compute;
        return true;
    }
    if (value == "library") {
        stage = raytime::shader::ShaderStage::Library;
        return true;
    }
    return false;
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
    bool validateShader = false;
    bool shaderCommand = false;
    bool validateDxr = false;
    bool reportGpuCapabilities = false;
    bool benchmarkGpu = false;
    bool reportRaytracingCapabilities = false;
    bool debugShaders = false;
    bool shaderOptionsSeen = false;
    bool shaderStageOptionsSeen = false;
    std::filesystem::path shaderSource;
    std::filesystem::path raytracingShaderSource;
    std::filesystem::path dxcExecutable;
    std::filesystem::path cacheDirectory;
    raytime::shader::ShaderStage shaderStage =
        raytime::shader::ShaderStage::Compute;
    std::string shaderEntry = "main";

    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--warp") {
            useWarp = true;
        } else if (argument == "--validate") {
            validate = true;
        } else if (argument == "--gpu-capabilities") {
            reportGpuCapabilities = true;
        } else if (argument == "--benchmark-gpu") {
            benchmarkGpu = true;
        } else if (argument == "--raytracing-capabilities") {
            reportRaytracingCapabilities = true;
        } else if (argument == "--validate-dxr") {
            if (index + 1 >= argc || validateDxr || shaderCommand) {
                std::cerr << "--validate-dxr requires one shader-library path.\n";
                printUsage();
                return 2;
            }
            validateDxr = true;
            raytracingShaderSource = argv[++index];
        } else if (argument == "--compile-shader" ||
                   argument == "--validate-shader") {
            if (index + 1 >= argc || shaderCommand || validateDxr) {
                std::cerr << "Provide exactly one shader source path.\n";
                printUsage();
                return 2;
            }
            shaderCommand = true;
            validateShader = argument == "--validate-shader";
            shaderSource = argv[++index];
        } else if (argument == "--stage") {
            shaderOptionsSeen = true;
            shaderStageOptionsSeen = true;
            if (index + 1 >= argc ||
                !parseShaderStage(argv[++index], shaderStage)) {
                std::cerr << "Shader stage must be vertex, pixel, compute, or library.\n";
                return 2;
            }
        } else if (argument == "--entry") {
            shaderOptionsSeen = true;
            shaderStageOptionsSeen = true;
            if (index + 1 >= argc) {
                std::cerr << "--entry requires a function name.\n";
                return 2;
            }
            shaderEntry = argv[++index];
        } else if (argument == "--dxc") {
            shaderOptionsSeen = true;
            if (index + 1 >= argc) {
                std::cerr << "--dxc requires an executable path.\n";
                return 2;
            }
            dxcExecutable = argv[++index];
        } else if (argument == "--cache-dir") {
            shaderOptionsSeen = true;
            if (index + 1 >= argc) {
                std::cerr << "--cache-dir requires a directory path.\n";
                return 2;
            }
            cacheDirectory = argv[++index];
        } else if (argument == "--debug-shaders") {
            shaderOptionsSeen = true;
            debugShaders = true;
        } else if (argument == "--help" || argument == "-h") {
            printUsage();
            return 0;
        } else {
            std::cerr << "Unknown argument: " << argument << '\n';
            printUsage();
            return 2;
        }
    }

    if (shaderOptionsSeen && !shaderCommand && !validateDxr) {
        std::cerr << "Shader options require a shader compilation command.\n";
        printUsage();
        return 2;
    }
    if (shaderStageOptionsSeen && validateDxr) {
        std::cerr << "--stage and --entry cannot be combined with --validate-dxr.\n";
        printUsage();
        return 2;
    }
    if (validateDxr &&
        (validate || reportGpuCapabilities || reportRaytracingCapabilities ||
         benchmarkGpu)) {
        std::cerr << "--validate-dxr cannot be combined with other D3D12 validation commands.\n";
        return 2;
    }
    if (shaderCommand && validateDxr) {
        std::cerr << "Select either shader compilation or DXR validation, not both.\n";
        return 2;
    }
    if (shaderCommand) {
        if (shaderSource.empty()) {
            std::cerr << "Shader source path cannot be empty.\n";
            return 2;
        }
        if (validate || useWarp || validateDxr || reportGpuCapabilities ||
            reportRaytracingCapabilities || benchmarkGpu) {
            std::cerr << "D3D12 options cannot be combined with shader compilation.\n";
            return 2;
        }

        raytime::shader::DxcShaderCompiler compiler;
        raytime::core::Error error;
        if (!compiler.initialize(dxcExecutable, cacheDirectory, error)) {
            std::cerr << "[error] " << error.message << '\n';
            return error.resultCode ==
                    raytime::shader::dxcUnavailableResultCode
                ? 77
                : 1;
        }

        raytime::shader::ShaderCompileDescription description;
        description.sourcePath = shaderSource;
        description.stage = shaderStage;
        description.entryPoint = shaderEntry;
        description.debugInformation = debugShaders;

        if (validateShader) {
            if (!raytime::shader::validateShaderCache(
                    compiler, description, error)) {
                std::cerr << "[error] " << error.message << '\n';
                return 1;
            }
            std::cout << "[shader] DXC compilation and cache validation passed.\n";
            return 0;
        }

        raytime::shader::CompiledShader compiled;
        if (!compiler.compile(description, compiled, error)) {
            std::cerr << "[error] " << error.message << '\n';
            return 1;
        }
        std::cout << "[shader] Compiled " << compiled.dxil.size()
                  << " DXIL bytes (cache "
                  << (compiled.cacheHit ? "hit" : "miss") << ").\n";
        if (!compiled.diagnostics.empty()) {
            std::clog << compiled.diagnostics;
        }
        return 0;
    }

    if ((reportRaytracingCapabilities || reportGpuCapabilities ||
         benchmarkGpu) &&
        validate) {
        std::cerr << "GPU reports and benchmarks cannot be combined with --validate.\n";
        return 2;
    }
    const unsigned int gpuCommandCount =
        static_cast<unsigned int>(reportRaytracingCapabilities) +
        static_cast<unsigned int>(reportGpuCapabilities) +
        static_cast<unsigned int>(benchmarkGpu);
    if (gpuCommandCount > 1) {
        std::cerr << "Select only one GPU capability report or benchmark.\n";
        return 2;
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

    if (benchmarkGpu) {
        raytime::gpu::GpuWorkloadMetrics metrics;
        if (!raytime::gpu::runGpuCopyBenchmark(
                context.device(), context.commandQueue(), metrics, error)) {
            std::cerr << "[error] " << error.message << '\n';
            return 1;
        }
        std::cout << std::fixed << std::setprecision(3);
        std::cout << "[GPU benchmark] Workload: " << metrics.copyCount
                  << " copies of 4 MiB (" << metrics.bytesCopied
                  << " bytes total)\n";
        std::cout << "[GPU benchmark] GPU timestamp duration: "
                  << metrics.gpuElapsedMilliseconds << " ms\n";
        std::cout << "[GPU benchmark] CPU submit + fence wait: "
                  << metrics.cpuSubmitAndWaitMilliseconds << " ms\n";
        std::cout << "[GPU benchmark] Effective copy throughput: "
                  << metrics.effectiveThroughputGigabytesPerSecond()
                  << " GB/s\n";
        std::cout << "[GPU benchmark] Synthetic workload metric only; not "
                     "frame time or GPU utilization.\n";
        return 0;
    }

    if (reportGpuCapabilities) {
        raytime::gpu::GpuCapabilities capabilities;
        if (!raytime::gpu::queryGpuCapabilities(
                context.device(), context.vendorId(), capabilities, error)) {
            std::cerr << "[error] " << error.message << '\n';
            return 1;
        }

        std::wcout << L"[GPU] Adapter name: " << context.adapterName() << L'\n';
        std::cout << "[GPU] Vendor: "
                  << raytime::gpu::gpuVendorName(capabilities.vendor)
                  << " (vendor ID 0x" << std::hex << context.vendorId()
                  << ", device ID 0x" << context.deviceId() << std::dec << ")\n";
        std::cout << "[GPU] AMD vendor match: "
                  << (capabilities.vendor == raytime::gpu::GpuVendor::Amd
                          ? "yes"
                          : "no")
                  << '\n';
        std::cout << "[GPU] Dedicated video memory: ";
        if (context.dedicatedVideoMemoryBytes() == 0) {
            std::cout << "not reported by DXGI\n";
        } else {
            std::cout << context.dedicatedVideoMemoryBytes() /
                             (1024U * 1024U)
                      << " MiB\n";
        }
        raytime::gpu::GpuLocalMemoryInfo memoryInfo;
        if (!raytime::gpu::queryGpuLocalMemoryInfo(
                context.adapter(), memoryInfo, error)) {
            std::cerr << "[error] " << error.message << '\n';
            return 1;
        }
        std::cout << "[GPU] Process local-memory usage/budget: ";
        if (memoryInfo.available) {
            constexpr std::uint64_t bytesPerMiB = 1024U * 1024U;
            std::cout << memoryInfo.currentUsageBytes / bytesPerMiB << '/'
                      << memoryInfo.budgetBytes / bytesPerMiB << " MiB\n";
        } else {
            std::cout << "not available from DXGI\n";
        }
        std::cout << "[GPU] D3D feature level: "
                  << featureLevelName(context.featureLevel()) << '\n';
        std::cout << "[GPU] Highest reported shader model: "
                  << raytime::gpu::shaderModelName(capabilities.shaderModel);
        if (raytime::gpu::shaderModelName(capabilities.shaderModel) ==
            "unknown") {
            std::cout << " (value 0x" << std::hex
                      << static_cast<unsigned int>(capabilities.shaderModel)
                      << std::dec << ')';
        }
        std::cout << '\n';
        std::cout << "[GPU] Resource binding tier: "
                  << static_cast<unsigned int>(
                         capabilities.resourceBindingTier)
                  << '\n';

        raytime::raytracing::DxrCapabilities dxrCapabilities;
        if (!raytime::raytracing::queryDxrCapabilities(
                context.device(), dxrCapabilities, error)) {
            std::cerr << "[error] " << error.message << '\n';
            return 1;
        }
        std::cout << "[GPU] DXR tier: " << dxrCapabilities.tierName();
        if (dxrCapabilities.tierName() == "unknown") {
            std::cout << " (value "
                      << static_cast<unsigned int>(dxrCapabilities.tier) << ')';
        }
        std::cout << '\n';
        std::cout << "[GPU] Vendor identification uses the DXGI PCI vendor ID; "
                     "it does not validate a specific Radeon model or driver.\n";
        return 0;
    }

    if (reportRaytracingCapabilities) {
        raytime::raytracing::DxrCapabilities capabilities;
        if (!raytime::raytracing::queryDxrCapabilities(
                context.device(), capabilities, error)) {
            std::cerr << "[error] " << error.message << '\n';
            return 1;
        }
        std::cout << "[DXR] D3D12 ray-tracing tier: "
                  << capabilities.tierName();
        if (capabilities.tierName() == "unknown") {
            std::cout << " (value "
                      << static_cast<unsigned int>(capabilities.tier) << ')';
        }
        std::cout << '\n';
        if (capabilities.supported()) {
            std::cout << "[DXR] The selected device reports DXR support; "
                         "this probe does not establish hardware acceleration "
                         "or performance.\n";
        } else {
            std::cout << "[DXR] Unsupported by this device; no ray-tracing "
                         "workload was submitted and no software fallback is "
                         "implemented.\n";
        }
        return 0;
    }

    if (validateDxr) {
        raytime::raytracing::DxrCapabilities capabilities;
        if (!raytime::raytracing::queryDxrCapabilities(
                context.device(), capabilities, error)) {
            std::cerr << "[error] " << error.message << '\n';
            return 1;
        }
        if (!capabilities.supported()) {
            std::cout << "[DXR] Validation skipped: selected device does not "
                         "report DXR support.\n";
            return 77;
        }

        raytime::shader::DxcShaderCompiler compiler;
        if (!compiler.initialize(dxcExecutable, cacheDirectory, error)) {
            std::cerr << "[error] " << error.message << '\n';
            return error.resultCode ==
                    raytime::shader::dxcUnavailableResultCode
                ? 77
                : 1;
        }
        if (!raytime::raytracing::runDxrSmokeTest(
                context.device(),
                context.commandQueue(),
                compiler,
                raytracingShaderSource,
                debugShaders,
                error)) {
            std::cerr << "[error] " << error.message << '\n';
            return 1;
        }
        std::cout << "[DXR] Synthetic triangle hit/miss validation passed.\n";
        return 0;
    }

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
