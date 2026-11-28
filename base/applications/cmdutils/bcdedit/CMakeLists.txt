add_executable(bcdedit bcdedit.c bcdedit.rc)
set_module_type(bcdedit win32cui UNICODE)
target_link_libraries(bcdedit conutils rc95ldr_bcd)
target_include_directories(bcdedit PRIVATE
    ${CMAKE_SOURCE_DIR}/boot/rc95ldr/core/include
    ${CMAKE_SOURCE_DIR}/boot/rc95ldr/bcd/include)
add_importlibs(bcdedit msvcrt kernel32)
add_cd_file(TARGET bcdedit DESTINATION reactos/system32 FOR all)
