# Build a native FreeType library for host-side asset generators.
#
# This directory is reached only from sdk/tools, which is built by the
# non-cross-compiling host-tools configuration. Do not add this target to
# sdk/lib/3rdparty or link it into bootloader/kernel targets.
set(FREETYPE_HOST_SOURCES
    ${CMAKE_SOURCE_DIR}/sdk/lib/3rdparty/freetype/src/autofit/autofit.c
    ${CMAKE_SOURCE_DIR}/sdk/lib/3rdparty/freetype/src/base/ftbase.c
    ${CMAKE_SOURCE_DIR}/sdk/lib/3rdparty/freetype/src/base/ftbbox.c
    ${CMAKE_SOURCE_DIR}/sdk/lib/3rdparty/freetype/src/base/ftbdf.c
    ${CMAKE_SOURCE_DIR}/sdk/lib/3rdparty/freetype/src/base/ftbitmap.c
    ${CMAKE_SOURCE_DIR}/sdk/lib/3rdparty/freetype/src/base/ftcid.c
    ${CMAKE_SOURCE_DIR}/sdk/lib/3rdparty/freetype/src/base/ftfstype.c
    ${CMAKE_SOURCE_DIR}/sdk/lib/3rdparty/freetype/src/base/ftgasp.c
    ${CMAKE_SOURCE_DIR}/sdk/lib/3rdparty/freetype/src/base/ftglyph.c
    ${CMAKE_SOURCE_DIR}/sdk/lib/3rdparty/freetype/src/base/ftgxval.c
    ${CMAKE_SOURCE_DIR}/sdk/lib/3rdparty/freetype/src/base/ftinit.c
    ${CMAKE_SOURCE_DIR}/sdk/lib/3rdparty/freetype/src/base/ftmm.c
    ${CMAKE_SOURCE_DIR}/sdk/lib/3rdparty/freetype/src/base/ftotval.c
    ${CMAKE_SOURCE_DIR}/sdk/lib/3rdparty/freetype/src/base/ftpatent.c
    ${CMAKE_SOURCE_DIR}/sdk/lib/3rdparty/freetype/src/base/ftpfr.c
    ${CMAKE_SOURCE_DIR}/sdk/lib/3rdparty/freetype/src/base/ftstroke.c
    ${CMAKE_SOURCE_DIR}/sdk/lib/3rdparty/freetype/src/base/ftsynth.c
    ${CMAKE_SOURCE_DIR}/sdk/lib/3rdparty/freetype/src/base/fttype1.c
    ${CMAKE_SOURCE_DIR}/sdk/lib/3rdparty/freetype/src/base/ftwinfnt.c
    ${CMAKE_SOURCE_DIR}/sdk/lib/3rdparty/freetype/src/bdf/bdf.c
    ${CMAKE_SOURCE_DIR}/sdk/lib/3rdparty/freetype/src/bzip2/ftbzip2.c
    ${CMAKE_SOURCE_DIR}/sdk/lib/3rdparty/freetype/src/cache/ftcache.c
    ${CMAKE_SOURCE_DIR}/sdk/lib/3rdparty/freetype/src/cff/cff.c
    ${CMAKE_SOURCE_DIR}/sdk/lib/3rdparty/freetype/src/cid/type1cid.c
    ${CMAKE_SOURCE_DIR}/sdk/lib/3rdparty/freetype/src/gzip/ftgzip.c
    ${CMAKE_SOURCE_DIR}/sdk/lib/3rdparty/freetype/src/lzw/ftlzw.c
    ${CMAKE_SOURCE_DIR}/sdk/lib/3rdparty/freetype/src/pcf/pcf.c
    ${CMAKE_SOURCE_DIR}/sdk/lib/3rdparty/freetype/src/pfr/pfr.c
    ${CMAKE_SOURCE_DIR}/sdk/lib/3rdparty/freetype/src/psaux/psaux.c
    ${CMAKE_SOURCE_DIR}/sdk/lib/3rdparty/freetype/src/pshinter/pshinter.c
    ${CMAKE_SOURCE_DIR}/sdk/lib/3rdparty/freetype/src/psnames/psnames.c
    ${CMAKE_SOURCE_DIR}/sdk/lib/3rdparty/freetype/src/raster/raster.c
    ${CMAKE_SOURCE_DIR}/sdk/lib/3rdparty/freetype/src/sfnt/sfnt.c
    ${CMAKE_SOURCE_DIR}/sdk/lib/3rdparty/freetype/src/smooth/smooth.c
    ${CMAKE_SOURCE_DIR}/sdk/lib/3rdparty/freetype/src/truetype/truetype.c
    ${CMAKE_SOURCE_DIR}/sdk/lib/3rdparty/freetype/src/type1/type1.c
    ${CMAKE_SOURCE_DIR}/sdk/lib/3rdparty/freetype/src/type42/type42.c
    ${CMAKE_SOURCE_DIR}/sdk/lib/3rdparty/freetype/src/winfonts/winfnt.c
    ${CMAKE_SOURCE_DIR}/sdk/lib/3rdparty/freetype/src/base/ftsystem.c
    ${CMAKE_SOURCE_DIR}/sdk/lib/3rdparty/freetype/builds/windows/ftdebug.c
)

add_library(host_freetype STATIC ${FREETYPE_HOST_SOURCES})
target_compile_definitions(host_freetype PRIVATE
    FT2_BUILD_LIBRARY
    _CRT_SECURE_NO_WARNINGS
    _CRT_NONSTDC_NO_WARNINGS
)
target_include_directories(host_freetype PUBLIC
    ${CMAKE_SOURCE_DIR}/sdk/lib/3rdparty/freetype/include
)
set_target_properties(host_freetype PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME freetype-host
)

if(CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID STREQUAL "Clang")
    target_compile_options(host_freetype PRIVATE -Wno-array-parameter)
endif()
