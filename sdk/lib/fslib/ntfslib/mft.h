/*
 * Clean-room NTFS FILE-record validation primitives.
 *
 * This module only validates and exposes bounded record/attribute views.
 * It does not perform MFT allocation, log replay, repair, or mount operations.
 */
#ifndef NTFSLIB_MFT_H
#define NTFSLIB_MFT_H

#include <ntdef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NTFS_MFT_FILE_SIGNATURE 0x454C4946UL /* "FILE", little-endian */
#define NTFS_MFT_ATTRIBUTE_END  0xFFFFFFFFUL
#define NTFS_MFT_FILE_HEADER_SIZE 48

typedef struct _NTFS_MFT_RECORD_VIEW
{
    PUCHAR Buffer;
    ULONG RecordLength;
    ULONG BytesInUse;
    ULONG FirstAttributeOffset;
    USHORT SequenceNumber;
    USHORT Flags;
    ULONG RecordNumber;
} NTFS_MFT_RECORD_VIEW, *PNTFS_MFT_RECORD_VIEW;

typedef struct _NTFS_MFT_RUN
{
    ULONGLONG StartingVcn;
    ULONGLONG ClusterCount;
    LONGLONG StartingLcn;
    BOOLEAN Sparse;
} NTFS_MFT_RUN, *PNTFS_MFT_RUN;

typedef struct _NTFS_MFT_ATTRIBUTE_VIEW
{
    PUCHAR Buffer;
    ULONG RecordOffset;
    ULONG Type;
    ULONG Length;
    BOOLEAN NonResident;
    UCHAR NameLength;
    USHORT NameOffset;
    USHORT Flags;
    USHORT Instance;
    ULONG ValueOffset;
    ULONG ValueLength;
    ULONG DataRunsOffset;
    ULONGLONG LowestVcn;
    ULONGLONG HighestVcn;
} NTFS_MFT_ATTRIBUTE_VIEW, *PNTFS_MFT_ATTRIBUTE_VIEW;

/*
 * Apply Update Sequence Array fixups to caller-owned, non-overlapping scratch
 * storage and validate the FILE header. Input is never modified. SectorSize
 * must be a power of two and RecordLength an exact multiple of SectorSize.
 */
NTSTATUS
NTAPI
NtfsMftOpenRecord(
    _In_reads_bytes_(RecordLength) const VOID *Input,
    _In_ ULONG RecordLength,
    _In_ ULONG SectorSize,
    _Out_writes_bytes_(RecordLength) VOID *Scratch,
    _Out_ PNTFS_MFT_RECORD_VIEW View);

/*
 * Decode a mapping-pairs array into caller-owned run storage. The function
 * validates nibble sizes, run lengths, signed LCN deltas and VCN overflow.
 * Sparse runs have Sparse=TRUE and StartingLcn=0.
 */
NTSTATUS
NTAPI
NtfsMftDecodeRunlist(
    _In_reads_bytes_(RunlistLength) const UCHAR *Runlist,
    _In_ ULONG RunlistLength,
    _In_ ULONGLONG StartingVcn,
    _Out_writes_to_(RunCapacity, *RunCount) PNTFS_MFT_RUN Runs,
    _In_ ULONG RunCapacity,
    _Out_ PULONG RunCount);

/* Enumerate one bounded attribute. STATUS_NO_MORE_ENTRIES means end marker. */
NTSTATUS
NTAPI
NtfsMftGetAttribute(
    _In_ const NTFS_MFT_RECORD_VIEW *Record,
    _Inout_ PULONG Cursor,
    _Out_ PNTFS_MFT_ATTRIBUTE_VIEW Attribute);

#ifdef __cplusplus
}
#endif

#endif /* NTFSLIB_MFT_H */
