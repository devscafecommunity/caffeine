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
    test_editor.cpp
    test_octree.cpp
    test_pipeline.cpp
)

if(SDL3_FOUND)
    list(APPEND CAFFEINE_TEST_SOURCES test_rhi.cpp test_batchrenderer.cpp test_audio.cpp)
endif()

add_executable(CaffeineTest ${CAFFEINE_TEST_SOURCES})

target_link_libraries(CaffeineTest PRIVATE Caffeine)
target_include_directories(CaffeineTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/Catch2)
if(MSVC)
    target_compile_options(CaffeineTest PRIVATE /W4 /WX-)
else()
    target_compile_options(CaffeineTest PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()
add_test(NAME CaffeineTest COMMAND CaffeineTest)

if(SDL3_FOUND AND WIN32)
    add_custom_command(TARGET CaffeineTest POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL3::SDL3>
            $<TARGET_FILE_DIR:CaffeineTest>
    )
endif()
