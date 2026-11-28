# Enforce the source-level ownership boundary for RC95LDR.
# This intentionally scans implementation and build files, not migration
# documentation, which must be able to name the legacy interfaces it replaces.

if(NOT DEFINED RC95LDR_SOURCE_DIR OR NOT IS_DIRECTORY "${RC95LDR_SOURCE_DIR}")
    message(FATAL_ERROR "RC95LDR_SOURCE_DIR must name the boot/rc95ldr source tree")
endif()

file(GLOB_RECURSE _rc95ldr_files LIST_DIRECTORIES FALSE
    "${RC95LDR_SOURCE_DIR}/*.c"
    "${RC95LDR_SOURCE_DIR}/*.h"
    "${RC95LDR_SOURCE_DIR}/*.S"
    "${RC95LDR_SOURCE_DIR}/*.asm"
    "${RC95LDR_SOURCE_DIR}/*.cmake"
    "${RC95LDR_SOURCE_DIR}/CMakeLists.txt")

set(_violations "")
foreach(_file IN LISTS _rc95ldr_files)
    file(READ "${_file}" _contents)

    if(_contents MATCHES "#[ \t]*include[ \t]*[<\"]freeldr\\.h[>\"]")
        list(APPEND _violations "${_file}: includes freeldr.h")
    endif()
    if(_contents MATCHES "#[ \t]*include[ \t]*[<\"]arcsupp\\.h[>\"]")
        list(APPEND _violations "${_file}: includes arcsupp.h")
    endif()
    if(_contents MATCHES "#[ \t]*include[ \t]*[<\"]arcemul\\.h[>\"]")
        list(APPEND _violations "${_file}: includes arcemul.h")
    endif()
    if(_contents MATCHES "Int386(x)?[ \t\r\n]*\\(")
        list(APPEND _violations "${_file}: calls the FreeLoader real-mode interrupt API")
    endif()
    if(_contents MATCHES "SwitchToReal[ \t\r\n]*\\(")
        list(APPEND _violations "${_file}: calls the FreeLoader real-mode relocator")
    endif()
    if(_contents MATCHES "Arc(Open|Read|Close|Seek|GetFileInformation|GetDirectoryEntry)[ \t\r\n]*\\(")
        list(APPEND _violations "${_file}: calls the legacy ARC filesystem API")
    endif()
    if(_contents MATCHES "target_link_libraries[ \t\r\n]*\\([^)]*[ \t]freeldr([ \t)]|$)")
        list(APPEND _violations "${_file}: links the FreeLoader library")
    endif()
endforeach()

if(_violations)
    string(JOIN "\n  " _report ${_violations})
    message(FATAL_ERROR "RC95LDR legacy dependency boundary violated:\n  ${_report}")
endif()

message(STATUS "RC95LDR dependency boundary: no FreeLoader/ARC implementation dependencies found")
