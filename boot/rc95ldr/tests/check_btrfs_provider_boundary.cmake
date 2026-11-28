# Btrfs provider isolation contract.
# Scan only the native Btrfs provider subtree so legacy FreeLoader code elsewhere
# in the repository cannot be mistaken for an allowed implementation dependency.
if(NOT DEFINED RC95LDR_SOURCE_DIR OR NOT IS_DIRECTORY "${RC95LDR_SOURCE_DIR}")
    message(FATAL_ERROR "RC95LDR_SOURCE_DIR must name the boot/rc95ldr source tree")
endif()

set(_provider_dir "${RC95LDR_SOURCE_DIR}/fs/btrfs")
set(_provider_files "")
if(IS_DIRECTORY "${_provider_dir}")
    file(GLOB_RECURSE _provider_files LIST_DIRECTORIES FALSE
        "${_provider_dir}/*.c" "${_provider_dir}/*.h" "${_provider_dir}/*.S"
        "${_provider_dir}/*.asm" "${_provider_dir}/CMakeLists.txt")
endif()

# VENEER is the current native Btrfs discovery/validation layer. Scan it too:
# it may depend on RC95LDR core, but never on FreeLoader or ARC services.
file(GLOB _veneer_files LIST_DIRECTORIES FALSE
    "${RC95LDR_SOURCE_DIR}/veneer/*.c"
    "${RC95LDR_SOURCE_DIR}/veneer/*.h"
    "${RC95LDR_SOURCE_DIR}/veneer/include/*.h")

# Also include flat Btrfs provider translation units if the implementation is
# organized as fs/btrfs_*.c rather than a dedicated directory.
file(GLOB _flat_provider_files LIST_DIRECTORIES FALSE
    "${RC95LDR_SOURCE_DIR}/fs/btrfs_*.c"
    "${RC95LDR_SOURCE_DIR}/fs/btrfs_*.h")
list(APPEND _provider_files ${_flat_provider_files} ${_veneer_files})

set(_violations "")
foreach(_file IN LISTS _provider_files)
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
    if(_contents MATCHES "(^|[^A-Za-z0-9_])(ArcOpen|ArcRead|ArcClose|ArcSeek|ArcGetFileInformation|ArcGetDirectoryEntry|FsOpenFile|FsReadFile|FsCloseFile|FsSetFilePointer)[ \t\r\n]*\\(")
        list(APPEND _violations "${_file}: calls a FreeLoader/ARC filesystem entry point")
    endif()
    if(_contents MATCHES "(^|[^A-Za-z0-9_])(FILE)[ \t]+\\*")
        list(APPEND _violations "${_file}: uses host/FreeLoader stdio file handles instead of RC95LDR block I/O")
    endif()
endforeach()

# A Btrfs-specific library target may depend on RC95LDR core only. In particular,
# it must not link freeldr, bootlib, or ARC compatibility libraries.
set(_fs_cmake "${RC95LDR_SOURCE_DIR}/fs/CMakeLists.txt")
if(EXISTS "${_fs_cmake}")
    file(READ "${_fs_cmake}" _fs_contents)
    if(_fs_contents MATCHES "target_link_libraries[ \t\r\n]*\\([^)]*(freeldr|bootlib|arccompat)")
        list(APPEND _violations "${_fs_cmake}: Btrfs provider links a legacy boot/ARC library")
    endif()
endif()

if(_violations)
    string(JOIN "\n  " _report ${_violations})
    message(FATAL_ERROR "RC95LDR Btrfs provider dependency boundary violated:\n  ${_report}")
endif()

message(STATUS "RC95LDR Btrfs provider boundary: no FreeLoader/ARC dependencies found")
