/*
 * PROJECT: RC95-OS LZMS codec
 * LICENSE: GPL-3.0-or-later
 * PURPOSE: LZMS token execution and delayed repeat-source queues
 */
#ifndef _LIBWIM_LZMS_TOKEN_H_
#define _LIBWIM_LZMS_TOKEN_H_
#include <stddef.h>
#include <stdint.h>
#include <libwim/lzms_dictionary.h>
#ifdef __cplusplus
extern "C" {
#endif

#define LZMS_TOKEN_REP_COUNT 3u
#define LZMS_TOKEN_REP_QUEUE_SIZE (LZMS_TOKEN_REP_COUNT + 1u)

typedef enum _LZMS_TOKEN_KIND {
    LZMS_TOKEN_LITERAL=0, LZMS_TOKEN_MATCH, LZMS_TOKEN_REP_MATCH,
    LZMS_TOKEN_DELTA_MATCH, LZMS_TOKEN_REP_DELTA_MATCH
} LZMS_TOKEN_KIND;
typedef enum _LZMS_TOKEN_STATUS {
    LZMS_TOKEN_OK=0, LZMS_TOKEN_INVALID_ARGUMENT, LZMS_TOKEN_BAD_DISTANCE,
    LZMS_TOKEN_OUTPUT_FULL, LZMS_TOKEN_OUTPUT_LIMIT, LZMS_TOKEN_BAD_STATE
} LZMS_TOKEN_STATUS;

typedef struct _LZMS_TOKEN {
    LZMS_TOKEN_KIND Kind;
    uint8_t Literal;
    uint32_t Length;
    /*
     * For LZ matches: byte distance.
     * For delta matches: raw distance; effective distance is
     * Distance * (1 << DeltaPower).
     */
    uint32_t Distance;
    uint8_t RepIndex;
    uint8_t DeltaPower;
} LZMS_TOKEN;

typedef struct _LZMS_TOKEN_DECODER {
    LZMS_DICTIONARY *Dictionary;
    /* Four slots implement the three visible repeat choices plus overflow. */
    uint32_t RepDistances[LZMS_TOKEN_REP_QUEUE_SIZE];
    uint32_t DeltaRepDistances[LZMS_TOKEN_REP_QUEUE_SIZE];
    uint8_t DeltaRepPowers[LZMS_TOKEN_REP_QUEUE_SIZE];
    uint8_t RepValid[LZMS_TOKEN_REP_QUEUE_SIZE];
    uint8_t DeltaRepValid[LZMS_TOKEN_REP_QUEUE_SIZE];
    LZMS_TOKEN_KIND PreviousTokenKind;
    /* LZMS item type: literal=0, LZ=1, delta=2. */
    uint8_t PreviousItemType;
    uint8_t HasPreviousToken;
    uint64_t Produced, ExpectedOutputSize, MaximumOutputSize;
} LZMS_TOKEN_DECODER;

LZMS_TOKEN_STATUS
LzmsTokenDecoderInitialize(LZMS_TOKEN_DECODER*, LZMS_DICTIONARY*,
                           uint64_t ExpectedOutputSize,
                           uint64_t MaximumOutputSize);
LZMS_TOKEN_STATUS
LzmsTokenDecode(LZMS_TOKEN_DECODER*, const LZMS_TOKEN*,
                uint8_t*, size_t, size_t*);

#ifdef __cplusplus
}
#endif
#endif
