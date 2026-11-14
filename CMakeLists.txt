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
    src/caffeine/render/StbImageWrite.cpp
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

# ── C ABI shared library for non-C++ frontends ──────────────────
# Rust/Iced, tools and tests can load this library without linking the C++
# engine directly. The ABI is versioned in include/caffeine/capi/EngineAPI.h.
add_library(caffeine-capi SHARED
    src/caffeine/capi/EngineAPI.cpp
)
target_include_directories(caffeine-capi PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(caffeine-capi PRIVATE caffeine-core)
target_compile_features(caffeine-capi PRIVATE cxx_std_20)
target_compile_definitions(caffeine-capi PRIVATE CAFFEINE_CAPI_BUILD)
set_target_properties(caffeine-capi PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME "caffeine-capi"
)
add_library(Caffeine::CAPI ALIAS caffeine-capi)

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
    message(WARNING "caf-pack submodule not found. Building without asset packing support.")
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
#  IMGUI HOST SUPPORT
#
#  The C++ Doppio editor is archived. ImGui remains available for the
#  standalone runtime, the engine ImGui adapters and the legacy plugin SDK.
# ═══════════════════════════════════════════════════════════════════
if(SDL3_FOUND AND NOT CAFFEINE_BUILD_HEADLESS)
    include(FetchContent)
    FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG        docking
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
     target_compile_definitions(ImGui PUBLIC IMGUI_DEFINE_MATH_OPERATORS)

    # ImGui is a frontend/plugin dependency, not an engine dependency.
    # caffeine-core stays UI-free; hosts and plugins opt in explicitly.

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
    target_link_libraries(caffeine-editor-support PUBLIC caffeine-core ImGui)
    target_compile_features(caffeine-editor-support PUBLIC cxx_std_20)
    target_compile_definitions(caffeine-editor-support PUBLIC CF_HAS_IMGUI=1)
    target_compile_definitions(caffeine-editor-support PUBLIC
        CAFFEINE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    set_target_properties(caffeine-editor-support PROPERTIES POSITION_INDEPENDENT_CODE ON)
    add_library(Caffeine::EditorSupport ALIAS caffeine-editor-support)

    if(TARGET caffeine-combined)
        target_link_libraries(caffeine-combined PRIVATE caffeine-editor-support)
    endif()

      # ImGui host used by the standalone runtime.
      add_executable(caffeine-runtime
          apps/runtime/main.cpp
          src/caffeine/runtime/RuntimeSceneRenderer.cpp
          src/caffeine/project/ProjectManager.cpp
      )
      target_link_libraries(caffeine-runtime PRIVATE caffeine-core caffeine-editor-support ImGui)
      target_include_directories(caffeine-runtime PRIVATE
          ${CMAKE_CURRENT_SOURCE_DIR}/src
          ${imgui_SOURCE_DIR}
          ${imgui_SOURCE_DIR}/backends)
      target_compile_definitions(caffeine-runtime PRIVATE CF_HAS_IMGUI=1)
      target_compile_features(caffeine-runtime PRIVATE cxx_std_20)
      if(NOT MSVC)
          target_compile_options(caffeine-runtime PRIVATE -Wno-macro-redefined -Wno-cpp)
      endif()

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

    message(STATUS "ImGui host support enabled (legacy C++ Doppio archived)")
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
        COMMENT "Auditing plugins for coupling to archived editor internals"
        VERBATIM)
endif()

# ── Tests ────────────────────────────────────────────────────────
enable_testing()
add_subdirectory(tests)