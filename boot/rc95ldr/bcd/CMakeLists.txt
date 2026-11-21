add_library(rc95ldr_bcd STATIC
    bcdhive.c
    bcdload.c
    bcdelement.c
    bcddevice.c
    bcdwalk.c
)
target_include_directories(rc95ldr_bcd PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../winload/include)
target_link_libraries(rc95ldr_bcd PUBLIC rc95ldr_core)
set_target_properties(rc95ldr_bcd PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON)
