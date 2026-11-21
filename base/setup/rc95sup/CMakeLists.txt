
list(APPEND SOURCE
    spapisup/fileqsup.c
    spapisup/infsupp.c
    drivepage.c
    rc95sup.c
    treelist.c
    rc95sup_header.h)

file(GLOB rc95sup_rc_deps res/*.*)
add_rc_deps(rc95sup.rc ${rc95sup_rc_deps})
add_executable(rc95sup ${SOURCE} rc95sup.rc)
add_pch(rc95sup rc95sup_header.h SOURCE)

set_module_type(rc95sup win32gui UNICODE)
target_link_libraries(rc95sup uuid)
target_link_libraries(rc95sup zlib_solo) ## We use USETUP's cabinet implementation
add_delay_importlibs(rc95sup setuplib)
add_importlibs(rc95sup advapi32 gdi32 user32 comctl32 shlwapi setupapi msvcrt kernel32 ntdll)
add_cd_file(TARGET rc95sup DESTINATION reactos NO_CAB FOR bootcd)
