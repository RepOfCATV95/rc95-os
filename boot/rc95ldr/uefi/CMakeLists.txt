add_library(rc95ldr_uefi_discovery STATIC
    uefidiscover.c
    uedisk.c
    uefivolume.c
    uefinativemounts.c
    uefinativediscovery.c
    uefimemory.c
    uefientry.c
    uefiservices.c
    uefivideo.c
)
target_include_directories(rc95ldr_uefi_discovery PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../bcd/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../winload/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../fs/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../veneer/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../bgfx/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../firmware/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../sdk/include/reactos/edk2
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../sdk/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../sdk/include/psdk
    ${CMAKE_BINARY_DIR}/sdk/include/psdk)
target_link_libraries(rc95ldr_uefi_discovery PUBLIC
    rc95ldr_core rc95ldr_bcd rc95ldr_winload_policy rc95ldr_fs_fat32 rc95ldr_fs_ntfs_geometry rc95ldr_fs_btrfs rc95ldr_fs_wim rc95ldr_veneer rc95ldr_bgfx rc95ldr_firmware_menu)
set_target_properties(rc95ldr_uefi_discovery PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON)

# Firmware-facing WINLOAD orchestration over the concrete native UEFI adapters.
add_library(rc95ldr_winload_uefi_entry STATIC
    winload_entry.c
    winload_flow.c
    winload_provider_attestation.c
    nativeboot.c)
target_include_directories(rc95ldr_winload_uefi_entry PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../winload/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../textui/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../bgfx/include)
target_link_libraries(rc95ldr_winload_uefi_entry PUBLIC
    rc95ldr_winload_entry rc95ldr_winload_policy rc95ldr_uefi_discovery)
set_target_properties(rc95ldr_winload_uefi_entry PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON)

if(ARCH STREQUAL "amd64" AND MSVC)
    target_sources(rc95ldr_uefi_discovery PRIVATE uefihandoff.c)
    target_compile_definitions(rc95ldr_uefi_discovery PRIVATE
        RC95LDR_UEFI_NATIVE_AMD64=1)
endif()

# Final EFI applications link this dispatcher and define
# Rc95LdrUefiImageBootConfiguration in their image-specific boot config unit.
add_library(rc95ldr_uefi_image_entry STATIC image_entry.c)
target_include_directories(rc95ldr_uefi_image_entry PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../winload/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../bcd/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../fs/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../sdk/include/reactos/edk2
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../sdk/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../sdk/include/psdk
    ${CMAKE_BINARY_DIR}/sdk/include/psdk)
target_link_libraries(rc95ldr_uefi_image_entry PUBLIC
    rc95ldr_winload_uefi_entry rc95ldr_uefi_discovery)
set_target_properties(rc95ldr_uefi_image_entry PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON)
