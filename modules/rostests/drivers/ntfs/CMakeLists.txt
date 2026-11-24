include_directories(../../apitests/include)

list(APPEND SOURCE
    VolumeInfo.c
    testlist.c)

add_executable(ntfs_drvtest ${SOURCE})
target_link_libraries(ntfs_drvtest wine)
set_module_type(ntfs_drvtest win32cui)
add_importlibs(ntfs_drvtest msvcrt kernel32 ntdll)
add_rostests_file(TARGET ntfs_drvtest)

# Compile the production NTFS implementation into an isolated test-only
# kernel module. ntfs.c (the filesystem's production DriverEntry) is excluded;
# ntfs_test_entry.c runs private tests and never registers a filesystem.
set(NTFS_TEST_DRIVER_SOURCE
    ${REACTOS_SOURCE_DIR}/drivers/filesystems/ntfs/attrib.c
    ${REACTOS_SOURCE_DIR}/drivers/filesystems/ntfs/blockdev.c
    ${REACTOS_SOURCE_DIR}/drivers/filesystems/ntfs/btree.c
    ${REACTOS_SOURCE_DIR}/drivers/filesystems/ntfs/cleanup.c
    ${REACTOS_SOURCE_DIR}/drivers/filesystems/ntfs/close.c
    ${REACTOS_SOURCE_DIR}/drivers/filesystems/ntfs/create.c
    ${REACTOS_SOURCE_DIR}/drivers/filesystems/ntfs/devctl.c
    ${REACTOS_SOURCE_DIR}/drivers/filesystems/ntfs/dirctl.c
    ${REACTOS_SOURCE_DIR}/drivers/filesystems/ntfs/dispatch.c
    ${REACTOS_SOURCE_DIR}/drivers/filesystems/ntfs/fastio.c
    ${REACTOS_SOURCE_DIR}/drivers/filesystems/ntfs/flush.c
    ${REACTOS_SOURCE_DIR}/drivers/filesystems/ntfs/fcb.c
    ${REACTOS_SOURCE_DIR}/drivers/filesystems/ntfs/finfo.c
    ${REACTOS_SOURCE_DIR}/drivers/filesystems/ntfs/fsctl.c
    ${REACTOS_SOURCE_DIR}/drivers/filesystems/ntfs/lockctrl.c
    ${REACTOS_SOURCE_DIR}/drivers/filesystems/ntfs/mft.c
    ${REACTOS_SOURCE_DIR}/drivers/filesystems/ntfs/misc.c
    ${REACTOS_SOURCE_DIR}/drivers/filesystems/ntfs/rw.c
    ${REACTOS_SOURCE_DIR}/drivers/filesystems/ntfs/volinfo.c
    ${REACTOS_SOURCE_DIR}/drivers/filesystems/ntfs/ntfs.h
    ${REACTOS_SOURCE_DIR}/sdk/lib/fslib/ntfslib/logfile.c
    ${REACTOS_SOURCE_DIR}/sdk/lib/fslib/ntfslib/logfile_tests.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ntfs_test_entry.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ntfs_test.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ntfs_test_storage.c)

add_library(ntfs_test MODULE ${NTFS_TEST_DRIVER_SOURCE})
target_include_directories(ntfs_test PRIVATE
    ${REACTOS_SOURCE_DIR}/drivers/filesystems/ntfs
    ${REACTOS_SOURCE_DIR}/drivers/filesystems/ntfs/inc
    ${REACTOS_SOURCE_DIR}/sdk/include/reactos/drivers
    ${REACTOS_SOURCE_DIR}/sdk/lib/fslib/ntfslib)
target_compile_definitions(ntfs_test PRIVATE __KERNEL__ NTFS_TEST_BUILD NTFS_LOGFILE_KERNEL_BUILD)
set_module_type(ntfs_test kernelmodedriver)
target_link_libraries(ntfs_test ${PSEH_LIB})
add_importlibs(ntfs_test ntoskrnl hal)
add_rostests_file(TARGET ntfs_test)
