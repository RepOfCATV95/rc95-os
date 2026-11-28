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
/* Allocate an aligned cell from an existing free cell. The returned offset
 * is relative to the start of the hive bins, as used by registry cell links.
 * Uses first-fit and splits only when the remainder is >= 8 bytes. */
RC95LDR_STATUS Rc95BcdAllocateCell(void *Image, size_t ImageSize,
    uint32_t RequestedDataSize, uint32_t *CellOffset,
    uint32_t *CellDataCapacity);

/* Create an NK key cell and automatically rebuild its parent's subkey
 * index when ParentCell is not 0xffffffff. CreateValueCell creates a VK cell
 * and automatically appends it to ParentCell's value list. Both preserve
 * existing entries, reject duplicate names, and commit transactionally using
 * an image-sized, distinct scratch buffer. */
RC95LDR_STATUS Rc95BcdCreateKeyCell(void *Image, size_t ImageSize,
    void *Scratch, size_t ScratchSize, uint32_t ParentCell,
    const char *Name, uint32_t *NewKeyCell);
RC95LDR_STATUS Rc95BcdCreateValueCell(void *Image, size_t ImageSize,
    void *Scratch, size_t ScratchSize, uint32_t ParentCell,
    const char *Name, uint32_t Type,
    const void *Data, uint32_t DataLength, uint32_t *NewValueCell,
    uint32_t *NewDataCell);

/* Rebuild a parent's stable subkey LI index and VK value list from
 * caller-supplied, validated hive-bin-relative cell offsets. The child NK
 * cells must name ParentCell in their parent field. The operation is
 * transactional and updates SubKeyCount/SubKeyList and ValueCount/ValueList
 * together, then reclaims the detached old RI/LF/LH/LI index tree and VK
 * value-list cell before committing. Counts are limited to the on-disk 16-bit
 * LI entry count. */
RC95LDR_STATUS Rc95BcdRebuildKeyStructure(void *Image, size_t ImageSize,
    void *Scratch, size_t ScratchSize, uint32_t ParentCell,
    const uint32_t *SubKeyCells, uint32_t SubKeyCount,
    const uint32_t *ValueCells, uint32_t ValueCount);

/* Validate REGF metadata, the complete HBIN chain, and every cell boundary. */
RC95LDR_STATUS Rc95BcdValidateHiveBins(const void *Image, size_t ImageSize);

/* Reclaim a detached allocated cell only after a conservative whole-hive
 * reference scan proves its offset is not present in any other allocated
 * cell. Adjacent free cells in its HBIN are coalesced transactionally.
 * Opaque payload coincidences may conservatively prevent reclamation. */
RC95LDR_STATUS Rc95BcdReclaimDetachedCell(void *Image, size_t ImageSize,
    void *Scratch, size_t ScratchSize, uint32_t CellOffset);

/* Replace an existing value with an arbitrary-length payload. The operation
 * creates a replacement VK cell, optional external data cell, and value-list
 * cell in Scratch, switches the parent NK link, reclaims the obsolete list/VK/
 * external data cells, validates the result, and commits atomically. Existing
 * value-list ordering and the VK name encoding are preserved. */
RC95LDR_STATUS Rc95BcdReplaceValueCell(void *Image, size_t ImageSize,
    void *Scratch, size_t ScratchSize, uint32_t KeyCell,
    const char *ValueName, uint32_t Type, const void *Data,
    uint32_t DataLength, uint32_t *NewValueCell, uint32_t *NewDataCell);

/* Replace an existing REG_BINARY value without changing hive cell layout.
 * Replacement must have exactly the current data length. Image is mutable and
 * caller-owned; the operation updates both REGF sequence numbers/checksum. */
RC95LDR_STATUS Rc95BcdReplaceBinaryValue(void *Image, size_t ImageSize,
    uint32_t KeyCell, const char *ValueName, const void *Data, uint32_t Length);

RC95LDR_STATUS Rc95BcdFindObject(const RC95BCD_HIVE*,const char*,uint32_t*);
RC95LDR_STATUS Rc95BcdFindElement(const RC95BCD_HIVE*,uint32_t,uint32_t,RC95BCD_VALUE*);
typedef RC95LDR_STATUS (*RC95BCD_ELEMENT_ENUM_CALLBACK)(uint32_t,const RC95BCD_VALUE*,void*);
typedef RC95LDR_STATUS (*RC95BCD_KEY_ENUM_CALLBACK)(const RC95BCD_KEY_INFO*,void*);
RC95LDR_STATUS Rc95BcdEnumerateSubKeys(const RC95BCD_HIVE*,uint32_t,RC95BCD_KEY_ENUM_CALLBACK,void*);
RC95LDR_STATUS Rc95BcdEnumerateElements(const RC95BCD_HIVE*,uint32_t,RC95BCD_ELEMENT_ENUM_CALLBACK,void*);
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
