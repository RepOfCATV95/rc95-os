list(APPEND SOURCE
    wim_codec.c
    wim_io.c
    wim_write.c
    wim_capture.c
    sha1.c
    wim_read.c
    wim_image.c
    wim_xpress.c
    wim_xpress_huffman.c
    wim_lzx.c
    wim_lzms.c
    lzms_bitstream.c
    lzms_range.c
    lzms_dictionary.c
    lzms_token.c
    lzms_probability.c
    lzms_huffman.c
    lzms_range16.c
    lzms_context.c
    lzms_slots.c
    lzms_block.c)

add_library(libwim STATIC ${SOURCE})

target_include_directories(libwim
    PUBLIC
        ${REACTOS_SOURCE_DIR}/sdk/include/reactos/libs
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})
