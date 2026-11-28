cmake_minimum_required(VERSION 3.16)
project(RC95LDR_Standalone C)

enable_testing()

# Build the same libwim sources used by the full tree so the standalone
# RC95LDR host suite exercises the production WIM reader and codecs.
if(NOT DEFINED REACTOS_SOURCE_DIR)
    get_filename_component(REACTOS_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
endif()
add_subdirectory(${REACTOS_SOURCE_DIR}/sdk/lib/libwim
                 ${CMAKE_CURRENT_BINARY_DIR}/libwim)

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../core
                 ${CMAKE_CURRENT_BINARY_DIR}/rc95ldr_core)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../bcd
                 ${CMAKE_CURRENT_BINARY_DIR}/rc95ldr_bcd)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../textui
                 ${CMAKE_CURRENT_BINARY_DIR}/rc95ldr_textui)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../winload
                 ${CMAKE_CURRENT_BINARY_DIR}/rc95ldr_winload)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../veneer
                 ${CMAKE_CURRENT_BINARY_DIR}/rc95ldr_veneer)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../fs
                 ${CMAKE_CURRENT_BINARY_DIR}/rc95ldr_fs)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../bgfx
                 ${CMAKE_CURRENT_BINARY_DIR}/rc95ldr_bgfx)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../firmware
                 ${CMAKE_CURRENT_BINARY_DIR}/rc95ldr_firmware)

# Keep the privileged resume transition out of host executables while still
# resolving the policy object's architecture hook for link-complete tests.
target_sources(rc95ldr_winload_policy PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/resume_stub.c)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../uefi
                 ${CMAKE_CURRENT_BINARY_DIR}/rc95ldr_uefi)
# UEFI headers consume NT base types supplied by the generated SDK in
# production builds; the standalone host project substitutes its narrow shim.
target_include_directories(rc95ldr_uefi_discovery BEFORE PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/compat)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../bios
                 ${CMAKE_CURRENT_BINARY_DIR}/rc95ldr_bios)
target_include_directories(rc95ldr_bios_platform BEFORE PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/compat)

# The isolated host-test project does not build the SDK's generated xdk
# headers. Supply only the fixed-width NT primitive types needed by ARC
# layout headers; production builds continue to use generated SDK ntdef.h.
target_include_directories(rc95ldr_winload_policy BEFORE PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/compat)

add_executable(rc95ldr_ramdisk_tests ../ramdisk_tests.c)
target_link_libraries(rc95ldr_ramdisk_tests PRIVATE rc95ldr_core)
target_include_directories(rc95ldr_ramdisk_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../core/include)
set_target_properties(rc95ldr_ramdisk_tests PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_ramdisk_tests COMMAND rc95ldr_ramdisk_tests)

add_executable(rc95ldr_module_services_tests ../module_services_tests.c)
target_link_libraries(rc95ldr_module_services_tests PRIVATE rc95ldr_core)
target_include_directories(rc95ldr_module_services_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../core/include)
set_target_properties(rc95ldr_module_services_tests PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_module_services_tests COMMAND rc95ldr_module_services_tests)

add_executable(rc95ldr_volumeprovider_tests ../volumeprovider_tests.c)
target_link_libraries(rc95ldr_volumeprovider_tests PRIVATE rc95ldr_core)
target_include_directories(rc95ldr_volumeprovider_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../core/include)
set_target_properties(rc95ldr_volumeprovider_tests PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_volumeprovider_tests COMMAND rc95ldr_volumeprovider_tests)

add_executable(rc95ldr_discovery_tests ../discovery_tests.c)
target_link_libraries(rc95ldr_discovery_tests PRIVATE rc95ldr_core)
target_include_directories(rc95ldr_discovery_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../core/include)
set_target_properties(rc95ldr_discovery_tests PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_discovery_tests COMMAND rc95ldr_discovery_tests)

add_executable(rc95ldr_bcd_tests ../bcd_tests.c)
target_link_libraries(rc95ldr_bcd_tests PRIVATE rc95ldr_bcd)
target_include_directories(rc95ldr_bcd_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../core/include
    ${CMAKE_CURRENT_LIST_DIR}/../../bcd/include)
set_target_properties(rc95ldr_bcd_tests PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON)

add_test(NAME rc95ldr_bcd_tests COMMAND rc95ldr_bcd_tests)

add_executable(rc95ldr_loaderblock_hive_tests ../loaderblock_hive_tests.c)
target_link_libraries(rc95ldr_loaderblock_hive_tests PRIVATE rc95ldr_winload_policy)
target_include_directories(rc95ldr_loaderblock_hive_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../core/include
    ${CMAKE_CURRENT_LIST_DIR}/../../bcd/include
    ${CMAKE_CURRENT_LIST_DIR}/../../textui/include
    ${CMAKE_CURRENT_LIST_DIR}/../../winload/include
    ${CMAKE_CURRENT_LIST_DIR}/../../../../sdk/include)
set_target_properties(rc95ldr_loaderblock_hive_tests PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON)

add_test(NAME rc95ldr_loaderblock_hive_tests COMMAND rc95ldr_loaderblock_hive_tests)

add_executable(rc95ldr_serviceorder_tests ../serviceorder_tests.c)
target_link_libraries(rc95ldr_serviceorder_tests PRIVATE rc95ldr_winload_policy)
target_include_directories(rc95ldr_serviceorder_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../core/include
    ${CMAKE_CURRENT_LIST_DIR}/../../winload/include)
set_target_properties(rc95ldr_serviceorder_tests PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON)

add_test(NAME rc95ldr_serviceorder_tests COMMAND rc95ldr_serviceorder_tests)

add_executable(rc95ldr_textui_tests ../textui_tests.c)
target_link_libraries(rc95ldr_textui_tests PRIVATE rc95ldr_textui)
target_include_directories(rc95ldr_textui_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../core/include
    ${CMAKE_CURRENT_LIST_DIR}/../../textui/include)
set_target_properties(rc95ldr_textui_tests PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON)

add_test(NAME rc95ldr_textui_tests COMMAND rc95ldr_textui_tests)

# Exercise the native framebuffer renderer in the standalone host build.
add_executable(rc95ldr_bgfx_tests ../bgfx_tests.c)
target_link_libraries(rc95ldr_bgfx_tests PRIVATE rc95ldr_bgfx)
target_include_directories(rc95ldr_bgfx_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../core/include
    ${CMAKE_CURRENT_LIST_DIR}/../../textui/include
    ${CMAKE_CURRENT_LIST_DIR}/../../bgfx/include)
set_target_properties(rc95ldr_bgfx_tests PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_bgfx_tests COMMAND rc95ldr_bgfx_tests)

# Exercise BCD-derived firmware boot-menu policy in the standalone host build.
add_executable(rc95ldr_firmware_menu_tests ../firmware_menu_tests.c)
target_link_libraries(rc95ldr_firmware_menu_tests PRIVATE rc95ldr_firmware_menu)
target_include_directories(rc95ldr_firmware_menu_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../core/include
    ${CMAKE_CURRENT_LIST_DIR}/../../bcd/include
    ${CMAKE_CURRENT_LIST_DIR}/../../textui/include
    ${CMAKE_CURRENT_LIST_DIR}/../../bgfx/include
    ${CMAKE_CURRENT_LIST_DIR}/../../firmware/include)
set_target_properties(rc95ldr_firmware_menu_tests PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_firmware_menu_tests COMMAND rc95ldr_firmware_menu_tests)


add_executable(rc95ldr_pe_tests ../pe_tests.c)
target_link_libraries(rc95ldr_pe_tests PRIVATE rc95ldr_winload_policy)
target_include_directories(rc95ldr_pe_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../core/include
    ${CMAKE_CURRENT_LIST_DIR}/../../bcd/include
    ${CMAKE_CURRENT_LIST_DIR}/../../textui/include
    ${CMAKE_CURRENT_LIST_DIR}/../../winload/include
    ${CMAKE_CURRENT_LIST_DIR}/../../../../sdk/include)
set_target_properties(rc95ldr_pe_tests PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_pe_tests COMMAND rc95ldr_pe_tests)

add_executable(rc95ldr_uefi_disk_tests ../uefi_disk_tests.c)
target_link_libraries(rc95ldr_uefi_disk_tests PRIVATE rc95ldr_uefi_discovery)
target_include_directories(rc95ldr_uefi_disk_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../core/include
    ${CMAKE_CURRENT_LIST_DIR}/../../uefi/include)
set_target_properties(rc95ldr_uefi_disk_tests PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON)

add_test(NAME rc95ldr_uefi_disk_tests COMMAND rc95ldr_uefi_disk_tests)

add_executable(rc95ldr_uefi_volume_tests ../uefi_volume_tests.c)
target_link_libraries(rc95ldr_uefi_volume_tests PRIVATE rc95ldr_uefi_discovery)
target_include_directories(rc95ldr_uefi_volume_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../core/include
    ${CMAKE_CURRENT_LIST_DIR}/../../uefi/include
    ${CMAKE_CURRENT_LIST_DIR}/../../fs/include)
set_target_properties(rc95ldr_uefi_volume_tests PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_uefi_volume_tests COMMAND rc95ldr_uefi_volume_tests)

add_executable(rc95ldr_uefi_discovery_tests ../uefi_discovery_tests.c)
target_link_libraries(rc95ldr_uefi_discovery_tests PRIVATE rc95ldr_uefi_discovery)
target_include_directories(rc95ldr_uefi_discovery_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../core/include
    ${CMAKE_CURRENT_LIST_DIR}/../../bcd/include
    ${CMAKE_CURRENT_LIST_DIR}/../../winload/include
    ${CMAKE_CURRENT_LIST_DIR}/../../uefi/include)
set_target_properties(rc95ldr_uefi_discovery_tests PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_uefi_discovery_tests COMMAND rc95ldr_uefi_discovery_tests)


add_executable(rc95ldr_fat32_tests ../fat32_tests.c)
target_link_libraries(rc95ldr_fat32_tests PRIVATE rc95ldr_fs_fat32)
target_include_directories(rc95ldr_fat32_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../core/include
    ${CMAKE_CURRENT_LIST_DIR}/../../fs/include)
set_target_properties(rc95ldr_fat32_tests PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_fat32_tests COMMAND rc95ldr_fat32_tests)

add_executable(rc95ldr_ntfs_geometry_tests ../ntfs_geometry_tests.c)
target_link_libraries(rc95ldr_ntfs_geometry_tests PRIVATE rc95ldr_fs_ntfs_geometry)
target_include_directories(rc95ldr_ntfs_geometry_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../core/include
    ${CMAKE_CURRENT_LIST_DIR}/../../fs/include)
set_target_properties(rc95ldr_ntfs_geometry_tests PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_ntfs_geometry_tests COMMAND rc95ldr_ntfs_geometry_tests)

add_executable(rc95ldr_ntfs_volume_tests ../ntfs_volume_tests.c)
target_link_libraries(rc95ldr_ntfs_volume_tests PRIVATE
    rc95ldr_fs_ntfs_geometry rc95ldr_uefi_discovery)
target_include_directories(rc95ldr_ntfs_volume_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../core/include
    ${CMAKE_CURRENT_LIST_DIR}/../../fs/include)
set_target_properties(rc95ldr_ntfs_volume_tests PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_ntfs_volume_tests COMMAND rc95ldr_ntfs_volume_tests)

add_executable(rc95ldr_ntfs_record_tests ../ntfs_record_tests.c)
target_link_libraries(rc95ldr_ntfs_record_tests PRIVATE rc95ldr_fs_ntfs_geometry)
target_include_directories(rc95ldr_ntfs_record_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../core/include
    ${CMAKE_CURRENT_LIST_DIR}/../../fs/include)
set_target_properties(rc95ldr_ntfs_record_tests PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_ntfs_record_tests COMMAND rc95ldr_ntfs_record_tests)

add_executable(rc95ldr_ntfs_runlist_tests ../ntfs_runlist_tests.c)
target_link_libraries(rc95ldr_ntfs_runlist_tests PRIVATE rc95ldr_fs_ntfs_geometry)
target_include_directories(rc95ldr_ntfs_runlist_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../core/include
    ${CMAKE_CURRENT_LIST_DIR}/../../fs/include)
set_target_properties(rc95ldr_ntfs_runlist_tests PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_ntfs_runlist_tests COMMAND rc95ldr_ntfs_runlist_tests)

add_executable(rc95ldr_ntfs_stream_tests ../ntfs_stream_tests.c)
target_link_libraries(rc95ldr_ntfs_stream_tests PRIVATE rc95ldr_fs_ntfs_geometry)
target_include_directories(rc95ldr_ntfs_stream_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../core/include
    ${CMAKE_CURRENT_LIST_DIR}/../../fs/include)
set_target_properties(rc95ldr_ntfs_stream_tests PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_ntfs_stream_tests COMMAND rc95ldr_ntfs_stream_tests)


# Exercise the remaining winload policy objects in the standalone host build.
# Linking these executables also catches missing object/source registrations
# that would otherwise remain hidden inside a static archive.
function(rc95ldr_add_winload_host_test target source)
    add_executable(${target} ../${source})
    target_link_libraries(${target} PRIVATE rc95ldr_winload_policy)
    target_include_directories(${target} PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/../../core/include
        ${CMAKE_CURRENT_LIST_DIR}/../../bcd/include
        ${CMAKE_CURRENT_LIST_DIR}/../../textui/include
        ${CMAKE_CURRENT_LIST_DIR}/../../winload/include
        ${CMAKE_CURRENT_LIST_DIR}/../../../../sdk/include)
    set_target_properties(${target} PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED ON)
    add_test(NAME ${target} COMMAND ${target})
endfunction()

rc95ldr_add_winload_host_test(rc95ldr_bcd_policy_tests bcd_policy_tests.c)
rc95ldr_add_winload_host_test(rc95ldr_dependency_tests dependency_tests.c)
rc95ldr_add_winload_host_test(rc95ldr_handoff_tests handoff_tests.c)
rc95ldr_add_winload_host_test(rc95ldr_rc95nativeprovider_tests rc95nativeprovider_tests.c)
rc95ldr_add_winload_host_test(rc95ldr_ntprepare_tests ntprepare_tests.c)
rc95ldr_add_winload_host_test(rc95ldr_osprepare_tests osprepare_tests.c)
rc95ldr_add_winload_host_test(rc95ldr_profile_tests profile_tests.c)
rc95ldr_add_winload_host_test(rc95ldr_platforminventory_tests platforminventory_tests.c)
rc95ldr_add_winload_host_test(rc95ldr_resume_tests resume_tests.c)
rc95ldr_add_winload_host_test(rc95ldr_memorymap_tests memorymap_tests.c)

# Exercise the optional bootres module through the production admission policy
# and into the RC95 native loader-block load-order list.
rc95ldr_add_winload_host_test(rc95ldr_bootres_handoff_tests bootres_handoff_tests.c)

add_executable(rc95ldr_ntfs_index_tests ../ntfs_index_tests.c)
target_link_libraries(rc95ldr_ntfs_index_tests PRIVATE rc95ldr_fs_ntfs_geometry)
target_include_directories(rc95ldr_ntfs_index_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../core/include
    ${CMAKE_CURRENT_LIST_DIR}/../../fs/include)
set_target_properties(rc95ldr_ntfs_index_tests PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_ntfs_index_tests COMMAND rc95ldr_ntfs_index_tests)

add_executable(rc95ldr_winload_entry_tests ../winload_entry_tests.c)
target_link_libraries(rc95ldr_winload_entry_tests PRIVATE
    rc95ldr_winload_bios_entry rc95ldr_winload_uefi_entry
    rc95ldr_winload_entry rc95ldr_winload_policy)
target_include_directories(rc95ldr_winload_entry_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../core/include
    ${CMAKE_CURRENT_LIST_DIR}/../../winload/include)
set_target_properties(rc95ldr_winload_entry_tests PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_winload_entry_tests COMMAND rc95ldr_winload_entry_tests)

add_executable(rc95ldr_bios_firmware_state_tests ../bios_firmware_state_tests.c)
target_link_libraries(rc95ldr_bios_firmware_state_tests PRIVATE rc95ldr_bios_platform)
target_include_directories(rc95ldr_bios_firmware_state_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../bios/include
    ${CMAKE_CURRENT_LIST_DIR}/../../winload/include
    ${CMAKE_CURRENT_LIST_DIR}/../../core/include)
set_target_properties(rc95ldr_bios_firmware_state_tests PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_bios_firmware_state_tests
    COMMAND rc95ldr_bios_firmware_state_tests)

add_executable(rc95ldr_bios_tests ../bios_tests.c)
target_link_libraries(rc95ldr_bios_tests PRIVATE
    rc95ldr_bios_stage rc95ldr_bios_platform rc95ldr_winload_bios_entry)
target_include_directories(rc95ldr_bios_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../bios/include
    ${CMAKE_CURRENT_LIST_DIR}/../../winload/include
    ${CMAKE_CURRENT_LIST_DIR}/../../core/include)
set_target_properties(rc95ldr_bios_tests PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_bios_tests COMMAND rc95ldr_bios_tests)

# Run the same ownership check in the standalone host build. This catches
# accidental legacy imports even when the full ReactOS build is not configured.
add_test(NAME rc95ldr_dependency_boundary
    COMMAND ${CMAKE_COMMAND}
        -DRC95LDR_SOURCE_DIR=${CMAKE_CURRENT_LIST_DIR}/../..
        -P ${CMAKE_CURRENT_LIST_DIR}/../check_dependency_boundary.cmake)

add_executable(rc95ldr_iso9660_tests ../iso9660_tests.c)
target_link_libraries(rc95ldr_iso9660_tests PRIVATE rc95ldr_fs_iso9660)
target_include_directories(rc95ldr_iso9660_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../core/include
    ${CMAKE_CURRENT_LIST_DIR}/../../fs/include)
set_target_properties(rc95ldr_iso9660_tests PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_iso9660_tests COMMAND rc95ldr_iso9660_tests)

add_executable(rc95ldr_bios_cd_tests ../bios_cd_tests.c)
target_link_libraries(rc95ldr_bios_cd_tests PRIVATE rc95ldr_bios_platform)
target_include_directories(rc95ldr_bios_cd_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../bios/include
    ${CMAKE_CURRENT_LIST_DIR}/../../core/include
    ${CMAKE_CURRENT_LIST_DIR}/../../fs/include)
set_target_properties(rc95ldr_bios_cd_tests PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_bios_cd_tests COMMAND rc95ldr_bios_cd_tests)

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME rc95ldr_bios_cd_stage0_tests
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_LIST_DIR}/../bios_cd_stage0_tests.py)
endif()

if(Python3_Interpreter_FOUND)
    add_test(NAME rc95ldr_bios_stage0_tests
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_LIST_DIR}/../bios_stage0_tests.py)
    add_test(NAME rc95ldr_bios_image_packer_tests
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_LIST_DIR}/../bios_image_packer_tests.py)
endif()

# Exercise the standalone BTRFS shim and the WINLOAD filesystem attestation.
add_executable(rc95ldr_veneer_btrfs_tests ../veneer_btrfs_tests.c)
target_link_libraries(rc95ldr_veneer_btrfs_tests PRIVATE rc95ldr_veneer)
target_include_directories(rc95ldr_veneer_btrfs_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../core/include
    ${CMAKE_CURRENT_LIST_DIR}/../../veneer/include)
set_target_properties(rc95ldr_veneer_btrfs_tests PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_veneer_btrfs_tests COMMAND rc95ldr_veneer_btrfs_tests)

add_executable(rc95ldr_btrfs_volume_tests ../btrfs_volume_tests.c)
target_link_libraries(rc95ldr_btrfs_volume_tests PRIVATE
    rc95ldr_fs_btrfs rc95ldr_uefi_discovery rc95ldr_winload_policy
    rc95ldr_winload_uefi_entry)
target_include_directories(rc95ldr_btrfs_volume_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../core/include
    ${CMAKE_CURRENT_LIST_DIR}/../../fs/include
    ${CMAKE_CURRENT_LIST_DIR}/../../veneer/include
    ${CMAKE_CURRENT_LIST_DIR}/../../uefi/include
    ${CMAKE_CURRENT_LIST_DIR}/../../winload/include
    ${CMAKE_CURRENT_LIST_DIR}/../../bcd/include
    ${CMAKE_CURRENT_LIST_DIR}/../../../../sdk/include/reactos/edk2
    ${CMAKE_CURRENT_LIST_DIR}/../../../../sdk/include)
set_target_properties(rc95ldr_btrfs_volume_tests PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_btrfs_volume_tests COMMAND rc95ldr_btrfs_volume_tests)

# The BIOS Btrfs Windows validator is a generic read-only volume consumer;
# compile it in the standalone host suite with a mocked filesystem.
add_executable(rc95ldr_bios_btrfsvalidate_tests
    ../bios_btrfsvalidate_tests.c
    ../../bios/btrfsvalidate.c)
target_link_libraries(rc95ldr_bios_btrfsvalidate_tests PRIVATE rc95ldr_core)
target_include_directories(rc95ldr_bios_btrfsvalidate_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../bios/include
    ${CMAKE_CURRENT_LIST_DIR}/../../core/include
    ${CMAKE_CURRENT_LIST_DIR}/../../fs/include
    ${CMAKE_CURRENT_LIST_DIR}/../../bcd/include
    ${CMAKE_CURRENT_LIST_DIR}/../../winload/include)
set_target_properties(rc95ldr_bios_btrfsvalidate_tests PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_bios_btrfsvalidate_tests
    COMMAND rc95ldr_bios_btrfsvalidate_tests)

add_executable(rc95ldr_winload_btrfs_policy_tests ../winload_btrfs_policy_tests.c)
target_link_libraries(rc95ldr_winload_btrfs_policy_tests PRIVATE rc95ldr_winload_policy)
target_include_directories(rc95ldr_winload_btrfs_policy_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../core/include
    ${CMAKE_CURRENT_LIST_DIR}/../../winload/include)
set_target_properties(rc95ldr_winload_btrfs_policy_tests PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_winload_btrfs_policy_tests COMMAND rc95ldr_winload_btrfs_policy_tests)

add_executable(rc95ldr_module_handoff_tests ../module_handoff_tests.c)
target_link_libraries(rc95ldr_module_handoff_tests PRIVATE rc95ldr_core)
target_include_directories(rc95ldr_module_handoff_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../core/include)
set_target_properties(rc95ldr_module_handoff_tests PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_module_handoff_tests COMMAND rc95ldr_module_handoff_tests)

add_executable(rc95ldr_uefi_entry_tests ../uefi_entry_tests.c)
target_link_libraries(rc95ldr_uefi_entry_tests PRIVATE rc95ldr_uefi_discovery)
target_include_directories(rc95ldr_uefi_entry_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../core/include
    ${CMAKE_CURRENT_LIST_DIR}/../../uefi/include
    ${CMAKE_CURRENT_LIST_DIR}/../../../../sdk/include/reactos/edk2
    ${CMAKE_CURRENT_LIST_DIR}/../../../../sdk/include)
set_target_properties(rc95ldr_uefi_entry_tests PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_uefi_entry_tests COMMAND rc95ldr_uefi_entry_tests)

add_executable(rc95ldr_uefi_services_tests ../uefi_services_tests.c)
target_link_libraries(rc95ldr_uefi_services_tests PRIVATE rc95ldr_uefi_discovery)
target_include_directories(rc95ldr_uefi_services_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../core/include
    ${CMAKE_CURRENT_LIST_DIR}/../../uefi/include
    ${CMAKE_CURRENT_LIST_DIR}/../../../../sdk/include/reactos/edk2
    ${CMAKE_CURRENT_LIST_DIR}/../../../../sdk/include)
set_target_properties(rc95ldr_uefi_services_tests PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_uefi_services_tests COMMAND rc95ldr_uefi_services_tests)

add_executable(rc95ldr_uefi_video_tests ../uefi_video_tests.c)
target_link_libraries(rc95ldr_uefi_video_tests PRIVATE rc95ldr_uefi_discovery)
target_include_directories(rc95ldr_uefi_video_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../core/include
    ${CMAKE_CURRENT_LIST_DIR}/../../uefi/include
    ${CMAKE_CURRENT_LIST_DIR}/../../../../sdk/include/reactos/edk2
    ${CMAKE_CURRENT_LIST_DIR}/../../../../sdk/include)
set_target_properties(rc95ldr_uefi_video_tests PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_uefi_video_tests COMMAND rc95ldr_uefi_video_tests)

add_executable(rc95ldr_uefi_memory_tests ../uefi_memory_tests.c)
target_link_libraries(rc95ldr_uefi_memory_tests PRIVATE rc95ldr_uefi_discovery)
target_include_directories(rc95ldr_uefi_memory_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../core/include
    ${CMAKE_CURRENT_LIST_DIR}/../../uefi/include
    ${CMAKE_CURRENT_LIST_DIR}/../../../../sdk/include/reactos/edk2
    ${CMAKE_CURRENT_LIST_DIR}/../../../../sdk/include)
set_target_properties(rc95ldr_uefi_memory_tests PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_uefi_memory_tests COMMAND rc95ldr_uefi_memory_tests)

add_executable(rc95ldr_bios_btrfsboot_tests ../bios_btrfsboot_tests.c)
target_link_libraries(rc95ldr_bios_btrfsboot_tests PRIVATE
    rc95ldr_winload_bios_entry)
target_include_directories(rc95ldr_bios_btrfsboot_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../bios/include
    ${CMAKE_CURRENT_LIST_DIR}/../../core/include
    ${CMAKE_CURRENT_LIST_DIR}/../../winload/include
    ${CMAKE_CURRENT_LIST_DIR}/../../fs/include
    ${CMAKE_CURRENT_LIST_DIR}/../../bcd/include)
set_target_properties(rc95ldr_bios_btrfsboot_tests PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_bios_btrfsboot_tests
    COMMAND rc95ldr_bios_btrfsboot_tests)

add_executable(rc95ldr_bios_module_tests ../bios_module_tests.c)
target_link_libraries(rc95ldr_bios_module_tests PRIVATE rc95ldr_bios_platform)
target_include_directories(rc95ldr_bios_module_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../bios/include
    ${CMAKE_CURRENT_LIST_DIR}/../../core/include
    ${CMAKE_CURRENT_LIST_DIR}/../../winload/include
    ${CMAKE_CURRENT_LIST_DIR}/../../textui/include
    ${CMAKE_CURRENT_LIST_DIR}/../../../../sdk/include)
set_target_properties(rc95ldr_bios_module_tests PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_bios_module_tests COMMAND rc95ldr_bios_module_tests)

# Optional Clang libFuzzer target for continuous Btrfs parser fuzzing.
option(RC95LDR_ENABLE_LIBFUZZER "Build the VENEER Btrfs libFuzzer target" OFF)
if(RC95LDR_ENABLE_LIBFUZZER)
    if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "RC95LDR_ENABLE_LIBFUZZER requires Clang")
    endif()
    add_executable(rc95ldr_veneer_btrfs_fuzz ../veneer_btrfs_fuzz.c)
    target_link_libraries(rc95ldr_veneer_btrfs_fuzz PRIVATE rc95ldr_veneer)
    target_include_directories(rc95ldr_veneer_btrfs_fuzz PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/../../core/include
        ${CMAKE_CURRENT_LIST_DIR}/../../veneer/include)
    # Instrument the parser and its core dependencies too; instrumenting
    # only the harness would miss memory errors inside the library.
    target_compile_options(rc95ldr_veneer PRIVATE
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_compile_options(rc95ldr_core PRIVATE
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_compile_options(rc95ldr_veneer_btrfs_fuzz PRIVATE
        -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer)
    target_link_options(rc95ldr_veneer_btrfs_fuzz PRIVATE
        -fsanitize=fuzzer,address,undefined)
    set_target_properties(rc95ldr_veneer_btrfs_fuzz PROPERTIES
        C_STANDARD 99 C_STANDARD_REQUIRED ON)
endif()

add_executable(rc95ldr_wim_provider_tests ../wim_provider_tests.c)
target_link_libraries(rc95ldr_wim_provider_tests PRIVATE rc95ldr_fs_wim)
target_include_directories(rc95ldr_wim_provider_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../core/include
    ${CMAKE_CURRENT_LIST_DIR}/../../fs/include
    ${CMAKE_CURRENT_LIST_DIR}/../../../../sdk/include)
set_target_properties(rc95ldr_wim_provider_tests PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_wim_provider_tests COMMAND rc95ldr_wim_provider_tests)

