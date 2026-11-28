# Enforce the reciprocal ownership boundary: legacy FreeLoader must not
# include, call, or link the native RC95LDR implementation.
if(NOT DEFINED FREELDR_SOURCE_DIR OR NOT IS_DIRECTORY "${FREELDR_SOURCE_DIR}")
    message(FATAL_ERROR "FREELDR_SOURCE_DIR must name the boot/freeldr source tree")
endif()

file(GLOB_RECURSE _freeldr_files LIST_DIRECTORIES FALSE
    "${FREELDR_SOURCE_DIR}/*.c"
    "${FREELDR_SOURCE_DIR}/*.h"
    "${FREELDR_SOURCE_DIR}/*.S"
    "${FREELDR_SOURCE_DIR}/*.asm"
    "${FREELDR_SOURCE_DIR}/*.cmake"
    "${FREELDR_SOURCE_DIR}/CMakeLists.txt")

set(_violations "")
foreach(_file IN LISTS _freeldr_files)
    file(READ "${_file}" _contents)
    if(_contents MATCHES "#[ \t]*include[ \t]*[<\"][^>\"]*rc95ldr[^>\"]*[>\"]")
        list(APPEND _violations "${_file}: includes an RC95LDR header")
    endif()
    if(_contents MATCHES "(^|[^A-Za-z0-9_])Rc95Ldr[A-Za-z0-9_]*[ \t\r\n]*\\(")
        list(APPEND _violations "${_file}: calls an RC95LDR API")
    endif()
    if(_contents MATCHES "target_link_libraries[ \t\r\n]*\\([^)]*rc95ldr")
        list(APPEND _violations "${_file}: links an RC95LDR target")
    endif()
endforeach()

if(_violations)
    string(JOIN "\n  " _report ${_violations})
    message(FATAL_ERROR "FreeLoader/RC95LDR isolation violated:\n  ${_report}")
endif()

message(STATUS "FreeLoader isolation: no RC95LDR headers, API calls, or link dependencies found")
