add_library(rc95ldr_fs_fat32 STATIC fat32.c)
target_include_directories(rc95ldr_fs_fat32 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include)
target_link_libraries(rc95ldr_fs_fat32 PUBLIC rc95ldr_core)
set_target_properties(rc95ldr_fs_fat32 PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON)

# Native read-only Btrfs volume binding over the validated VENEER metadata.
add_library(rc95ldr_fs_btrfs STATIC btrfs_volume.c btrfs_root.c btrfs_filesystem.c)
target_include_directories(rc95ldr_fs_btrfs PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../veneer/include)
target_link_libraries(rc95ldr_fs_btrfs PUBLIC rc95ldr_core rc95ldr_veneer)
set_target_properties(rc95ldr_fs_btrfs PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON)

add_library(rc95ldr_fs_ntfs_geometry STATIC ntfs_geometry.c ntfs_runlist.c ntfs_record.c ntfs_volume.c ntfs_index.c ntfs_stream.c ntfs_file.c ntfs_provider.c)
target_include_directories(rc95ldr_fs_ntfs_geometry PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include)
target_link_libraries(rc95ldr_fs_ntfs_geometry PUBLIC rc95ldr_core)
set_target_properties(rc95ldr_fs_ntfs_geometry PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON)

add_library(rc95ldr_fs_iso9660 STATIC iso9660.c)
target_include_directories(rc95ldr_fs_iso9660 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include)
target_link_libraries(rc95ldr_fs_iso9660 PUBLIC rc95ldr_core)
set_target_properties(rc95ldr_fs_iso9660 PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON)

# WIM image filesystem. libwim is the shared repository implementation; the
# bootloader adapter owns only mount lifetime, path policy and file handles.
if(NOT TARGET libwim)
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../../sdk/lib/libwim
                     ${CMAKE_CURRENT_BINARY_DIR}/libwim)
endif()
add_library(rc95ldr_fs_wim STATIC wim_provider.c)
target_include_directories(rc95ldr_fs_wim PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
    ${CMAKE_CURRENT_LIST_DIR}/../../../sdk/include)
target_link_libraries(rc95ldr_fs_wim PUBLIC rc95ldr_core libwim)
set_target_properties(rc95ldr_fs_wim PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON)
