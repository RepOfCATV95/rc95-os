add_executable(rc95ldr_bcd_tests bcd_tests.c)
target_link_libraries(rc95ldr_bcd_tests PRIVATE rc95ldr_bcd)
target_include_directories(rc95ldr_bcd_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../bcd/include)
set_target_properties(rc95ldr_bcd_tests PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)

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

add_executable(rc95ldr_fat32_tests fat32_tests.c)
target_link_libraries(rc95ldr_fat32_tests PRIVATE rc95ldr_fs_fat32)
target_include_directories(rc95ldr_fat32_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../fs/include)
set_target_properties(rc95ldr_fat32_tests PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)
add_test(NAME rc95ldr_fat32_tests COMMAND rc95ldr_fat32_tests)

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
