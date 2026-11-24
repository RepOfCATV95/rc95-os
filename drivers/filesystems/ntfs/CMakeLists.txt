
list(APPEND SOURCE
    attrib.c
    blockdev.c
    btree.c
    cleanup.c
    close.c
    create.c
    devctl.c
    dirctl.c
    dispatch.c
    fastio.c
    flush.c
    fcb.c
    finfo.c
    fsctl.c
    lockctrl.c
    mft.c
    misc.c
    ntfs.c
    rw.c
    volinfo.c
    ${REACTOS_SOURCE_DIR}/sdk/lib/fslib/ntfslib/logfile.c
    ntfs.h)

add_library(ntfs MODULE ${SOURCE} ntfs.rc)
target_include_directories(ntfs PRIVATE
    ${REACTOS_SOURCE_DIR}/sdk/include/reactos/drivers
    ${REACTOS_SOURCE_DIR}/sdk/lib/fslib/ntfslib
    ${CMAKE_CURRENT_SOURCE_DIR}/inc)

set_module_type(ntfs kernelmodedriver)
target_compile_definitions(ntfs PRIVATE __KERNEL__ NTFS_LOGFILE_KERNEL_BUILD)
target_link_libraries(ntfs ${PSEH_LIB})
add_importlibs(ntfs ntoskrnl hal)
add_cd_file(TARGET ntfs DESTINATION reactos/system32/drivers NO_CAB FOR all)
