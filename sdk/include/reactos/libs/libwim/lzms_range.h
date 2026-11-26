#ifndef _LIBWIM_LZMS_RANGE_H_
#define _LIBWIM_LZMS_RANGE_H_
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum _LZMS_RANGE_STATUS { LZMS_RANGE_OK=0, LZMS_RANGE_INVALID_ARGUMENT, LZMS_RANGE_INPUT_EXHAUSTED, LZMS_RANGE_OUTPUT_FULL, LZMS_RANGE_CORRUPT_DATA, LZMS_RANGE_FINALIZED } LZMS_RANGE_STATUS;
#define LZMS_PROBABILITY_SCALE 4096u
#define LZMS_PROBABILITY_INITIAL 2048u
#define LZMS_PROBABILITY_MIN 1u
#define LZMS_PROBABILITY_MAX 4095u
typedef struct _LZMS_ADAPTIVE_MODEL { uint32_t ZeroCount, OneCount, MaximumTotal; } LZMS_ADAPTIVE_MODEL;
typedef struct _LZMS_RANGE_ENCODER { uint32_t Low,High,PendingBits; uint8_t *Output; size_t OutputCapacity,OutputPosition; uint8_t BitAccumulator,BitsInAccumulator; LZMS_RANGE_STATUS Status; uint8_t Finalized; } LZMS_RANGE_ENCODER;
typedef struct _LZMS_RANGE_DECODER { uint32_t Low,High,Code; const uint8_t *Input; size_t InputSize,InputPosition; uint8_t BitPosition; LZMS_RANGE_STATUS Status; } LZMS_RANGE_DECODER;
void LzmsAdaptiveModelInitialize(LZMS_ADAPTIVE_MODEL*,uint32_t,uint32_t,uint32_t);
uint32_t LzmsAdaptiveModelGetZeroProbability(const LZMS_ADAPTIVE_MODEL*);
void LzmsAdaptiveModelUpdate(LZMS_ADAPTIVE_MODEL*,uint32_t);
LZMS_RANGE_STATUS LzmsRangeEncoderInitialize(LZMS_RANGE_ENCODER*,uint8_t*,size_t);
LZMS_RANGE_STATUS LzmsRangeEncodeBit(LZMS_RANGE_ENCODER*,LZMS_ADAPTIVE_MODEL*,uint32_t);
LZMS_RANGE_STATUS LzmsRangeEncodeUniformBit(LZMS_RANGE_ENCODER*,uint32_t);
LZMS_RANGE_STATUS LzmsRangeEncoderFinish(LZMS_RANGE_ENCODER*);
size_t LzmsRangeEncoderBytesWritten(const LZMS_RANGE_ENCODER*);
LZMS_RANGE_STATUS LzmsRangeDecoderInitialize(LZMS_RANGE_DECODER*,const uint8_t*,size_t);
LZMS_RANGE_STATUS LzmsRangeDecodeBit(LZMS_RANGE_DECODER*,LZMS_ADAPTIVE_MODEL*,uint32_t*);
LZMS_RANGE_STATUS LzmsRangeDecodeUniformBit(LZMS_RANGE_DECODER*,uint32_t*);
#ifdef __cplusplus
}
#endif
#endif
