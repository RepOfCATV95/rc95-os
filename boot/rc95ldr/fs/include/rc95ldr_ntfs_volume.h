#ifndef _RC95LDR_NTFS_VOLUME_H_
#define _RC95LDR_NTFS_VOLUME_H_
#include "rc95ldr_ntfs_geometry.h"
#include "rc95ldr_ntfs_runlist.h"
#include "rc95ldr_ntfs_record.h"
#ifdef __cplusplus
extern "C" {
#endif
#define RC95LDR_NTFS_MAX_MFT_RUNS 32u
typedef struct _RC95LDR_NTFS_VOLUME {
    /* Descriptor is copied at mount; DeviceContext remains caller-owned. */
    RC95LDR_BLOCK_DEVICE DeviceStorage;
    uint64_t PartitionStartLba, PartitionBlockCount;
    RC95LDR_NTFS_GEOMETRY Geometry;
    RC95LDR_NTFS_RUN MftRuns[RC95LDR_NTFS_MAX_MFT_RUNS];
    size_t MftRunCount;
    uint64_t MftNextVcn, MftDataSize;
    uint32_t Mounted;
} RC95LDR_NTFS_VOLUME;
/* Mounts NTFS read-only and bootstraps the MFT run map from FILE record 0.
 * The volume snapshots the block-device descriptor, but its DeviceContext and
 * all callback-owned resources must outlive the mounted volume.
 * Scratch must hold one complete file record. If a resident $ATTRIBUTE_LIST
 * references MFT::$DATA extension records, it must hold two records; the
 * second record is temporary extension storage. Scratch remains caller-owned.
 * Nonresident $ATTRIBUTE_LIST streams are rejected as unsupported. */
RC95LDR_STATUS Rc95LdrNtfsMountPartition(
    RC95LDR_NTFS_VOLUME *Volume, const RC95LDR_BLOCK_DEVICE *Device,
    uint64_t PartitionStartLba, uint64_t PartitionBlockCount,
    void *Scratch, size_t ScratchLength);
void Rc95LdrNtfsUnmount(RC95LDR_NTFS_VOLUME *Volume);
/* Resolves a root-relative UTF-16 child name through the directory index. */
RC95LDR_STATUS Rc95LdrNtfsFindChildByName(
    const RC95LDR_NTFS_VOLUME *Volume, uint64_t ParentReference,
    const uint16_t *Name, size_t NameLength, void *Scratch,
    size_t ScratchLength, uint64_t *ChildReference, uint16_t *FileFlags);
/* Reads a bounded prefix of a file addressed by a root-relative UTF-16 path.
 * Scratch must hold two FILE records, one complete index buffer, and one sector
 * of bounce storage (2*FileRecordBytes + IndexBufferBytes + BytesPerSector).
 * The second record resolves directory $ATTRIBUTE_LIST extension records.
 * Directory lookup uses the resident $I30 root and allocated INDX buffers
 * selected by the directory $BITMAP; it does not scan the MFT for names.
 * OutputCapacity must also accommodate sector rounding for nonresident data
 * streams. The volume remains read-only. */
RC95LDR_STATUS Rc95LdrNtfsReadFilePrefix(
    const RC95LDR_NTFS_VOLUME *Volume, const uint16_t *Path,
    size_t PathCharacters, void *Scratch, size_t ScratchLength,
    void *Output, size_t OutputCapacity, size_t *BytesRead,
    uint64_t *FileSize);
/* Reads and applies USA fixups to one MFT FILE record. RecordBuffer must hold
 * Geometry.FileRecordBytes; reads are bounded by the discovered MFT extent. */
RC95LDR_STATUS Rc95LdrNtfsReadMftRecord(
    const RC95LDR_NTFS_VOLUME *Volume, uint64_t RecordNumber,
    void *RecordBuffer, size_t BufferLength);
/* Reads the referenced record and validates its in-use and sequence identity. */
RC95LDR_STATUS Rc95LdrNtfsReadMftRecordByReference(
    const RC95LDR_NTFS_VOLUME *Volume, uint64_t FileReference,
    void *RecordBuffer, size_t BufferLength);
#ifdef __cplusplus
}
#endif
#endif
