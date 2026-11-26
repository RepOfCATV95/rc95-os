/*
 * PROJECT:    RC95-OS LZMS codec
 * LICENSE:    GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:    Clean-room rolling-window binary probability models
 */
#ifndef _LIBWIM_LZMS_PROBABILITY_H_
#define _LIBWIM_LZMS_PROBABILITY_H_
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define LZMS_PROBABILITY_WINDOW_BITS 64u
#define LZMS_PROBABILITY_INITIAL_ZERO_COUNT 48u
#define LZMS_PROBABILITY_INITIAL_HISTORY UINT64_C(0x0000000055555555)
typedef struct _LZMS_PROBABILITY_MODEL {
    uint64_t RecentBits;
    uint32_t ZeroCount;
} LZMS_PROBABILITY_MODEL;
typedef enum _LZMS_PROBABILITY_STATUS {
    LZMS_PROBABILITY_OK = 0,
    LZMS_PROBABILITY_INVALID_ARGUMENT
} LZMS_PROBABILITY_STATUS;
void LzmsProbabilityModelInitialize(LZMS_PROBABILITY_MODEL *Model);
uint32_t LzmsProbabilityModelGetZeroCount(const LZMS_PROBABILITY_MODEL *Model);
LZMS_PROBABILITY_STATUS LzmsProbabilityModelUpdate(LZMS_PROBABILITY_MODEL *Model, uint32_t Bit);
LZMS_PROBABILITY_STATUS LzmsProbabilityAdvanceState(uint32_t *State, uint32_t StateCount, uint32_t Bit);
#ifdef __cplusplus
}
#endif
#endif
