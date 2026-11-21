add_library(rc95ldr_uefi_discovery STATIC
    uefidiscover.c
    uedisk.c
)
target_include_directories(rc95ldr_uefi_discovery PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../bcd/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../winload/include)
target_link_libraries(rc95ldr_uefi_discovery PUBLIC
    rc95ldr_core rc95ldr_bcd rc95ldr_winload_policy)
set_target_properties(rc95ldr_uefi_discovery PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON)
