add_executable(bcdboot bcdboot.c bcdboot.rc)
set_module_type(bcdboot win32cui UNICODE)
target_link_libraries(bcdboot conutils rc95ldr_bcd)
target_include_directories(bcdboot PRIVATE
    ${CMAKE_SOURCE_DIR}/boot/rc95ldr/core/include
    ${CMAKE_SOURCE_DIR}/boot/rc95ldr/bcd/include)
add_importlibs(bcdboot msvcrt kernel32)
add_cd_file(TARGET bcdboot DESTINATION reactos/system32 FOR all)
