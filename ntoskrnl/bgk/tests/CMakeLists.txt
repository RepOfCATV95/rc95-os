cmake_minimum_required(VERSION 3.16)
project(bgfx_host_tests C)

enable_testing()
add_executable(bgfx_bitmap_tests
    bgfx_bitmap_tests.c
    ../bitmap/bgfx_bitmap.c)
target_include_directories(bgfx_bitmap_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/host
    ${CMAKE_CURRENT_SOURCE_DIR}/../include)
set_target_properties(bgfx_bitmap_tests PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON)
add_test(NAME bgfx_bitmap_tests COMMAND bgfx_bitmap_tests)

add_executable(bgfx_framebuffer_tests
    bgfx_framebuffer_tests.c
    ../framebuffer/bgfx_framebuffer_validate.c)
target_include_directories(bgfx_framebuffer_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/host
    ${CMAKE_CURRENT_SOURCE_DIR}/../include)
set_target_properties(bgfx_framebuffer_tests PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON)
add_test(NAME bgfx_framebuffer_tests COMMAND bgfx_framebuffer_tests)

add_executable(bgfx_rendering_tests
    bgfx_rendering_tests.c
    ../framebuffer/bgfx_framebuffer.c
    ../framebuffer/bgfx_framebuffer_validate.c
    ../rendering/bgfx_rendering.c)
target_include_directories(bgfx_rendering_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/host
    ${CMAKE_CURRENT_SOURCE_DIR}/../include)
set_target_properties(bgfx_rendering_tests PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON)
add_test(NAME bgfx_rendering_tests COMMAND bgfx_rendering_tests)

add_executable(bgfx_animation_tests
    bgfx_animation_tests.c
    ../animation/bgfx_animation.c)
target_include_directories(bgfx_animation_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/host
    ${CMAKE_CURRENT_SOURCE_DIR}/../include)
set_target_properties(bgfx_animation_tests PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON)
add_test(NAME bgfx_animation_tests COMMAND bgfx_animation_tests)
