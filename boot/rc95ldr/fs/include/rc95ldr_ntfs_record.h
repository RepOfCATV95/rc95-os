#ifndef _RC95LDR_NTFS_RECORD_H_
#define _RC95LDR_NTFS_RECORD_H_
#include "rc95ldr.h"
#ifdef __cplusplus
extern "C" {
#endif
#define RC95LDR_NTFS_FILE_MAGIC 0x454c4946u
#define RC95LDR_NTFS_ATTRIBUTE_END 0xffffffffu
#define RC95LDR_NTFS_ATTRIBUTE_LIST 0x20u
typedef struct _RC95LDR_NTFS_ATTRIBUTE {
    uint32_t Type, Length;
    uint16_t Instance, Flags;
    uint8_t NonResident, NameLength;
    const uint16_t *Name;
    const uint8_t *Record, *Value;
    uint32_t ValueLength;
    uint64_t LowestVcn, HighestVcn;
    uint16_t MappingPairsOffset;
    uint64_t AllocatedSize, DataSize, InitializedSize;
} RC95LDR_NTFS_ATTRIBUTE;
RC95LDR_STATUS Rc95LdrNtfsApplyFileRecordFixups(
    uint8_t *Record, size_t RecordLength, uint32_t BytesPerSector);
typedef struct _RC95LDR_NTFS_FILE_RECORD_INFO {
    uint16_t SequenceNumber, HardLinkCount, Flags, NextAttributeId;
    uint32_t BytesInUse, BytesAllocated, RecordNumber;
    uint64_t BaseFileReference;
} RC95LDR_NTFS_FILE_RECORD_INFO;
typedef struct _RC95LDR_NTFS_ATTRIBUTE_LIST_ENTRY {
    uint32_t Type;
    uint64_t LowestVcn, FileReference;
    uint16_t AttributeId;
    uint8_t NameLength;
    const uint16_t *Name;
} RC95LDR_NTFS_ATTRIBUTE_LIST_ENTRY;
typedef RC95LDR_STATUS (*RC95LDR_NTFS_ATTRIBUTE_LIST_CALLBACK)(
    void *Context, const RC95LDR_NTFS_ATTRIBUTE_LIST_ENTRY *Entry);
typedef RC95LDR_STATUS (*RC95LDR_NTFS_ATTRIBUTE_CALLBACK)(
    void *Context, const RC95LDR_NTFS_ATTRIBUTE *Attribute);
RC95LDR_STATUS Rc95LdrNtfsEnumerateAttributeList(
    const void *Value, size_t ValueLength,
    RC95LDR_NTFS_ATTRIBUTE_LIST_CALLBACK Callback, void *Context,
    size_t *EntryCount);
RC95LDR_STATUS Rc95LdrNtfsGetFileRecordInfo(
    const uint8_t *Record, size_t RecordLength,
    RC95LDR_NTFS_FILE_RECORD_INFO *Information);
RC95LDR_STATUS Rc95LdrNtfsValidateFileReference(
    const uint8_t *Record, size_t RecordLength, uint64_t FileReference);
/* Enumerates every attribute of Type in a validated, fixed-up FILE record. */
RC95LDR_STATUS Rc95LdrNtfsEnumerateAttributes(
    const uint8_t *Record, size_t RecordLength, uint32_t BytesPerSector,
    uint32_t Type, RC95LDR_NTFS_ATTRIBUTE_CALLBACK Callback,
    void *Context, size_t *EntryCount);
RC95LDR_STATUS Rc95LdrNtfsFindNamedAttribute(
    const uint8_t *Record, size_t RecordLength, uint32_t BytesPerSector,
    uint32_t Type, const uint16_t *AttributeName, size_t AttributeNameLength,
    RC95LDR_NTFS_ATTRIBUTE *Attribute);
RC95LDR_STATUS Rc95LdrNtfsFindAttribute(
    const uint8_t *Record, size_t RecordLength, uint32_t BytesPerSector,
    uint32_t Type, RC95LDR_NTFS_ATTRIBUTE *Attribute);
#ifdef __cplusplus
}
#endif
#endif
