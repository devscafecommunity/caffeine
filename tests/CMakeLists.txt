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
    test_photon_bake.cpp
    test_photon_farfield.cpp
    test_vegetation_kit.cpp
    test_capi.cpp
    test_phase2_3.cpp
    test_natural_fluids.cpp
    ../src/caffeine/project/ProjectManager.cpp
    ../src/caffeine/assets/MaterialCache.cpp
    ../src/caffeine/assets/MaterialFile.cpp
)

if(SDL3_FOUND)
    list(APPEND CAFFEINE_TEST_SOURCES
        test_rhi.cpp
        test_batchrenderer.cpp
        test_audio.cpp
    )
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

target_include_directories(CaffeineTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/Catch2 ${CMAKE_SOURCE_DIR}/caf-pack/include)
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



