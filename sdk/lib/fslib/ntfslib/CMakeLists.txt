add_library(ntfslib ntfslib.c bootsect.c files.c mft.c logfile.c ntfslib.h mft.h logfile.h data.h refdata.h)
add_dependencies(ntfslib psdk)
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(ntfslib PRIVATE -Wno-shift-overflow)
endif()

# Optional compile-only fixture harness for the bounded $LogFile parser.
# A platform-specific test runner can call NtfsLogfileRunRecordFixtures().
if(NTFSLIB_BUILD_TESTS)
    add_library(ntfslib_logfile_fixtures OBJECT logfile_tests.c)
    add_dependencies(ntfslib_logfile_fixtures psdk)
endif()
