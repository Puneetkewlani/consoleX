#include "raytracing/dxr_validation.hpp"

#include "gpu/d3d12_error.hpp"
#include "graphics/d3d12_command_list.hpp"
#include "graphics/d3d12_command_queue.hpp"
#include "shader/dxc_shader_compiler.hpp"

#include <d3d12.h>
#include <wrl/client.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <span>
#include <string>

namespace raytime::raytracing {
namespace {

using Microsoft::WRL::ComPtr;

constexpr UINT outputWidth = 2;
constexpr UINT outputHeight = 1;
constexpr std::size_t pixelSizeBytes = 4;

struct Vertex {
    float position[3];
};

struct Payload {
    float color[4];
};

bool createBuffer(
    ID3D12Device* device,
    D3D12_HEAP_TYPE heapType,
    UINT64 sizeBytes,
    D3D12_RESOURCE_FLAGS flags,
    D3D12_RESOURCE_STATES initialState,
    ComPtr<ID3D12Resource>& output,
    core::Error& error,
    const char* operation) {
    D3D12_HEAP_PROPERTIES heapProperties{};
    heapProperties.Type = heapType;
    heapProperties.CreationNodeMask = 1;
    heapProperties.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC description{};
    description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    description.Width = sizeBytes;
    description.Height = 1;
    description.DepthOrArraySize = 1;
    description.MipLevels = 1;
    description.Format = DXGI_FORMAT_UNKNOWN;
    description.SampleDesc.Count = 1;
    description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    description.Flags = flags;

    const HRESULT result = device->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &description,
        initialState,
        nullptr,
        IID_PPV_ARGS(&output));
    if (FAILED(result)) {
        return gpu::detail::setHresultError(error, operation, result);
    }
    return true;
}

bool writeUploadBuffer(
    ID3D12Resource* resource,
    const void* data,
    std::size_t sizeBytes,
    core::Error& error,
    const char* operation) {
    D3D12_RANGE noReadRange{0, 0};
    void* mapped = nullptr;
    const HRESULT result = resource->Map(0, &noReadRange, &mapped);
    if (FAILED(result)) {
        return gpu::detail::setHresultError(error, operation, result);
    }
    std::memcpy(mapped, data, sizeBytes);
    const D3D12_RANGE writtenRange{0, sizeBytes};
    resource->Unmap(0, &writtenRange);
    return true;
}

bool createAccelerationStructure(
    ID3D12Device5* device,
    ID3D12GraphicsCommandList4* commandList,
    const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& inputs,
    const char* label,
    ComPtr<ID3D12Resource>& result,
    ComPtr<ID3D12Resource>& scratch,
    core::Error& error) {
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuildInfo{};
    device->GetRaytracingAccelerationStructurePrebuildInfo(
        &inputs, &prebuildInfo);
    if (prebuildInfo.ResultDataMaxSizeInBytes == 0 ||
        prebuildInfo.ScratchDataSizeInBytes == 0) {
        error.message = std::string("DXR returned invalid prebuild sizes for ") +
                        label + ".";
        return false;
    }

    const std::string resultOperation =
        std::string("CreateCommittedResource(") + label + " result)";
    if (!createBuffer(
            device,
            D3D12_HEAP_TYPE_DEFAULT,
            prebuildInfo.ResultDataMaxSizeInBytes,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
            result,
            error,
            resultOperation.c_str())) {
        return false;
    }

    const std::string scratchOperation =
        std::string("CreateCommittedResource(") + label + " scratch)";
    if (!createBuffer(
            device,
            D3D12_HEAP_TYPE_DEFAULT,
            prebuildInfo.ScratchDataSizeInBytes,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            scratch,
            error,
            scratchOperation.c_str())) {
        return false;
    }

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDescription{};
    buildDescription.Inputs = inputs;
    buildDescription.ScratchAccelerationStructureData =
        scratch->GetGPUVirtualAddress();
    buildDescription.DestAccelerationStructureData =
        result->GetGPUVirtualAddress();
    commandList->BuildRaytracingAccelerationStructure(
        &buildDescription, 0, nullptr);

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = result.Get();
    commandList->ResourceBarrier(1, &barrier);
    return true;
}

bool createShaderTable(
    ID3D12Device* device,
    ID3D12StateObjectProperties* stateObjectProperties,
    std::span<const wchar_t* const> exports,
    ComPtr<ID3D12Resource>& shaderTable,
    core::Error& error) {
    constexpr UINT recordSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    const UINT64 tableSize = static_cast<UINT64>(recordSize) * exports.size();
    if (!createBuffer(
            device,
            D3D12_HEAP_TYPE_UPLOAD,
            tableSize,
            D3D12_RESOURCE_FLAG_NONE,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            shaderTable,
            error,
            "CreateCommittedResource(shader table)")) {
        return false;
    }

    D3D12_RANGE noReadRange{0, 0};
    void* mapped = nullptr;
    const HRESULT result = shaderTable->Map(0, &noReadRange, &mapped);
    if (FAILED(result)) {
        return gpu::detail::setHresultError(
            error, "ID3D12Resource::Map(shader table)", result);
    }

    auto* destination = static_cast<std::byte*>(mapped);
    for (std::size_t index = 0; index < exports.size(); ++index) {
        const void* identifier =
            stateObjectProperties->GetShaderIdentifier(exports[index]);
        if (identifier == nullptr) {
            D3D12_RANGE noWriteRange{0, 0};
            shaderTable->Unmap(0, &noWriteRange);
            error.message = "DXR state object did not return a shader "
                            "identifier for table export index " +
                            std::to_string(index) + ".";
            return false;
        }
        std::memcpy(
            destination + index * recordSize,
            identifier,
            D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
    }
    const D3D12_RANGE writtenRange{
        0, static_cast<SIZE_T>(tableSize)};
    shaderTable->Unmap(0, &writtenRange);
    return true;
}

bool createRootSignature(
    ID3D12Device* device,
    ComPtr<ID3D12RootSignature>& rootSignature,
    core::Error& error) {
    D3D12_DESCRIPTOR_RANGE outputRange{};
    outputRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    outputRange.NumDescriptors = 1;
    outputRange.BaseShaderRegister = 0;
    outputRange.OffsetInDescriptorsFromTableStart =
        D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    std::array<D3D12_ROOT_PARAMETER, 2> parameters{};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[0].DescriptorTable.NumDescriptorRanges = 1;
    parameters[0].DescriptorTable.pDescriptorRanges = &outputRange;
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    parameters[1].Descriptor.ShaderRegister = 0;
    parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC description{};
    description.NumParameters = static_cast<UINT>(parameters.size());
    description.pParameters = parameters.data();
    description.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ComPtr<ID3DBlob> serialized;
    ComPtr<ID3DBlob> diagnostics;
    const HRESULT serializeResult = D3D12SerializeRootSignature(
        &description,
        D3D_ROOT_SIGNATURE_VERSION_1,
        &serialized,
        &diagnostics);
    if (FAILED(serializeResult)) {
        gpu::detail::setHresultError(
            error, "D3D12SerializeRootSignature(DXR)", serializeResult);
        if (diagnostics && diagnostics->GetBufferSize() != 0) {
            error.message.append(
                "\n");
            error.message.append(
                static_cast<const char*>(diagnostics->GetBufferPointer()),
                diagnostics->GetBufferSize());
        }
        return false;
    }

    const HRESULT result = device->CreateRootSignature(
        0,
        serialized->GetBufferPointer(),
        serialized->GetBufferSize(),
        IID_PPV_ARGS(&rootSignature));
    if (FAILED(result)) {
        return gpu::detail::setHresultError(
            error, "ID3D12Device::CreateRootSignature(DXR)", result);
    }
    return true;
}

bool createRaytracingPipeline(
    ID3D12Device5* device,
    ID3D12RootSignature* rootSignature,
    const shader::CompiledShader& library,
    ComPtr<ID3D12StateObject>& stateObject,
    core::Error& error) {
    std::array exports{
        D3D12_EXPORT_DESC{L"RayGen", nullptr, D3D12_EXPORT_FLAG_NONE},
        D3D12_EXPORT_DESC{L"Miss", nullptr, D3D12_EXPORT_FLAG_NONE},
        D3D12_EXPORT_DESC{L"ClosestHit", nullptr, D3D12_EXPORT_FLAG_NONE},
    };
    D3D12_DXIL_LIBRARY_DESC libraryDescription{};
    libraryDescription.DXILLibrary.pShaderBytecode = library.dxil.data();
    libraryDescription.DXILLibrary.BytecodeLength = library.dxil.size();
    libraryDescription.NumExports = static_cast<UINT>(exports.size());
    libraryDescription.pExports = exports.data();

    D3D12_HIT_GROUP_DESC hitGroup{};
    hitGroup.HitGroupExport = L"HitGroup";
    hitGroup.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
    hitGroup.ClosestHitShaderImport = L"ClosestHit";

    D3D12_RAYTRACING_SHADER_CONFIG shaderConfig{};
    shaderConfig.MaxPayloadSizeInBytes = static_cast<UINT>(sizeof(Payload));
    shaderConfig.MaxAttributeSizeInBytes =
        static_cast<UINT>(2U * sizeof(float));
    D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfig{1};
    D3D12_GLOBAL_ROOT_SIGNATURE globalRootSignature{rootSignature};

    std::array<D3D12_STATE_SUBOBJECT, 6> subobjects{};
    subobjects[0] = {
        D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY, &libraryDescription};
    subobjects[1] = {
        D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP, &hitGroup};
    subobjects[2] = {
        D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG, &shaderConfig};
    subobjects[3] = {
        D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG,
        &pipelineConfig};
    subobjects[4] = {
        D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE,
        &globalRootSignature};

    std::array<const wchar_t*, 4> shaderExports{
        L"RayGen", L"Miss", L"ClosestHit", L"HitGroup"};
    D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION shaderConfigAssociation{};
    shaderConfigAssociation.pSubobjectToAssociate = &subobjects[2];
    shaderConfigAssociation.NumExports =
        static_cast<UINT>(shaderExports.size());
    shaderConfigAssociation.pExports = shaderExports.data();
    subobjects[5] = {
        D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION,
        &shaderConfigAssociation};

    D3D12_STATE_OBJECT_DESC description{};
    description.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
    description.NumSubobjects = static_cast<UINT>(subobjects.size());
    description.pSubobjects = subobjects.data();
    const HRESULT result = device->CreateStateObject(
        &description, IID_PPV_ARGS(&stateObject));
    if (FAILED(result)) {
        return gpu::detail::setHresultError(
            error, "ID3D12Device::CreateStateObject(DXR pipeline)", result);
    }
    return true;
}

bool validateOutput(
    ID3D12Resource* readback,
    const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& footprint,
    core::Error& error) {
    void* mapped = nullptr;
    const HRESULT result = readback->Map(0, nullptr, &mapped);
    if (FAILED(result)) {
        return gpu::detail::setHresultError(
            error, "ID3D12Resource::Map(DXR readback)", result);
    }

    const auto* pixels = static_cast<const std::uint8_t*>(mapped) +
                         footprint.Offset;
    const std::array<std::uint8_t, 4> expectedHit{0, 255, 0, 255};
    const std::array<std::uint8_t, 4> expectedMiss{0, 0, 255, 255};
    const bool outputMatches =
        std::memcmp(pixels, expectedHit.data(), pixelSizeBytes) == 0 &&
        std::memcmp(
            pixels + pixelSizeBytes, expectedMiss.data(), pixelSizeBytes) == 0;

    D3D12_RANGE noWriteRange{0, 0};
    readback->Unmap(0, &noWriteRange);
    if (!outputMatches) {
        error.message =
            "DXR readback did not match the synthetic triangle hit/miss "
            "colors.";
        return false;
    }
    return true;
}

} // namespace

bool runDxrSmokeTest(
    ID3D12Device* device,
    graphics::D3D12CommandQueue& commandQueue,
    const shader::DxcShaderCompiler& shaderCompiler,
    const std::filesystem::path& shaderPath,
    bool debugShaders,
    core::Error& error) {
    error = {};
    if (device == nullptr) {
        error.message = "Cannot run the DXR smoke test without a D3D12 device.";
        return false;
    }

    shader::ShaderCompileDescription shaderDescription;
    shaderDescription.sourcePath = shaderPath;
    shaderDescription.stage = shader::ShaderStage::Library;
    shaderDescription.debugInformation = debugShaders;
    shader::CompiledShader compiledLibrary;
    if (!shaderCompiler.compile(shaderDescription, compiledLibrary, error)) {
        return false;
    }
    if (!compiledLibrary.diagnostics.empty()) {
        std::clog << compiledLibrary.diagnostics;
    }

    ComPtr<ID3D12Device5> device5;
    HRESULT result = device->QueryInterface(IID_PPV_ARGS(&device5));
    if (FAILED(result)) {
        return gpu::detail::setHresultError(
            error, "QueryInterface(ID3D12Device5)", result);
    }

    ComPtr<ID3D12RootSignature> rootSignature;
    if (!createRootSignature(device, rootSignature, error)) {
        return false;
    }

    ComPtr<ID3D12StateObject> stateObject;
    if (!createRaytracingPipeline(
            device5.Get(),
            rootSignature.Get(),
            compiledLibrary,
            stateObject,
            error)) {
        return false;
    }
    ComPtr<ID3D12StateObjectProperties> stateObjectProperties;
    result = stateObject->QueryInterface(IID_PPV_ARGS(&stateObjectProperties));
    if (FAILED(result)) {
        return gpu::detail::setHresultError(
            error, "QueryInterface(ID3D12StateObjectProperties)", result);
    }

    const std::array<const wchar_t*, 3> tableExports{
        L"RayGen", L"Miss", L"HitGroup"};
    ComPtr<ID3D12Resource> shaderTable;
    if (!createShaderTable(
            device,
            stateObjectProperties.Get(),
            tableExports,
            shaderTable,
            error)) {
        return false;
    }

    const std::array<Vertex, 3> vertices{{
        {{-1.0F, -1.0F, 0.0F}},
        {{1.0F, -1.0F, 0.0F}},
        {{0.0F, 1.0F, 0.0F}},
    }};
    ComPtr<ID3D12Resource> vertexBuffer;
    if (!createBuffer(
            device,
            D3D12_HEAP_TYPE_UPLOAD,
            sizeof(vertices),
            D3D12_RESOURCE_FLAG_NONE,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            vertexBuffer,
            error,
            "CreateCommittedResource(DXR vertices)") ||
        !writeUploadBuffer(
            vertexBuffer.Get(),
            vertices.data(),
            sizeof(vertices),
            error,
            "ID3D12Resource::Map(DXR vertices)")) {
        return false;
    }

    D3D12_RAYTRACING_GEOMETRY_DESC geometry{};
    geometry.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    geometry.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
    geometry.Triangles.VertexBuffer.StartAddress =
        vertexBuffer->GetGPUVirtualAddress();
    geometry.Triangles.VertexBuffer.StrideInBytes =
        static_cast<UINT>(sizeof(Vertex));
    geometry.Triangles.VertexCount = static_cast<UINT>(vertices.size());
    geometry.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS bottomLevelInputs{};
    bottomLevelInputs.Type =
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    bottomLevelInputs.Flags =
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    bottomLevelInputs.NumDescs = 1;
    bottomLevelInputs.DescsLayout =
        D3D12_ELEMENTS_LAYOUT_ARRAY;
    bottomLevelInputs.pGeometryDescs = &geometry;

    D3D12_RAYTRACING_INSTANCE_DESC instance{};
    instance.Transform[0][0] = 1.0F;
    instance.Transform[1][1] = 1.0F;
    instance.Transform[2][2] = 1.0F;
    instance.InstanceMask = 0xFF;

    ComPtr<ID3D12Resource> instanceBuffer;
    if (!createBuffer(
            device,
            D3D12_HEAP_TYPE_UPLOAD,
            sizeof(instance),
            D3D12_RESOURCE_FLAG_NONE,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            instanceBuffer,
            error,
            "CreateCommittedResource(DXR instance)") ||
        !writeUploadBuffer(
            instanceBuffer.Get(),
            &instance,
            sizeof(instance),
            error,
            "ID3D12Resource::Map(DXR instance)")) {
        return false;
    }

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS topLevelInputs{};
    topLevelInputs.Type =
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    topLevelInputs.Flags =
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    topLevelInputs.NumDescs = 1;
    topLevelInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    topLevelInputs.InstanceDescs = instanceBuffer->GetGPUVirtualAddress();

    D3D12_RESOURCE_DESC outputDescription{};
    outputDescription.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    outputDescription.Width = outputWidth;
    outputDescription.Height = outputHeight;
    outputDescription.DepthOrArraySize = 1;
    outputDescription.MipLevels = 1;
    outputDescription.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    outputDescription.SampleDesc.Count = 1;
    outputDescription.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    outputDescription.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    D3D12_HEAP_PROPERTIES defaultHeap{};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
    defaultHeap.CreationNodeMask = 1;
    defaultHeap.VisibleNodeMask = 1;
    ComPtr<ID3D12Resource> outputTexture;
    result = device->CreateCommittedResource(
        &defaultHeap,
        D3D12_HEAP_FLAG_NONE,
        &outputDescription,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr,
        IID_PPV_ARGS(&outputTexture));
    if (FAILED(result)) {
        return gpu::detail::setHresultError(
            error,
            "CreateCommittedResource(DXR output texture)",
            result);
    }

    D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDescription{};
    descriptorHeapDescription.Type =
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    descriptorHeapDescription.NumDescriptors = 1;
    descriptorHeapDescription.Flags =
        D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ComPtr<ID3D12DescriptorHeap> descriptorHeap;
    result = device->CreateDescriptorHeap(
        &descriptorHeapDescription, IID_PPV_ARGS(&descriptorHeap));
    if (FAILED(result)) {
        return gpu::detail::setHresultError(
            error, "ID3D12Device::CreateDescriptorHeap(DXR)", result);
    }

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDescription{};
    uavDescription.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    uavDescription.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    device->CreateUnorderedAccessView(
        outputTexture.Get(),
        nullptr,
        &uavDescription,
        descriptorHeap->GetCPUDescriptorHandleForHeapStart());

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT numRows = 0;
    UINT64 rowSizeBytes = 0;
    UINT64 totalReadbackBytes = 0;
    device->GetCopyableFootprints(
        &outputDescription,
        0,
        1,
        0,
        &footprint,
        &numRows,
        &rowSizeBytes,
        &totalReadbackBytes);
    if (numRows != outputHeight || rowSizeBytes < outputWidth * pixelSizeBytes ||
        totalReadbackBytes == 0) {
        error.message = "D3D12 returned an invalid DXR output readback footprint.";
        return false;
    }

    ComPtr<ID3D12Resource> readbackBuffer;
    if (!createBuffer(
            device,
            D3D12_HEAP_TYPE_READBACK,
            totalReadbackBytes,
            D3D12_RESOURCE_FLAG_NONE,
            D3D12_RESOURCE_STATE_COPY_DEST,
            readbackBuffer,
            error,
            "CreateCommittedResource(DXR readback)")) {
        return false;
    }

    graphics::D3D12CommandList commandList;
    if (!commandList.initialize(device, error) ||
        !commandList.reset(error)) {
        return false;
    }
    ComPtr<ID3D12GraphicsCommandList4> commandList4;
    result = commandList.nativeHandle()->QueryInterface(
        IID_PPV_ARGS(&commandList4));
    if (FAILED(result)) {
        return gpu::detail::setHresultError(
            error, "QueryInterface(ID3D12GraphicsCommandList4)", result);
    }

    ComPtr<ID3D12Resource> bottomLevel;
    ComPtr<ID3D12Resource> bottomLevelScratch;
    if (!createAccelerationStructure(
            device5.Get(),
            commandList4.Get(),
            bottomLevelInputs,
            "bottom-level acceleration structure",
            bottomLevel,
            bottomLevelScratch,
            error)) {
        return false;
    }

    instance.AccelerationStructure = bottomLevel->GetGPUVirtualAddress();
    if (!writeUploadBuffer(
            instanceBuffer.Get(),
            &instance,
            sizeof(instance),
            error,
            "ID3D12Resource::Map(DXR instance update)")) {
        return false;
    }

    ComPtr<ID3D12Resource> topLevel;
    ComPtr<ID3D12Resource> topLevelScratch;
    if (!createAccelerationStructure(
            device5.Get(),
            commandList4.Get(),
            topLevelInputs,
            "top-level acceleration structure",
            topLevel,
            topLevelScratch,
            error)) {
        return false;
    }

    ID3D12DescriptorHeap* descriptorHeaps[] = {descriptorHeap.Get()};
    commandList4->SetDescriptorHeaps(1, descriptorHeaps);
    commandList4->SetComputeRootSignature(rootSignature.Get());
    commandList4->SetComputeRootDescriptorTable(
        0, descriptorHeap->GetGPUDescriptorHandleForHeapStart());
    commandList4->SetComputeRootShaderResourceView(
        1, topLevel->GetGPUVirtualAddress());
    commandList4->SetPipelineState1(stateObject.Get());

    constexpr UINT shaderRecordSize =
        D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    D3D12_DISPATCH_RAYS_DESC dispatch{};
    const D3D12_GPU_VIRTUAL_ADDRESS shaderTableAddress =
        shaderTable->GetGPUVirtualAddress();
    dispatch.RayGenerationShaderRecord.StartAddress = shaderTableAddress;
    dispatch.RayGenerationShaderRecord.SizeInBytes = shaderRecordSize;
    dispatch.MissShaderTable.StartAddress =
        shaderTableAddress + shaderRecordSize;
    dispatch.MissShaderTable.SizeInBytes = shaderRecordSize;
    dispatch.MissShaderTable.StrideInBytes = shaderRecordSize;
    dispatch.HitGroupTable.StartAddress =
        shaderTableAddress + 2U * shaderRecordSize;
    dispatch.HitGroupTable.SizeInBytes = shaderRecordSize;
    dispatch.HitGroupTable.StrideInBytes = shaderRecordSize;
    dispatch.Width = outputWidth;
    dispatch.Height = outputHeight;
    dispatch.Depth = 1;
    commandList4->DispatchRays(&dispatch);

    D3D12_RESOURCE_BARRIER outputBarrier{};
    outputBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    outputBarrier.Transition.pResource = outputTexture.Get();
    outputBarrier.Transition.Subresource =
        D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    outputBarrier.Transition.StateBefore =
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    outputBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    commandList4->ResourceBarrier(1, &outputBarrier);

    D3D12_TEXTURE_COPY_LOCATION destination{};
    destination.pResource = readbackBuffer.Get();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    destination.PlacedFootprint = footprint;
    D3D12_TEXTURE_COPY_LOCATION source{};
    source.pResource = outputTexture.Get();
    source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    source.SubresourceIndex = 0;
    commandList4->CopyTextureRegion(
        &destination, 0, 0, 0, &source, nullptr);

    if (!commandList.close(error) ||
        !commandQueue.executeAndWait(commandList, error)) {
        return false;
    }

    if (!validateOutput(readbackBuffer.Get(), footprint, error)) {
        return false;
    }
    return true;
}

} // namespace raytime::raytracing
