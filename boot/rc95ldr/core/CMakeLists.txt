add_library(rc95ldr_core STATIC
    rc95ldr.c
    rc95ldrentry.c
    rc95ldrdiscover.c
)

target_include_directories(rc95ldr_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

set_target_properties(rc95ldr_core PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
)
