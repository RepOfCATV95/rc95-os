#ifndef _LIBWIM_LZMS_RANGE16_H_
#define _LIBWIM_LZMS_RANGE16_H_

#include <stddef.h>
#include <stdint.h>
#include <libwim/lzms_probability.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * LZMS uses a 32-bit range/code pair and consumes the compressed stream in
 * little-endian 16-bit words.  This is deliberately separate from the
 * generic MSB-first bitstream range coder in lzms_range.c.
 */
typedef enum _LZMS_RANGE16_STATUS {
    LZMS_RANGE16_OK = 0,
    LZMS_RANGE16_INVALID_ARGUMENT,
    LZMS_RANGE16_INPUT_EXHAUSTED,
    LZMS_RANGE16_CORRUPT_DATA
} LZMS_RANGE16_STATUS;

#define LZMS_RANGE16_PROBABILITY_SCALE 64u
#define LZMS_RANGE16_NORMALIZE         0x00010000u

typedef struct _LZMS_RANGE16_DECODER {
    const uint8_t *Input;
    size_t InputSize;
    size_t InputPosition;
    uint32_t Range;
    uint32_t Code;
    LZMS_RANGE16_STATUS Status;
} LZMS_RANGE16_DECODER;

LZMS_RANGE16_STATUS
LzmsRange16DecoderInitialize(
    LZMS_RANGE16_DECODER *Decoder,
    const uint8_t *Input,
    size_t InputSize);

LZMS_RANGE16_STATUS
LzmsRange16DecodeBit(
    LZMS_RANGE16_DECODER *Decoder,
    uint32_t ZeroProbability,
    uint32_t *Bit);

/* Decode using the 64-observation rolling probability model. */
LZMS_RANGE16_STATUS
LzmsRange16DecodeModelBit(
    LZMS_RANGE16_DECODER *Decoder,
    LZMS_PROBABILITY_MODEL *Model,
    uint32_t *Bit);

/* Decode N equiprobable bits, most-significant bit first. */
LZMS_RANGE16_STATUS
LzmsRange16DecodeBits(
    LZMS_RANGE16_DECODER *Decoder,
    uint32_t BitCount,
    uint32_t *Value);

/* Number of compressed bytes consumed (always even). */
size_t
LzmsRange16DecoderBytesRead(
    const LZMS_RANGE16_DECODER *Decoder);

#ifdef __cplusplus
}
#endif

#endif
