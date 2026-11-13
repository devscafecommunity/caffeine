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
option(CAFFEINE_ENABLE_SCRIPTING "Enable Lua scripting support" ON)
option(CAFFEINE_ENABLE_BULLET3 "Enable Bullet3 rigid-body physics" ON)

# ── glTF Support (tinygltf for mesh encoding) ──────────────────────
include(FetchContent)
FetchContent_Declare(
    tinygltf
    GIT_REPOSITORY https://github.com/syoyo/tinygltf.git
    GIT_TAG        v2.8.10
    GIT_SHALLOW    TRUE
)
FetchContent_GetProperties(tinygltf)
if(NOT tinygltf_POPULATED)
    FetchContent_Populate(tinygltf)
endif()
set(TINYGLTF_INCLUDE_DIR "${tinygltf_SOURCE_DIR}")

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
    src/caffeine/core/Timer.cpp
    src/caffeine/core/TimerScheduler.cpp
    src/caffeine/core/GameLoop.cpp
    src/caffeine/core/io/BlobLoader.cpp
    src/caffeine/core/io/CafWriter.cpp
    src/caffeine/core/io/FileWatcher.cpp
    src/caffeine/input/InputManager.cpp
    src/caffeine/debug/LogSystem.cpp
    src/caffeine/debug/CrashHandler.cpp
    src/caffeine/debug/Profiler.cpp
    src/caffeine/debug/SystemMetrics.cpp
    src/caffeine/threading/JobSystem.cpp
    src/caffeine/ecs/CommandBuffer.cpp
    src/caffeine/ecs/World.cpp
    src/caffeine/events/EventBus.cpp
    src/caffeine/assets/AssetManager.cpp
    src/caffeine/assets/AssetPipeline.cpp
    src/caffeine/assets/TextureCompiler.cpp
    src/caffeine/assets/MeshLoader.cpp
    src/caffeine/animation/SkinLibrary.cpp
    src/caffeine/effects/EffectSystem.cpp
    src/caffeine/ecs/ParticleSystem.cpp
    src/caffeine/ecs/ParticleSystem.cpp
    src/caffeine/ecs/MeshGeometry.cpp
    src/caffeine/assets/MeshCache.cpp
    src/caffeine/assets/MeshImportValidator.cpp
    src/caffeine/assets/MeshLOD.cpp
    src/caffeine/assets/PrefabSerializer.cpp
    src/caffeine/assets/HotReloader.cpp
    src/caffeine/scene/PlayMode2D.cpp
    src/caffeine/scene/PickQuery.cpp
    src/caffeine/scene/PickRaycast.cpp
    src/caffeine/scene/MapSurfaceSystem.cpp
    src/caffeine/scene/SpriteLod2DSystem.cpp
    src/caffeine/render/Overlay2D.cpp
    src/caffeine/runtime/PlayClock.cpp
    src/caffeine/runtime/GameStateStack.cpp
    src/caffeine/scene/EnvironmentSystem.cpp
    src/caffeine/scene/CpuDirectionalShadowMap.cpp
    src/caffeine/scene/LightingSystem.cpp
    src/caffeine/scene/Camera3DSystem.cpp
    src/caffeine/scene/TerrainSystem.cpp
    src/caffeine/scene/NaturalFluidSystem.cpp
    src/caffeine/fluid/WaveField.cpp
    src/caffeine/fluid/ShallowWater.cpp
    src/caffeine/fluid/EulerFluid.cpp
    src/caffeine/fluid/FluidVolume.cpp
    src/caffeine/fluid/FluidWorld.cpp
    src/caffeine/terrain/TerrainHeightmap.cpp
    src/caffeine/terrain/TerrainMeshBuilder.cpp
    src/caffeine/terrain/TerrainCache.cpp
    src/caffeine/terrain/TerrainSculptor.cpp
    src/caffeine/terrain/TerrainLodSystem.cpp
    src/caffeine/terrain/TerrainSplatmap.cpp
    src/caffeine/terrain/TerrainSplatPainter.cpp
    src/caffeine/terrain/TerrainSerializer.cpp
    src/caffeine/terrain/TerrainGpuTextures.cpp
    src/caffeine/terrain/TerrainHeightmapImporter.cpp
    src/caffeine/terrain/TerrainCollisionMeshBuilder.cpp
    src/caffeine/procedural/ProceduralNoise.cpp
    src/caffeine/procedural/ProceduralProfiles.cpp
    src/caffeine/procedural/ProceduralGenerator.cpp
    src/caffeine/procedural/ProceduralStreamer.cpp
    src/caffeine/procedural/ProceduralWorldSystem.cpp
    src/caffeine/procedural/ProceduralBindings.cpp
    src/caffeine/script/PostProcessBindings.cpp
    src/caffeine/script/ForwardRenderBindings.cpp
    src/caffeine/render/ForwardRenderFeatures.cpp
    src/caffeine/engine/AssetLoader.cpp
    src/caffeine/core/DebugHookRegistry.cpp
    src/caffeine/capi/EngineAPI.cpp
    src/caffeine/project/ProjectManager.cpp
    src/caffeine/scene/EditorContext.cpp
    src/caffeine/scene/EditorSceneSerializer.cpp
)

target_include_directories(caffeine-core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/caffeine>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendor>
    $<BUILD_INTERFACE:${TINYGLTF_INCLUDE_DIR}>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(caffeine-core PUBLIC cxx_std_20)
# Required when editor plugins (.so) link this static library.
set_target_properties(caffeine-core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_definitions(caffeine-core PUBLIC
    CAFFEINE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_sources(caffeine-core PRIVATE
    src/caffeine/assets/MaterialCache.cpp
    src/caffeine/assets/MaterialFile.cpp
    src/caffeine/assets/MeshMaterialExport.cpp)

if(CAFFEINE_ENABLE_BULLET3)
    set(BUILD_BULLET2_DEMOS OFF CACHE BOOL "" FORCE)
    set(BUILD_CPU_DEMOS OFF CACHE BOOL "" FORCE)
    set(BUILD_OPENGL3_DEMOS OFF CACHE BOOL "" FORCE)
    set(BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
    set(BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
    set(BUILD_ENET OFF CACHE BOOL "" FORCE)
    set(BUILD_CLSOCKET OFF CACHE BOOL "" FORCE)
    set(USE_GRAPHICAL_BENCHMARK OFF CACHE BOOL "" FORCE)
    set(USE_GLUT OFF CACHE BOOL "" FORCE)
    set(INSTALL_LIBS OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        bullet3
        GIT_REPOSITORY https://github.com/bulletphysics/bullet3.git
        GIT_TAG        3.25
        GIT_SHALLOW    TRUE
    )
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    FetchContent_MakeAvailable(bullet3)
    target_sources(caffeine-core PRIVATE src/caffeine/physics/PhysicsWorld3D.cpp)
    target_include_directories(caffeine-core PUBLIC ${bullet3_SOURCE_DIR}/src)
    target_link_libraries(caffeine-core PUBLIC BulletDynamics BulletCollision LinearMath)
    target_compile_definitions(caffeine-core PUBLIC CF_HAS_BULLET3)
else()
    target_sources(caffeine-core PRIVATE src/caffeine/physics/PhysicsWorld3D.cpp)
endif()

if(CAFFEINE_BUILD_HEADLESS)
    target_compile_definitions(caffeine-core PUBLIC CF_HEADLESS=1)
    message(STATUS "Building caffeine-core in HEADLESS mode (no SDL3, no RHI)")
else()
    if(SDL3_FOUND)
        find_program(GLSLC_EXECUTABLE glslc)
        find_program(DXC_EXECUTABLE dxc)
        find_program(SPIRV_CROSS_EXECUTABLE spirv-cross)
        set(CAFFEINE_GENERATED_SHADER_DIR "${CMAKE_BINARY_DIR}/generated/shaders")
        if(GLSLC_EXECUTABLE)
            include("${CMAKE_SOURCE_DIR}/cmake/CompileShaders.cmake")
            set(_shader_formats SPIRV)
            if(DXC_EXECUTABLE AND SPIRV_CROSS_EXECUTABLE)
                list(APPEND _shader_formats DXBC)
            endif()
            if(SPIRV_CROSS_EXECUTABLE)
                list(APPEND _shader_formats MSL)
            endif()

            # name|stage|file — every entry ends up in BuiltinShaders_spirv.hpp.
            set(_caffeine_builtin_shaders
                "sphere_lit_vert|vertex|sphere_lit.vert"
                "sphere_lit_frag|fragment|sphere_lit.frag"
                "mesh_lit_vert|vertex|mesh_lit.vert"
                "scene_lit_vert|vertex|scene_lit.vert"
                "scene_instanced_vert|vertex|scene_instanced.vert"
                "scene_lit_frag|fragment|scene_lit.frag"
                "terrain_lit_frag|fragment|terrain_lit.frag"
                "shadow_depth_vert|vertex|shadow_depth.vert"
                "shadow_depth_frag|fragment|shadow_depth.frag"
                "fullscreen_vert|vertex|fullscreen.vert"
                "sky_frag|fragment|sky.frag"
                "grid_frag|fragment|grid.frag"
                "effect_billboard_vert|vertex|effect_billboard.vert"
                "effect_billboard_frag|fragment|effect_billboard.frag"
                "post_luminance_frag|fragment|post_luminance.frag"
                "post_adapt_frag|fragment|post_adapt.frag"
                "post_bloom_prefilter_frag|fragment|post_bloom_prefilter.frag"
                "post_bloom_down_frag|fragment|post_bloom_down.frag"
                "post_bloom_up_frag|fragment|post_bloom_up.frag"
                "post_ssao_frag|fragment|post_ssao.frag"
                "post_ssao_blur_frag|fragment|post_ssao_blur.frag"
                "post_dof_frag|fragment|post_dof.frag"
                "post_motion_blur_frag|fragment|post_motion_blur.frag"
                "post_composite_frag|fragment|post_composite.frag"
                "post_fxaa_frag|fragment|post_fxaa.frag"
                "post_taa_frag|fragment|post_taa.frag"
                "post_photon_frag|fragment|post_photon.frag"
                "post_photon_resolve_frag|fragment|post_photon_resolve.frag"
                "minz_downsample_frag|fragment|minz_downsample.frag"
                "photon_cascade|compute|photon_cascade.comp"
                "photon_c0|compute|photon_c0.comp"
                "photon_voxelize|compute|photon_voxelize.comp"
                "photon_inject|compute|photon_inject.comp"
                "photon_probe|compute|photon_probe.comp"
                "photon_bake_ao|compute|photon_bake_ao.comp"
            )
            set(_caffeine_shader_headers)
            set(_caffeine_shader_cat_commands)
            foreach(_entry_raw IN LISTS _caffeine_builtin_shaders)
                string(REPLACE "|" ";" _entry "${_entry_raw}")
                list(GET _entry 0 _name)
                list(GET _entry 1 _stage)
                list(GET _entry 2 _file)
                caffeine_compile_shader(NAME ${_name} STAGE ${_stage} SOURCE "${CMAKE_SOURCE_DIR}/src/caffeine/render/shaders/${_file}" OUTPUT_DIR "${CAFFEINE_GENERATED_SHADER_DIR}" FORMATS ${_shader_formats})
                list(APPEND _caffeine_shader_headers "${CAFFEINE_GENERATED_SHADER_DIR}/${_name}_spirv.hpp")
                list(APPEND _caffeine_shader_cat_commands COMMAND ${CMAKE_COMMAND} -E cat "${CAFFEINE_GENERATED_SHADER_DIR}/${_name}_spirv.hpp" >> "${CAFFEINE_GENERATED_SHADER_DIR}/BuiltinShaders_spirv.hpp")
            endforeach()

            set(BUILTIN_SHADERS_SPIRV_HPP "${CAFFEINE_GENERATED_SHADER_DIR}/BuiltinShaders_spirv.hpp")
            add_custom_command(
                OUTPUT "${BUILTIN_SHADERS_SPIRV_HPP}"
                COMMAND ${CMAKE_COMMAND} -E echo "// Auto-generated SPIR-V shader bundle" > "${BUILTIN_SHADERS_SPIRV_HPP}"
                ${_caffeine_shader_cat_commands}
                DEPENDS ${_caffeine_shader_headers}
            )
            add_custom_target(caffeine_generated_shaders DEPENDS "${BUILTIN_SHADERS_SPIRV_HPP}")
        else()
            message(WARNING "glslc not found — GPU builtin shaders disabled (CPU preview fallback)")
        endif()

        target_sources(caffeine-core PRIVATE
            src/caffeine/rhi/RenderDevice.cpp
            src/caffeine/rhi/CommandBuffer.cpp
            src/caffeine/render/ShaderBytecode.cpp
            src/caffeine/render/ShaderCompiler.cpp
            src/caffeine/render/GpuTextureCache.cpp
            src/caffeine/render/GpuSceneRenderer.cpp
            src/caffeine/render/PostProcessStack.cpp
            src/caffeine/render/MinZPyramid.cpp
            src/caffeine/render/PhotonCascades.cpp
            src/caffeine/render/PhotonGpuBake.cpp
            src/caffeine/render/PhotonAOBake.cpp
            src/caffeine/render/GpuEnvironmentMap.cpp
            src/caffeine/render/MaterialPreviewRenderer.cpp
            src/caffeine/render/GpuPointShadowMap.cpp
            src/caffeine/render/GpuDirectionalShadowMap.cpp
            src/caffeine/render/GpuSpotShadowMap.cpp
            src/caffeine/render/GpuProceduralMeshes.cpp
        )
        target_link_libraries(caffeine-core PUBLIC SDL3::SDL3 ${CMAKE_DL_LIBS})
        find_library(SHADERC_LIBRARY shaderc_shared)
        find_path(SHADERC_INCLUDE_DIR shaderc/shaderc.hpp)
        if(SHADERC_LIBRARY AND SHADERC_INCLUDE_DIR)
            target_include_directories(caffeine-core PRIVATE ${SHADERC_INCLUDE_DIR})
            target_link_libraries(caffeine-core PRIVATE ${SHADERC_LIBRARY})
            target_compile_definitions(caffeine-core PRIVATE CF_HAS_SHADERC=1)
        endif()
        target_compile_definitions(caffeine-core PUBLIC CF_HAS_SDL3=1)
        if(GLSLC_EXECUTABLE)
            add_dependencies(caffeine-core caffeine_generated_shaders)
            target_include_directories(caffeine-core PRIVATE "${CAFFEINE_GENERATED_SHADER_DIR}")
            target_compile_definitions(caffeine-core PRIVATE CF_HAS_GENERATED_SHADERS=1)
        endif()
        message(STATUS "SDL3 found – RHI module enabled")
    else()
        message(STATUS "SDL3 not found – RHI module disabled")
    endif()
endif()

# ── Backward-compat alias ───────────────────────────────────────
add_library(Caffeine::Core ALIAS caffeine-core)

# ═══════════════════════════════════════════════════════════════════
#  CAFFEINE UI — header-only UI module
#
#  INTERFACE library: widget system, layout, hit-testing.
#  Depends on caffeine-core.
# ═══════════════════════════════════════════════════════════════════
add_library(caffeine-ui INTERFACE)
target_include_directories(caffeine-ui INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(caffeine-ui INTERFACE caffeine-core)
add_library(Caffeine::UI ALIAS caffeine-ui)

# ═══════════════════════════════════════════════════════════════════
#  CAF-PACK LIBRARY
#
#  Shared asset packer library used by the IDE for generating game.cap.
# ═══════════════════════════════════════════════════════════════════
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/caf-pack/CMakeLists.txt")
    add_subdirectory(caf-pack)
    set(CAF_PACK_AVAILABLE TRUE)
else()
    set(CAF_PACK_AVAILABLE FALSE)
    message(WARNING "caf-pack submodule not found. IDE (doppio) will be built without asset packing support.")
endif()

# ═══════════════════════════════════════════════════════════════════
#  CAFFEINE COMBINED — demonstra core + UI no mesmo executável
#
#  Executable that links both caffeine-core and caffeine-ui.
# ═══════════════════════════════════════════════════════════════════
add_executable(caffeine-combined
    apps/combined/main.cpp
)
target_link_libraries(caffeine-combined PRIVATE caffeine-core caffeine-ui)
target_compile_features(caffeine-combined PRIVATE cxx_std_20)

# ── Dryad interpreter (sibling repo, optional) ───────────────────
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/source/dryad-cpp/CMakeLists.txt")
    set(DRYAD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(DRYAD_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
    set(DRYAD_ENABLE_TLS OFF CACHE BOOL "" FORCE)
    set(DRYAD_ENABLE_SANITIZERS OFF CACHE BOOL "" FORCE)
    add_subdirectory(source/dryad-cpp)
    set(DRYAD_AVAILABLE TRUE)
else()
    set(DRYAD_AVAILABLE FALSE)
endif()

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
        src/caffeine/script/ScriptEngine.cpp
        src/caffeine/script/ScriptSystem.cpp
        src/caffeine/script/ScriptWatcher.cpp
        src/caffeine/script/CppScriptRegistry.cpp
        src/caffeine/script/AnimationBindings.cpp
        src/caffeine/script/EffectBindings.cpp
        src/caffeine/script/NavigationBindings.cpp
        src/caffeine/script/RuntimeScriptBindings.cpp
        src/caffeine/script/AssetTextLoader.cpp
        src/caffeine/script/NativeModuleHost.cpp
    )
    if(DRYAD_AVAILABLE)
        target_sources(caffeine-core PRIVATE
            src/caffeine/script/DryadHost.cpp
            src/caffeine/script/nodes/NodeGraph.cpp
            src/caffeine/script/nodes/NodeCatalog.cpp
            src/caffeine/script/nodes/NodeEmitter.cpp
            src/caffeine/script/nodes/NodeGraphIO.cpp
            src/caffeine/script/nodes/NodeRoundTrip.cpp
        )
        target_link_libraries(caffeine-core PUBLIC dryad_compiler)
        target_compile_definitions(caffeine-core PUBLIC
            CF_HAS_DRYAD=1
            DRYAD_STDLIB_DIR="${CMAKE_CURRENT_SOURCE_DIR}/source/dryad-cpp/stdlib"
        )
    endif()
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
        GIT_TAG        docking
        GIT_SHALLOW    TRUE
    )
    FetchContent_Declare(
        imnodes
        GIT_REPOSITORY https://github.com/Nelarius/imnodes.git
        GIT_TAG        v0.5
        GIT_SHALLOW    TRUE
    )
    set(FETCHCONTENT_QUIET OFF)
    FetchContent_MakeAvailable(imgui imnodes)

    # Patch imnodes for ImGui docking compatibility (TextureId -> TexRef)
    file(WRITE "${CMAKE_BINARY_DIR}/patch_imnodes.py"
"import sys
with open(sys.argv[1], 'r') as f:
    content = f.read()

# Fix: _TextureIdStack -> _TextureStack.back()._TexID
content = content.replace(
    'draw_list->_TextureIdStack.back()',
    'draw_list->_TextureStack.back()._TexID'
)

# Fix: TextureId -> TexRef with proper initialization
content = content.replace(
    'draw_cmd.TextureId',
    'draw_cmd.TexRef._TexID'
)

with open(sys.argv[1], 'w') as f:
    f.write(content)
")
    execute_process(COMMAND python3 "${CMAKE_BINARY_DIR}/patch_imnodes.py" "${imnodes_SOURCE_DIR}/imnodes.cpp")

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
     target_compile_definitions(ImGui PUBLIC IMGUI_ENABLE_TEST_ENGINE=1 IMGUI_DEFINE_MATH_OPERATORS)

    add_library(ImNodes STATIC
        ${imnodes_SOURCE_DIR}/imnodes.cpp
    )
    target_include_directories(ImNodes PUBLIC
        ${imnodes_SOURCE_DIR}
    )
     target_compile_definitions(ImNodes PRIVATE 
         IM_OFFSETOF=offsetof
         IMGUI_DEFINE_MATH_OPERATORS
     )
     if(NOT MSVC)
         target_compile_options(ImNodes PRIVATE -Wno-macro-redefined -Wno-cpp)
     endif()
     target_link_libraries(ImNodes PUBLIC ImGui)

    # ImGui is an editor dependency, not an engine dependency. Editor targets
    # (doppio, editor tests, plugins) opt in by linking ImGui/doppio-ui-kit or
    # defining CF_HAS_IMGUI themselves. caffeine-core stays UI-free.

    # ── Caffeine UI kit — public, reusable by Doppio and plugins ───────────────
    # Header-only widgets + a small host library for theme, panel registry and
    # workspace layout. It deliberately has no dependency on editor internals.
    add_library(doppio-ui-kit STATIC
        src/doppio/ui_kit/Theme.cpp
        src/doppio/ui_kit/Icons.cpp
        src/doppio/ui_kit/PanelRegistry.cpp
        src/doppio/ui_kit/Workspace.cpp
        src/doppio/ui_kit/NodeCanvas.cpp
    )
    target_include_directories(doppio-ui-kit PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/doppio
    )
    target_link_libraries(doppio-ui-kit PUBLIC ImGui ImNodes)
    target_compile_features(doppio-ui-kit PUBLIC cxx_std_20)
    target_compile_definitions(doppio-ui-kit PUBLIC CF_HAS_IMGUI=1)
    if(NOT MSVC)
        target_compile_options(doppio-ui-kit PRIVATE -Wno-macro-redefined -Wno-cpp)
    endif()
    add_library(Doppio::UIKit ALIAS doppio-ui-kit)

    # ── Engine ImGui adapters (editor-only) ────────────────────────────────────
    # Engine render helpers that expose ImGui draw-list entry points live here,
    # not in caffeine-core. Any ImGui-based frontend can link this target.
    add_library(caffeine-editor-support STATIC
        src/caffeine/render/Overlay2DImGui.cpp
        src/caffeine/render/SkyboxRenderer.cpp
    )
    target_include_directories(caffeine-editor-support PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/caffeine>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendor>
        $<BUILD_INTERFACE:${TINYGLTF_INCLUDE_DIR}>
        $<INSTALL_INTERFACE:include>
    )
    target_link_libraries(caffeine-editor-support PUBLIC caffeine-core ImGui ImNodes)
    target_compile_features(caffeine-editor-support PUBLIC cxx_std_20)
    target_compile_definitions(caffeine-editor-support PUBLIC CF_HAS_IMGUI=1)
    target_compile_definitions(caffeine-editor-support PUBLIC
        CAFFEINE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    set_target_properties(caffeine-editor-support PROPERTIES POSITION_INDEPENDENT_CODE ON)
    add_library(Caffeine::EditorSupport ALIAS caffeine-editor-support)

    if(TARGET caffeine-combined)
        target_link_libraries(caffeine-combined PRIVATE caffeine-editor-support)
    endif()

      # ── imgui_test_engine (for editor UI tests) ────────────────────
     FetchContent_Declare(
         imgui_test_engine
         GIT_REPOSITORY https://github.com/ocornut/imgui_test_engine.git
         GIT_TAG        main
         GIT_SHALLOW    TRUE
     )
     FetchContent_MakeAvailable(imgui_test_engine)

     FetchContent_Declare(
         nanosvg
         GIT_REPOSITORY https://github.com/memononen/nanosvg.git
         GIT_TAG        master
         GIT_SHALLOW    TRUE
     )
     FetchContent_MakeAvailable(nanosvg)

      add_executable(doppio
          apps/doppio/main.cpp
          src/doppio/editor/TransformGizmo.cpp
          src/doppio/editor/HierarchyPanel.cpp
          src/doppio/editor/SceneViewport.cpp
          src/doppio/editor/AssetBrowser.cpp
          src/doppio/editor/BrowseSession.cpp
          src/doppio/editor/AssetPreviewRenderer.cpp
          src/doppio/editor/EditorPreferences.cpp
          src/doppio/editor/InspectorPanel.cpp
          src/doppio/editor/ComponentRegistry.cpp
          src/doppio/editor/SettingsPanel.cpp
          src/doppio/editor/LayoutManager.cpp
          src/doppio/editor/SceneEditor.cpp
          src/doppio/editor/UiGalleryPanel.cpp
          src/doppio/editor/PhotonPanel.cpp
          src/doppio/editor/TerrainPanel.cpp
          src/doppio/editor/DragDropSystem.cpp
          src/doppio/editor/SceneTabManager.cpp
          src/doppio/editor/ScriptEditorWindow.cpp
          src/doppio/editor/DryadNodeScriptPanel.cpp
          src/doppio/editor/DryadNodesPanel.cpp
          src/doppio/editor/AnimationTimeline.cpp
          src/doppio/editor/AnimatorController.cpp
          src/doppio/editor/TilemapEditor.cpp
          src/doppio/editor/CommandPalette.cpp
          src/doppio/editor/ShaderNode.cpp
          src/doppio/editor/ShaderGraph.cpp
          src/doppio/editor/MaterialSerializer.cpp
          src/doppio/editor/MaterialEditorPanel.cpp
          src/doppio/editor/ImageManagerPanel.cpp
          src/doppio/editor/HudEditorPanel.cpp
          src/doppio/editor/ParticleEditorPanel.cpp
          src/doppio/editor/EffectEditorPanel.cpp
          src/doppio/editor/EntityPresetTypes.cpp
          src/doppio/editor/EntityPresetRegistry.cpp
          src/doppio/editor/EntityPresetPackages.cpp
          src/doppio/editor/EntityPresetFactories.cpp
          src/doppio/editor/EntityPresetsPanel.cpp
           src/doppio/editor/AudioPreviewPanel.cpp
           src/doppio/editor/CameraPreviewPanel.cpp
           src/doppio/editor/GameplayPreviewPanel.cpp
          src/doppio/editor/BuildSystem.cpp
          src/doppio/editor/BuildDialog.cpp
          src/doppio/editor/BuildLog.cpp
          src/doppio/editor/PrefabSystem.cpp
          src/doppio/editor/EntityDebugger.cpp
          src/doppio/editor/PluginSystem.cpp
          src/doppio/editor/PluginServiceRegistry.cpp
          src/doppio/editor/PluginSymbolExports.cpp
           src/doppio/editor/TestUIMapper.cpp
           src/doppio/editor/TestRequestHandler.cpp
           src/doppio/editor/AssetCooker.cpp
          src/doppio/editor/CapLoader.cpp
          src/doppio/editor/ProjectStartupDialog.cpp
          src/caffeine/project/ProjectManager.cpp
          src/doppio/editor/FilePicker.cpp
          src/doppio/editor/EditorPaths.cpp
          src/doppio/editor/EditorTheme.cpp
          src/doppio/editor/EditorIcons.cpp
          src/doppio/editor/EditorSpinner.cpp
          assets/scripts/ScriptBindings.cpp
          "${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_capture_tool.cpp"
         "${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_context.cpp"
         "${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_coroutine.cpp"
         "${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_engine.cpp"
         "${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_exporters.cpp"
         "${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_perftool.cpp"
         "${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_ui.cpp"
         "${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_utils.cpp"
     )
     target_link_libraries(doppio PRIVATE caffeine-core caffeine-editor-support doppio-ui-kit ImGui ImNodes ${CMAKE_DL_LIBS})
     if(NOT MSVC)
         target_link_options(doppio PRIVATE "-rdynamic")
     endif()
     if(CAF_PACK_AVAILABLE)
         target_link_libraries(doppio PRIVATE caf-pack-lib)
         target_compile_definitions(doppio PRIVATE CF_HAS_CAF_PACK=1)
     endif()
      target_include_directories(doppio PRIVATE
          ${CMAKE_CURRENT_SOURCE_DIR}/src/doppio
          ${CMAKE_CURRENT_SOURCE_DIR}/src
          ${CMAKE_CURRENT_SOURCE_DIR}/include
          ${CMAKE_CURRENT_SOURCE_DIR}/assets/scripts
          ${CMAKE_CURRENT_SOURCE_DIR}/caf-pack/include
          ${imgui_test_engine_SOURCE_DIR}
          ${nanosvg_SOURCE_DIR}/src)
      add_executable(caffeine-runtime
          apps/runtime/main.cpp
          src/caffeine/runtime/RuntimeSceneRenderer.cpp
          src/caffeine/project/ProjectManager.cpp
          "${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_capture_tool.cpp"
          "${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_context.cpp"
          "${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_coroutine.cpp"
          "${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_engine.cpp"
          "${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_exporters.cpp"
          "${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_perftool.cpp"
          "${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_ui.cpp"
          "${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_utils.cpp"
      )
      target_link_libraries(caffeine-runtime PRIVATE caffeine-core caffeine-editor-support ImGui)
      target_include_directories(caffeine-runtime PRIVATE
          ${CMAKE_CURRENT_SOURCE_DIR}/src
          ${CMAKE_CURRENT_SOURCE_DIR}/src/doppio
          ${imgui_SOURCE_DIR}
          ${imgui_SOURCE_DIR}/backends)
      target_compile_definitions(caffeine-runtime PRIVATE CF_HAS_IMGUI=1)
      target_compile_features(caffeine-runtime PRIVATE cxx_std_20)
      if(NOT MSVC)
          target_compile_options(caffeine-runtime PRIVATE -Wno-macro-redefined -Wno-cpp)
      endif()
      add_dependencies(doppio caffeine-runtime)

      # Editor plugins are owned by the doppio-plugins submodule, but they are
      # part of the normal build: every plugin is compiled and its library is
      # written to build/plugins. Loading remains an explicit Plugin Manager
      # choice; nothing is silently omitted from the build.
      option(CAFFEINE_BUILD_DOPPIO_PLUGINS
          "Build editor plugins from the doppio-plugins submodule" ON)
      if(CAFFEINE_BUILD_DOPPIO_PLUGINS)
          if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/doppio-plugins/CMakeLists.txt")
              if(NOT CAFFEINE_ROOT)
                  set(CAFFEINE_ROOT "${CMAKE_SOURCE_DIR}")
              endif()
              if(NOT IMGUI_DIR)
                  set(IMGUI_DIR "${imgui_SOURCE_DIR}")
              endif()
              add_subdirectory(doppio-plugins "${CMAKE_BINARY_DIR}/doppio-plugins")
          else()
              message(FATAL_ERROR
                  "doppio-plugins submodule is missing. Run ./scripts/submodules.sh, "
                  "or configure explicitly with -DCAFFEINE_BUILD_DOPPIO_PLUGINS=OFF.")
          endif()
      endif()

      add_executable(build-smoke
          apps/build_smoke/main.cpp
          src/doppio/editor/BuildSystem.cpp
          src/doppio/editor/AssetCooker.cpp
          src/doppio/editor/BuildLog.cpp
      )
      target_link_libraries(build-smoke PRIVATE caffeine-core)
      target_include_directories(build-smoke PRIVATE
          ${CMAKE_CURRENT_SOURCE_DIR}/src
          ${CMAKE_CURRENT_SOURCE_DIR}/src/doppio)
      target_compile_features(build-smoke PRIVATE cxx_std_20)
      add_dependencies(build-smoke caffeine-runtime)
      target_compile_definitions(build-smoke PRIVATE
          CAFFEINE_RUNTIME_PATH="$<TARGET_FILE:caffeine-runtime>")
      if(NOT MSVC)
          target_compile_options(build-smoke PRIVATE -Wno-macro-redefined -Wno-cpp)
      endif()

      # Editor plugins live in the separate doppio-plugins repository and are
      # built there against the public plugin SDK. Doppio never builds or
      # preloads them: install .so/.dll files in a plugins directory and enable
      # them from Plugins > Plugin Manager.
      #   ./scripts/submodules.sh
      #   cmake -S doppio-plugins -B doppio-plugins/build \
      #         -DCAFFEINE_ROOT=<this checkout>
      #   cmake --build doppio-plugins/build

      target_compile_definitions(doppio PRIVATE
          CF_HAS_IMGUI=1
          CAFFEINE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
          CAFFEINE_RUNTIME_PATH="$<TARGET_FILE:caffeine-runtime>")
      add_custom_command(TARGET doppio POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:doppio>/assets
          COMMAND ${CMAKE_COMMAND} -E copy_if_different
              ${CMAKE_SOURCE_DIR}/assets/café_no_back_logo.png
              $<TARGET_FILE_DIR:doppio>/assets/café_no_back_logo.png
          COMMAND ${CMAKE_COMMAND} -E copy_directory
              ${CMAKE_SOURCE_DIR}/assets/icons $<TARGET_FILE_DIR:doppio>/assets/icons
          COMMAND ${CMAKE_COMMAND} -E copy_directory
              ${CMAKE_SOURCE_DIR}/assets/ui $<TARGET_FILE_DIR:doppio>/assets/ui
          COMMAND ${CMAKE_COMMAND} -E copy_directory
              ${CMAKE_SOURCE_DIR}/assets/spinners $<TARGET_FILE_DIR:doppio>/assets/spinners
          COMMAND ${CMAKE_COMMAND} -E copy_directory
              ${CMAKE_SOURCE_DIR}/assets/jetbrains-mono $<TARGET_FILE_DIR:doppio>/assets/jetbrains-mono
          COMMAND ${CMAKE_COMMAND} -E copy_directory
              ${CMAKE_SOURCE_DIR}/assets/kenney_skyboxes $<TARGET_FILE_DIR:doppio>/assets/kenney_skyboxes
          COMMAND ${CMAKE_COMMAND} -E copy_directory
              ${CMAKE_SOURCE_DIR}/assets/kenney_prototype-textures $<TARGET_FILE_DIR:doppio>/assets/kenney_prototype-textures
          COMMENT "Copying editor UI assets")
     target_compile_features(doppio PRIVATE cxx_std_20)
     # Suppress redefinition warning for IMGUI_DEFINE_MATH_OPERATORS (defined in header)
     if(NOT MSVC)
         target_compile_options(doppio PRIVATE -Wno-macro-redefined -Wno-cpp)
     endif()

    message(STATUS "Doppio (IDE) enabled – ImGui v1.91.9 fetched")
endif()

# ── caf-encode CLI tool (legacy — prefer caf-pack CLI) ─────────
add_executable(caf-encode tools/caf-encode/main.cpp)
target_link_libraries(caf-encode PRIVATE caffeine-core)
target_include_directories(caf-encode PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_features(caf-encode PRIVATE cxx_std_20)

# ── Render snapshot (reference scene → PNG through the GPU renderer) ──
if(SDL3_FOUND AND NOT CAFFEINE_BUILD_HEADLESS)
    add_executable(caffeine-render-snapshot tools/render-snapshot/main.cpp)
    target_link_libraries(caffeine-render-snapshot PRIVATE caffeine-core)
    target_include_directories(caffeine-render-snapshot PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/vendor/stb)
    target_compile_features(caffeine-render-snapshot PRIVATE cxx_std_20)
endif()

# ── Plugin ↔ Doppio coupling audit ──────────────────────────────
find_program(CAFFEINE_PYTHON3 NAMES python3 python)
if(CAFFEINE_PYTHON3)
    add_custom_target(audit-plugin-coupling
        COMMAND ${CAFFEINE_PYTHON3}
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/audit_plugin_coupling.py
            --root ${CMAKE_CURRENT_SOURCE_DIR}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Auditing plugin coupling in src/caffeine/editor"
        VERBATIM)
endif()

# ── Tests ────────────────────────────────────────────────────────
add_subdirectory(tests)