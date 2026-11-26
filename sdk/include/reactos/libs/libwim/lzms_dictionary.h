#ifndef _LIBWIM_LZMS_DICTIONARY_H_
#define _LIBWIM_LZMS_DICTIONARY_H_
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define LZMS_DICTIONARY_NO_POSITION UINT64_MAX
typedef enum _LZMS_DICTIONARY_STATUS { LZMS_DICTIONARY_OK=0,LZMS_DICTIONARY_INVALID_ARGUMENT,LZMS_DICTIONARY_BAD_DISTANCE,LZMS_DICTIONARY_BUFFER_FULL,LZMS_DICTIONARY_BAD_STATE } LZMS_DICTIONARY_STATUS;
typedef struct _LZMS_DICTIONARY { uint8_t *Window; size_t WindowSize; uint64_t TotalBytes; } LZMS_DICTIONARY;
typedef struct _LZMS_MATCH_FINDER { LZMS_DICTIONARY *Dictionary; uint64_t *HashHeads; size_t HashSize; uint64_t *Previous,*SlotPositions; uint32_t MaximumChain; } LZMS_MATCH_FINDER;
typedef struct _LZMS_MATCH { uint32_t Length,Distance; } LZMS_MATCH;
LZMS_DICTIONARY_STATUS LzmsDictionaryInitialize(LZMS_DICTIONARY*,uint8_t*,size_t);
LZMS_DICTIONARY_STATUS LzmsDictionaryAppend(LZMS_DICTIONARY*,const uint8_t*,size_t);
LZMS_DICTIONARY_STATUS LzmsDictionaryCopyMatch(LZMS_DICTIONARY*,uint32_t,uint32_t);
uint8_t LzmsDictionaryGetByte(const LZMS_DICTIONARY*,uint64_t);
size_t LzmsDictionaryAvailable(const LZMS_DICTIONARY*);
LZMS_DICTIONARY_STATUS LzmsMatchFinderInitialize(LZMS_MATCH_FINDER*,LZMS_DICTIONARY*,uint64_t*,size_t,uint64_t*,uint64_t*,uint32_t);
LZMS_DICTIONARY_STATUS LzmsMatchFinderInsert(LZMS_MATCH_FINDER*,const uint8_t*,size_t);
LZMS_DICTIONARY_STATUS LzmsMatchFinderFind(LZMS_MATCH_FINDER*,const uint8_t*,size_t,uint32_t,uint32_t,LZMS_MATCH*);
#ifdef __cplusplus
}
#endif
#endif
