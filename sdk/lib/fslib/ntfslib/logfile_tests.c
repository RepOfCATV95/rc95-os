/*
 * Synthetic $LogFile RCRD record fixtures.
 *
 * Enable the object target with -DNTFSLIB_BUILD_TESTS=ON. The exported
 * NtfsLogfileRunRecordFixtures routine is intended to be called by a
 * platform test runner; this file deliberately does not define main().
 */
#include "ntfslib.h"
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
    NTFS_LOGFILE_RECORD_PAGE_VIEW UsaView;
    NTFS_LOGFILE_RECORD_PAGE_VIEW Pages[3];
    NTFS_LOGFILE_RECORD_FRAGMENT Fragments[3];
    NTFS_LOGFILE_ASSEMBLED_RECORD Record;
    NTFS_LFS_RECORD_HEADER_VIEW Header;
    NTSTATUS Status;

    /* V1 decoder and a record split over two pages across circular wrap. */
    FixtureInitializeRecord(RecordBytes, NTFS_LFS_RECORD_MULTI_PAGE);
    Status = NtfsLogfileDecodeRecordHeaderV1(
        RecordBytes, sizeof(RecordBytes), &Header);
    if (!NT_SUCCESS(Status) || Header.ThisLsn != 0x0000000100001234ULL ||
        Header.ClientDataLength != 0x20 || Header.ClientIndex != 0)
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

    return STATUS_SUCCESS;
}
