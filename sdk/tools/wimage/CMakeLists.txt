# wimage is a host utility. Build a host-side copy of libwim rather than
# attempting to link the target-architecture static library from sdk/lib.
add_library(wimage_lib STATIC
    ${REACTOS_SOURCE_DIR}/sdk/lib/libwim/wim_codec.c
    ${REACTOS_SOURCE_DIR}/sdk/lib/libwim/wim_io.c
    ${REACTOS_SOURCE_DIR}/sdk/lib/libwim/wim_write.c
    ${REACTOS_SOURCE_DIR}/sdk/lib/libwim/wim_capture.c
    ${REACTOS_SOURCE_DIR}/sdk/lib/libwim/sha1.c
    ${REACTOS_SOURCE_DIR}/sdk/lib/libwim/wim_read.c
    ${REACTOS_SOURCE_DIR}/sdk/lib/libwim/wim_image.c
    ${REACTOS_SOURCE_DIR}/sdk/lib/libwim/wim_xpress.c
    ${REACTOS_SOURCE_DIR}/sdk/lib/libwim/wim_xpress_huffman.c
    ${REACTOS_SOURCE_DIR}/sdk/lib/libwim/wim_lzx.c
    ${REACTOS_SOURCE_DIR}/sdk/lib/libwim/wim_lzms.c
    ${REACTOS_SOURCE_DIR}/sdk/lib/libwim/lzms_bitstream.c
    ${REACTOS_SOURCE_DIR}/sdk/lib/libwim/lzms_range.c
    ${REACTOS_SOURCE_DIR}/sdk/lib/libwim/lzms_dictionary.c
    ${REACTOS_SOURCE_DIR}/sdk/lib/libwim/lzms_token.c
    ${REACTOS_SOURCE_DIR}/sdk/lib/libwim/lzms_probability.c
    ${REACTOS_SOURCE_DIR}/sdk/lib/libwim/lzms_huffman.c
    ${REACTOS_SOURCE_DIR}/sdk/lib/libwim/lzms_range16.c
    ${REACTOS_SOURCE_DIR}/sdk/lib/libwim/lzms_context.c
    ${REACTOS_SOURCE_DIR}/sdk/lib/libwim/lzms_slots.c
    ${REACTOS_SOURCE_DIR}/sdk/lib/libwim/lzms_block.c)
target_include_directories(wimage_lib PUBLIC
    ${REACTOS_SOURCE_DIR}/sdk/include/reactos/libs
    PRIVATE ${REACTOS_SOURCE_DIR}/sdk/lib/libwim)

add_host_tool(wimage wimage.c)
target_link_libraries(wimage PRIVATE wimage_lib)
