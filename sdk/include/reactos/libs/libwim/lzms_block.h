#ifndef _LIBWIM_LZMS_BLOCK_H_
#define _LIBWIM_LZMS_BLOCK_H_
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum _LZMS_BLOCK_STATUS { LZMS_BLOCK_OK=0,LZMS_BLOCK_INVALID_ARGUMENT,LZMS_BLOCK_BAD_FRAMING,LZMS_BLOCK_CORRUPT_DATA,LZMS_BLOCK_OUTPUT_LIMIT,LZMS_BLOCK_INPUT_OVERLAP } LZMS_BLOCK_STATUS;
/* Raw LZMS has no in-band output-size header; WIM chunk metadata supplies it. */
LZMS_BLOCK_STATUS LzmsDecodeRawBlock(const uint8_t*,size_t,uint8_t*,size_t,size_t*);
#ifdef __cplusplus
}
#endif
#endif
