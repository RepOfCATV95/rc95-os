
add_executable(rc95ldr_baseline_pagetables_tests baseline_pagetables_tests.c)
target_link_libraries(rc95ldr_baseline_pagetables_tests PRIVATE rc95ldr_winload_policy)
target_include_directories(rc95ldr_baseline_pagetables_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../winload/include)
set_target_properties(rc95ldr_baseline_pagetables_tests PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_baseline_pagetables_tests
    COMMAND rc95ldr_baseline_pagetables_tests)

add_executable(rc95ldr_block_io_tests block_io_tests.c)
target_link_libraries(rc95ldr_block_io_tests PRIVATE rc95ldr_core)
target_include_directories(rc95ldr_block_io_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include)
set_target_properties(rc95ldr_block_io_tests PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_block_io_tests COMMAND rc95ldr_block_io_tests)

add_executable(rc95ldr_ramdisk_tests ramdisk_tests.c)
target_link_libraries(rc95ldr_ramdisk_tests PRIVATE rc95ldr_core)
target_include_directories(rc95ldr_ramdisk_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include)
set_target_properties(rc95ldr_ramdisk_tests PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_ramdisk_tests COMMAND rc95ldr_ramdisk_tests)

add_executable(rc95ldr_bcd_tests bcd_tests.c)
target_link_libraries(rc95ldr_bcd_tests PRIVATE rc95ldr_bcd)
target_include_directories(rc95ldr_bcd_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../bcd/include)
set_target_properties(rc95ldr_bcd_tests PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)
# Host regression builds do not generate the SDK's xdk/ntdef.h header.
# Use the same narrow primitive-type shim as the standalone host test project.
target_include_directories(rc95ldr_winload_policy BEFORE PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/host/compat)

add_test(NAME rc95ldr_bcd_tests COMMAND rc95ldr_bcd_tests)


add_executable(rc95ldr_readonly_provider_tests readonly_provider_tests.c)
target_link_libraries(rc95ldr_readonly_provider_tests PRIVATE rc95ldr_core)
target_include_directories(rc95ldr_readonly_provider_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include)
set_target_properties(rc95ldr_readonly_provider_tests PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_readonly_provider_tests COMMAND rc95ldr_readonly_provider_tests)

add_executable(rc95ldr_discovery_tests discovery_tests.c)
target_link_libraries(rc95ldr_discovery_tests PRIVATE rc95ldr_core)
target_include_directories(rc95ldr_discovery_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include)
set_target_properties(rc95ldr_discovery_tests PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_discovery_tests COMMAND rc95ldr_discovery_tests)

add_executable(rc95ldr_textui_tests textui_tests.c)
target_link_libraries(rc95ldr_textui_tests PRIVATE rc95ldr_textui)
target_include_directories(rc95ldr_textui_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../textui/include)
set_target_properties(rc95ldr_textui_tests PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_textui_tests COMMAND rc95ldr_textui_tests)

add_executable(rc95ldr_bcd_policy_tests bcd_policy_tests.c)
target_link_libraries(rc95ldr_bcd_policy_tests PRIVATE rc95ldr_winload_policy)
target_include_directories(rc95ldr_bcd_policy_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../bcd/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../winload/include)
set_target_properties(rc95ldr_bcd_policy_tests PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_bcd_policy_tests COMMAND rc95ldr_bcd_policy_tests)

add_executable(rc95ldr_uefi_disk_tests uefi_disk_tests.c)
target_link_libraries(rc95ldr_uefi_disk_tests PRIVATE rc95ldr_uefi_discovery)
target_include_directories(rc95ldr_uefi_disk_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../uefi/include)
set_target_properties(rc95ldr_uefi_disk_tests PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_uefi_disk_tests COMMAND rc95ldr_uefi_disk_tests)

add_executable(rc95ldr_veneer_btrfs_tests veneer_btrfs_tests.c)
target_link_libraries(rc95ldr_veneer_btrfs_tests PRIVATE rc95ldr_veneer)
target_include_directories(rc95ldr_veneer_btrfs_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../veneer/include)
set_target_properties(rc95ldr_veneer_btrfs_tests PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_veneer_btrfs_tests COMMAND rc95ldr_veneer_btrfs_tests)

add_executable(rc95ldr_btrfs_volume_tests btrfs_volume_tests.c)
target_link_libraries(rc95ldr_btrfs_volume_tests PRIVATE rc95ldr_fs_btrfs rc95ldr_uefi_discovery rc95ldr_winload_policy rc95ldr_winload_uefi_entry)
target_include_directories(rc95ldr_btrfs_volume_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../fs/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../veneer/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../uefi/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../winload/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../bcd/include
    ${CMAKE_SOURCE_DIR}/sdk/include/reactos/edk2
    ${CMAKE_SOURCE_DIR}/sdk/include)
set_target_properties(rc95ldr_btrfs_volume_tests PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_btrfs_volume_tests COMMAND rc95ldr_btrfs_volume_tests)

add_executable(rc95ldr_uefi_provider_attestation_tests uefi_provider_attestation_tests.c)
target_link_libraries(rc95ldr_uefi_provider_attestation_tests PRIVATE rc95ldr_winload_uefi_entry)
target_include_directories(rc95ldr_uefi_provider_attestation_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../bcd/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../uefi/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../winload/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../fs/include)
set_target_properties(rc95ldr_uefi_provider_attestation_tests PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_uefi_provider_attestation_tests
    COMMAND rc95ldr_uefi_provider_attestation_tests)

add_executable(rc95ldr_winload_btrfs_policy_tests winload_btrfs_policy_tests.c)
target_link_libraries(rc95ldr_winload_btrfs_policy_tests PRIVATE rc95ldr_winload_policy)
target_include_directories(rc95ldr_winload_btrfs_policy_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../winload/include)
set_target_properties(rc95ldr_winload_btrfs_policy_tests PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_winload_btrfs_policy_tests COMMAND rc95ldr_winload_btrfs_policy_tests)

add_executable(rc95ldr_fat32_tests fat32_tests.c)
target_link_libraries(rc95ldr_fat32_tests PRIVATE rc95ldr_fs_fat32)
target_include_directories(rc95ldr_fat32_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../fs/include)
set_target_properties(rc95ldr_fat32_tests PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_fat32_tests COMMAND rc95ldr_fat32_tests)

foreach(_ntfs_test IN ITEMS ntfs_geometry ntfs_runlist ntfs_record ntfs_index ntfs_volume)
    add_executable(rc95ldr_${_ntfs_test}_tests ${CMAKE_CURRENT_SOURCE_DIR}/${_ntfs_test}_tests.c)
    target_link_libraries(rc95ldr_${_ntfs_test}_tests PRIVATE rc95ldr_fs_ntfs_geometry)
    target_include_directories(rc95ldr_${_ntfs_test}_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../fs/include)
    set_target_properties(rc95ldr_${_ntfs_test}_tests PROPERTIES
        C_STANDARD 99 C_STANDARD_REQUIRED ON)
    add_test(NAME rc95ldr_${_ntfs_test}_tests COMMAND rc95ldr_${_ntfs_test}_tests)
endforeach()

# ntfs_volume_tests also exercises the native UEFI mount lifecycle with
# mocked firmware Block I/O, device paths, and pool allocation callbacks.
target_link_libraries(rc95ldr_ntfs_volume_tests PRIVATE rc95ldr_uefi_discovery)
target_include_directories(rc95ldr_ntfs_volume_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../uefi/include
    ${CMAKE_SOURCE_DIR}/sdk/include/reactos/edk2
    ${CMAKE_SOURCE_DIR}/sdk/include)

add_executable(rc95ldr_ntfs_stream_tests ntfs_stream_tests.c)
target_link_libraries(rc95ldr_ntfs_stream_tests PRIVATE rc95ldr_fs_ntfs_geometry)
target_include_directories(rc95ldr_ntfs_stream_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../fs/include)
set_target_properties(rc95ldr_ntfs_stream_tests PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_ntfs_stream_tests COMMAND rc95ldr_ntfs_stream_tests)


add_executable(rc95ldr_pe_tests pe_tests.c)
target_link_libraries(rc95ldr_pe_tests PRIVATE rc95ldr_winload_policy)
target_include_directories(rc95ldr_pe_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../winload/include)
set_target_properties(rc95ldr_pe_tests PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_pe_tests COMMAND rc95ldr_pe_tests)

add_executable(rc95ldr_ntprepare_tests ntprepare_tests.c)
target_link_libraries(rc95ldr_ntprepare_tests PRIVATE rc95ldr_winload_policy)
target_include_directories(rc95ldr_ntprepare_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../winload/include)
set_target_properties(rc95ldr_ntprepare_tests PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_ntprepare_tests COMMAND rc95ldr_ntprepare_tests)

add_executable(rc95ldr_profile_tests profile_tests.c)
target_link_libraries(rc95ldr_profile_tests PRIVATE rc95ldr_winload_policy)
target_include_directories(rc95ldr_profile_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../winload/include)
set_target_properties(rc95ldr_profile_tests PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_profile_tests COMMAND rc95ldr_profile_tests)

add_executable(rc95ldr_osprepare_tests osprepare_tests.c)
target_link_libraries(rc95ldr_osprepare_tests PRIVATE rc95ldr_winload_policy)
target_include_directories(rc95ldr_osprepare_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../winload/include)
set_target_properties(rc95ldr_osprepare_tests PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_osprepare_tests COMMAND rc95ldr_osprepare_tests)

add_executable(rc95ldr_loaderblock_hive_tests loaderblock_hive_tests.c)
target_link_libraries(rc95ldr_loaderblock_hive_tests PRIVATE rc95ldr_winload_policy)
target_include_directories(rc95ldr_loaderblock_hive_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../winload/include)
set_target_properties(rc95ldr_loaderblock_hive_tests PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_loaderblock_hive_tests COMMAND rc95ldr_loaderblock_hive_tests)

add_executable(rc95ldr_handoff_tests handoff_tests.c)
target_link_libraries(rc95ldr_handoff_tests PRIVATE rc95ldr_winload_policy)
target_include_directories(rc95ldr_handoff_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../winload/include)
set_target_properties(rc95ldr_handoff_tests PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_handoff_tests COMMAND rc95ldr_handoff_tests)


add_executable(rc95ldr_platforminventory_tests platforminventory_tests.c)
target_link_libraries(rc95ldr_platforminventory_tests PRIVATE rc95ldr_winload_policy)
target_include_directories(rc95ldr_platforminventory_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../winload/include)
set_target_properties(rc95ldr_platforminventory_tests PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_platforminventory_tests COMMAND rc95ldr_platforminventory_tests)


add_executable(rc95ldr_hiberdelete_tests hiberdelete_tests.c)
target_link_libraries(rc95ldr_hiberdelete_tests PRIVATE rc95ldr_winload_policy)
target_include_directories(rc95ldr_hiberdelete_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../winload/include)
set_target_properties(rc95ldr_hiberdelete_tests PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_hiberdelete_tests COMMAND rc95ldr_hiberdelete_tests)

add_executable(rc95ldr_resume_tests resume_tests.c)
target_link_libraries(rc95ldr_resume_tests PRIVATE rc95ldr_winload_policy)
target_include_directories(rc95ldr_resume_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../winload/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../textui/include)
set_target_properties(rc95ldr_resume_tests PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_resume_tests COMMAND rc95ldr_resume_tests)

add_executable(rc95ldr_memorymap_tests memorymap_tests.c)
target_link_libraries(rc95ldr_memorymap_tests PRIVATE rc95ldr_winload_policy)
target_include_directories(rc95ldr_memorymap_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../winload/include
    ${CMAKE_SOURCE_DIR}/sdk/include)
set_target_properties(rc95ldr_memorymap_tests PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_memorymap_tests COMMAND rc95ldr_memorymap_tests)

add_executable(rc95ldr_firmware_menu_tests firmware_menu_tests.c)
target_link_libraries(rc95ldr_firmware_menu_tests PRIVATE rc95ldr_firmware_menu)
target_include_directories(rc95ldr_firmware_menu_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../bcd/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../textui/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../bgfx/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../firmware/include)
set_target_properties(rc95ldr_firmware_menu_tests PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_firmware_menu_tests COMMAND rc95ldr_firmware_menu_tests)

add_executable(rc95ldr_serviceorder_tests serviceorder_tests.c)
target_link_libraries(rc95ldr_serviceorder_tests PRIVATE rc95ldr_winload_policy)
target_include_directories(rc95ldr_serviceorder_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../winload/include)
set_target_properties(rc95ldr_serviceorder_tests PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_serviceorder_tests COMMAND rc95ldr_serviceorder_tests)

add_executable(rc95ldr_winload_entry_tests winload_entry_tests.c)
target_link_libraries(rc95ldr_winload_entry_tests PRIVATE
    rc95ldr_winload_bios_entry rc95ldr_winload_uefi_entry
    rc95ldr_winload_entry rc95ldr_winload_policy)
target_include_directories(rc95ldr_winload_entry_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../winload/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include)
set_target_properties(rc95ldr_winload_entry_tests PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_winload_entry_tests COMMAND rc95ldr_winload_entry_tests)

# BIOS firmware service and startup-contract regression coverage.
add_executable(rc95ldr_bios_firmware_state_tests bios_firmware_state_tests.c)
target_link_libraries(rc95ldr_bios_firmware_state_tests PRIVATE rc95ldr_bios_platform)
target_include_directories(rc95ldr_bios_firmware_state_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../bios/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../winload/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include)
set_target_properties(rc95ldr_bios_firmware_state_tests PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_bios_firmware_state_tests
    COMMAND rc95ldr_bios_firmware_state_tests)

# BIOS BGFX framebuffer descriptor lifecycle and mapping validation.
add_executable(rc95ldr_bios_video_tests bios_video_tests.c)
target_link_libraries(rc95ldr_bios_video_tests PRIVATE rc95ldr_bios_platform)
target_include_directories(rc95ldr_bios_video_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../bios/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../bgfx/include)
set_target_properties(rc95ldr_bios_video_tests PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_bios_video_tests COMMAND rc95ldr_bios_video_tests)

add_executable(rc95ldr_bios_tests bios_tests.c)
target_link_libraries(rc95ldr_bios_tests PRIVATE
    rc95ldr_bios_stage rc95ldr_bios_platform rc95ldr_winload_bios_entry)
target_include_directories(rc95ldr_bios_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../bios/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../textui/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../winload/include)
set_target_properties(rc95ldr_bios_tests PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_bios_tests COMMAND rc95ldr_bios_tests)

add_executable(rc95ldr_bios_disk_tests bios_disk_tests.c)
target_link_libraries(rc95ldr_bios_disk_tests PRIVATE
    rc95ldr_bios_platform rc95ldr_fs_fat32)
target_include_directories(rc95ldr_bios_disk_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../bios/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../winload/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../fs/include)
set_target_properties(rc95ldr_bios_disk_tests PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_bios_disk_tests COMMAND rc95ldr_bios_disk_tests)

# Exercise the host-side BIOS image packager when Python is available.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME rc95ldr_bios_image_packer_tests
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/bios_image_packer_tests.py)
    add_test(NAME rc95ldr_bios_stage0_tests
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/bios_stage0_tests.py)
    add_test(NAME rc95ldr_bios_cd_stage0_tests
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/bios_cd_stage0_tests.py)
endif()

# El Torito boot-drive identification through the RC95LDR BIOS gateway.
add_executable(rc95ldr_bios_cd_tests bios_cd_tests.c)
target_link_libraries(rc95ldr_bios_cd_tests PRIVATE rc95ldr_bios_platform)
target_include_directories(rc95ldr_bios_cd_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../bios/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../winload/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../textui/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../fs/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../bcd/include)
set_target_properties(rc95ldr_bios_cd_tests PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_bios_cd_tests COMMAND rc95ldr_bios_cd_tests)

add_executable(rc95ldr_iso9660_tests iso9660_tests.c)
target_link_libraries(rc95ldr_iso9660_tests PRIVATE rc95ldr_fs_iso9660)
target_include_directories(rc95ldr_iso9660_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../fs/include)
set_target_properties(rc95ldr_iso9660_tests PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_iso9660_tests COMMAND rc95ldr_iso9660_tests)

add_executable(rc95ldr_bgfx_tests bgfx_tests.c)
target_link_libraries(rc95ldr_bgfx_tests PRIVATE rc95ldr_bgfx)
target_include_directories(rc95ldr_bgfx_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../textui/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../bgfx/include)
set_target_properties(rc95ldr_bgfx_tests PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_bgfx_tests COMMAND rc95ldr_bgfx_tests)

# Keep module and firmware-entry integration tests in the full-tree build as
# well as the standalone host build. These targets force the final link step,
# catching missing source registration and unresolved cross-library symbols.
add_executable(rc95ldr_module_services_tests module_services_tests.c)
target_link_libraries(rc95ldr_module_services_tests PRIVATE rc95ldr_core)
target_include_directories(rc95ldr_module_services_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include)
set_target_properties(rc95ldr_module_services_tests PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_module_services_tests COMMAND rc95ldr_module_services_tests)

add_executable(rc95ldr_module_handoff_tests module_handoff_tests.c)
target_link_libraries(rc95ldr_module_handoff_tests PRIVATE rc95ldr_core)
target_include_directories(rc95ldr_module_handoff_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include)
set_target_properties(rc95ldr_module_handoff_tests PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_module_handoff_tests COMMAND rc95ldr_module_handoff_tests)

add_executable(rc95ldr_uefi_entry_tests uefi_entry_tests.c)
target_link_libraries(rc95ldr_uefi_entry_tests PRIVATE rc95ldr_uefi_discovery)
target_include_directories(rc95ldr_uefi_entry_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../uefi/include
    ${CMAKE_SOURCE_DIR}/sdk/include/reactos/edk2
    ${CMAKE_SOURCE_DIR}/sdk/include)
set_target_properties(rc95ldr_uefi_entry_tests PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_uefi_entry_tests COMMAND rc95ldr_uefi_entry_tests)

add_executable(rc95ldr_uefi_services_tests uefi_services_tests.c)
target_link_libraries(rc95ldr_uefi_services_tests PRIVATE rc95ldr_uefi_discovery)
target_include_directories(rc95ldr_uefi_services_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../uefi/include
    ${CMAKE_SOURCE_DIR}/sdk/include/reactos/edk2
    ${CMAKE_SOURCE_DIR}/sdk/include)
set_target_properties(rc95ldr_uefi_services_tests PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_uefi_services_tests COMMAND rc95ldr_uefi_services_tests)

add_executable(rc95ldr_uefi_video_tests uefi_video_tests.c)
target_link_libraries(rc95ldr_uefi_video_tests PRIVATE rc95ldr_uefi_discovery)
target_include_directories(rc95ldr_uefi_video_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../uefi/include
    ${CMAKE_SOURCE_DIR}/sdk/include/reactos/edk2
    ${CMAKE_SOURCE_DIR}/sdk/include)
set_target_properties(rc95ldr_uefi_video_tests PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_uefi_video_tests COMMAND rc95ldr_uefi_video_tests)

add_executable(rc95ldr_uefi_memory_tests uefi_memory_tests.c)
target_link_libraries(rc95ldr_uefi_memory_tests PRIVATE rc95ldr_uefi_discovery)
target_include_directories(rc95ldr_uefi_memory_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../uefi/include
    ${CMAKE_SOURCE_DIR}/sdk/include/reactos/edk2
    ${CMAKE_SOURCE_DIR}/sdk/include)
set_target_properties(rc95ldr_uefi_memory_tests PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_uefi_memory_tests COMMAND rc95ldr_uefi_memory_tests)

add_executable(rc95ldr_bios_module_tests bios_module_tests.c)
target_link_libraries(rc95ldr_bios_module_tests PRIVATE rc95ldr_bios_platform)
target_include_directories(rc95ldr_bios_module_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../bios/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../winload/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../textui/include
    ${CMAKE_SOURCE_DIR}/sdk/include)
set_target_properties(rc95ldr_bios_module_tests PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_bios_module_tests COMMAND rc95ldr_bios_module_tests)


# Native UEFI discovery/boot orchestration argument and lifecycle contracts.
add_executable(rc95ldr_uefi_nativeboot_tests uefi_nativeboot_tests.c)
target_link_libraries(rc95ldr_uefi_nativeboot_tests PRIVATE
    rc95ldr_winload_uefi_entry)
target_include_directories(rc95ldr_uefi_nativeboot_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../uefi/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../winload/include
    ${CMAKE_SOURCE_DIR}/sdk/include/reactos/edk2
    ${CMAKE_SOURCE_DIR}/sdk/include)
set_target_properties(rc95ldr_uefi_nativeboot_tests PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_uefi_nativeboot_tests COMMAND rc95ldr_uefi_nativeboot_tests)

add_executable(rc95ldr_module_services_v2_tests module_services_v2_tests.c)
target_link_libraries(rc95ldr_module_services_v2_tests PRIVATE rc95ldr_core)
target_include_directories(rc95ldr_module_services_v2_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include)
set_target_properties(rc95ldr_module_services_v2_tests PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_module_services_v2_tests COMMAND rc95ldr_module_services_v2_tests)

add_executable(rc95ldr_uefi_image_entry_tests uefi_image_entry_tests.c)
target_link_libraries(rc95ldr_uefi_image_entry_tests PRIVATE
    rc95ldr_uefi_image_entry)
target_include_directories(rc95ldr_uefi_image_entry_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../uefi/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../winload/include
    ${CMAKE_SOURCE_DIR}/sdk/include/reactos/edk2
    ${CMAKE_SOURCE_DIR}/sdk/include)
set_target_properties(rc95ldr_uefi_image_entry_tests PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_uefi_image_entry_tests
    COMMAND rc95ldr_uefi_image_entry_tests)

add_executable(rc95ldr_bios_ntfsboot_tests bios_ntfsboot_tests.c)
target_link_libraries(rc95ldr_bios_ntfsboot_tests PRIVATE
    rc95ldr_winload_bios_entry)
target_include_directories(rc95ldr_bios_ntfsboot_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../bios/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../fs/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../winload/include)
set_target_properties(rc95ldr_bios_ntfsboot_tests PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_bios_ntfsboot_tests
    COMMAND rc95ldr_bios_ntfsboot_tests)

add_executable(rc95ldr_bios_btrfsboot_tests bios_btrfsboot_tests.c)
target_link_libraries(rc95ldr_bios_btrfsboot_tests PRIVATE
    rc95ldr_winload_bios_entry)
target_include_directories(rc95ldr_bios_btrfsboot_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../bios/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../winload/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../fs/include)
set_target_properties(rc95ldr_bios_btrfsboot_tests PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_bios_btrfsboot_tests
    COMMAND rc95ldr_bios_btrfsboot_tests)

add_executable(rc95ldr_bios_btrfsvalidate_tests bios_btrfsvalidate_tests.c)
target_link_libraries(rc95ldr_bios_btrfsvalidate_tests PRIVATE
    rc95ldr_bios_platform)
target_include_directories(rc95ldr_bios_btrfsvalidate_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../bios/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../winload/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../fs/include)
set_target_properties(rc95ldr_bios_btrfsvalidate_tests PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_bios_btrfsvalidate_tests
    COMMAND rc95ldr_bios_btrfsvalidate_tests)

add_executable(rc95ldr_wim_provider_tests wim_provider_tests.c)
target_link_libraries(rc95ldr_wim_provider_tests PRIVATE rc95ldr_fs_wim)
target_include_directories(rc95ldr_wim_provider_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../fs/include
    ${CMAKE_SOURCE_DIR}/sdk/include)
set_target_properties(rc95ldr_wim_provider_tests PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_wim_provider_tests COMMAND rc95ldr_wim_provider_tests)

add_executable(rc95ldr_rc95nativeprovider_tests rc95nativeprovider_tests.c)
target_link_libraries(rc95ldr_rc95nativeprovider_tests PRIVATE
    rc95ldr_winload_policy)
target_include_directories(rc95ldr_rc95nativeprovider_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../winload/include)
set_target_properties(rc95ldr_rc95nativeprovider_tests PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_rc95nativeprovider_tests
    COMMAND rc95ldr_rc95nativeprovider_tests)

add_executable(rc95ldr_cpuprobe_tests cpuprobe_tests.c)
target_link_libraries(rc95ldr_cpuprobe_tests PRIVATE rc95ldr_winload_policy)
target_include_directories(rc95ldr_cpuprobe_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../winload/include)
set_target_properties(rc95ldr_cpuprobe_tests PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_cpuprobe_tests COMMAND rc95ldr_cpuprobe_tests)

add_executable(rc95ldr_paewindow_tests paewindow_tests.c)
target_link_libraries(rc95ldr_paewindow_tests PRIVATE rc95ldr_winload_policy)
target_include_directories(rc95ldr_paewindow_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../winload/include)
set_target_properties(rc95ldr_paewindow_tests PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_paewindow_tests COMMAND rc95ldr_paewindow_tests)


# Full UEFI BCD discovery path with a mocked ESP and exact Btrfs osdevice.
add_executable(rc95ldr_uefi_discovery_tests uefi_discovery_tests.c)
target_link_libraries(rc95ldr_uefi_discovery_tests PRIVATE rc95ldr_uefi_discovery)
target_include_directories(rc95ldr_uefi_discovery_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../bcd/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../winload/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../uefi/include)
set_target_properties(rc95ldr_uefi_discovery_tests PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_uefi_discovery_tests
    COMMAND rc95ldr_uefi_discovery_tests)
