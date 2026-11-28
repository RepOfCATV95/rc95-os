# Winload orchestration is kept separate from BCD policy.
add_library(rc95ldr_winload_policy STATIC
    bcdpolicy.c
    pe.c
    ntprepare.c
    systemhive.c
    loaderblock.c
    loaderstate.c
    rc95loaderstate.c
    physmem.c
    memorymap.c
    resume.c
    hiberdelete.c
    platforminventory.c
    metadata.c
    archprepare.c
    rc95nativeprovider.c
    rc95nativebootdriver.c
    baseline_pagetables.c
    paewindow.c
    cpuprobe.c
    nativeprepare.c
    dependency.c
    serviceorder.c
    handoff.c
    profile.c
    osprepare.c
    osload.c
    fileprovider.c
)
target_include_directories(rc95ldr_winload_policy PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../bcd/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../textui/include
    ${CMAKE_CURRENT_LIST_DIR}/../../../sdk/include
    ${CMAKE_CURRENT_LIST_DIR}/../../../sdk/include/psdk
    ${CMAKE_BINARY_DIR}/sdk/include/psdk
    ${CMAKE_BINARY_DIR}/sdk/include/asm)
if(TARGET xdk)
    add_dependencies(rc95ldr_winload_policy xdk)
endif()
if(TARGET asm)
    add_dependencies(rc95ldr_winload_policy asm)
endif()
target_link_libraries(rc95ldr_winload_policy PUBLIC
    rc95ldr_bcd rc95ldr_core rc95ldr_textui)
set_target_properties(rc95ldr_winload_policy PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON)

# The common entry coordinator is shared by the BIOS and UEFI frontends.
add_library(rc95ldr_winload_entry STATIC entry.c)
target_include_directories(rc95ldr_winload_entry PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../textui/include)
target_link_libraries(rc95ldr_winload_entry PUBLIC rc95ldr_winload_policy)
set_target_properties(rc95ldr_winload_entry PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON)

# The portable resume reader is host-testable, but the final processor-state
# transition is architecture-specific and is linked only into native builds.
if(ARCH STREQUAL "i386")
    add_asm_files(rc95ldr_winload_resume_asm arch/i386/resume.S)
    target_sources(rc95ldr_winload_policy PRIVATE
        resume_arch.c
        ${rc95ldr_winload_resume_asm})
elseif(ARCH STREQUAL "amd64")
    add_asm_files(rc95ldr_winload_resume_asm arch/amd64/resume.S)
    target_sources(rc95ldr_winload_policy PRIVATE
        resume_arch.c
        ${rc95ldr_winload_resume_asm})
endif()

# Native AMD64 cold-boot transfer. Kept out of host/i386 builds; the C
# wrapper always calls Rc95LdrValidateKernelHandoff before entering assembly.
if(ARCH STREQUAL "amd64" AND MSVC)
    add_asm_files(rc95ldr_winload_transfer_amd64_asm arch/amd64/transfer.S)
    target_sources(rc95ldr_winload_policy PRIVATE
        amd64transfer.c
        ${rc95ldr_winload_transfer_amd64_asm})
endif()
