# Native BIOS assembly dialect boundary:
# - MSVC selects ASM_MASM (ml/ml64) and the _USE_ML branch of sdk/include/asm/asm.inc.
# - GNU/Clang selects ASM/GAS and the non-_USE_ML branch (Intel syntax plus GAS directives).
# - Do not pass MASM-only declarations in these .S files directly to GNU as.
add_library(rc95ldr_bios_platform STATIC
    bios.c
    biosdisk.c
    bioscd.c
    video.c
    ntfsvalidate.c
    btrfsvalidate.c
    memory.c
    biosfirmwarestate.c
    module.c
    module_transfer.c)
target_include_directories(rc95ldr_bios_platform PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../winload/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../textui/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../bgfx/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../bcd/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../fs/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../veneer/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../firmware/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../sdk/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../sdk/include/psdk
    ${CMAKE_BINARY_DIR}/sdk/include/psdk
    ${CMAKE_BINARY_DIR}/sdk/include/asm)
target_link_libraries(rc95ldr_bios_platform PUBLIC
    rc95ldr_core rc95ldr_winload_policy
    rc95ldr_fs_fat32 rc95ldr_fs_ntfs_geometry rc95ldr_fs_btrfs rc95ldr_fs_iso9660
    rc95ldr_veneer rc95ldr_bgfx)
set_target_properties(rc95ldr_bios_platform PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON)

if((ARCH STREQUAL "i386" OR ARCH STREQUAL "amd64") AND MSVC)
    # The current startup sources use the Microsoft MASM/asm.inc dialect.
    # Do not feed them to GNU as on Linux; portable policy and host tests are
    # still built there, while the native BIOS assembly is built by MSVC.
    CreateBootSectorTarget(rc95ldr_bios_cd_stage0
        ${CMAKE_CURRENT_SOURCE_DIR}/arch/i386/stage0_cd.S
        ${CMAKE_CURRENT_BINARY_DIR}/rc95ldr_bios_cd_stage0.bin 7c00)
elseif((ARCH STREQUAL "i386" OR ARCH STREQUAL "amd64") AND NOT MSVC)
    message(STATUS "RC95LDR: GNU/GAS build selected; using the parallel arch/*/gas implementations")
    if(ARCH STREQUAL "i386")
        # Complete i386 GAS path: gateway, module bridge, stage transition,
        # and the two 512-byte BIOS/El Torito bootstrap sectors.
        target_sources(rc95ldr_bios_platform PRIVATE
            arch/i386/gateway.c
            arch/i386/gas/gateway.S
            arch/i386/gas/module_transfer.S)
        target_compile_definitions(rc95ldr_bios_platform PRIVATE
            RC95LDR_BIOS_NATIVE_I386=1)

        add_library(rc95ldr_bios_stage0_gas OBJECT arch/i386/gas/stage0.S)
        add_library(rc95ldr_bios_cd_stage0_gas OBJECT arch/i386/gas/stage0_cd.S)
        if(CMAKE_OBJCOPY)
            add_custom_target(rc95ldr_bios_stage0_gas_binary ALL
                COMMAND ${CMAKE_OBJCOPY} -O binary -j .text
                    $<TARGET_OBJECTS:rc95ldr_bios_stage0_gas>
                    ${CMAKE_CURRENT_BINARY_DIR}/rc95ldr_bios_stage0_gas.bin
                DEPENDS rc95ldr_bios_stage0_gas
                BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/rc95ldr_bios_stage0_gas.bin
                VERBATIM)
            add_custom_target(rc95ldr_bios_cd_stage0_gas_binary ALL
                COMMAND ${CMAKE_OBJCOPY} -O binary -j .text
                    $<TARGET_OBJECTS:rc95ldr_bios_cd_stage0_gas>
                    ${CMAKE_CURRENT_BINARY_DIR}/rc95ldr_bios_cd_stage0_gas.bin
                DEPENDS rc95ldr_bios_cd_stage0_gas
                BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/rc95ldr_bios_cd_stage0_gas.bin
                VERBATIM)
        endif()
    elseif(ARCH STREQUAL "amd64")
        target_sources(rc95ldr_bios_platform PRIVATE
            arch/amd64/gas/module_transfer.S)
        target_sources(rc95ldr_bios_platform PRIVATE amd64handoff.c)
        target_compile_definitions(rc95ldr_bios_platform PRIVATE
            RC95LDR_BIOS_NATIVE_AMD64=1)
    endif()
endif()

if(ARCH STREQUAL "amd64" AND MSVC)
    add_asm_files(rc95ldr_bios_module_transfer_amd64_asm
        arch/amd64/module_transfer.S)
    target_sources(rc95ldr_bios_platform PRIVATE
        ${rc95ldr_bios_module_transfer_amd64_asm}
        amd64handoff.c)
    target_compile_definitions(rc95ldr_bios_platform PRIVATE
        RC95LDR_BIOS_NATIVE_AMD64=1)
endif()

if(ARCH STREQUAL "i386" AND MSVC)
    # Standalone 512-byte BIOS boot sector. Kept out of the protected-mode
    # libraries; the image packager consumes its flat binary as --boot-sector.
    add_asm_files(rc95ldr_bios_stage0_asm arch/i386/stage0.S)
    add_library(rc95ldr_bios_stage0 OBJECT ${rc95ldr_bios_stage0_asm})
    CreateBootSectorTarget(rc95ldr_bios_stage0_binary
        ${CMAKE_CURRENT_SOURCE_DIR}/arch/i386/stage0.S
        ${CMAKE_CURRENT_BINARY_DIR}/rc95ldr_bios_stage0.bin 7c00)

    add_asm_files(rc95ldr_bios_gateway_asm arch/i386/gateway.S)
    add_asm_files(rc95ldr_bios_entry_asm arch/i386/entry.S)
    add_asm_files(rc95ldr_bios_stage_asm arch/i386/stage.S)
    add_asm_files(rc95ldr_bios_module_transfer_asm arch/i386/module_transfer.S)
    target_sources(rc95ldr_bios_platform PRIVATE
        arch/i386/gateway.c
        ${rc95ldr_bios_gateway_asm}
        ${rc95ldr_bios_module_transfer_asm})
    target_compile_definitions(rc95ldr_bios_platform PRIVATE RC95LDR_BIOS_NATIVE_I386=1)
endif()

# Protected-mode RC95LDR entry and shared WINLOAD dispatch. This is a native
# entry component, not a ROSLOAD/FreeLoader compatibility shim.
add_library(rc95ldr_winload_bios_entry STATIC
    winload_entry.c
    ntfsboot.c
    btrfsboot.c
    startup.c)
if(ARCH STREQUAL "i386" AND MSVC)
    target_sources(rc95ldr_winload_bios_entry PRIVATE
        entry.c
        ${rc95ldr_bios_entry_asm})
    target_compile_definitions(rc95ldr_winload_bios_entry PRIVATE
        RC95LDR_BIOS_NATIVE_I386=1)
elseif(ARCH STREQUAL "amd64" AND MSVC)
    add_asm_files(rc95ldr_bios_entry_amd64_asm arch/amd64/entry.S)
    target_sources(rc95ldr_winload_bios_entry PRIVATE
        entry.c
        ${rc95ldr_bios_entry_amd64_asm})
    target_compile_definitions(rc95ldr_winload_bios_entry PRIVATE
        RC95LDR_BIOS_NATIVE_AMD64=1)
endif()

if(NOT MSVC AND ARCH STREQUAL "i386")
    target_sources(rc95ldr_winload_bios_entry PRIVATE
        entry.c
        arch/i386/gas/entry.S)
    target_compile_definitions(rc95ldr_winload_bios_entry PRIVATE
        RC95LDR_BIOS_NATIVE_I386=1)
elseif(NOT MSVC AND ARCH STREQUAL "amd64")
    target_sources(rc95ldr_winload_bios_entry PRIVATE
        entry.c
        arch/amd64/gas/entry.S)
    target_compile_definitions(rc95ldr_winload_bios_entry PRIVATE
        RC95LDR_BIOS_NATIVE_AMD64=1)
endif()
target_include_directories(rc95ldr_winload_bios_entry PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../winload/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../textui/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../firmware/include)
target_link_libraries(rc95ldr_winload_bios_entry PUBLIC
    rc95ldr_bios_platform rc95ldr_winload_entry rc95ldr_winload_policy
    rc95ldr_firmware_menu)
set_target_properties(rc95ldr_winload_bios_entry PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON)

# The real-mode-to-protected-mode stage is a separate linkable component.
# Its caller still supplies the already-loaded image and platform provider;
# this target does not pretend that an on-disk first-stage loader exists.
add_library(rc95ldr_bios_stage STATIC stage.c)
if(ARCH STREQUAL "i386" AND MSVC)
    target_sources(rc95ldr_bios_stage PRIVATE ${rc95ldr_bios_stage_asm})
    target_compile_definitions(rc95ldr_bios_stage PRIVATE
        RC95LDR_BIOS_NATIVE_I386=1)
elseif(ARCH STREQUAL "i386" AND NOT MSVC)
    target_sources(rc95ldr_bios_stage PRIVATE arch/i386/gas/stage.S)
    target_compile_definitions(rc95ldr_bios_stage PRIVATE
        RC95LDR_BIOS_NATIVE_I386=1)
elseif(ARCH STREQUAL "amd64" AND MSVC)
    target_compile_definitions(rc95ldr_bios_stage PRIVATE
        RC95LDR_BIOS_NATIVE_AMD64=1)
endif()
target_include_directories(rc95ldr_bios_stage PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../winload/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../textui/include)
target_link_libraries(rc95ldr_bios_stage PUBLIC
    rc95ldr_winload_bios_entry rc95ldr_bios_platform)
set_target_properties(rc95ldr_bios_stage PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON)

