/*
 * PROJECT: RC95-OS clean-room LZMS decoder
 * PURPOSE: Independent adaptive binary contexts for range-coded decisions
 */
#ifndef _LIBWIM_LZMS_CONTEXT_H_
#define _LIBWIM_LZMS_CONTEXT_H_
#include <stdint.h>
#include <libwim/lzms_range16.h>
#ifdef __cplusplus
extern "C" {
#endif
#define LZMS_CONTEXT_MAX_STATES 64u
typedef struct _LZMS_BINARY_CONTEXT {
    uint32_t State;
    uint8_t StateBits;
    uint8_t StateCount;
    LZMS_PROBABILITY_MODEL Models[LZMS_CONTEXT_MAX_STATES];
} LZMS_BINARY_CONTEXT;
typedef struct _LZMS_CONTEXT_SET {
    LZMS_BINARY_CONTEXT Main;
    LZMS_BINARY_CONTEXT Match;
    LZMS_BINARY_CONTEXT Lz;
    LZMS_BINARY_CONTEXT Delta;
    LZMS_BINARY_CONTEXT LzRepeat[2];
    LZMS_BINARY_CONTEXT DeltaRepeat[2];
} LZMS_CONTEXT_SET;
typedef enum _LZMS_ITEM_CLASS {
    LZMS_ITEM_LITERAL=0, LZMS_ITEM_LZ_MATCH=1, LZMS_ITEM_DELTA_MATCH=2
} LZMS_ITEM_CLASS;
typedef enum _LZMS_CONTEXT_STATUS {
    LZMS_CONTEXT_OK=0, LZMS_CONTEXT_INVALID_ARGUMENT,
    LZMS_CONTEXT_RANGE_ERROR
} LZMS_CONTEXT_STATUS;
LZMS_CONTEXT_STATUS LzmsBinaryContextInitialize(LZMS_BINARY_CONTEXT*, uint8_t);
LZMS_CONTEXT_STATUS LzmsBinaryContextDecode(LZMS_BINARY_CONTEXT*, LZMS_RANGE16_DECODER*, uint32_t*);
LZMS_CONTEXT_STATUS LzmsContextSetInitialize(LZMS_CONTEXT_SET*);
LZMS_CONTEXT_STATUS LzmsContextDecodeItemClass(LZMS_CONTEXT_SET*, LZMS_RANGE16_DECODER*, LZMS_ITEM_CLASS*);
LZMS_CONTEXT_STATUS LzmsContextDecodeOffsetMode(LZMS_CONTEXT_SET*, LZMS_RANGE16_DECODER*, LZMS_ITEM_CLASS, uint32_t*);
LZMS_CONTEXT_STATUS LzmsContextDecodeRepeatRank(LZMS_CONTEXT_SET*, LZMS_RANGE16_DECODER*, LZMS_ITEM_CLASS, uint8_t*);
#ifdef __cplusplus
}
#endif
#endif
