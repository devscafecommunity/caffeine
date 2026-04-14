cmake_minimum_required(VERSION 3.20)

project(Caffeine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_definitions(CF_DEBUG)
endif()

# ── SDL3 (optional – RHI is only built when SDL3 is available) ──
find_package(SDL3 CONFIG QUIET)

add_library(Caffeine
    src/core/Timer.cpp
    src/core/GameLoop.cpp
    src/input/InputManager.cpp
    src/debug/LogSystem.cpp
    src/debug/Profiler.cpp
)

if(SDL3_FOUND)
    target_sources(Caffeine PRIVATE
        src/rhi/RenderDevice.cpp
        src/rhi/CommandBuffer.cpp
    )
endif()

target_include_directories(Caffeine PUBLIC 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:include>
)

if(SDL3_FOUND)
    target_link_libraries(Caffeine PUBLIC SDL3::SDL3)
    target_compile_definitions(Caffeine PUBLIC CF_HAS_SDL3=1)
    message(STATUS "SDL3 found – RHI module enabled")
else()
    message(STATUS "SDL3 not found – RHI module disabled")
endif()

target_compile_features(Caffeine PUBLIC cxx_std_20)

add_library(Caffeine::Core ALIAS Caffeine)

add_subdirectory(tests)