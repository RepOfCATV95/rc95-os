/*
 * PROJECT: RC95-OS LZMS codec
 * PURPOSE: Canonical and adaptive Huffman alphabets for LZMS
 */
#ifndef _LIBWIM_LZMS_HUFFMAN_H_
#define _LIBWIM_LZMS_HUFFMAN_H_
#include <stdint.h>
#include <libwim/lzms_bitstream.h>
#ifdef __cplusplus
extern "C" {
#endif
#define LZMS_HUFFMAN_MAX_BITS 15u
#define LZMS_HUFFMAN_MAX_SYMBOLS 799u

typedef enum _LZMS_HUFFMAN_STATUS {
    LZMS_HUFFMAN_OK=0, LZMS_HUFFMAN_INVALID_ARGUMENT,
    LZMS_HUFFMAN_OVERSUBSCRIBED, LZMS_HUFFMAN_INCOMPLETE,
    LZMS_HUFFMAN_INVALID_CODE, LZMS_HUFFMAN_BITSTREAM_ERROR,
    LZMS_HUFFMAN_FREQUENCY_OVERFLOW
} LZMS_HUFFMAN_STATUS;

typedef enum _LZMS_HUFFMAN_ALPHABET_KIND {
    LZMS_HUFFMAN_LITERAL=0,
    LZMS_HUFFMAN_LZ_OFFSET,
    LZMS_HUFFMAN_LENGTH,
    LZMS_HUFFMAN_DELTA_OFFSET,
    LZMS_HUFFMAN_DELTA_POWER,
    LZMS_HUFFMAN_ALPHABET_COUNT
} LZMS_HUFFMAN_ALPHABET_KIND;

typedef struct _LZMS_HUFFMAN_ALPHABET {
    uint16_t SymbolCount;
    uint8_t MaximumBits;
    uint8_t Lengths[LZMS_HUFFMAN_MAX_SYMBOLS];
    uint16_t Counts[LZMS_HUFFMAN_MAX_BITS+1];
    uint32_t FirstCode[LZMS_HUFFMAN_MAX_BITS+1];
    uint16_t FirstSymbol[LZMS_HUFFMAN_MAX_BITS+1];
    uint16_t SortedSymbols[LZMS_HUFFMAN_MAX_SYMBOLS];
} LZMS_HUFFMAN_ALPHABET;

typedef struct _LZMS_ADAPTIVE_HUFFMAN {
    LZMS_HUFFMAN_ALPHABET Code;
    uint32_t Frequencies[LZMS_HUFFMAN_MAX_SYMBOLS];
    uint16_t SymbolCount;
    uint16_t SymbolsUntilRebuild;
    uint16_t RebuildFrequency;
    uint8_t MaximumBits;
} LZMS_ADAPTIVE_HUFFMAN;

LZMS_HUFFMAN_STATUS
LzmsHuffmanBuildCanonical(LZMS_HUFFMAN_ALPHABET*,const uint8_t*,
                          uint16_t,uint8_t,int);
LZMS_HUFFMAN_STATUS
LzmsHuffmanDecode(const LZMS_HUFFMAN_ALPHABET*,LZMS_BIT_READER*,uint16_t*);
LZMS_HUFFMAN_STATUS
LzmsHuffmanDecodeReverse16(const LZMS_HUFFMAN_ALPHABET*,
                           LZMS_REVERSE16_READER*,uint16_t*);
LZMS_HUFFMAN_STATUS
LzmsAdaptiveHuffmanInitialize(LZMS_ADAPTIVE_HUFFMAN*,uint16_t,
                              uint16_t,uint8_t);
LZMS_HUFFMAN_STATUS
LzmsAdaptiveHuffmanRebuild(LZMS_ADAPTIVE_HUFFMAN*);
LZMS_HUFFMAN_STATUS
LzmsAdaptiveHuffmanDecode(LZMS_ADAPTIVE_HUFFMAN*,
                          LZMS_REVERSE16_READER*,uint16_t*);
LZMS_HUFFMAN_STATUS
LzmsAdaptiveHuffmanInitializeLzmsSet(
    LZMS_ADAPTIVE_HUFFMAN Alphabets[LZMS_HUFFMAN_ALPHABET_COUNT],
    uint16_t NumOffsetSymbols);
#ifdef __cplusplus
}
#endif
#endif
