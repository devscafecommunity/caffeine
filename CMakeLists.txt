cmake_minimum_required(VERSION 3.20)

project(Caffeine VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_definitions(CF_DEBUG)
endif()

# ── Headless mode (build without SDL3) ─────────────────────────
option(CAFFEINE_BUILD_HEADLESS "Build caffeine-core without SDL3/RHI" OFF)

# ── Scripting (Lua + sol2) ──────────────────────────────────────
option(CAFFEINE_ENABLE_SCRIPTING "Enable Lua scripting support" OFF)

# ── SDL3 (optional — only needed when NOT headless) ──────────────
if(NOT CAFFEINE_BUILD_HEADLESS)
    find_package(SDL3 CONFIG QUIET)
endif()

# ═══════════════════════════════════════════════════════════════════
#  CAFFEINE ENGINE CORE
#
#  Pure engine library. ZERO dependency on ImGui or editor code.
#  Can be built headless (without SDL3) for server/CI workloads.
# ═══════════════════════════════════════════════════════════════════
add_library(caffeine-core
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
    src/core/DebugHookRegistry.cpp
    src/editor/EditorContext.cpp
)

target_include_directories(caffeine-core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(caffeine-core PUBLIC cxx_std_20)

if(CAFFEINE_BUILD_HEADLESS)
    target_compile_definitions(caffeine-core PUBLIC CF_HEADLESS=1)
    message(STATUS "Building caffeine-core in HEADLESS mode (no SDL3, no RHI)")
else()
    if(SDL3_FOUND)
        target_sources(caffeine-core PRIVATE
            src/rhi/RenderDevice.cpp
            src/rhi/CommandBuffer.cpp
        )
        target_link_libraries(caffeine-core PUBLIC SDL3::SDL3)
        target_compile_definitions(caffeine-core PUBLIC CF_HAS_SDL3=1)
        message(STATUS "SDL3 found – RHI module enabled")
    else()
        message(STATUS "SDL3 not found – RHI module disabled")
    endif()
endif()

# ── Backward-compat alias ───────────────────────────────────────
add_library(Caffeine::Core ALIAS caffeine-core)

# ── Scripting ────────────────────────────────────────────────────
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

    target_link_libraries(caffeine-core PRIVATE sol2::sol2 lua54)
    target_compile_definitions(caffeine-core PUBLIC CF_HAS_SCRIPTING=1)

    target_sources(caffeine-core PRIVATE
        src/script/ScriptEngine.cpp
        src/script/ScriptSystem.cpp
        src/script/ScriptWatcher.cpp
    )
endif()

# ═══════════════════════════════════════════════════════════════════
#  DOPPIO — Caffeine Studio IDE
#
#  Executable da IDE. Linka caffeine-core + ImGui.
#  NÃO funciona sem o core.
# ═══════════════════════════════════════════════════════════════════
if(SDL3_FOUND AND NOT CAFFEINE_BUILD_HEADLESS)
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

    add_executable(doppio
        apps/doppio/main.cpp
        src/editor/HierarchyPanel.cpp
    )
    target_link_libraries(doppio PRIVATE caffeine-core ImGui)
    target_include_directories(doppio PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_definitions(doppio PRIVATE CF_HAS_IMGUI=1)
    target_compile_features(doppio PRIVATE cxx_std_20)

    message(STATUS "Doppio (IDE) enabled – ImGui v1.91.9 fetched")
endif()

# ── caf-encode CLI tool ────────────────────────────────────────
add_executable(caf-encode tools/caf-encode/main.cpp)
target_link_libraries(caf-encode PRIVATE caffeine-core)
target_include_directories(caf-encode PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_features(caf-encode PRIVATE cxx_std_20)

# ── Tests ────────────────────────────────────────────────────────
add_subdirectory(tests)
