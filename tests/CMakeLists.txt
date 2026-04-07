cmake_minimum_required(VERSION 3.20)

add_executable(CaffeineTest
    test_allocators.cpp
    test_containers.cpp
    test_math.cpp
    test_core.cpp
)

target_link_libraries(CaffeineTest PRIVATE Caffeine)
target_include_directories(CaffeineTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/Catch2)
if(MSVC)
    target_compile_options(CaffeineTest PRIVATE /W4 /WX-)
else()
    target_compile_options(CaffeineTest PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()
add_test(NAME CaffeineTest COMMAND CaffeineTest)