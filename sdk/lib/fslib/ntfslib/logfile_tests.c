/*
 * Synthetic $LogFile RCRD record fixtures.
 *
 * Enable the object target with -DNTFSLIB_BUILD_TESTS=ON. The exported
 * NtfsLogfileRunRecordFixtures routine is intended to be called by a
 * platform test runner; this file deliberately does not define main().
 */
#if defined(NTFS_LOGFILE_KERNEL_BUILD)
#include <ntifs.h>
#else
#include "ntfslib.h"
#endif
#include "logfile.h"

#define FIXTURE_PAGE_SIZE 512
#define FIXTURE_DATA_OFFSET 0x20
#define FIXTURE_RECORD_LENGTH 0x50
#define FIXTURE_USA_OFFSET 0x28
#define FIXTURE_USA_SEQUENCE 0xA55A

NTSTATUS NTAPI NtfsLogfileRunRecordFixtures(VOID);

static VOID
FixturePut16(UCHAR *Buffer, ULONG Offset, USHORT Value)
{
    Buffer[Offset] = (UCHAR)Value;
    Buffer[Offset + 1] = (UCHAR)(Value >> 8);
}

static VOID
FixturePut32(UCHAR *Buffer, ULONG Offset, ULONG Value)
{
    Buffer[Offset] = (UCHAR)Value;
    Buffer[Offset + 1] = (UCHAR)(Value >> 8);
    Buffer[Offset + 2] = (UCHAR)(Value >> 16);
    Buffer[Offset + 3] = (UCHAR)(Value >> 24);
}

static VOID
FixturePut64(UCHAR *Buffer, ULONG Offset, ULONGLONG Value)
{
    FixturePut32(Buffer, Offset, (ULONG)Value);
    FixturePut32(Buffer, Offset + 4, (ULONG)(Value >> 32));
}

static VOID
FixtureInitializeRecord(UCHAR *Record, USHORT Flags)
{
    ULONG i;

    RtlZeroMemory(Record, FIXTURE_RECORD_LENGTH);
    FixturePut64(Record, 0x00, 0x0000000100001234ULL);
    FixturePut64(Record, 0x08, 0);
    FixturePut64(Record, 0x10, 0);
    FixturePut32(Record, 0x18, 0x20);
    FixturePut16(Record, 0x1C, 7);
    FixturePut16(Record, 0x1E, 0);
    FixturePut32(Record, 0x20, 1);
    FixturePut32(Record, 0x24, 0x12345678);
    FixturePut16(Record, 0x28, Flags);
    for (i = NTFS_LFS_RECORD_HEADER_SIZE; i < FIXTURE_RECORD_LENGTH; ++i)
        Record[i] = (UCHAR)(i ^ 0xA5);
}

static VOID
FixtureInitializePage(
    NTFS_LOGFILE_RECORD_PAGE_VIEW *Page,
    const UCHAR *Buffer,
    ULONG NextOffset,
    USHORT PageCount,
    USHORT PagePosition)
{
    Page->Page = Buffer;
    Page->PageLength = FIXTURE_PAGE_SIZE;
    Page->DataOffset = FIXTURE_DATA_OFFSET;
    Page->NextRecordOffset = NextOffset;
    Page->LastLsn = 0x0000000100001234ULL;
    Page->Flags = 0;
    Page->PageCount = PageCount;
    Page->PagePosition = PagePosition;
}

static NTSTATUS
FixtureAssemble(
    NTFS_LFS_FORMAT Format,
    NTFS_LOGFILE_RECORD_PAGE_VIEW *Pages,
    ULONG PageCount,
    ULONG FirstPage,
    ULONG FirstOffset,
    PVOID Scratch,
    NTFS_LOGFILE_RECORD_FRAGMENT *Fragments,
    ULONG FragmentCapacity,
    PNTFS_LOGFILE_ASSEMBLED_RECORD Record)
{
    return NtfsLogfileAssembleRecord(
        Pages, PageCount, FirstPage, FirstOffset, Format,
        NTFS_LFS_RECORD_HEADER_SIZE, 1, Scratch,
        FIXTURE_RECORD_LENGTH, Fragments, FragmentCapacity, Record);
}

NTSTATUS
NTAPI
NtfsLogfileRunRecordFixtures(VOID)
{
    UCHAR RecordBytes[FIXTURE_RECORD_LENGTH];
    UCHAR Scratch[FIXTURE_RECORD_LENGTH];
    UCHAR PageBytes[3][FIXTURE_PAGE_SIZE];
    UCHAR UsaPage[FIXTURE_PAGE_SIZE];
    UCHAR UsaScratch[FIXTURE_PAGE_SIZE];
    UCHAR RestartPage[FIXTURE_PAGE_SIZE];
    UCHAR RestartScratch[FIXTURE_PAGE_SIZE];
    UCHAR MultiSectorPage[1024];
    UCHAR MultiSectorScratch[1024];
    NTFS_LOGFILE_RESTART_VIEW RestartView;
    UCHAR ClientAreaBytes[0x200];
    NTFS_LOGFILE_RESTART_AREA_VIEW ClientArea;
    NTFS_LOGFILE_CLIENT_ENTRY_VIEW ClientEntries[2];
    ULONG ClientEntriesRead;
    NTFS_LOGFILE_TRANSACTION_EVENT TxEvents[4];
    NTFS_LOGFILE_TRANSACTION_STATE_VIEW TxStates[4];
    ULONG TxStatesWritten;
    NTFS_LOGFILE_RESTART_AREA_VIEW RestartArea;
    NTFS_LOGFILE_RESTART_AREA_VIEW AlternateRestartArea;
    ULONG SelectedRestart;
    NTFS_LOGFILE_RECORD_ITERATOR Iterator;
    NTFS_LOGFILE_RECORD_VIEW IteratedRecord;
    NTFS_LOGFILE_TRANSACTION_LINK Links[3];
    ULONGLONG CircularOffset;
    LONG LsnComparison;
    NTFS_LOGFILE_RECORD_PAGE_VIEW UsaView;
    NTFS_LOGFILE_RECORD_PAGE_VIEW Pages[3];
    NTFS_LOGFILE_RECORD_FRAGMENT Fragments[3];
    NTFS_LOGFILE_ASSEMBLED_RECORD Record;
    UCHAR OperationBytes[0x60];
    NTFS_LOGFILE_ASSEMBLED_RECORD OperationRecord;
    NTFS_LOGFILE_NTFS_OPERATION_VIEW OperationView;
    ULONGLONG OperationLcn;
    NTFS_LFS_RECORD_HEADER_VIEW Header;
    NTSTATUS Status;

    /* V1 decoder and a record split over two pages across circular wrap. */
    FixtureInitializeRecord(RecordBytes, NTFS_LFS_RECORD_MULTI_PAGE);
    Status = NtfsLogfileDecodeRecordHeaderV1(
        RecordBytes, sizeof(RecordBytes), &Header);
    if (!NT_SUCCESS(Status) || Header.ThisLsn != 0x0000000100001234ULL ||
        Header.ClientDataLength != 0x20 || Header.ClientIndex != 0)
        return STATUS_DATA_ERROR;

    /* Decode the NTFS-specific operation envelope without replaying it. */
    RtlZeroMemory(OperationBytes, sizeof(OperationBytes));
    RtlZeroMemory(&OperationRecord, sizeof(OperationRecord));
    OperationRecord.Buffer = OperationBytes;
    OperationRecord.Length = sizeof(OperationBytes);
    OperationRecord.HeaderLength = NTFS_LFS_RECORD_HEADER_SIZE;
    OperationRecord.Header.ClientDataLength = 0x30;
    FixturePut16(OperationBytes, 0x30, 0x0008);
    FixturePut16(OperationBytes, 0x32, 0x0007);
    FixturePut16(OperationBytes, 0x34, 0x28);
    FixturePut16(OperationBytes, 0x36, 4);
    FixturePut16(OperationBytes, 0x38, 0x2C);
    FixturePut16(OperationBytes, 0x3A, 4);
    FixturePut16(OperationBytes, 0x3C, 0x80);
    FixturePut16(OperationBytes, 0x3E, 1);
    FixturePut16(OperationBytes, 0x40, 0x18);
    FixturePut16(OperationBytes, 0x42, 0x20);
    FixturePut64(OperationBytes, 0x48, 0x1234);
    FixturePut64(OperationBytes, 0x50, 0x5678);
    Status = NtfsLogfileDecodeNtfsOperation(
        &OperationRecord, &OperationView);
    if (!NT_SUCCESS(Status) || OperationView.RedoOperation != 8 ||
        OperationView.UndoOperation != 7 ||
        OperationView.RedoOffset != 0x28 ||
        OperationView.UndoLength != 4 ||
        OperationView.RedoData != OperationBytes + 0x58 ||
        OperationView.UndoData != OperationBytes + 0x5C ||
        OperationView.TargetAttribute != 0x80 ||
        OperationView.LcnsToFollow != 1 ||
        OperationView.TargetVcn != 0x1234 ||
        OperationView.LcnList[0] != 0x78 ||
        OperationView.LcnList[1] != 0x56 ||
        OperationView.LcnList[2] != 0x00 ||
        OperationView.LcnList[3] != 0x00 ||
        OperationView.LcnList[4] != 0x00 ||
        OperationView.LcnList[5] != 0x00 ||
        OperationView.LcnList[6] != 0x00 ||
        OperationView.LcnList[7] != 0x00 ||
        !NT_SUCCESS(NtfsLogfileReadOperationLcn(
            &OperationView, 0, &OperationLcn)) ||
        OperationLcn != 0x5678 ||
        NtfsLogfileReadOperationLcn(
            &OperationView, 1, &OperationLcn) != STATUS_INVALID_PARAMETER)
        return STATUS_DATA_ERROR;

    FixturePut16(OperationBytes, 0x34, 0x2F);
    if (NtfsLogfileDecodeNtfsOperation(
            &OperationRecord, &OperationView) != STATUS_DATA_ERROR ||
        OperationView.RedoOperation != 0)
        return STATUS_DATA_ERROR;
    FixturePut16(OperationBytes, 0x34, 0x20);
    if (NtfsLogfileDecodeNtfsOperation(
            &OperationRecord, &OperationView) != STATUS_DATA_ERROR)
        return STATUS_DATA_ERROR;
    FixturePut16(OperationBytes, 0x34, 0x28);
    FixturePut16(OperationBytes, 0x3E, 3);
    if (NtfsLogfileDecodeNtfsOperation(
            &OperationRecord, &OperationView) != STATUS_DATA_ERROR)
        return STATUS_DATA_ERROR;
    FixturePut16(OperationBytes, 0x3E, 1);
    OperationRecord.Header.ClientDataLength = 0x1F;
    if (NtfsLogfileDecodeNtfsOperation(
            &OperationRecord, &OperationView) != STATUS_DATA_ERROR)
        return STATUS_DATA_ERROR;

    RtlZeroMemory(PageBytes, sizeof(PageBytes));
    RtlCopyMemory(PageBytes[2] + FIXTURE_DATA_OFFSET, RecordBytes, 0x38);
    RtlCopyMemory(PageBytes[0] + FIXTURE_DATA_OFFSET, RecordBytes + 0x38, 0x18);
    FixtureInitializePage(&Pages[0], PageBytes[0], 0x38, 3, 0);
    FixtureInitializePage(&Pages[1], PageBytes[1], FIXTURE_DATA_OFFSET, 3, 1);
    FixtureInitializePage(&Pages[2], PageBytes[2], 0x58, 3, 2);

    Status = FixtureAssemble(NtfsLfsVersion1, Pages, 3, 2,
                             FIXTURE_DATA_OFFSET, Scratch, Fragments, 3, &Record);
    if (!NT_SUCCESS(Status) || Record.FragmentCount != 2 ||
        RtlCompareMemory(Scratch, RecordBytes, sizeof(RecordBytes)) != sizeof(RecordBytes))
        return STATUS_DATA_ERROR;

    /* V2 decoder and a three-page record which wraps 1 -> 2 -> 0. */
    Status = NtfsLogfileDecodeRecordHeaderV2(
        RecordBytes, sizeof(RecordBytes), &Header);
    if (!NT_SUCCESS(Status) || Header.TransactionId != 0x12345678)
        return STATUS_DATA_ERROR;

    RtlZeroMemory(PageBytes, sizeof(PageBytes));
    RtlCopyMemory(PageBytes[1] + FIXTURE_DATA_OFFSET, RecordBytes, 0x38);
    RtlCopyMemory(PageBytes[2] + FIXTURE_DATA_OFFSET, RecordBytes + 0x38, 0x10);
    RtlCopyMemory(PageBytes[0] + FIXTURE_DATA_OFFSET, RecordBytes + 0x48, 0x08);
    FixtureInitializePage(&Pages[0], PageBytes[0], 0x28, 3, 0);
    FixtureInitializePage(&Pages[1], PageBytes[1], 0x58, 3, 1);
    FixtureInitializePage(&Pages[2], PageBytes[2], 0x30, 3, 2);

    Status = FixtureAssemble(NtfsLfsVersion2, Pages, 3, 1,
                             FIXTURE_DATA_OFFSET, Scratch, Fragments, 3, &Record);
    if (!NT_SUCCESS(Status) || Record.FragmentCount != 3 ||
        RtlCompareMemory(Scratch, RecordBytes, sizeof(RecordBytes)) != sizeof(RecordBytes))
        return STATUS_DATA_ERROR;

    /* The supplied circular array must match the page header's ring size. */
    Pages[1].PageCount = 4;
    Status = FixtureAssemble(NtfsLfsVersion2, Pages, 3, 1,
                             FIXTURE_DATA_OFFSET, Scratch, Fragments, 3, &Record);
    if (NT_SUCCESS(Status))
        return STATUS_DATA_ERROR;
    Pages[1].PageCount = 3;

    /* A skipped continuation position must be rejected. */
    Pages[2].PagePosition = 0;
    Status = FixtureAssemble(NtfsLfsVersion2, Pages, 3, 1,
                             FIXTURE_DATA_OFFSET, Scratch, Fragments, 3, &Record);
    if (NT_SUCCESS(Status))
        return STATUS_DATA_ERROR;

    /* A multi-page split without the multi-page flag is corrupt. */
    FixtureInitializeRecord(RecordBytes, 0);
    RtlCopyMemory(PageBytes[1] + FIXTURE_DATA_OFFSET, RecordBytes, 0x38);
    RtlCopyMemory(PageBytes[2] + FIXTURE_DATA_OFFSET, RecordBytes + 0x38, 0x18);
    FixtureInitializePage(&Pages[1], PageBytes[1], 0x58, 3, 1);
    FixtureInitializePage(&Pages[2], PageBytes[2], 0x38, 3, 2);
    Status = FixtureAssemble(NtfsLfsVersion1, Pages, 3, 1,
                             FIXTURE_DATA_OFFSET, Scratch, Fragments, 3, &Record);
    if (NT_SUCCESS(Status))
        return STATUS_DATA_ERROR;

    /*
     * A well-formed one-sector RCRD page: the USA occupies the first-sector
     * header area, and the sector trailer is replaced by the saved word.
     */
    RtlZeroMemory(UsaPage, sizeof(UsaPage));
    FixturePut32(UsaPage, 0x00, NTFS_LOGFILE_RECORD_PAGE_SIGNATURE);
    FixturePut16(UsaPage, 0x04, FIXTURE_USA_OFFSET);
    FixturePut16(UsaPage, 0x06, 2); /* one sector plus the USA sequence word */
    FixturePut64(UsaPage, 0x08, 0x0000000100001234ULL);
    FixturePut32(UsaPage, 0x10, 0);
    FixturePut16(UsaPage, 0x14, 1);
    FixturePut16(UsaPage, 0x16, 0);
    FixturePut16(UsaPage, 0x18, 0x30);
    FixturePut16(UsaPage, FIXTURE_USA_OFFSET, FIXTURE_USA_SEQUENCE);
    FixturePut16(UsaPage, FIXTURE_USA_OFFSET + 2, 0xBEEF);
    FixturePut16(UsaPage, FIXTURE_PAGE_SIZE - 2, FIXTURE_USA_SEQUENCE);

    Status = NtfsLogfileOpenRecordPage(
        UsaPage, sizeof(UsaPage), 512, 0x30,
        UsaScratch, &UsaView);
    if (!NT_SUCCESS(Status) ||
        UsaView.Page != UsaScratch ||
        UsaScratch[FIXTURE_PAGE_SIZE - 2] != 0xEF ||
        UsaScratch[FIXTURE_PAGE_SIZE - 1] != 0xBE)
        return STATUS_DATA_ERROR;

    /* A two-sector RCRD page must restore both USA-protected trailers. */
    RtlZeroMemory(MultiSectorPage, sizeof(MultiSectorPage));
    FixturePut32(MultiSectorPage, 0x00, NTFS_LOGFILE_RECORD_PAGE_SIGNATURE);
    FixturePut16(MultiSectorPage, 0x04, 0x28);
    FixturePut16(MultiSectorPage, 0x06, 3);
    FixturePut64(MultiSectorPage, 0x08, 0x0000000100001234ULL);
    FixturePut16(MultiSectorPage, 0x14, 1);
    FixturePut16(MultiSectorPage, 0x16, 0);
    FixturePut16(MultiSectorPage, 0x18, 0x30);
    FixturePut16(MultiSectorPage, 0x28, 0x5AA5);
    FixturePut16(MultiSectorPage, 0x2A, 0x1111);
    FixturePut16(MultiSectorPage, 0x2C, 0x2222);
    FixturePut16(MultiSectorPage, 510, 0x5AA5);
    FixturePut16(MultiSectorPage, 1022, 0x5AA5);
    Status = NtfsLogfileOpenRecordPage(
        MultiSectorPage, sizeof(MultiSectorPage), 512, 0x30,
        MultiSectorScratch, &UsaView);
    if (!NT_SUCCESS(Status) ||
        MultiSectorScratch[510] != 0x11 ||
        MultiSectorScratch[511] != 0x11 ||
        MultiSectorScratch[1022] != 0x22 ||
        MultiSectorScratch[1023] != 0x22)
        return STATUS_DATA_ERROR;

    /* Record data must not overlap the update-sequence array itself. */
    Status = NtfsLogfileOpenRecordPage(
        UsaPage, sizeof(UsaPage), 512, FIXTURE_DATA_OFFSET,
        UsaScratch, &UsaView);
    if (NT_SUCCESS(Status))
        return STATUS_DATA_ERROR;

    /* USA count must equal the number of sectors plus one. */
    FixturePut16(UsaPage, 0x06, 1);
    Status = NtfsLogfileOpenRecordPage(
        UsaPage, sizeof(UsaPage), 512, 0x30,
        UsaScratch, &UsaView);
    if (NT_SUCCESS(Status))
        return STATUS_DATA_ERROR;

    /* The complete USA array must fit before the first sector trailer. */
    FixturePut16(UsaPage, 0x06, 2);
    FixturePut16(UsaPage, 0x04, 0x01FE);
    Status = NtfsLogfileOpenRecordPage(
        UsaPage, sizeof(UsaPage), 512, 0x30,
        UsaScratch, &UsaView);
    if (NT_SUCCESS(Status))
        return STATUS_DATA_ERROR;

    /* Every protected sector trailer must contain the USA sequence word. */
    FixturePut16(UsaPage, 0x04, FIXTURE_USA_OFFSET);
    FixturePut16(UsaPage, FIXTURE_PAGE_SIZE - 2, 0xDEAD);
    Status = NtfsLogfileOpenRecordPage(
        UsaPage, sizeof(UsaPage), 512, 0x30,
        UsaScratch, &UsaView);
    if (NT_SUCCESS(Status))
        return STATUS_DATA_ERROR;


    /*
     * Restart page and restart-area fixtures. These validate USA restoration,
     * version selection, fixed-width field decoding, and area-relative bounds.
     */
    RtlZeroMemory(RestartPage, sizeof(RestartPage));
    FixturePut32(RestartPage, 0x00, NTFS_LOGFILE_RESTART_SIGNATURE);
    FixturePut16(RestartPage, 0x04, 0x30);
    FixturePut16(RestartPage, 0x06, 2);
    FixturePut32(RestartPage, 0x10, FIXTURE_PAGE_SIZE);
    FixturePut32(RestartPage, 0x14, FIXTURE_PAGE_SIZE);
    FixturePut16(RestartPage, 0x18, 0x40);
    FixturePut16(RestartPage, 0x1A, 0);
    FixturePut16(RestartPage, 0x1C, 1);
    FixturePut16(RestartPage, 0x30, FIXTURE_USA_SEQUENCE);
    FixturePut16(RestartPage, 0x32, 0xBEEF);
    FixturePut16(RestartPage, FIXTURE_PAGE_SIZE - 2, FIXTURE_USA_SEQUENCE);

    /* A minimal valid 1.x restart area with no active clients. */
    FixturePut64(RestartPage, 0x40, 0x0001000000001000ULL);
    FixturePut16(RestartPage, 0x48, 0);
    FixturePut16(RestartPage, 0x4A, 0xFFFF);
    FixturePut16(RestartPage, 0x4C, 0xFFFF);
    FixturePut16(RestartPage, 0x4E, 0);
    FixturePut32(RestartPage, 0x50, 48);
    FixturePut16(RestartPage, 0x54, 48);
    FixturePut16(RestartPage, 0x56, 0x30);
    FixturePut64(RestartPage, 0x58, 0x10000);
    FixturePut32(RestartPage, 0x60, 0x200);
    FixturePut16(RestartPage, 0x64, 0x30);
    FixturePut16(RestartPage, 0x66, 0x30);
    FixturePut32(RestartPage, 0x68, 1);

    Status = NtfsLogfileOpenRestartPage(
        RestartPage, sizeof(RestartPage), 512,
        RestartScratch, &RestartView);
    if (!NT_SUCCESS(Status) || RestartView.Format != NtfsLfsVersion1 ||
        RestartView.Page != RestartScratch)
        return STATUS_DATA_ERROR;

    Status = NtfsLogfileOpenRestartArea(&RestartView, &RestartArea);
    if (!NT_SUCCESS(Status) || RestartArea.LogClients != 0 ||
        RestartArea.SequenceNumberBits != 48 ||
        RestartArea.FileSize != 0x10000 ||
        RestartArea.RecordHeaderLength != 0x30 ||
        RestartArea.RestartLogOpenCount != 1 ||
        RestartArea.Reserved != 0)
        return STATUS_DATA_ERROR;

    RestartView.LogPageSize = 0;
    if (NtfsLogfileOpenRestartArea(
            &RestartView, &AlternateRestartArea) != STATUS_DATA_ERROR)
        return STATUS_DATA_ERROR;
    RestartView.LogPageSize = FIXTURE_PAGE_SIZE;

    /*
     * Restart-page selection uses modular LSN ordering. Equal LSNs
     * deterministically retain the first copy; half-range and width
     * mismatches are rejected without changing the caller's output.
     */
    AlternateRestartArea = RestartArea;
    AlternateRestartArea.CurrentLsn = RestartArea.CurrentLsn + 0x100;
    SelectedRestart = 99;
    Status = NtfsLogfileSelectRestartArea(
        &RestartArea, &AlternateRestartArea, &SelectedRestart);
    if (!NT_SUCCESS(Status) || SelectedRestart != 1)
        return STATUS_DATA_ERROR;
    AlternateRestartArea.CurrentLsn = RestartArea.CurrentLsn;
    Status = NtfsLogfileSelectRestartArea(
        &RestartArea, &AlternateRestartArea, &SelectedRestart);
    if (!NT_SUCCESS(Status) || SelectedRestart != 0)
        return STATUS_DATA_ERROR;
    /* Restart-page selection must use modular sequence ordering at wrap. */
    RestartArea.SequenceNumberBits = 16;
    AlternateRestartArea.SequenceNumberBits = 16;
    RestartArea.CurrentLsn = 0xFFFF000000000080ULL;
    AlternateRestartArea.CurrentLsn = 0x0000000000000100ULL;
    Status = NtfsLogfileSelectRestartArea(
        &RestartArea, &AlternateRestartArea, &SelectedRestart);
    if (!NT_SUCCESS(Status) || SelectedRestart != 1)
        return STATUS_DATA_ERROR;
    AlternateRestartArea.CurrentLsn = 0x8000000000000000ULL;
    RestartArea.CurrentLsn = 0;
    RestartArea.SequenceNumberBits = 4;
    AlternateRestartArea.SequenceNumberBits = 4;
    SelectedRestart = 99;
    if (NtfsLogfileSelectRestartArea(
            &RestartArea, &AlternateRestartArea, &SelectedRestart) !=
            STATUS_DATA_ERROR || SelectedRestart != 99)
        return STATUS_DATA_ERROR;
    AlternateRestartArea.SequenceNumberBits = 5;
    if (NtfsLogfileSelectRestartArea(
            &RestartArea, &AlternateRestartArea, &SelectedRestart) !=
            STATUS_INVALID_PARAMETER || SelectedRestart != 99)
        return STATUS_DATA_ERROR;
    if (NtfsLogfileSelectRestartArea(
            NULL, &AlternateRestartArea, &SelectedRestart) !=
            STATUS_INVALID_PARAMETER)
        return STATUS_DATA_ERROR;

    /* Restart area length must cover its fixed header. */
    FixturePut16(RestartScratch, 0x54, 0x2F);
    Status = NtfsLogfileOpenRestartArea(&RestartView, &RestartArea);
    if (NT_SUCCESS(Status))
        return STATUS_DATA_ERROR;
    FixturePut16(RestartScratch, 0x54, 0x30);

    /* Client-array offsets are aligned and must fit the declared area. */
    FixturePut16(RestartScratch, 0x56, 0x29);
    Status = NtfsLogfileOpenRestartArea(&RestartView, &RestartArea);
    if (NT_SUCCESS(Status))
        return STATUS_DATA_ERROR;
    FixturePut16(RestartScratch, 0x56, 0x30);

    /* A list head cannot refer to a client when the array is empty. */
    FixturePut16(RestartScratch, 0x4C, 0);
    Status = NtfsLogfileOpenRestartArea(&RestartView, &RestartArea);
    if (NT_SUCCESS(Status))
        return STATUS_DATA_ERROR;
    FixturePut16(RestartScratch, 0x4C, 0xFFFF);

    /* Sequence-number width and page-data framing are mandatory. */
    FixturePut32(RestartScratch, 0x50, 0);
    Status = NtfsLogfileOpenRestartArea(&RestartView, &RestartArea);
    if (NT_SUCCESS(Status))
        return STATUS_DATA_ERROR;
    FixturePut32(RestartScratch, 0x50, 48);
    FixturePut32(RestartScratch, 0x50, 64);
    Status = NtfsLogfileOpenRestartArea(&RestartView, &RestartArea);
    if (NT_SUCCESS(Status))
        return STATUS_DATA_ERROR;
    FixturePut32(RestartScratch, 0x50, 48);
    FixturePut16(RestartScratch, 0x66, 0x20);
    Status = NtfsLogfileOpenRestartArea(&RestartView, &RestartArea);
    if (NT_SUCCESS(Status))
        return STATUS_DATA_ERROR;
    FixturePut16(RestartScratch, 0x66, 0x30);

    /* The restart-area file extent and last-LSN data length must be sane. */
    FixturePut64(RestartScratch, 0x58, 0);
    Status = NtfsLogfileOpenRestartArea(&RestartView, &RestartArea);
    if (NT_SUCCESS(Status))
        return STATUS_DATA_ERROR;
    FixturePut64(RestartScratch, 0x58, 0x10000);
    FixturePut64(RestartScratch, 0x58, 0x10001);
    Status = NtfsLogfileOpenRestartArea(&RestartView, &RestartArea);
    if (NT_SUCCESS(Status))
        return STATUS_DATA_ERROR;
    FixturePut64(RestartScratch, 0x58, 0x10000);
    FixturePut64(RestartScratch, 0x58, 0x8000000000000000ULL);
    Status = NtfsLogfileOpenRestartArea(&RestartView, &RestartArea);
    if (NT_SUCCESS(Status))
        return STATUS_DATA_ERROR;
    FixturePut64(RestartScratch, 0x58, 0x10000);
    FixturePut32(RestartScratch, 0x60, 0x10001);
    Status = NtfsLogfileOpenRestartArea(&RestartView, &RestartArea);
    if (NT_SUCCESS(Status))
        return STATUS_DATA_ERROR;
    FixturePut32(RestartScratch, 0x60, 0x200);

    /* Client count must fit the declared restart-area client array. */
    FixturePut16(RestartScratch, 0x48, 1);
    Status = NtfsLogfileOpenRestartArea(&RestartView, &RestartArea);
    if (NT_SUCCESS(Status))
        return STATUS_DATA_ERROR;
    FixturePut16(RestartScratch, 0x48, 0);

    /* The USA must not overlap the fixed restart header or restart area. */
    FixturePut16(RestartPage, 0x04, 0x18);
    Status = NtfsLogfileOpenRestartPage(
        RestartPage, sizeof(RestartPage), 512,
        RestartScratch, &RestartView);
    if (NT_SUCCESS(Status))
        return STATUS_DATA_ERROR;
    FixturePut16(RestartPage, 0x04, 0x40);
    Status = NtfsLogfileOpenRestartPage(
        RestartPage, sizeof(RestartPage), 512,
        RestartScratch, &RestartView);
    if (NT_SUCCESS(Status))
        return STATUS_DATA_ERROR;
    FixturePut16(RestartPage, 0x04, 0x30);

    /* A restart page with an unsupported major version must not be guessed. */
    FixturePut16(RestartPage, 0x1C, 3);
    Status = NtfsLogfileOpenRestartPage(
        RestartPage, sizeof(RestartPage), 512,
        RestartScratch, &RestartView);
    if (Status != STATUS_NOT_SUPPORTED)
        return STATUS_DATA_ERROR;
    FixturePut16(RestartPage, 0x1C, 1);

    /* Torn/corrupt USA trailers invalidate the complete restart page. */
    FixturePut16(RestartPage, FIXTURE_PAGE_SIZE - 2, 0xDEAD);
    Status = NtfsLogfileOpenRestartPage(
        RestartPage, sizeof(RestartPage), 512,
        RestartScratch, &RestartView);
    if (NT_SUCCESS(Status))
        return STATUS_DATA_ERROR;
    FixturePut16(RestartPage, FIXTURE_PAGE_SIZE - 2, FIXTURE_USA_SEQUENCE);

    /* Iterator bounds, alignment, client-index checks, and EOF behavior. */
    RtlZeroMemory(PageBytes[0], sizeof(PageBytes[0]));
    FixturePut32(PageBytes[0], 0x20, 0x20);
    FixturePut16(PageBytes[0], 0x24, 0);
    FixturePut32(PageBytes[0], 0x40, 0x20);
    FixturePut16(PageBytes[0], 0x44, 0);
    Pages[0].Page = PageBytes[0];
    Pages[0].PageLength = FIXTURE_PAGE_SIZE;
    Pages[0].DataOffset = 0x20;
    Pages[0].NextRecordOffset = 0x60;
    Status = NtfsLogfileInitializeRecordIterator(
        &Pages[0], 0x10, 0, 8, TRUE, 4, &Iterator);
    if (!NT_SUCCESS(Status))
        return STATUS_DATA_ERROR;
    Status = NtfsLogfileNextRecord(&Iterator, 1, &IteratedRecord);
    if (!NT_SUCCESS(Status) || IteratedRecord.Offset != 0x20 ||
        IteratedRecord.Length != 0x20 || IteratedRecord.ClientIndex != 0)
        return STATUS_DATA_ERROR;
    Status = NtfsLogfileNextRecord(&Iterator, 1, &IteratedRecord);
    if (!NT_SUCCESS(Status) || IteratedRecord.Offset != 0x40)
        return STATUS_DATA_ERROR;
    if (NtfsLogfileNextRecord(&Iterator, 1, &IteratedRecord) !=
        STATUS_NO_MORE_ENTRIES)
        return STATUS_DATA_ERROR;

    /* Caller-tampered iterator fields must fail closed without arithmetic wrap. */
    Status = NtfsLogfileInitializeRecordIterator(
        &Pages[0], 0x10, 0, 8, TRUE, 4, &Iterator);
    if (!NT_SUCCESS(Status))
        return STATUS_DATA_ERROR;
    Iterator.Alignment = 0;
    if (NtfsLogfileNextRecord(&Iterator, 1, &IteratedRecord) != STATUS_DATA_ERROR)
        return STATUS_DATA_ERROR;

    Status = NtfsLogfileInitializeRecordIterator(
        &Pages[0], 0x10, 0, 8, TRUE, 4, &Iterator);
    if (!NT_SUCCESS(Status))
        return STATUS_DATA_ERROR;
    Iterator.Cursor = Pages[0].NextRecordOffset + 1;
    if (NtfsLogfileNextRecord(&Iterator, 1, &IteratedRecord) != STATUS_DATA_ERROR)
        return STATUS_DATA_ERROR;

    Status = NtfsLogfileInitializeRecordIterator(
        &Pages[0], 0x10, 0, 8, TRUE, 4, &Iterator);
    if (!NT_SUCCESS(Status))
        return STATUS_DATA_ERROR;
    Iterator.LengthFieldOffset = MAXULONG;
    if (NtfsLogfileNextRecord(&Iterator, 1, &IteratedRecord) != STATUS_DATA_ERROR)
        return STATUS_DATA_ERROR;

    Status = NtfsLogfileInitializeRecordIterator(
        &Pages[0], 0x10, 0, 8, TRUE, 4, &Iterator);
    if (!NT_SUCCESS(Status))
        return STATUS_DATA_ERROR;
    Pages[0].NextRecordOffset = Pages[0].PageLength + 1;
    if (NtfsLogfileNextRecord(&Iterator, 1, &IteratedRecord) != STATUS_DATA_ERROR)
        return STATUS_DATA_ERROR;
    Pages[0].NextRecordOffset = 0x60;

    FixturePut32(PageBytes[0], 0x20, 0x48);
    Status = NtfsLogfileInitializeRecordIterator(
        &Pages[0], 0x10, 0, 8, TRUE, 4, &Iterator);
    if (!NT_SUCCESS(Status) ||
        NtfsLogfileNextRecord(&Iterator, 1, &IteratedRecord) != STATUS_DATA_ERROR)
        return STATUS_DATA_ERROR;

    /* Client references must be real entries, never the no-client sentinel. */
    if (!NT_SUCCESS(NtfsLogfileValidateClientReference(0, 1)) ||
        NtfsLogfileValidateClientReference(1, 1) != STATUS_DATA_ERROR ||
        NtfsLogfileValidateClientReference(NTFS_LOGFILE_NO_CLIENT, 1) !=
            STATUS_DATA_ERROR)
        return STATUS_DATA_ERROR;

    /* Fragment extents must be contiguous logically and disjoint physically. */
    {
        NTFS_LOGFILE_RECORD_FRAGMENT FragmentSet[2];

        FragmentSet[0].LogicalOffset = 0;
        FragmentSet[0].PageIndex = 0;
        FragmentSet[0].PageOffset = 0x20;
        FragmentSet[0].Length = 0x10;
        FragmentSet[0].PageLength = 0x200;
        FragmentSet[1].LogicalOffset = 0x10;
        FragmentSet[1].PageIndex = 1;
        FragmentSet[1].PageOffset = 0x30;
        FragmentSet[1].Length = 0x10;
        FragmentSet[1].PageLength = 0x200;
        if (!NT_SUCCESS(NtfsLogfileValidateRecordFragments(
                FragmentSet, 2, 0x20)))
            return STATUS_DATA_ERROR;

        FragmentSet[1].LogicalOffset = 0x11;
        if (NtfsLogfileValidateRecordFragments(
                FragmentSet, 2, 0x20) != STATUS_DATA_ERROR)
            return STATUS_DATA_ERROR;

        FragmentSet[1].LogicalOffset = 0x10;
        FragmentSet[1].PageIndex = 0;
        FragmentSet[1].PageOffset = 0x28;
        if (NtfsLogfileValidateRecordFragments(
                FragmentSet, 2, 0x20) != STATUS_DATA_ERROR)
            return STATUS_DATA_ERROR;
    }

    /* Modular LSN ordering must handle wrap and reject ambiguous half-range. */
    Status = NtfsLogfileCompareLsn(
        0x0000000000000100ULL, 0xFFFF000000000080ULL, 16,
        &LsnComparison);
    if (!NT_SUCCESS(Status) || LsnComparison != 1)
        return STATUS_DATA_ERROR;
    if (NtfsLogfileCompareLsn(0x8000000000000000ULL, 0, 4,
                              &LsnComparison) != STATUS_DATA_ERROR)
        return STATUS_DATA_ERROR;

    /* Invalid LSN sequence widths and malformed native headers fail closed. */
    if (NtfsLogfileCompareLsn(1, 2, 0, &LsnComparison) !=
            STATUS_INVALID_PARAMETER ||
        NtfsLogfileCompareLsn(1, 2, 64, &LsnComparison) !=
            STATUS_INVALID_PARAMETER)
        return STATUS_DATA_ERROR;
    FixtureInitializeRecord(RecordBytes, 0);
    FixturePut64(RecordBytes, 0x00, 0);
    if (NT_SUCCESS(NtfsLogfileDecodeRecordHeaderV1(
            RecordBytes, sizeof(RecordBytes), &Header)))
        return STATUS_DATA_ERROR;
    FixturePut64(RecordBytes, 0x00, 0x0000000100001234ULL);
    FixturePut16(RecordBytes, 0x28, 0x8000);
    if (NT_SUCCESS(NtfsLogfileDecodeRecordHeaderV2(
            RecordBytes, sizeof(RecordBytes), &Header)))
        return STATUS_DATA_ERROR;

    /* Circular log offsets wrap without overflowing the file extent. */
    Status = NtfsLogfileAdvanceCircularOffset(
        190, 20, 100, 100, 1000, &CircularOffset);
    if (!NT_SUCCESS(Status) || CircularOffset != 110)
        return STATUS_DATA_ERROR;
    if (NtfsLogfileAdvanceCircularOffset(
            99, 1, 100, 100, 1000, &CircularOffset) !=
        STATUS_INVALID_PARAMETER)
        return STATUS_DATA_ERROR;
    if (NtfsLogfileAdvanceCircularOffset(
            190, (~(ULONGLONG)0), 100, 100, 1000, &CircularOffset) !=
        STATUS_SUCCESS)
        return STATUS_DATA_ERROR;
    if (NtfsLogfileAdvanceCircularOffset(
            190, 1, 100, (~(ULONGLONG)0), (~(ULONGLONG)0), &CircularOffset) !=
        STATUS_INVALID_PARAMETER)
        return STATUS_DATA_ERROR;

    /* Transaction predecessor chains: valid, missing, cross-client, cyclic. */
    Links[0].Lsn = 0x100;
    Links[0].PreviousLsn = 0;
    Links[0].ClientIndex = 0;
    Links[1].Lsn = 0x200;
    Links[1].PreviousLsn = 0x100;
    Links[1].ClientIndex = 0;
    Links[2].Lsn = 0x300;
    Links[2].PreviousLsn = 0x200;
    Links[2].ClientIndex = 0;
    Status = NtfsLogfileValidateTransactionChains(Links, 3, 1);
    if (!NT_SUCCESS(Status))
        return STATUS_DATA_ERROR;

    Links[2].PreviousLsn = 0x400;
    if (NtfsLogfileValidateTransactionChains(Links, 3, 1) != STATUS_DATA_ERROR)
        return STATUS_DATA_ERROR;
    Links[2].PreviousLsn = 0x200;
    Links[2].ClientIndex = 1;
    if (NtfsLogfileValidateTransactionChains(Links, 3, 2) != STATUS_DATA_ERROR)
        return STATUS_DATA_ERROR;
    Links[2].ClientIndex = 0;
    Links[0].PreviousLsn = 0x300;
    if (NtfsLogfileValidateTransactionChains(Links, 3, 1) != STATUS_DATA_ERROR)
        return STATUS_DATA_ERROR;
    Links[0].PreviousLsn = 0;
    Links[2].Lsn = 0x200;
    if (NtfsLogfileValidateTransactionChains(Links, 3, 1) != STATUS_DATA_ERROR)
        return STATUS_DATA_ERROR;

    /* Self-cycles, two-node cycles, invalid clients, and empty inputs fail. */
    Links[2].Lsn = 0x300;
    Links[0].PreviousLsn = 0x100;
    if (NtfsLogfileValidateTransactionChains(Links, 3, 1) != STATUS_DATA_ERROR)
        return STATUS_DATA_ERROR;
    Links[0].PreviousLsn = 0x200;
    Links[1].PreviousLsn = 0x100;
    if (NtfsLogfileValidateTransactionChains(Links, 3, 1) != STATUS_DATA_ERROR)
        return STATUS_DATA_ERROR;
    Links[0].PreviousLsn = 0;
    Links[1].PreviousLsn = 0x100;
    Links[2].PreviousLsn = 0x200;
    Links[2].ClientIndex = 1;
    if (NtfsLogfileValidateTransactionChains(Links, 3, 1) != STATUS_DATA_ERROR)
        return STATUS_DATA_ERROR;
    Links[2].ClientIndex = 0;
    if (NtfsLogfileValidateTransactionChains(NULL, 0, 1) != STATUS_INVALID_PARAMETER ||
        NtfsLogfileValidateTransactionChains(Links, 0, 1) != STATUS_INVALID_PARAMETER ||
        NtfsLogfileValidateTransactionChains(Links, 3, 0) != STATUS_INVALID_PARAMETER)
        return STATUS_DATA_ERROR;

    /*
     * Restart client-table extraction validates the complete table before
     * exposing entries and rejects links/name extents outside the area.
     */
    RtlZeroMemory(ClientAreaBytes, sizeof(ClientAreaBytes));
    ClientArea.Buffer = ClientAreaBytes;
    ClientArea.Length = sizeof(ClientAreaBytes);
    ClientArea.LogClients = 1;
    ClientArea.ClientArrayOffset = 0x30;
    FixturePut64(ClientAreaBytes, 0x30, 0x100);
    FixturePut64(ClientAreaBytes, 0x38, 0x200);
    FixturePut16(ClientAreaBytes, 0x40, 0xFFFF);
    FixturePut16(ClientAreaBytes, 0x42, 0xFFFF);
    FixturePut16(ClientAreaBytes, 0x44, 7);
    FixturePut32(ClientAreaBytes, 0x4C, 8);
    ClientAreaBytes[0x50] = 'N';
    ClientAreaBytes[0x52] = 'T';
    ClientAreaBytes[0x54] = 'F';
    ClientAreaBytes[0x56] = 'S';
    Status = NtfsLogfileReadClientTable(
        &ClientArea, ClientEntries, 2, &ClientEntriesRead);
    if (!NT_SUCCESS(Status) || ClientEntriesRead != 1 ||
        ClientEntries[0].OldestLsn != 0x100 ||
        ClientEntries[0].ClientRestartLsn != 0x200 ||
        ClientEntries[0].SequenceNumber != 7 ||
        ClientEntries[0].ClientNameLength != 8 ||
        ClientEntries[0].ClientName[0] != 'N' ||
        ClientEntries[0].ClientName[2] != 'T' ||
        ClientEntries[0].ClientName[4] != 'F' ||
        ClientEntries[0].ClientName[6] != 'S')
        return STATUS_DATA_ERROR;

    if (NtfsLogfileReadClientTable(
            &ClientArea, ClientEntries, 0, &ClientEntriesRead) !=
            STATUS_BUFFER_TOO_SMALL)
        return STATUS_DATA_ERROR;
    ClientArea.ClientArrayOffset = 0x31;
    if (NtfsLogfileReadClientTable(
            &ClientArea, ClientEntries, 2, &ClientEntriesRead) !=
            STATUS_DATA_ERROR)
        return STATUS_DATA_ERROR;
    ClientArea.ClientArrayOffset = 0x30;
    FixturePut16(ClientAreaBytes, 0x40, 2);
    if (NtfsLogfileReadClientTable(
            &ClientArea, ClientEntries, 2, &ClientEntriesRead) !=
            STATUS_DATA_ERROR)
        return STATUS_DATA_ERROR;
    FixturePut16(ClientAreaBytes, 0x40, 0xFFFF);
    FixturePut32(ClientAreaBytes, 0x4C, 0x82);
    if (NtfsLogfileReadClientTable(
            &ClientArea, ClientEntries, 2, &ClientEntriesRead) !=
            STATUS_DATA_ERROR)
        return STATUS_DATA_ERROR;
    FixturePut32(ClientAreaBytes, 0x4C, 3);
    if (NtfsLogfileReadClientTable(
            &ClientArea, ClientEntries, 2, &ClientEntriesRead) !=
            STATUS_DATA_ERROR)
        return STATUS_DATA_ERROR;
    FixturePut32(ClientAreaBytes, 0x4C, 8);

    /* Invalid later entries must not leave a partially published table. */
    ClientArea.LogClients = 2;
    FixturePut32(ClientAreaBytes, 0x4C, 8);
    FixturePut16(ClientAreaBytes, 0xD0 + 0x10, 2);
    ClientEntries[0].OldestLsn = 0xBAD;
    if (NtfsLogfileReadClientTable(
            &ClientArea, ClientEntries, 2, &ClientEntriesRead) !=
            STATUS_DATA_ERROR || ClientEntriesRead != 0 ||
        ClientEntries[0].OldestLsn != 0xBAD)
        return STATUS_DATA_ERROR;
    ClientArea.LogClients = 1;
    FixturePut16(ClientAreaBytes, 0xD0 + 0x10, 0xFFFF);
    ClientArea.Buffer = NULL;
    if (NtfsLogfileReadClientTable(
            &ClientArea, ClientEntries, 2, &ClientEntriesRead) !=
            STATUS_INVALID_PARAMETER)
        return STATUS_DATA_ERROR;
    ClientArea.Buffer = ClientAreaBytes;
    ClientArea.LogClients = 0;
    ClientArea.ClientArrayOffset = sizeof(ClientAreaBytes);
    Status = NtfsLogfileReadClientTable(
        &ClientArea, NULL, 0, &ClientEntriesRead);
    if (!NT_SUCCESS(Status) || ClientEntriesRead != 0)
        return STATUS_DATA_ERROR;
    ClientArea.LogClients = 1;
    ClientArea.ClientArrayOffset = 0x30;
    ClientArea.LogClients = 0x10000;
    if (NtfsLogfileReadClientTable(
            &ClientArea, ClientEntries, 2, &ClientEntriesRead) !=
            STATUS_DATA_ERROR)
        return STATUS_DATA_ERROR;
    ClientArea.LogClients = 1;

    /* Input events and output states must never alias. */
    TxStatesWritten = 99;
    if (NtfsLogfileAnalyzeTransactionEvents(
            (const NTFS_LOGFILE_TRANSACTION_EVENT *)TxStates,
            1, 1, TxStates, 4, &TxStatesWritten) !=
            STATUS_INVALID_PARAMETER || TxStatesWritten != 0)
        return STATUS_DATA_ERROR;

    /* The count output must not overwrite either caller-owned buffer. */
    TxEvents[0].Lsn = 0xABCDEF;
    if (NtfsLogfileAnalyzeTransactionEvents(
            TxEvents, 1, 1, NULL, 0,
            (PULONG)&TxEvents[0]) != STATUS_INVALID_PARAMETER ||
        TxEvents[0].Lsn != 0xABCDEF)
        return STATUS_DATA_ERROR;
    TxStates[0].TransactionId = 0x1234;
    if (NtfsLogfileAnalyzeTransactionEvents(
            NULL, 0, 1, TxStates, 4,
            (PULONG)&TxStates[0]) != STATUS_INVALID_PARAMETER ||
        TxStates[0].TransactionId != 0x1234)
        return STATUS_DATA_ERROR;
    RtlZeroMemory(TxStates, sizeof(TxStates));

    /* Empty analysis is a valid empty result when no client stream exists. */
    TxStatesWritten = 99;
    Status = NtfsLogfileAnalyzeTransactionEvents(
        NULL, 0, 1, NULL, 0, &TxStatesWritten);
    if (!NT_SUCCESS(Status) || TxStatesWritten != 0 ||
        NtfsLogfileAnalyzeTransactionEvents(
            NULL, 0, 0, NULL, 0, &TxStatesWritten) != STATUS_INVALID_PARAMETER ||
        NtfsLogfileAnalyzeTransactionEvents(
            NULL, 0, 1, NULL, 1, &TxStatesWritten) != STATUS_INVALID_PARAMETER)
        return STATUS_DATA_ERROR;

    /* Classified transaction records must follow legal lifecycle transitions. */
    RtlZeroMemory(TxEvents, sizeof(TxEvents));
    TxEvents[0].Lsn = 0x100;
    TxEvents[0].TransactionId = 10;
    TxEvents[0].ClientIndex = 0;
    TxEvents[0].Action = NtfsLogfileTransactionBegin;
    TxEvents[1].Lsn = 0x110;
    TxEvents[1].TransactionId = 10;
    TxEvents[1].ClientIndex = 0;
    TxEvents[1].Action = NtfsLogfileTransactionUpdate;
    TxEvents[2].Lsn = 0x120;
    TxEvents[2].TransactionId = 10;
    TxEvents[2].ClientIndex = 0;
    TxEvents[2].Action = NtfsLogfileTransactionCommit;
    TxEvents[3].Lsn = 0x200;
    TxEvents[3].TransactionId = 11;
    TxEvents[3].ClientIndex = 0;
    TxEvents[3].Action = NtfsLogfileTransactionBegin;
    Status = NtfsLogfileAnalyzeTransactionEvents(
        TxEvents, 4, 1, TxStates, 4, &TxStatesWritten);
    if (!NT_SUCCESS(Status) || TxStatesWritten != 2 ||
        TxStates[0].State != NtfsLogfileTransactionCommitted ||
        TxStates[0].RecordCount != 3 ||
        TxStates[0].FirstLsn != 0x100 || TxStates[0].LastLsn != 0x120 ||
        TxStates[1].State != NtfsLogfileTransactionActive)
        return STATUS_DATA_ERROR;

    /* Capacity is based on distinct transactions, not event count. */
    Status = NtfsLogfileAnalyzeTransactionEvents(
        TxEvents, 4, 1, TxStates, 2, &TxStatesWritten);
    if (!NT_SUCCESS(Status) || TxStatesWritten != 2)
        return STATUS_DATA_ERROR;
    if (NtfsLogfileAnalyzeTransactionEvents(
            TxEvents, 4, 1, TxStates, 1, &TxStatesWritten) !=
            STATUS_BUFFER_TOO_SMALL || TxStatesWritten != 0)
        return STATUS_DATA_ERROR;
    TxEvents[1].Action = NtfsLogfileTransactionCommit;
    if (NtfsLogfileAnalyzeTransactionEvents(
            TxEvents, 4, 1, TxStates, 4, &TxStatesWritten) !=
            STATUS_DATA_ERROR || TxStatesWritten != 0 ||
        TxStates[0].TransactionId != 0)
        return STATUS_DATA_ERROR;
    TxEvents[1].Action = NtfsLogfileTransactionUpdate;
    TxEvents[3].Action = NtfsLogfileTransactionAbort;
    Status = NtfsLogfileAnalyzeTransactionEvents(
        TxEvents, 4, 1, TxStates, 4, &TxStatesWritten);
    if (!NT_SUCCESS(Status) ||
        TxStates[1].State != NtfsLogfileTransactionAborted)
        return STATUS_DATA_ERROR;
    TxEvents[3].ClientIndex = 1;
    if (NtfsLogfileAnalyzeTransactionEvents(
            TxEvents, 4, 1, TxStates, 4, &TxStatesWritten) !=
            STATUS_DATA_ERROR)
        return STATUS_DATA_ERROR;
    TxEvents[3].ClientIndex = 0;
    TxEvents[3].Lsn = 0;
    if (NtfsLogfileAnalyzeTransactionEvents(
            TxEvents, 4, 1, TxStates, 4, &TxStatesWritten) !=
            STATUS_DATA_ERROR)
        return STATUS_DATA_ERROR;
    TxEvents[3].Lsn = 0x200;
    TxEvents[3].Action = NtfsLogfileTransactionBegin;
    TxEvents[3].TransactionId = 10;
    if (NtfsLogfileAnalyzeTransactionEvents(
            TxEvents, 4, 1, TxStates, 4, &TxStatesWritten) !=
            STATUS_DATA_ERROR)
        return STATUS_DATA_ERROR;

    /* One physical LSN cannot be attributed to two transaction events. */
    TxEvents[3].TransactionId = 11;
    TxEvents[3].Lsn = TxEvents[0].Lsn;
    TxStatesWritten = 99;
    if (NtfsLogfileAnalyzeTransactionEvents(
            TxEvents, 4, 1, TxStates, 4, &TxStatesWritten) !=
            STATUS_DATA_ERROR || TxStatesWritten != 0 ||
        TxStates[0].TransactionId != 0)
        return STATUS_DATA_ERROR;
    TxEvents[3].Lsn = 0x200;
    /* A late failure rolls back every partially constructed state. */
    TxEvents[0].TransactionId = 30;
    TxEvents[0].Action = NtfsLogfileTransactionBegin;
    TxEvents[0].Lsn = 0x300;
    TxEvents[1].TransactionId = 31;
    TxEvents[1].Action = NtfsLogfileTransactionBegin;
    TxEvents[1].Lsn = 0x310;
    TxEvents[2].TransactionId = 32;
    TxEvents[2].Action = NtfsLogfileTransactionBegin;
    TxEvents[2].Lsn = 0x300; /* Duplicate of the first event. */
    TxStatesWritten = 99;
    Status = NtfsLogfileAnalyzeTransactionEvents(
        TxEvents, 3, 1, TxStates, 4, &TxStatesWritten);
    if (Status != STATUS_DATA_ERROR || TxStatesWritten != 0 ||
        TxStates[0].TransactionId != 0 ||
        TxStates[1].TransactionId != 0 ||
        TxStates[2].TransactionId != 0)
        return STATUS_DATA_ERROR;

    /* Restore the event fixture for the remaining validation cases. */
    TxEvents[0].TransactionId = 10;
    TxEvents[0].Action = NtfsLogfileTransactionBegin;
    TxEvents[0].Lsn = 0x100;
    TxEvents[1].TransactionId = 10;
    TxEvents[1].Action = NtfsLogfileTransactionUpdate;
    TxEvents[1].Lsn = 0x110;
    TxEvents[2].TransactionId = 10;
    TxEvents[2].Action = NtfsLogfileTransactionCommit;
    TxEvents[2].Lsn = 0x120;
    TxEvents[3].TransactionId = 11;
    TxEvents[3].Action = NtfsLogfileTransactionBegin;
    TxEvents[3].Lsn = 0x200;

    TxEvents[3].TransactionId = 12;
    TxEvents[3].Action = (NTFS_LOGFILE_TRANSACTION_ACTION)99;
    if (NtfsLogfileAnalyzeTransactionEvents(
            TxEvents, 4, 1, TxStates, 4, &TxStatesWritten) !=
            STATUS_DATA_ERROR)
        return STATUS_DATA_ERROR;

    /* Client-link validation checks both directions of every edge. */
    RtlZeroMemory(ClientEntries, sizeof(ClientEntries));
    ClientEntries[0].PreviousClient = 1;
    ClientEntries[0].NextClient = 1;
    ClientEntries[1].PreviousClient = 0;
    ClientEntries[1].NextClient = 0;
    if (!NT_SUCCESS(NtfsLogfileValidateClientLinks(ClientEntries, 2)))
        return STATUS_DATA_ERROR;
    ClientEntries[1].PreviousClient = 1;
    if (NtfsLogfileValidateClientLinks(ClientEntries, 2) != STATUS_DATA_ERROR)
        return STATUS_DATA_ERROR;
    ClientEntries[1].PreviousClient = 0;
    ClientEntries[0].NextClient = 2;
    if (NtfsLogfileValidateClientLinks(ClientEntries, 2) != STATUS_DATA_ERROR)
        return STATUS_DATA_ERROR;
    ClientEntries[0].NextClient = 1;
    if (NtfsLogfileValidateClientLinks(NULL, 2) != STATUS_INVALID_PARAMETER ||
        NtfsLogfileValidateClientLinks(ClientEntries, 0) != STATUS_INVALID_PARAMETER ||
        NtfsLogfileValidateClientLinks(ClientEntries, 0x10000) !=
            STATUS_INVALID_PARAMETER)
        return STATUS_DATA_ERROR;

    /* Free/in-use list heads must partition every client exactly once. */
    ClientArea.LogClients = 1;
    ClientArea.FreeList = NTFS_LOGFILE_NO_CLIENT;
    ClientArea.InUseList = 0;
    ClientEntries[0].PreviousClient = 0xFFFF;
    ClientEntries[0].NextClient = 0xFFFF;
    if (!NT_SUCCESS(NtfsLogfileValidateClientLists(
            &ClientArea, ClientEntries, 1)))
        return STATUS_DATA_ERROR;
    ClientArea.FreeList = 0;
    if (NtfsLogfileValidateClientLists(
            &ClientArea, ClientEntries, 1) != STATUS_DATA_ERROR)
        return STATUS_DATA_ERROR;
    ClientArea.FreeList = NTFS_LOGFILE_NO_CLIENT;
    ClientArea.InUseList = NTFS_LOGFILE_NO_CLIENT;
    if (NtfsLogfileValidateClientLists(
            &ClientArea, ClientEntries, 1) != STATUS_DATA_ERROR)
        return STATUS_DATA_ERROR;

    ClientArea.LogClients = 2;
    ClientArea.FreeList = 2;
    ClientArea.InUseList = 1;
    if (NtfsLogfileValidateClientLists(
            &ClientArea, ClientEntries, 2) != STATUS_DATA_ERROR)
        return STATUS_DATA_ERROR;
    ClientArea.FreeList = 0;
    ClientArea.InUseList = 1;
    ClientEntries[0].PreviousClient = 0xFFFF;
    ClientEntries[0].NextClient = 0xFFFF;
    ClientEntries[1].PreviousClient = 0xFFFF;
    ClientEntries[1].NextClient = 0xFFFF;
    if (!NT_SUCCESS(NtfsLogfileValidateClientLists(
            &ClientArea, ClientEntries, 2)))
        return STATUS_DATA_ERROR;
    ClientEntries[0].NextClient = 1;
    if (NtfsLogfileValidateClientLists(
            &ClientArea, ClientEntries, 2) != STATUS_DATA_ERROR)
        return STATUS_DATA_ERROR;
    ClientEntries[0].NextClient = 0xFFFF;
    ClientArea.InUseList = 0;
    if (NtfsLogfileValidateClientLists(
            &ClientArea, ClientEntries, 2) != STATUS_DATA_ERROR)
        return STATUS_DATA_ERROR;
    ClientArea.LogClients = 0;
    ClientArea.FreeList = NTFS_LOGFILE_NO_CLIENT;
    ClientArea.InUseList = NTFS_LOGFILE_NO_CLIENT;
    if (!NT_SUCCESS(NtfsLogfileValidateClientLists(
            &ClientArea, NULL, 0)))
        return STATUS_DATA_ERROR;
    ClientArea.LogClients = 0x10000;
    if (NtfsLogfileValidateClientLists(
            &ClientArea, NULL, 0x10000) != STATUS_INVALID_PARAMETER)
        return STATUS_DATA_ERROR;
    ClientArea.LogClients = 0;

    /* Transaction identifiers are scoped to their LFS client. */
    TxEvents[0].TransactionId = 20;
    TxEvents[0].Action = NtfsLogfileTransactionBegin;
    TxEvents[0].ClientIndex = 0;
    TxEvents[1].TransactionId = 20;
    TxEvents[1].Action = NtfsLogfileTransactionBegin;
    TxEvents[1].ClientIndex = 1;
    Status = NtfsLogfileAnalyzeTransactionEvents(
        TxEvents, 2, 2, TxStates, 2, &TxStatesWritten);
    if (!NT_SUCCESS(Status) || TxStatesWritten != 2 ||
        TxStates[0].ClientIndex == TxStates[1].ClientIndex)
        return STATUS_DATA_ERROR;

    return STATUS_SUCCESS;
}
