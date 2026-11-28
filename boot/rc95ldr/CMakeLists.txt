add_subdirectory(core)
add_subdirectory(veneer)
add_subdirectory(bcd)
add_subdirectory(textui)
add_subdirectory(bgfx)
add_subdirectory(fs)
add_subdirectory(winload)
add_subdirectory(mkbtrfs)
# Firmware menu is a shared dependency of both UEFI and BIOS entry paths.
# Define it before those targets so CMake records a target dependency rather
# than a bare linker-library name.
add_subdirectory(firmware)
add_subdirectory(uefi)
add_subdirectory(bios)
# RC95LDR-owned BIOS MBR/VBR and El Torito images.
add_subdirectory(bootsect)

option(RC95LDR_BUILD_TESTS "Build host-side RC95LDR regression tests" OFF)
if(RC95LDR_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Guard the native source tree against accidental reintroduction of the
# FreeLoader entry, interrupt, ARC filesystem, or link dependencies.
if(RC95LDR_BUILD_TESTS)
    add_test(NAME rc95ldr_dependency_boundary
        COMMAND ${CMAKE_COMMAND}
            -DRC95LDR_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_dependency_boundary.cmake)
endif()

if(RC95LDR_BUILD_TESTS)
    add_test(NAME rc95ldr_btrfs_provider_dependency_boundary
        COMMAND ${CMAKE_COMMAND}
            -DRC95LDR_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_btrfs_provider_boundary.cmake)
endif()

if(RC95LDR_BUILD_TESTS)
    add_test(NAME rc95ldr_freeldr_isolation
        COMMAND ${CMAKE_COMMAND}
            -DFREELDR_SOURCE_DIR=${CMAKE_SOURCE_DIR}/boot/freeldr
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_freeldr_isolation.cmake)
endif()

if(RC95LDR_BUILD_TESTS)
    add_test(NAME rc95ldr_rc95_nt_abi_boundary
        COMMAND ${CMAKE_COMMAND}
            -DRC95LDR_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_rc95_nt_abi_boundary.cmake)
endif()
