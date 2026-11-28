add_library(rc95ldr_bgfx STATIC bgfx.c)
target_include_directories(rc95ldr_bgfx PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../textui/include)
target_link_libraries(rc95ldr_bgfx PUBLIC rc95ldr_core rc95ldr_textui)
set_target_properties(rc95ldr_bgfx PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON)
