cmake_minimum_required(VERSION 3.20)

project(Caffeine VERSION 0.1.0 LANGUAGES CXX C)

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
    src/core/io/BlobLoader.cpp
    src/core/io/CafWriter.cpp
    src/input/InputManager.cpp
    src/debug/LogSystem.cpp
    src/debug/Profiler.cpp
    src/threading/JobSystem.cpp
    src/ecs/CommandBuffer.cpp
    src/ecs/World.cpp
    src/events/EventBus.cpp
    src/assets/AssetManager.cpp
    src/editor/TransformGizmo.cpp
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

    include(FetchContent)
    FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG        v1.91.9
        GIT_SHALLOW    TRUE
    )
    set(FETCHCONTENT_QUIET OFF)
    FetchContent_MakeAvailable(imgui)

    add_library(ImGui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlgpu3.cpp
    )
    target_include_directories(ImGui PUBLIC
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
    )
    target_link_libraries(ImGui PUBLIC SDL3::SDL3)

    target_link_libraries(Caffeine PUBLIC ImGui)
    target_compile_definitions(Caffeine PUBLIC CF_HAS_IMGUI=1)
    message(STATUS "ImGui fetched and enabled")
else()
    message(STATUS "SDL3 not found – RHI module disabled")
endif()

target_compile_features(Caffeine PUBLIC cxx_std_20)

add_library(Caffeine::Core ALIAS Caffeine)

# ── caf-encode CLI tool ────────────────────────────────────────
add_executable(caf-encode tools/caf-encode/main.cpp)
target_link_libraries(caf-encode PRIVATE Caffeine)
target_include_directories(caf-encode PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_features(caf-encode PRIVATE cxx_std_20)

add_subdirectory(tests)

# ── Scripting (Lua + sol2) ────────────────────────────────────
option(CAFFEINE_ENABLE_SCRIPTING "Enable Lua scripting support" OFF)

if(CAFFEINE_ENABLE_SCRIPTING)
    include(FetchContent)

    # Build Lua 5.4 from source (sol2 v3.3.0 needs exact 5.4 API)
    FetchContent_Declare(lua54
        GIT_REPOSITORY https://github.com/lua/lua.git
        GIT_TAG v5.4.7
        GIT_SHALLOW TRUE
    )
    FetchContent_GetProperties(lua54)
    if(NOT lua54_POPULATED)
        FetchContent_Populate(lua54)
        set(LUA54_SRC ${lua54_SOURCE_DIR})
        add_library(lua54 STATIC
            ${LUA54_SRC}/lapi.c
            ${LUA54_SRC}/lauxlib.c
            ${LUA54_SRC}/lbaselib.c
            ${LUA54_SRC}/lcode.c
            ${LUA54_SRC}/lcorolib.c
            ${LUA54_SRC}/lctype.c
            ${LUA54_SRC}/ldblib.c
            ${LUA54_SRC}/ldebug.c
            ${LUA54_SRC}/ldo.c
            ${LUA54_SRC}/ldump.c
            ${LUA54_SRC}/lfunc.c
            ${LUA54_SRC}/lgc.c
            ${LUA54_SRC}/linit.c
            ${LUA54_SRC}/liolib.c
            ${LUA54_SRC}/llex.c
            ${LUA54_SRC}/lmathlib.c
            ${LUA54_SRC}/lmem.c
            ${LUA54_SRC}/loadlib.c
            ${LUA54_SRC}/lobject.c
            ${LUA54_SRC}/lopcodes.c
            ${LUA54_SRC}/loslib.c
            ${LUA54_SRC}/lparser.c
            ${LUA54_SRC}/lstate.c
            ${LUA54_SRC}/lstring.c
            ${LUA54_SRC}/lstrlib.c
            ${LUA54_SRC}/ltable.c
            ${LUA54_SRC}/ltablib.c
            ${LUA54_SRC}/ltm.c
            ${LUA54_SRC}/lundump.c
            ${LUA54_SRC}/lutf8lib.c
            ${LUA54_SRC}/lvm.c
            ${LUA54_SRC}/lzio.c
        )
        target_include_directories(lua54 BEFORE PUBLIC ${LUA54_SRC})
        # Create lua.hpp C++ wrapper so sol2's #include <lua.hpp> finds our 5.4 headers
        file(WRITE ${LUA54_SRC}/lua.hpp
            "extern \"C\" {\n"
            "#include \"lua.h\"\n"
            "#include \"lauxlib.h\"\n"
            "#include \"lualib.h\"\n"
            "}\n")
    endif()

    # sol2 (header-only C++/Lua binding library)
    set(SOL2_BUILD_LUA OFF)
    FetchContent_Declare(sol2
        GIT_REPOSITORY https://github.com/ThePhD/sol2.git
        GIT_TAG v3.3.0
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(sol2)

    # Patch sol2 optional_implementation.hpp for GCC 16+ compatibility
    execute_process(COMMAND python3 "${CMAKE_CURRENT_SOURCE_DIR}/cmake/patch_sol2_optional.py"
        "${sol2_SOURCE_DIR}/include/sol/optional_implementation.hpp"
        RESULT_VARIABLE PATCH_RESULT)
    if(NOT PATCH_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to patch sol2 for GCC 16 compatibility")
    endif()

    target_link_libraries(Caffeine PRIVATE sol2::sol2 lua54)

    target_compile_definitions(Caffeine PUBLIC CF_HAS_SCRIPTING=1)

    target_sources(Caffeine PRIVATE
        src/script/ScriptEngine.cpp
        src/script/ScriptSystem.cpp
        src/script/ScriptWatcher.cpp
    )
endif()