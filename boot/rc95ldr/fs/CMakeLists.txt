add_library(rc95ldr_fs_fat32 STATIC fat32.c)
target_include_directories(rc95ldr_fs_fat32 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include)
target_link_libraries(rc95ldr_fs_fat32 PUBLIC rc95ldr_core)
set_target_properties(rc95ldr_fs_fat32 PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON)
