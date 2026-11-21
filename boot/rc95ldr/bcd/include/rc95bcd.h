#ifndef _RC95BCD_H_
#define _RC95BCD_H_
#include "rc95ldr.h"
#ifdef __cplusplus
extern "C" {
#endif
#define RC95BCD_HIVE_HEADER_SIZE 4096u
#define RC95BCD_MAX_KEY_DEPTH 32u
#define RC95BCD_MAX_INDEX_DEPTH 8u
typedef struct _RC95BCD_HIVE { const uint8_t *Image; size_t ImageSize; uint32_t HiveBinsSize; uint32_t RootCell; uint32_t Sequence1; uint32_t Sequence2; } RC95BCD_HIVE;
typedef struct _RC95BCD_VALUE { const uint8_t *Data; uint32_t Length; uint32_t Type; uint32_t KeyCell; uint32_t ValueCell; } RC95BCD_VALUE;
typedef struct _RC95BCD_KEY_INFO { uint32_t Cell; uint32_t SubKeyCount; uint32_t ValueCount; const uint8_t *Name; uint16_t NameLength; uint16_t Flags; } RC95BCD_KEY_INFO;
typedef enum _RC95BCD_ELEMENT_KIND { Rc95BcdElementUnknown=0, Rc95BcdElementDevice=1, Rc95BcdElementString=2, Rc95BcdElementObject=3, Rc95BcdElementObjectList=4, Rc95BcdElementInteger=5, Rc95BcdElementBoolean=6, Rc95BcdElementIntegerList=7, Rc95BcdElementBinary=8 } RC95BCD_ELEMENT_KIND;
typedef struct _RC95BCD_GUID { uint32_t Data1; uint16_t Data2; uint16_t Data3; uint8_t Data4[8]; } RC95BCD_GUID;
typedef struct _RC95BCD_ELEMENT_VIEW
{
    uint32_t ElementId;
    RC95BCD_ELEMENT_KIND Kind;
    const uint8_t *Data;
    uint32_t Length;
    uint64_t IntegerValue;
    uint32_t BooleanValue;
    const uint8_t *StringData;
    uint32_t StringCharacters;
    RC95BCD_GUID ObjectGuid;
    uint32_t ItemCount;
    const uint8_t *Items;
    uint32_t DeviceType;
    uint32_t AdditionalOptions;
    const uint8_t *DeviceData;
    uint32_t DeviceDataLength;
} RC95BCD_ELEMENT_VIEW;
/* Image is caller-owned and must outlive Hive. */
RC95LDR_STATUS Rc95BcdOpenHive(const void*,size_t,RC95BCD_HIVE*);
RC95LDR_STATUS Rc95BcdGetRootKey(const RC95BCD_HIVE*,RC95BCD_KEY_INFO*);
RC95LDR_STATUS Rc95BcdFindSubKey(const RC95BCD_HIVE*,uint32_t,const char*,uint32_t*);
RC95LDR_STATUS Rc95BcdFindValue(const RC95BCD_HIVE*,uint32_t,const char*,RC95BCD_VALUE*);
RC95LDR_STATUS Rc95BcdFindObject(const RC95BCD_HIVE*,const char*,uint32_t*);
RC95LDR_STATUS Rc95BcdFindElement(const RC95BCD_HIVE*,uint32_t,uint32_t,RC95BCD_VALUE*);
/* Decode class-specific payloads without allocating or copying source data.
 * Device-specific tails remain opaque until a device-type-specific decoder
 * is selected; all returned views point into Value->Data. */
RC95LDR_STATUS Rc95BcdReadGuid(const uint8_t*,size_t,RC95BCD_GUID*);
RC95LDR_STATUS Rc95BcdDecodeElement(uint32_t,const RC95BCD_VALUE*,RC95BCD_ELEMENT_VIEW*);
RC95LDR_STATUS Rc95BcdLoadFromVolume(const RC95LDR_VOLUME*,const char*,void*,size_t,size_t*,RC95BCD_HIVE*);
#ifdef __cplusplus
}
#endif
#endif
