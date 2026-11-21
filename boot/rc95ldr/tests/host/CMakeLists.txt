cmake_minimum_required(VERSION 3.16)
project(rc95ldr_pe_host_tests C)

enable_testing()

add_executable(rc95ldr_pe_host_tests
    ../pe_tests.c
    ../../winload/pe.c
    ../../winload/osload.c
    ../../winload/profile.c
    ../../winload/osprepare.c
    ../../core/rc95ldr.c
)
target_include_directories(rc95ldr_pe_host_tests PRIVATE
    ../../core/include
    ../../winload/include
)
set_target_properties(rc95ldr_pe_host_tests PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
)
add_test(NAME rc95ldr_pe_host_tests COMMAND rc95ldr_pe_host_tests)

add_executable(rc95ldr_profile_host_tests
    ../profile_tests.c
    ../../winload/profile.c
)
target_include_directories(rc95ldr_profile_host_tests PRIVATE
    ../../core/include
    ../../winload/include
)
set_target_properties(rc95ldr_profile_host_tests PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
)
add_test(NAME rc95ldr_profile_host_tests COMMAND rc95ldr_profile_host_tests)

add_executable(rc95ldr_osprepare_host_tests
    ../osprepare_tests.c
    ../../winload/osprepare.c
)
target_include_directories(rc95ldr_osprepare_host_tests PRIVATE
    ../../core/include
    ../../winload/include
)
set_target_properties(rc95ldr_osprepare_host_tests PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
)
add_test(NAME rc95ldr_osprepare_host_tests COMMAND rc95ldr_osprepare_host_tests)
