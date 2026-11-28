#ifndef _RC95LDR_NTFS_STREAM_H_
#define _RC95LDR_NTFS_STREAM_H_
#include "rc95ldr_ntfs_volume.h"
#ifdef __cplusplus
extern "C" {
#endif
#define RC95LDR_NTFS_MAX_STREAM_RUNS 128u
typedef struct _RC95LDR_NTFS_STREAM {
    RC95LDR_NTFS_RUN Runs[RC95LDR_NTFS_MAX_STREAM_RUNS];
    size_t RunCount;
    uint64_t NextVcn;
    uint64_t AllocatedSize, DataSize, InitializedSize;
    uint16_t Flags;
} RC95LDR_NTFS_STREAM;
/* Collects a named stream's base and ATTRIBUTE_LIST extension extents.
 * Attribute-list data must be resident. ExtensionRecord is caller-owned
 * scratch and must hold one FILE record. The output is committed only after
 * all references, sequence numbers, attribute IDs, VCNs and physical runs
 * have been validated. */
RC95LDR_STATUS Rc95LdrNtfsCollectStream(
    const RC95LDR_NTFS_VOLUME *Volume,
    const uint8_t *BaseRecord, size_t BaseRecordLength,
    uint64_t BaseFileReference, uint32_t AttributeType,
    const uint16_t *Name, size_t NameLength,
    void *ExtensionRecord, size_t ExtensionRecordLength,
    RC95LDR_NTFS_STREAM *Stream);
/* Reads a sector-aligned range bounded by AllocatedSize. Bytes at and beyond
 * InitializedSize are returned as zeroes, including the tail of its final
 * sector; this also permits reading the containing sector of the final valid
 * $BITMAP byte. Bounce holds one sector. Sparse allocated ranges are rejected. */
RC95LDR_STATUS Rc95LdrNtfsReadStreamRange(
    const RC95LDR_NTFS_VOLUME *Volume, const RC95LDR_NTFS_STREAM *Stream,
    uint64_t Offset, void *Buffer, size_t Length,
    void *Bounce, size_t BounceLength);
#ifdef __cplusplus
}
#endif
#endif
