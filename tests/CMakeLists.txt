cmake_minimum_required(VERSION 3.20)

set(CAFFEINE_TEST_SOURCES
    test_allocators.cpp
    test_containers.cpp
    test_math.cpp
    test_core.cpp
    test_timer.cpp
    test_gameloop.cpp
    test_input.cpp
    test_debug.cpp
    test_jobsystem.cpp
    test_caf.cpp
    test_eventbus.cpp
    test_assetmanager.cpp
    test_camera2d.cpp
    test_camera3d.cpp
    test_textureatlas.cpp
    test_scenemanager.cpp
    test_physics2d.cpp
    test_physics3d.cpp
    test_mesh_lod.cpp
    test_render_features.cpp
    test_forward_render_features.cpp
    test_material.cpp
    test_ui.cpp
    test_pick_query.cpp
    test_animation.cpp
    test_navigation.cpp
    test_effects.cpp
    test_mesh.cpp
    test_terrain.cpp
    test_octree.cpp
    test_skeletal_animation.cpp
    test_pipeline.cpp
    test_editor.cpp
    test_photon_bake.cpp
    test_photon_farfield.cpp
    test_vegetation_kit.cpp
    test_cap_loader.cpp
    test_phase2_3.cpp
    test_natural_fluids.cpp
    editor/phase1_integration_test.cpp
    ../src/doppio/editor/DragDropSystem.cpp
    ../src/doppio/editor/AssetBrowser.cpp
    ../src/doppio/editor/BrowseSession.cpp
    ../src/doppio/editor/AssetPreviewRenderer.cpp
    ../src/doppio/editor/SceneTabManager.cpp
    ../src/doppio/editor/AnimationTimeline.cpp
    ../src/doppio/editor/AnimatorController.cpp
    ../src/doppio/editor/TilemapEditor.cpp
    ../src/doppio/editor/CommandPalette.cpp
    ../src/doppio/editor/BuildSystem.cpp
    ../src/doppio/editor/BuildDialog.cpp
    ../src/doppio/editor/BuildLog.cpp
    ../src/doppio/editor/AssetCooker.cpp
    ../src/doppio/editor/CapLoader.cpp
    ../src/doppio/editor/AudioWaveformRenderer.cpp
    ../src/caffeine/project/ProjectManager.cpp
    ../src/doppio/editor/ProjectStartupDialog.cpp
    ../src/doppio/editor/FilePicker.cpp
    ../src/doppio/editor/SettingsPanel.cpp
    ../src/doppio/editor/LayoutManager.cpp
    ../src/doppio/editor/EditorPaths.cpp
    ../src/doppio/editor/EditorIcons.cpp
    ../src/doppio/editor/EditorSpinner.cpp
    ../src/doppio/editor/EditorPreferences.cpp
    ../src/doppio/editor/EditorTheme.cpp
    ../src/doppio/editor/HierarchyPanel.cpp
    ../src/doppio/editor/SceneViewport.cpp
    ../src/doppio/editor/InspectorPanel.cpp
    ../src/doppio/editor/ComponentRegistry.cpp
    ../src/doppio/editor/SceneEditor.cpp
    ../src/doppio/editor/PhotonPanel.cpp
    ../src/doppio/editor/TerrainPanel.cpp
    ../src/doppio/editor/ImageManagerPanel.cpp
    ../src/doppio/editor/HudEditorPanel.cpp
    ../src/doppio/editor/ParticleEditorPanel.cpp
    ../src/doppio/editor/EffectEditorPanel.cpp
    ../src/doppio/editor/DryadNodeScriptPanel.cpp
    ../src/doppio/editor/DryadNodesPanel.cpp
    ../src/doppio/editor/TransformGizmo.cpp
    ../src/doppio/editor/PrefabSystem.cpp
    ../src/doppio/editor/MaterialSerializer.cpp
    ../src/doppio/editor/EntityPresetTypes.cpp
    ../src/doppio/editor/EntityPresetRegistry.cpp
    ../src/doppio/editor/EntityPresetPackages.cpp
    ../src/doppio/editor/EntityPresetFactories.cpp
    ../src/doppio/editor/EntityPresetsPanel.cpp
    ../src/doppio/editor/EntityDebugger.cpp
    ../src/doppio/editor/PluginSystem.cpp
    ../src/doppio/editor/PluginServiceRegistry.cpp
    ../src/doppio/editor/PluginSymbolExports.cpp
    ../src/doppio/editor/TestUIMapper.cpp
    ../src/doppio/editor/TestRequestHandler.cpp
    ../src/doppio/editor/ScriptEditorWindow.cpp
    ../src/doppio/editor/CameraPreviewPanel.cpp
    ../src/doppio/editor/GameplayPreviewPanel.cpp
    ../src/caffeine/assets/MaterialCache.cpp
    ../src/caffeine/assets/MaterialFile.cpp
)

if(SDL3_FOUND)
    list(APPEND CAFFEINE_TEST_SOURCES 
        test_rhi.cpp 
        test_batchrenderer.cpp 
        test_audio.cpp
        test_ui_kit.cpp
        ../src/doppio/editor/MaterialEditorPanel.cpp
        ../src/doppio/editor/AudioPreviewPanel.cpp
        ../src/doppio/editor/ShaderGraph.cpp
        ../src/doppio/editor/ShaderNode.cpp
    )
    
     # Build imgui_test_engine library
     file(GLOB IMGUI_TEST_ENGINE_SRCS
         "${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_*.cpp"
         "${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_capture_tool.cpp"
     )
     add_library(ImGuiTestEngine STATIC ${IMGUI_TEST_ENGINE_SRCS})
     target_include_directories(ImGuiTestEngine PUBLIC ${imgui_test_engine_SOURCE_DIR})
     target_link_libraries(ImGuiTestEngine PUBLIC ImGui)
     target_compile_definitions(ImGuiTestEngine PUBLIC IMGUI_TEST_ENGINE_ENABLE_COROUTINE_STDTHREAD_IMPL=1)
     # Suppress redefinition warning for IMGUI_DEFINE_MATH_OPERATORS (defined in header)
     if(NOT MSVC)
         target_compile_options(ImGuiTestEngine PRIVATE -Wno-macro-redefined -Wno-cpp)
     endif()
endif()

if(DRYAD_AVAILABLE)
    list(APPEND CAFFEINE_TEST_SOURCES test_dryad_host.cpp test_node_emitter.cpp)
endif()

add_executable(CaffeineTest ${CAFFEINE_TEST_SOURCES})

target_link_libraries(CaffeineTest PRIVATE caffeine-core)
if(CAF_PACK_AVAILABLE)
    target_link_libraries(CaffeineTest PRIVATE caf-pack-lib)
    target_compile_definitions(CaffeineTest PRIVATE CF_HAS_CAF_PACK=1)
endif()

# Link ImGui and test engine if SDL3 is available (needed for editor panels)
if(SDL3_FOUND)
    target_link_libraries(CaffeineTest PRIVATE ImGui ImNodes ImGuiTestEngine doppio-ui-kit)
    target_compile_definitions(CaffeineTest PRIVATE CF_HAS_IMGUI=1 IMGUI_TEST_ENGINE_ENABLE_COROUTINE_STDTHREAD_IMPL=1)
    target_include_directories(CaffeineTest PRIVATE ${imgui_test_engine_SOURCE_DIR})
    if(DEFINED nanosvg_SOURCE_DIR)
        target_include_directories(CaffeineTest PRIVATE ${nanosvg_SOURCE_DIR}/src)
    endif()
endif()

target_include_directories(CaffeineTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/Catch2 ${CMAKE_SOURCE_DIR}/caf-pack/include ${CMAKE_SOURCE_DIR}/src/doppio)
if(MSVC)
    target_compile_options(CaffeineTest PRIVATE /W4 /WX-)
else()
    target_compile_options(CaffeineTest PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()
add_test(NAME CaffeineTest COMMAND CaffeineTest)

if(CAFFEINE_ENABLE_SCRIPTING)
    list(APPEND CAFFEINE_TEST_SOURCES test_script.cpp)
endif()

if(SDL3_FOUND AND WIN32)
    add_custom_command(TARGET CaffeineTest POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL3::SDL3>
            $<TARGET_FILE_DIR:CaffeineTest>
    )
endif()

# ── DoppioTest — Editor UI tests (ImGui-dependent) ──────────────
if(SDL3_FOUND AND NOT CAFFEINE_BUILD_HEADLESS)
    file(GLOB IMGUI_TEST_ENGINE_SRCS
        "${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_*.cpp"
        "${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_capture_tool.cpp"
    )
    
      add_executable(DoppioTest
          test_editor_ui_main.cpp
          test_editor_ui/test_inspector.cpp
          test_editor_ui/test_hierarchy.cpp
          test_editor_ui/test_assetbrowser.cpp
          test_editor_ui/test_photon_bake.cpp
          test_editor_ui/test_photon_panel.cpp
          test_editor_ui/test_terrain_panel.cpp
          test_editor_ui/test_dryad_nodes.cpp
          ${CMAKE_SOURCE_DIR}/src/doppio/editor/InspectorPanel.cpp
          ${CMAKE_SOURCE_DIR}/src/doppio/editor/SettingsPanel.cpp
          ${CMAKE_SOURCE_DIR}/src/doppio/editor/LayoutManager.cpp
          ${CMAKE_SOURCE_DIR}/src/doppio/editor/SceneViewport.cpp
          ${CMAKE_SOURCE_DIR}/src/doppio/editor/HierarchyPanel.cpp
          ${CMAKE_SOURCE_DIR}/src/doppio/editor/AssetBrowser.cpp
          ${CMAKE_SOURCE_DIR}/src/doppio/editor/BrowseSession.cpp
          ${CMAKE_SOURCE_DIR}/src/doppio/editor/CapLoader.cpp
          ${CMAKE_SOURCE_DIR}/src/doppio/editor/AudioWaveformRenderer.cpp
          ${CMAKE_SOURCE_DIR}/src/doppio/editor/SceneEditor.cpp
          ${CMAKE_SOURCE_DIR}/src/doppio/editor/PhotonPanel.cpp
          ${CMAKE_SOURCE_DIR}/src/doppio/editor/TerrainPanel.cpp
          ${CMAKE_SOURCE_DIR}/src/doppio/editor/ImageManagerPanel.cpp
          ${CMAKE_SOURCE_DIR}/src/doppio/editor/HudEditorPanel.cpp
          ${CMAKE_SOURCE_DIR}/src/doppio/editor/ParticleEditorPanel.cpp
          ${CMAKE_SOURCE_DIR}/src/doppio/editor/EffectEditorPanel.cpp
          ${CMAKE_SOURCE_DIR}/src/doppio/editor/DryadNodeScriptPanel.cpp
          ${CMAKE_SOURCE_DIR}/src/doppio/editor/DryadNodesPanel.cpp
                    ${CMAKE_SOURCE_DIR}/src/doppio/editor/DragDropSystem.cpp
          ${CMAKE_SOURCE_DIR}/src/doppio/editor/SceneTabManager.cpp
          ${CMAKE_SOURCE_DIR}/src/doppio/editor/AnimationTimeline.cpp
          ${CMAKE_SOURCE_DIR}/src/doppio/editor/TilemapEditor.cpp
          ${CMAKE_SOURCE_DIR}/src/doppio/editor/CommandPalette.cpp
                    ${CMAKE_SOURCE_DIR}/src/caffeine/project/ProjectManager.cpp
          ${CMAKE_SOURCE_DIR}/src/doppio/editor/AudioPreviewPanel.cpp
          ${CMAKE_SOURCE_DIR}/src/doppio/editor/MaterialEditorPanel.cpp
          ${CMAKE_SOURCE_DIR}/src/doppio/editor/ShaderGraph.cpp
          ${CMAKE_SOURCE_DIR}/src/doppio/editor/ShaderNode.cpp
          ${CMAKE_SOURCE_DIR}/src/doppio/editor/ScriptEditorWindow.cpp
          ${CMAKE_SOURCE_DIR}/src/doppio/editor/BuildSystem.cpp
          ${CMAKE_SOURCE_DIR}/src/doppio/editor/BuildDialog.cpp
          ${CMAKE_SOURCE_DIR}/src/doppio/editor/AssetCooker.cpp
          ${CMAKE_SOURCE_DIR}/src/doppio/editor/BuildLog.cpp
          ${CMAKE_SOURCE_DIR}/src/doppio/editor/FilePicker.cpp
          ${CMAKE_SOURCE_DIR}/src/doppio/editor/EditorIcons.cpp
          ${CMAKE_SOURCE_DIR}/src/doppio/editor/EditorPaths.cpp
          ${CMAKE_SOURCE_DIR}/src/doppio/editor/ComponentRegistry.cpp
          ${CMAKE_SOURCE_DIR}/src/doppio/editor/PluginSystem.cpp
          ${CMAKE_SOURCE_DIR}/src/doppio/editor/PluginSymbolExports.cpp
          ${CMAKE_SOURCE_DIR}/src/doppio/editor/PluginServiceRegistry.cpp
          ${CMAKE_SOURCE_DIR}/src/doppio/editor/EntityPresetTypes.cpp
          ${CMAKE_SOURCE_DIR}/src/doppio/editor/EntityPresetRegistry.cpp
          ${CMAKE_SOURCE_DIR}/src/doppio/editor/EntityPresetPackages.cpp
          ${CMAKE_SOURCE_DIR}/src/doppio/editor/EntityPresetFactories.cpp
          ${CMAKE_SOURCE_DIR}/src/doppio/editor/PrefabSystem.cpp
          ${CMAKE_SOURCE_DIR}/src/doppio/editor/TransformGizmo.cpp
          ${CMAKE_SOURCE_DIR}/src/doppio/editor/AssetPreviewRenderer.cpp
          ${CMAKE_SOURCE_DIR}/src/doppio/editor/AnimatorController.cpp
          ${CMAKE_SOURCE_DIR}/src/doppio/editor/EditorPreferences.cpp
          ${CMAKE_SOURCE_DIR}/src/doppio/editor/EditorTheme.cpp
          ${CMAKE_SOURCE_DIR}/src/doppio/editor/EditorSpinner.cpp
          ${CMAKE_SOURCE_DIR}/src/doppio/editor/EntityPresetsPanel.cpp
          ${CMAKE_SOURCE_DIR}/src/doppio/editor/EntityDebugger.cpp
          ${CMAKE_SOURCE_DIR}/src/doppio/editor/TestUIMapper.cpp
          ${CMAKE_SOURCE_DIR}/src/doppio/editor/TestRequestHandler.cpp
          ${CMAKE_SOURCE_DIR}/src/doppio/editor/CameraPreviewPanel.cpp
          ${CMAKE_SOURCE_DIR}/src/doppio/editor/GameplayPreviewPanel.cpp
          ${CMAKE_SOURCE_DIR}/src/doppio/editor/MaterialSerializer.cpp
          ${IMGUI_TEST_ENGINE_SRCS}
      )
    
    target_link_libraries(DoppioTest PRIVATE caffeine-core caf-pack-lib ImGui ImNodes doppio-ui-kit)
    target_include_directories(DoppioTest PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/doppio
        ${CMAKE_SOURCE_DIR}/caf-pack/include
        ${imgui_test_engine_SOURCE_DIR}
        ${imgui_SOURCE_DIR}
    )
    target_compile_definitions(DoppioTest PRIVATE
        CF_HAS_IMGUI=1
        IMGUI_TEST_ENGINE_ENABLE_COROUTINE_STDTHREAD_IMPL=1
    )
    target_compile_features(DoppioTest PRIVATE cxx_std_20)
    if(DEFINED nanosvg_SOURCE_DIR)
        target_include_directories(DoppioTest PRIVATE ${nanosvg_SOURCE_DIR}/src)
    endif()
    
    add_test(NAME DoppioTest COMMAND DoppioTest)
endif()

find_program(PYTHON3_EXECUTABLE NAMES python3 python)
if(PYTHON3_EXECUTABLE AND TARGET doppio)
    add_test(
        NAME DoppioUIAutomated
        COMMAND ${PYTHON3_EXECUTABLE} -m pytest
            ${CMAKE_CURRENT_SOURCE_DIR}/test_ui_automated.py
            -v --tb=short
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
    set_tests_properties(DoppioUIAutomated PROPERTIES
        ENVIRONMENT "DOPPIO_BIN=$<TARGET_FILE:doppio>"
        TIMEOUT 60
        LABELS "ui;automated"
    )
endif()

