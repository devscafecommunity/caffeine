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
    test_ui.cpp
    test_animation.cpp
    test_mesh.cpp
    test_octree.cpp
    test_skeletal_animation.cpp
    test_pipeline.cpp
    test_editor.cpp
    test_cap_loader.cpp
    editor/phase1_integration_test.cpp
    ../src/editor/SceneSerializer.cpp
    ../src/editor/DragDropSystem.cpp
    ../src/editor/AssetBrowser.cpp
    ../src/editor/SceneTabManager.cpp
    ../src/editor/AnimationTimeline.cpp
    ../src/editor/TilemapEditor.cpp
    ../src/editor/CommandPalette.cpp
    ../src/editor/BuildSystem.cpp
    ../src/editor/BuildDialog.cpp
    ../src/editor/AssetCooker.cpp
    ../src/editor/CapLoader.cpp
    ../src/editor/AudioWaveformRenderer.cpp
    ../src/editor/ProjectManager.cpp
)

if(SDL3_FOUND)
    list(APPEND CAFFEINE_TEST_SOURCES 
        test_rhi.cpp 
        test_batchrenderer.cpp 
        test_audio.cpp
        ../src/editor/MaterialEditorPanel.cpp
        ../src/editor/AudioPreviewPanel.cpp
        ../src/editor/ShaderGraph.cpp
        ../src/editor/ShaderNode.cpp
        ../src/editor/PreviewRenderer.cpp
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

add_executable(CaffeineTest ${CAFFEINE_TEST_SOURCES})

target_link_libraries(CaffeineTest PRIVATE caffeine-core)

# Link ImGui and test engine if SDL3 is available (needed for editor panels)
if(SDL3_FOUND)
    target_link_libraries(CaffeineTest PRIVATE ImGui ImNodes ImGuiTestEngine)
    target_compile_definitions(CaffeineTest PRIVATE CF_HAS_IMGUI=1 IMGUI_TEST_ENGINE_ENABLE_COROUTINE_STDTHREAD_IMPL=1)
    target_include_directories(CaffeineTest PRIVATE ${imgui_test_engine_SOURCE_DIR})
endif()

target_include_directories(CaffeineTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/Catch2)
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
        ${CMAKE_SOURCE_DIR}/src/editor/InspectorPanel.cpp
        ${CMAKE_SOURCE_DIR}/src/editor/SceneViewport.cpp
        ${CMAKE_SOURCE_DIR}/src/editor/HierarchyPanel.cpp
        ${CMAKE_SOURCE_DIR}/src/editor/AssetBrowser.cpp
        ${CMAKE_SOURCE_DIR}/src/editor/SceneEditor.cpp
        ${CMAKE_SOURCE_DIR}/src/editor/SceneSerializer.cpp
        ${CMAKE_SOURCE_DIR}/src/editor/DragDropSystem.cpp
        ${CMAKE_SOURCE_DIR}/src/editor/SceneTabManager.cpp
        ${CMAKE_SOURCE_DIR}/src/editor/AnimationTimeline.cpp
        ${CMAKE_SOURCE_DIR}/src/editor/TilemapEditor.cpp
        ${CMAKE_SOURCE_DIR}/src/editor/CommandPalette.cpp
        ${CMAKE_SOURCE_DIR}/src/editor/EditorContext.cpp
        ${CMAKE_SOURCE_DIR}/src/editor/ProjectManager.cpp
        ${CMAKE_SOURCE_DIR}/src/editor/AudioPreviewPanel.cpp
        ${CMAKE_SOURCE_DIR}/src/editor/MaterialEditorPanel.cpp
        ${CMAKE_SOURCE_DIR}/src/editor/ShaderGraph.cpp
        ${CMAKE_SOURCE_DIR}/src/editor/ShaderNode.cpp
        ${CMAKE_SOURCE_DIR}/src/editor/PreviewRenderer.cpp
        ${CMAKE_SOURCE_DIR}/src/editor/ScriptEditorWindow.cpp
        ${CMAKE_SOURCE_DIR}/src/editor/BuildSystem.cpp
        ${CMAKE_SOURCE_DIR}/src/editor/BuildDialog.cpp
        ${CMAKE_SOURCE_DIR}/src/editor/AssetCooker.cpp
        ${IMGUI_TEST_ENGINE_SRCS}
    )
    
    target_link_libraries(DoppioTest PRIVATE caffeine-core ImGui ImNodes)
    target_include_directories(DoppioTest PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${imgui_test_engine_SOURCE_DIR}
        ${imgui_SOURCE_DIR}
    )
    target_compile_definitions(DoppioTest PRIVATE
        CF_HAS_IMGUI=1
        IMGUI_TEST_ENGINE_ENABLE_COROUTINE_STDTHREAD_IMPL=1
    )
    target_compile_features(DoppioTest PRIVATE cxx_std_20)
    
    add_test(NAME DoppioTest COMMAND DoppioTest)
endif()

