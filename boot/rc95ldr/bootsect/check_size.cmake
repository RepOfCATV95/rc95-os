# Fails the build if a boot sector binary is not exactly the expected size.
file(SIZE ${FILE} _actual)
if(NOT _actual EQUAL ${SIZE})
    message(FATAL_ERROR "${FILE}: expected ${SIZE} bytes, got ${_actual}")
endif()
