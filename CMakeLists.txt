cmake_minimum_required(VERSION 3.20)

project(Caffeine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_definitions(CF_DEBUG)
endif()

add_library(Caffeine INTERFACE)
target_include_directories(Caffeine INTERFACE 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:include>
)

add_library(Caffeine::Core ALIAS Caffeine)

add_subdirectory(tests)