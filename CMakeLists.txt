cmake_minimum_required(VERSION 3.24)

project(RayTime VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "RayTime Phase 1 currently requires Windows 10/11 and the Windows SDK.")
endif()

add_executable(RayTime
    src/main.cpp
    src/gpu/d3d12_context.cpp
)

target_compile_features(RayTime PRIVATE cxx_std_20)
target_include_directories(RayTime PRIVATE include)
target_compile_definitions(RayTime PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(RayTime PRIVATE d3d12 dxgi)

if(MSVC)
    target_compile_options(RayTime PRIVATE /W4 /permissive- /EHsc)
endif()

include(CTest)
if(BUILD_TESTING)
    add_test(
        NAME d3d12_warp_initialization
        COMMAND RayTime --validate --warp
    )
    set_tests_properties(d3d12_warp_initialization PROPERTIES LABELS "integration;d3d12")
endif()
