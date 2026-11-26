#include "runtime/runtime_session.hpp"

#include "graphics/graphics_validation.hpp"
#include "raytracing/dxr_validation.hpp"

#include <iostream>

namespace raytime::runtime {

bool RuntimeSession::initialize(
    const RuntimeConfiguration& configuration,
    core::Error& error) {
    error = {};
    if (m_initialized || m_graphicsContext.device() != nullptr) {
        error.message = "Runtime session is already initialized.";
        return false;
    }
    m_capabilities = {};

    if (!m_graphicsContext.initialize(configuration.useWarp, error)) {
        m_graphicsContext.shutdown();
        return false;
    }

    m_capabilities.adapterName = m_graphicsContext.adapterName();
    m_capabilities.vendorId = m_graphicsContext.vendorId();
    m_capabilities.deviceId = m_graphicsContext.deviceId();
    m_capabilities.dedicatedVideoMemoryBytes =
        m_graphicsContext.dedicatedVideoMemoryBytes();
    m_capabilities.featureLevel = m_graphicsContext.featureLevel();
    m_capabilities.usedWarp = m_graphicsContext.usedWarp();

    if (!gpu::queryGpuCapabilities(
            m_graphicsContext.device(),
            m_graphicsContext.vendorId(),
            m_capabilities.gpu,
            error) ||
        !gpu::queryGpuLocalMemoryInfo(
            m_graphicsContext.adapter(),
            m_capabilities.localVideoMemory,
            error) ||
        !raytracing::queryDxrCapabilities(
            m_graphicsContext.device(),
            m_capabilities.raytracing,
            error)) {
        m_graphicsContext.shutdown();
        m_capabilities = {};
        return false;
    }

    if (!m_shaderCompiler.initialize(
            configuration.dxcExecutable,
            configuration.shaderCacheDirectory,
            error)) {
        if (error.resultCode != shader::dxcUnavailableResultCode) {
            m_graphicsContext.shutdown();
            m_shaderCompiler = shader::DxcShaderCompiler{};
            m_capabilities = {};
            return false;
        }
        m_capabilities.shaderCompilerAvailable = false;
        m_capabilities.shaderCompilerStatus = error.message;
        error = {};
    } else {
        m_capabilities.shaderCompilerAvailable = true;
        m_capabilities.shaderCompilerStatus = "DXC is available.";
    }

    m_initialized = true;
    return true;
}

bool RuntimeSession::runSyntheticSmokeTest(
    const std::filesystem::path& computeShaderSource,
    const std::filesystem::path& raytracingShaderSource,
    bool debugShaders,
    RuntimeSmokeReport& report,
    core::Error& error) {
    error = {};
    report = {};
    if (!m_initialized) {
        error.message = "Initialize the runtime session before running its smoke test.";
        return false;
    }
    if (computeShaderSource.empty()) {
        error.message =
            "Runtime smoke test requires a compute shader source path.";
        return false;
    }

    if (!graphics::runGraphicsValidation(m_graphicsContext, error)) {
        return false;
    }
    report.graphicsValidationPassed = true;

    if (!gpu::runGpuCopyBenchmark(
            m_graphicsContext.device(),
            m_graphicsContext.commandQueue(),
            report.gpuWorkload,
            error)) {
        return false;
    }

    if (!m_capabilities.shaderCompilerAvailable) {
        report.shaderStatus = m_capabilities.shaderCompilerStatus;
        if (!m_capabilities.raytracing.supported()) {
            report.raytracingStatus =
                "Skipped: selected adapter does not report DXR support.";
        } else {
            report.raytracingStatus =
                "Skipped because DXC is unavailable for the DXR library.";
        }
        return true;
    }

    shader::ShaderCompileDescription description;
    description.sourcePath = computeShaderSource;
    description.stage = shader::ShaderStage::Compute;
    description.entryPoint = "main";
    description.debugInformation = debugShaders;
    shader::CompiledShader compiled;
    if (!m_shaderCompiler.compile(description, compiled, error)) {
        return false;
    }
    if (!compiled.diagnostics.empty()) {
        std::clog << compiled.diagnostics;
    }

    report.shaderCompiled = true;
    report.shaderCacheHit = compiled.cacheHit;
    report.shaderDxilBytes = compiled.dxil.size();
    report.shaderStatus = "Synthetic compute shader compiled to DXIL.";

    if (!m_capabilities.raytracing.supported()) {
        report.raytracingStatus =
            "Skipped: selected adapter does not report DXR support.";
        return true;
    }
    if (raytracingShaderSource.empty()) {
        report.raytracingStatus =
            "Skipped: no synthetic DXR shader-library path was supplied.";
        return true;
    }
    if (!raytracing::runDxrSmokeTest(
            m_graphicsContext.device(),
            m_graphicsContext.commandQueue(),
            m_shaderCompiler,
            raytracingShaderSource,
            debugShaders,
            error)) {
        return false;
    }
    report.raytracingValidated = true;
    report.raytracingStatus =
        "Synthetic DXR triangle hit/miss validation passed.";
    return true;
}

bool RuntimeSession::shutdown(core::Error& error) {
    error = {};
    bool queueFlushed = true;
    if (m_initialized && !m_graphicsContext.waitForGpu(error)) {
        queueFlushed = false;
    }

    m_graphicsContext.shutdown();
    m_shaderCompiler = shader::DxcShaderCompiler{};
    m_capabilities = {};
    m_initialized = false;
    if (!queueFlushed) {
        return false;
    }
    return true;
}

} // namespace raytime::runtime
