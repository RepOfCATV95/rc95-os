# Winload orchestration is kept separate from BCD policy.
add_library(rc95ldr_winload_policy STATIC
    bcdpolicy.c
    pe.c
    ntprepare.c
    systemhive.c
    loaderblock.c
    loaderstate.c
    physmem.c
    platforminventory.c
    metadata.c
    archprepare.c
    nativeprepare.c
    handoff.c
    profile.c
    osprepare.c
    osload.c
)
target_include_directories(rc95ldr_winload_policy PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../bcd/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../textui/include)
target_link_libraries(rc95ldr_winload_policy PUBLIC
    rc95ldr_bcd rc95ldr_core rc95ldr_textui)
set_target_properties(rc95ldr_winload_policy PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON)
