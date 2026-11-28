add_library(rc95ldr_firmware_menu STATIC firmwaremenu.c)
target_include_directories(rc95ldr_firmware_menu PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../bcd/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../winload/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../textui/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../bgfx/include)
target_link_libraries(rc95ldr_firmware_menu PUBLIC
    rc95ldr_bgfx rc95ldr_bcd rc95ldr_textui)
set_target_properties(rc95ldr_firmware_menu PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON)
