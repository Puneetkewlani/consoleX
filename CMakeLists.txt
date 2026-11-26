cmake_minimum_required(VERSION 3.24)

project(RayTime VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "RayTime currently requires Windows 10/11 and the Windows SDK.")
endif()

add_executable(RayTime
    src/main.cpp
    src/graphics/d3d12_command_list.cpp
    src/graphics/d3d12_command_queue.cpp
    src/graphics/d3d12_resources.cpp
    src/graphics/graphics_validation.cpp
    src/gpu/d3d12_context.cpp
    src/shader/dxc_shader_compiler.cpp
)

target_compile_features(RayTime PRIVATE cxx_std_20)
target_include_directories(RayTime PRIVATE include)
target_compile_definitions(RayTime PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    WINVER=0x0A00
    _WIN32_WINNT=0x0A00
)
target_link_libraries(RayTime PRIVATE d3d12 dxgi bcrypt)

if(MSVC)
    target_compile_options(RayTime PRIVATE /W4 /permissive- /EHsc)
endif()

include(CTest)
if(BUILD_TESTING)
    add_test(
        NAME d3d12_warp_graphics_resources
        COMMAND RayTime --validate --warp
    )
    set_tests_properties(
        d3d12_warp_graphics_resources
        PROPERTIES LABELS "integration;d3d12"
    )
    add_test(
        NAME shader_dxc_cache_validation
        COMMAND RayTime --validate-shader
            "${CMAKE_CURRENT_SOURCE_DIR}/shaders/phase3_smoke.hlsl"
            --stage compute
    )
    add_test(
        NAME shader_dxc_vertex_validation
        COMMAND RayTime --validate-shader
            "${CMAKE_CURRENT_SOURCE_DIR}/shaders/phase3_smoke.hlsl"
            --stage vertex
            --entry vertexMain
    )
    add_test(
        NAME shader_dxc_pixel_validation
        COMMAND RayTime --validate-shader
            "${CMAKE_CURRENT_SOURCE_DIR}/shaders/phase3_smoke.hlsl"
            --stage pixel
            --entry pixelMain
    )
    set_tests_properties(
        shader_dxc_cache_validation
        shader_dxc_vertex_validation
        shader_dxc_pixel_validation
        PROPERTIES LABELS "shader;integration" SKIP_RETURN_CODE 77
    )
endif()
