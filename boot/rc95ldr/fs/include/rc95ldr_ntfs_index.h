#ifndef _RC95LDR_NTFS_INDEX_H_
#define _RC95LDR_NTFS_INDEX_H_
#include "rc95ldr.h"
#ifdef __cplusplus
extern "C" {
#endif
#define RC95LDR_NTFS_INDEX_FILE_NAME 0x30u
#define RC95LDR_NTFS_INDEX_BUFFER_MAGIC 0x58444e49u
#define RC95LDR_NTFS_INDEX_ENTRY_NODE 0x0001u
#define RC95LDR_NTFS_INDEX_ENTRY_END  0x0002u
typedef struct _RC95LDR_NTFS_INDEX_ENTRY {
    uint64_t FileReference;
    const uint8_t *Key;
    uint16_t KeyLength, Flags;
    uint64_t ChildVcn;
} RC95LDR_NTFS_INDEX_ENTRY;
typedef RC95LDR_STATUS (*RC95LDR_NTFS_INDEX_CALLBACK)(
    void *Context, const RC95LDR_NTFS_INDEX_ENTRY *Entry);
typedef struct _RC95LDR_NTFS_FILE_NAME_KEY {
    uint64_t ParentFileReference;
    const uint16_t *Name;
    uint8_t NameLength, Namespace;
} RC95LDR_NTFS_FILE_NAME_KEY;
/* Parses the resident INDEX_ROOT value for a filename ($I30) index.
 * ExpectedIndexBufferBytes is zero when the caller only wants syntax checks. */
RC95LDR_STATUS Rc95LdrNtfsEnumerateIndexRoot(
    const void *Value, size_t ValueLength, uint32_t ExpectedIndexBufferBytes,
    RC95LDR_NTFS_INDEX_CALLBACK Callback, void *Context, size_t *EntryCount);
/* Finds the resident $I30 INDEX_ROOT attribute in a fixed-up directory
 * FILE record and enumerates its bounded filename entries. */
RC95LDR_STATUS Rc95LdrNtfsEnumerateDirectoryIndexRoot(
    const uint8_t *FileRecord, size_t RecordLength, uint32_t BytesPerSector,
    uint32_t ExpectedIndexBufferBytes,
    RC95LDR_NTFS_INDEX_CALLBACK Callback, void *Context, size_t *EntryCount);
/* Applies the update-sequence-array fixups to an INDX allocation buffer in place.
 * Every sector trailer is validated before any bytes are changed. */
RC95LDR_STATUS Rc95LdrNtfsApplyIndexBufferFixups(
    uint8_t *Buffer, size_t BufferLength, uint32_t BytesPerSector);
/* Enumerates an INDX allocation buffer after USA fixups have been applied. */
RC95LDR_STATUS Rc95LdrNtfsEnumerateIndexBuffer(
    const void *Buffer, size_t BufferLength, uint32_t BytesPerSector,
    uint64_t ExpectedVcn, RC95LDR_NTFS_INDEX_CALLBACK Callback,
    void *Context, size_t *EntryCount);
RC95LDR_STATUS Rc95LdrNtfsDecodeFileNameKey(
    const uint8_t *Key, size_t KeyLength, RC95LDR_NTFS_FILE_NAME_KEY *Name);
#ifdef __cplusplus
}
#endif
#endif
