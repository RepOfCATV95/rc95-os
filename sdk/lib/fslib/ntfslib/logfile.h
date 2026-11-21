/*
 * Clean-room NTFS $LogFile restart and bounded log-chain validation.
 * Parsing/validation only: these helpers do not perform metadata replay.
 */
#ifndef NTFSLIB_LOGFILE_H
#define NTFSLIB_LOGFILE_H
#include <ntdef.h>
#ifdef __cplusplus
extern "C" {
#endif
#define NTFS_LOGFILE_RESTART_SIGNATURE 0x52545352UL /* "RSTR" */
#define NTFS_LOGFILE_PAGE_SIGNATURE    0x45474150UL /* "PAGE" */
#define NTFS_LOGFILE_RECORD_PAGE_SIGNATURE 0x44524352UL /* "RCRD" */
#define NTFS_LOGFILE_RECORD_PAGE_HEADER_MIN 0x20
#define NTFS_LOGFILE_RESTART_HEADER_MIN 0x20
#define NTFS_LOGFILE_RESTART_AREA_MIN   0x30
#define NTFS_LOGFILE_NO_CLIENT          0xFFFFFFFFUL
typedef enum _NTFS_LFS_FORMAT { NtfsLfsUnknown=0, NtfsLfsVersion1=1, NtfsLfsVersion2=2 } NTFS_LFS_FORMAT;
typedef struct _NTFS_LOGFILE_RESTART_VIEW {
 const UCHAR *Page; ULONG PageLength; ULONG SystemPageSize; ULONG LogPageSize; ULONG RestartOffset;
 USHORT MajorVersion; USHORT MinorVersion; NTFS_LFS_FORMAT Format;
} NTFS_LOGFILE_RESTART_VIEW, *PNTFS_LOGFILE_RESTART_VIEW;
typedef struct _NTFS_LOGFILE_RESTART_AREA_VIEW {
 const UCHAR *Buffer; ULONG Length; ULONGLONG CurrentLsn; ULONG LogClients; ULONG FreeList;
 ULONG InUseList; ULONG Flags; USHORT SequenceNumberBits; ULONG RestartAreaLength;
 ULONG ClientArrayOffset; ULONGLONG FileSize; ULONG LastLsnDataLength;
 USHORT RecordHeaderLength; USHORT LogPageDataOffset;
} NTFS_LOGFILE_RESTART_AREA_VIEW, *PNTFS_LOGFILE_RESTART_AREA_VIEW;

typedef struct _NTFS_LOGFILE_RECORD_PAGE_VIEW
{
 const UCHAR *Page; ULONG PageLength; ULONG DataOffset; ULONG NextRecordOffset;
 ULONGLONG LastLsn; ULONG Flags; USHORT PageCount; USHORT PagePosition;
} NTFS_LOGFILE_RECORD_PAGE_VIEW, *PNTFS_LOGFILE_RECORD_PAGE_VIEW;

/* Version-specific record framing is supplied by the caller. The length
 * field is a little-endian ULONG total length including RecordHeaderLength. */
typedef struct _NTFS_LOGFILE_RECORD_ITERATOR
{
 const NTFS_LOGFILE_RECORD_PAGE_VIEW *Page;
 ULONG Cursor; ULONG RecordHeaderLength; ULONG LengthFieldOffset;
 ULONG Alignment; ULONG ClientIndexFieldOffset; BOOLEAN HasClientIndex;
} NTFS_LOGFILE_RECORD_ITERATOR, *PNTFS_LOGFILE_RECORD_ITERATOR;
typedef struct _NTFS_LOGFILE_RECORD_VIEW
{
 const UCHAR *Buffer; ULONG Offset; ULONG Length; ULONG HeaderLength;
 ULONG ClientIndex; BOOLEAN HasClientIndex;
} NTFS_LOGFILE_RECORD_VIEW, *PNTFS_LOGFILE_RECORD_VIEW;

/* Native LFS_RECORD_HEADER (quadword aligned, 0x30 bytes on disk). */
#define NTFS_LFS_RECORD_HEADER_SIZE 0x30
#define NTFS_LFS_RECORD_MULTI_PAGE  0x0001
typedef struct _NTFS_LFS_RECORD_HEADER_VIEW
{
 ULONGLONG ThisLsn;
 ULONGLONG ClientPreviousLsn;
 ULONGLONG ClientUndoNextLsn;
 ULONG ClientDataLength;
 USHORT ClientSequenceNumber;
 USHORT ClientIndex;
 ULONG RecordType;
 ULONG TransactionId;
 USHORT Flags;
} NTFS_LFS_RECORD_HEADER_VIEW, *PNTFS_LFS_RECORD_HEADER_VIEW;
typedef struct _NTFS_LOGFILE_ASSEMBLED_RECORD
{
 const UCHAR *Buffer; ULONG Length; ULONG HeaderLength;
 ULONG FirstPageIndex; ULONG FirstPageOffset; ULONG FragmentCount;
 NTFS_LFS_RECORD_HEADER_VIEW Header;
} NTFS_LOGFILE_ASSEMBLED_RECORD, *PNTFS_LOGFILE_ASSEMBLED_RECORD;

/* Logical fragments in record-byte order; offsets must cover [0, RecordLength). */
typedef struct _NTFS_LOGFILE_RECORD_FRAGMENT {
 ULONG LogicalOffset; ULONG PageIndex; ULONG PageOffset; ULONG Length; ULONG PageLength;
} NTFS_LOGFILE_RECORD_FRAGMENT, *PNTFS_LOGFILE_RECORD_FRAGMENT;
/* LSN zero terminates a predecessor chain; Links must contain the complete chain. */
typedef struct _NTFS_LOGFILE_TRANSACTION_LINK {
 ULONGLONG Lsn; ULONGLONG PreviousLsn; ULONG ClientIndex;
} NTFS_LOGFILE_TRANSACTION_LINK, *PNTFS_LOGFILE_TRANSACTION_LINK;
NTSTATUS NTAPI NtfsLogfileOpenRestartPage(_In_reads_bytes_(PageLength) const VOID *Page,
 _In_ ULONG PageLength, _In_ ULONG BytesPerSector, _Out_writes_bytes_(PageLength) VOID *Scratch,
 _Out_ PNTFS_LOGFILE_RESTART_VIEW View);
NTSTATUS NTAPI NtfsLogfileOpenRestartArea(_In_ const NTFS_LOGFILE_RESTART_VIEW *Restart,
 _Out_ PNTFS_LOGFILE_RESTART_AREA_VIEW Area);

NTSTATUS NTAPI NtfsLogfileOpenRecordPage(
 _In_reads_bytes_(PageLength) const VOID *Page, _In_ ULONG PageLength,
 _In_ ULONG BytesPerSector, _In_ ULONG DataOffset,
 _Out_writes_bytes_(PageLength) VOID *Scratch,
 _Out_ PNTFS_LOGFILE_RECORD_PAGE_VIEW View);
NTSTATUS NTAPI NtfsLogfileInitializeRecordIterator(
 _In_ const NTFS_LOGFILE_RECORD_PAGE_VIEW *Page,
 _In_ ULONG RecordHeaderLength, _In_ ULONG LengthFieldOffset,
 _In_ ULONG Alignment, _In_ BOOLEAN HasClientIndex,
 _In_ ULONG ClientIndexFieldOffset,
 _Out_ PNTFS_LOGFILE_RECORD_ITERATOR Iterator);
NTSTATUS NTAPI NtfsLogfileNextRecord(
 _Inout_ PNTFS_LOGFILE_RECORD_ITERATOR Iterator,
 _In_ ULONG ClientCount,
 _Out_ PNTFS_LOGFILE_RECORD_VIEW Record);

/* LFS 1.x and 2.0 use the same 0x30-byte record header; separate entry
 * points enforce the selected restart-page format explicitly. */
NTSTATUS NTAPI NtfsLogfileDecodeRecordHeaderV1(
 _In_reads_bytes_(Available) const VOID *Buffer, _In_ ULONG Available,
 _Out_ PNTFS_LFS_RECORD_HEADER_VIEW Header);
NTSTATUS NTAPI NtfsLogfileDecodeRecordHeaderV2(
 _In_reads_bytes_(Available) const VOID *Buffer, _In_ ULONG Available,
 _Out_ PNTFS_LFS_RECORD_HEADER_VIEW Header);
/* Pages are supplied in logical circular order; Scratch receives the joined
 * record bytes and Fragments describes the source-page extents. */
NTSTATUS NTAPI NtfsLogfileAssembleRecord(
 _In_reads_(PageCount) const NTFS_LOGFILE_RECORD_PAGE_VIEW *Pages,
 _In_ ULONG PageCount, _In_ ULONG FirstPageIndex, _In_ ULONG FirstPageOffset,
 _In_ NTFS_LFS_FORMAT Format, _In_ ULONG RecordHeaderLength,
 _In_ ULONG ClientCount, _Out_writes_bytes_(ScratchLength) VOID *Scratch, _In_ ULONG ScratchLength,
 _Out_writes_(FragmentCapacity) NTFS_LOGFILE_RECORD_FRAGMENT *Fragments,
 _In_ ULONG FragmentCapacity, _Out_ PNTFS_LOGFILE_ASSEMBLED_RECORD Record);

NTSTATUS NTAPI NtfsLogfileValidateRecordFragments(
 _In_reads_(FragmentCount) const NTFS_LOGFILE_RECORD_FRAGMENT *Fragments,
 _In_ ULONG FragmentCount, _In_ ULONG RecordLength);
NTSTATUS NTAPI NtfsLogfileAdvanceCircularOffset(_In_ ULONGLONG Offset, _In_ ULONGLONG Delta,
 _In_ ULONGLONG RegionStart, _In_ ULONGLONG RegionLength, _In_ ULONGLONG FileSize,
 _Out_ PULONGLONG Result);
NTSTATUS NTAPI NtfsLogfileCompareLsn(_In_ ULONGLONG FirstLsn, _In_ ULONGLONG SecondLsn,
 _In_ USHORT SequenceNumberBits, _Out_ PLONG Comparison);
NTSTATUS NTAPI NtfsLogfileValidateClientReference(_In_ ULONG ClientIndex, _In_ ULONG ClientCount);
NTSTATUS NTAPI NtfsLogfileValidateTransactionChains(
 _In_reads_(LinkCount) const NTFS_LOGFILE_TRANSACTION_LINK *Links,
 _In_ ULONG LinkCount, _In_ ULONG ClientCount);
#ifdef __cplusplus
}
#endif
#endif /* NTFSLIB_LOGFILE_H */
