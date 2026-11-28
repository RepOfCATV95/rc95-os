# RC95LDR BIOS boot sectors (GNU as, Intel syntax). These are flat binaries
# linked at fixed addresses with GNU ld rather than CreateBootSectorTarget
# (whose MSVC variant expects MASM sources).
# MBR/multi-sector images relocate to 0x0600; FAT VBRs run in place at 0x7C00.
if((ARCH STREQUAL "i386" OR ARCH STREQUAL "amd64") AND NOT MSVC)
    # Keep the three parallel lists separate: semicolons are CMake list
    # separators, so storing "name;base;size" in one list loses tuple grouping.
    set(_bootsect_names
        regular_mbr
        veneer_mbr
        etfsmbr
        fat16_mbr
        fat_mbr
        ntfs_mbr
        btrfs_mbr)
    set(_bootsect_bases
        0600
        0600
        0600
        7c00
        7c00
        0600
        0600)
    set(_bootsect_sizes
        512
        512
        2048
        512
        512
        4096
        6144)

    list(LENGTH _bootsect_names _bootsect_count)
    math(EXPR _bootsect_last "${_bootsect_count} - 1")
    set(RC95LDR_BOOTSECT_BINARIES)
    foreach(_index RANGE 0 ${_bootsect_last})
        list(GET _bootsect_names ${_index} _name)
        list(GET _bootsect_bases ${_index} _base)
        list(GET _bootsect_sizes ${_index} _size)
        set(_source ${CMAKE_CURRENT_SOURCE_DIR}/${_name}.S)
        set(_obj ${CMAKE_CURRENT_BINARY_DIR}/${_name}.o)
        set(_bin ${CMAKE_CURRENT_BINARY_DIR}/${_name}.bin)
        add_custom_command(
            OUTPUT ${_bin}
            COMMAND ${CMAKE_C_COMPILER} -m32 -x assembler-with-cpp -c
                    ${_source} -o ${_obj}
            COMMAND ${CMAKE_LINKER} -m elf_i386 -e 0 -Ttext 0x${_base}
                    --oformat binary -o ${_bin} ${_obj}
            COMMAND ${CMAKE_COMMAND} -DFILE=${_bin} -DSIZE=${_size}
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/check_size.cmake
            DEPENDS ${_source} ${CMAKE_CURRENT_SOURCE_DIR}/check_size.cmake
            VERBATIM)
        list(APPEND RC95LDR_BOOTSECT_BINARIES ${_bin})
    endforeach()
    add_custom_target(rc95ldr_bootsect ALL DEPENDS ${RC95LDR_BOOTSECT_BINARIES})
endif()
