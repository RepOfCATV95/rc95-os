#include "ntfslib.h"
#include "logfile.h"

#define LOG_U16(p) ((USHORT)((p)[0] | ((USHORT)(p)[1] << 8)))
#define LOG_U32(p) ((ULONG)((p)[0] | ((ULONG)(p)[1] << 8) | ((ULONG)(p)[2] << 16) | ((ULONG)(p)[3] << 24)))
#define LOG_U64(p) ((ULONGLONG)LOG_U32(p) | ((ULONGLONG)LOG_U32((p) + 4) << 32))

static BOOLEAN
LogPowerOfTwo(ULONG Value)
{
    return Value != 0 && (Value & (Value - 1)) == 0;
}

static BOOLEAN
LogRange(ULONG Offset, ULONG Length, ULONG Limit)
{
    return Offset <= Limit && Length <= Limit - Offset;
}

static BOOLEAN
LogBuffersOverlap(const VOID *First, const VOID *Second, ULONG Length)
{
    ULONG_PTR A = (ULONG_PTR)First;
    ULONG_PTR B = (ULONG_PTR)Second;

    if (A > ~(ULONG_PTR)0 - Length || B > ~(ULONG_PTR)0 - Length)
        return TRUE;

    return A < B + Length && B < A + Length;
}

NTSTATUS
NTAPI
NtfsLogfileOpenRestartPage(
    _In_reads_bytes_(PageLength) const VOID *Page,
    _In_ ULONG PageLength,
    _In_ ULONG BytesPerSector,
    _Out_writes_bytes_(PageLength) VOID *Scratch,
    _Out_ PNTFS_LOGFILE_RESTART_VIEW View)
{
    const UCHAR *Source = (const UCHAR *)Page;
    UCHAR *Bytes = (UCHAR *)Scratch;
    ULONG UsaOffset, UsaCount, SectorCount, i, UsaBytes;
    USHORT UpdateSequence, Major, Minor;
    ULONG SystemPageSize, LogPageSize, RestartOffset;

    if (!Page || !Scratch || !View ||
        LogBuffersOverlap(Page, Scratch, PageLength) ||
        !LogPowerOfTwo(BytesPerSector) || BytesPerSector < 512 ||
        PageLength < 0x50 || PageLength % BytesPerSector != 0)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(View, sizeof(*View));

    if (LOG_U32(Source) != NTFS_LOGFILE_RESTART_SIGNATURE)
        return STATUS_DATA_ERROR;

    UsaOffset = LOG_U16(Source + 4);
    UsaCount = LOG_U16(Source + 6);
    SectorCount = PageLength / BytesPerSector;
    if (UsaCount != SectorCount + 1 || (UsaOffset & 1) ||
        UsaOffset < 8 || UsaCount > MAXULONG / sizeof(USHORT))
        return STATUS_DATA_ERROR;

    UsaBytes = UsaCount * sizeof(USHORT);
    if (!LogRange(UsaOffset, UsaBytes, PageLength) ||
        UsaOffset + UsaBytes > BytesPerSector - sizeof(USHORT))
        return STATUS_DATA_ERROR;

    UpdateSequence = LOG_U16(Source + UsaOffset);
    RtlCopyMemory(Bytes, Source, PageLength);
    for (i = 0; i < SectorCount; ++i)
    {
        ULONG Trailer = (i + 1) * BytesPerSector - sizeof(USHORT);
        USHORT Replacement = LOG_U16(Source + UsaOffset + ((i + 1) * sizeof(USHORT)));
        if (LOG_U16(Source + Trailer) != UpdateSequence)
        {
            RtlZeroMemory(Bytes, PageLength);
            return STATUS_DATA_ERROR;
        }
        Bytes[Trailer] = (UCHAR)(Replacement & 0xFF);
        Bytes[Trailer + 1] = (UCHAR)(Replacement >> 8);
    }

    SystemPageSize = LOG_U32(Bytes + 0x10);
    LogPageSize = LOG_U32(Bytes + 0x14);
    RestartOffset = LOG_U16(Bytes + 0x18);
    Minor = LOG_U16(Bytes + 0x1A);
    Major = LOG_U16(Bytes + 0x1C);

    if (!LogPowerOfTwo(SystemPageSize) || !LogPowerOfTwo(LogPageSize) ||
        SystemPageSize != PageLength || LogPageSize < BytesPerSector ||
        LogPageSize > SystemPageSize ||
        (SystemPageSize % BytesPerSector) != 0 ||
        (LogPageSize % BytesPerSector) != 0 ||
        RestartOffset < 0x20 || (RestartOffset & 7) ||
        RestartOffset > PageLength - NTFS_LOGFILE_RESTART_AREA_MIN)
    {
        RtlZeroMemory(Bytes, PageLength);
        return STATUS_DATA_ERROR;
    }

    /* LFS 1.x and 2.0 are recognized; unknown versions remain unsupported. */
    if (Major == 1)
        View->Format = NtfsLfsVersion1;
    else if (Major == 2 && Minor == 0)
        View->Format = NtfsLfsVersion2;
    else
    {
        RtlZeroMemory(Bytes, PageLength);
        return STATUS_NOT_SUPPORTED;
    }

    View->Page = Bytes;
    View->PageLength = PageLength;
    View->SystemPageSize = SystemPageSize;
    View->LogPageSize = LogPageSize;
    View->RestartOffset = RestartOffset;
    View->MajorVersion = Major;
    View->MinorVersion = Minor;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NtfsLogfileOpenRestartArea(
    _In_ const NTFS_LOGFILE_RESTART_VIEW *Restart,
    _Out_ PNTFS_LOGFILE_RESTART_AREA_VIEW Area)
{
    const UCHAR *Bytes;
    ULONG Offset, Available, AreaLength, ClientOffset;
    ULONG Clients, Flags;
    USHORT FreeList, InUseList, SequenceBits;
    USHORT AreaLength16, ClientOffset16, HeaderLength, PageDataOffset;

    if (!Restart || !Area || !Restart->Page ||
        Restart->RestartOffset >= Restart->PageLength)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(Area, sizeof(*Area));
    Offset = Restart->RestartOffset;
    Available = Restart->PageLength - Offset;
    if (Available < NTFS_LOGFILE_RESTART_AREA_MIN)
        return STATUS_DATA_ERROR;

    Bytes = Restart->Page + Offset;
    /* Restart-area fields are read by fixed offsets, never native structure casts. */
    /*
     * RESTART_AREA layout: the client-list heads and the sequence/length/
     * offset fields are 16-bit values. Reading them as ULONGs shifts every
     * following field and can turn malformed metadata into bogus bounds.
     */
    Clients = LOG_U32(Bytes + 0x08);
    FreeList = LOG_U16(Bytes + 0x0C);
    InUseList = LOG_U16(Bytes + 0x0E);
    Flags = LOG_U32(Bytes + 0x10);
    SequenceBits = LOG_U16(Bytes + 0x14);
    AreaLength16 = LOG_U16(Bytes + 0x16);
    ClientOffset16 = LOG_U16(Bytes + 0x18);
    AreaLength = AreaLength16;
    ClientOffset = ClientOffset16;
    HeaderLength = LOG_U16(Bytes + 0x2C);
    PageDataOffset = LOG_U16(Bytes + 0x2E);

    if (AreaLength < NTFS_LOGFILE_RESTART_AREA_MIN ||
        AreaLength > Available ||
        ClientOffset < NTFS_LOGFILE_RESTART_AREA_MIN ||
        ClientOffset > AreaLength ||
        (ClientOffset & 7) ||
        (Clients > (AreaLength - ClientOffset) / 0x20) ||
        SequenceBits == 0 || SequenceBits > 63 ||
        HeaderLength < 0x20 || HeaderLength > Restart->LogPageSize ||
        PageDataOffset < HeaderLength || PageDataOffset >= Restart->LogPageSize ||
        (FreeList != 0xFFFF && FreeList >= Clients) ||
        (InUseList != 0xFFFF && InUseList >= Clients))
        return STATUS_DATA_ERROR;

    Area->Buffer = Bytes;
    Area->Length = AreaLength;
    Area->CurrentLsn = LOG_U64(Bytes);
    Area->LogClients = Clients;
    Area->FreeList = (FreeList == 0xFFFF) ? 0xFFFFFFFF : FreeList;
    Area->InUseList = (InUseList == 0xFFFF) ? 0xFFFFFFFF : InUseList;
    Area->Flags = Flags;
    Area->SequenceNumberBits = SequenceBits;
    Area->RestartAreaLength = AreaLength;
    Area->ClientArrayOffset = ClientOffset;
    Area->FileSize = LOG_U64(Bytes + 0x20);
    Area->LastLsnDataLength = LOG_U32(Bytes + 0x28);
    Area->RecordHeaderLength = HeaderLength;
    Area->LogPageDataOffset = PageDataOffset;

    if (Area->FileSize == 0 ||
        Area->LastLsnDataLength > Area->FileSize)
        return STATUS_DATA_ERROR;

    return STATUS_SUCCESS;
}



/* Find one LSN in a caller-provided, bounded transaction-link array. */
static LONG
LogFindTransactionLink(
    const NTFS_LOGFILE_TRANSACTION_LINK *Links,
    ULONG LinkCount,
    ULONGLONG Lsn)
{
    ULONG i;
    for (i = 0; i < LinkCount; ++i)
        if (Links[i].Lsn == Lsn)
            return (LONG)i;
    return -1;
}

NTSTATUS
NTAPI
NtfsLogfileValidateRecordFragments(
    const NTFS_LOGFILE_RECORD_FRAGMENT *Fragments,
    ULONG FragmentCount,
    ULONG RecordLength)
{
    ULONG i, ExpectedOffset = 0;

    if (!Fragments || FragmentCount == 0 || RecordLength == 0)
        return STATUS_INVALID_PARAMETER;

    for (i = 0; i < FragmentCount; ++i)
    {
        const NTFS_LOGFILE_RECORD_FRAGMENT *Fragment = &Fragments[i];

        ULONG j;

        if (Fragment->Length == 0 ||
            Fragment->LogicalOffset != ExpectedOffset ||
            Fragment->PageOffset > Fragment->PageLength ||
            Fragment->Length > Fragment->PageLength - Fragment->PageOffset ||
            Fragment->Length > RecordLength - ExpectedOffset)
            return STATUS_DATA_ERROR;

        /* Fragments from one page must agree on its size and not overlap. */
        for (j = 0; j < i; ++j)
        {
            const NTFS_LOGFILE_RECORD_FRAGMENT *Previous = &Fragments[j];
            if (Previous->PageIndex == Fragment->PageIndex)
            {
                if (Previous->PageLength != Fragment->PageLength ||
                    (Fragment->PageOffset < Previous->PageOffset + Previous->Length &&
                     Previous->PageOffset < Fragment->PageOffset + Fragment->Length))
                    return STATUS_DATA_ERROR;
            }
        }

        ExpectedOffset += Fragment->Length;
    }

    return (ExpectedOffset == RecordLength) ? STATUS_SUCCESS : STATUS_DATA_ERROR;
}

NTSTATUS
NTAPI
NtfsLogfileAdvanceCircularOffset(
    ULONGLONG Offset,
    ULONGLONG Delta,
    ULONGLONG RegionStart,
    ULONGLONG RegionLength,
    ULONGLONG FileSize,
    PULONGLONG Result)
{
    ULONGLONG Relative, Step, Remaining;

    if (!Result || RegionLength == 0 || RegionStart > FileSize ||
        RegionLength > FileSize - RegionStart ||
        Offset < RegionStart || Offset - RegionStart >= RegionLength)
        return STATUS_INVALID_PARAMETER;

    Relative = Offset - RegionStart;
    Step = Delta % RegionLength;
    Remaining = RegionLength - Relative;

    /* Avoid Relative + Step overflow for very large circular regions. */
    if (Step >= Remaining)
        Relative = Step - Remaining;
    else
        Relative += Step;

    *Result = RegionStart + Relative;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NtfsLogfileCompareLsn(
    ULONGLONG FirstLsn,
    ULONGLONG SecondLsn,
    USHORT SequenceNumberBits,
    PLONG Comparison)
{
    ULONG OffsetBits;
    ULONGLONG SequenceMask, OffsetMask;
    ULONGLONG FirstSequence, SecondSequence, Difference;
    ULONGLONG FirstOffset, SecondOffset, HalfRange;

    if (!Comparison || SequenceNumberBits == 0 || SequenceNumberBits > 63)
        return STATUS_INVALID_PARAMETER;

    OffsetBits = 64 - SequenceNumberBits;
    SequenceMask = (((ULONGLONG)1 << SequenceNumberBits) - 1);
    OffsetMask = (((ULONGLONG)1 << OffsetBits) - 1);
    FirstSequence = FirstLsn >> OffsetBits;
    SecondSequence = SecondLsn >> OffsetBits;
    FirstOffset = FirstLsn & OffsetMask;
    SecondOffset = SecondLsn & OffsetMask;

    if (FirstSequence == SecondSequence)
    {
        *Comparison = (FirstOffset > SecondOffset) ? 1 :
                      (FirstOffset < SecondOffset) ? -1 : 0;
        return STATUS_SUCCESS;
    }

    /* Sequence values are modular; ordinary integer comparison fails at wrap. */
    Difference = (FirstSequence - SecondSequence) & SequenceMask;
    HalfRange = (ULONGLONG)1 << (SequenceNumberBits - 1);
    if (Difference == HalfRange)
        return STATUS_DATA_ERROR; /* Ordering is ambiguous at exactly half range. */

    *Comparison = (Difference < HalfRange) ? 1 : -1;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NtfsLogfileValidateClientReference(
    ULONG ClientIndex,
    ULONG ClientCount)
{
    if (ClientCount == 0 || ClientIndex == NTFS_LOGFILE_NO_CLIENT ||
        ClientIndex >= ClientCount)
        return STATUS_DATA_ERROR;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NtfsLogfileValidateTransactionChains(
    const NTFS_LOGFILE_TRANSACTION_LINK *Links,
    ULONG LinkCount,
    ULONG ClientCount)
{
    ULONG i, j, Steps;
    LONG Current;

    if ((!Links && LinkCount != 0) || LinkCount == 0 || ClientCount == 0 ||
        LinkCount > 0x7FFFFFFFUL)
        return STATUS_INVALID_PARAMETER;

    /* Validate identities first; duplicate LSNs make predecessor lookup ambiguous. */
    for (i = 0; i < LinkCount; ++i)
    {
        if (Links[i].Lsn == 0 ||
            !NT_SUCCESS(NtfsLogfileValidateClientReference(Links[i].ClientIndex, ClientCount)))
            return STATUS_DATA_ERROR;

        for (j = i + 1; j < LinkCount; ++j)
            if (Links[i].Lsn == Links[j].Lsn)
                return STATUS_DATA_ERROR;
    }

    /* Every non-null predecessor must exist and belong to the same client. */
    for (i = 0; i < LinkCount; ++i)
    {
        if (Links[i].PreviousLsn == 0)
            continue;

        Current = LogFindTransactionLink(Links, LinkCount, Links[i].PreviousLsn);
        if (Current < 0 || Links[Current].ClientIndex != Links[i].ClientIndex)
            return STATUS_DATA_ERROR;
    }

    /*
     * Follow each predecessor chain with a strict LinkCount bound. A valid
     * acyclic chain reaches LSN zero in at most LinkCount steps; exceeding the
     * bound proves a cycle without recursion or temporary allocations.
     */
    for (i = 0; i < LinkCount; ++i)
    {
        Current = (LONG)i;
        for (Steps = 0; Steps <= LinkCount; ++Steps)
        {
            ULONGLONG Previous;

            if (Current < 0)
                return STATUS_DATA_ERROR;

            Previous = Links[Current].PreviousLsn;
            if (Previous == 0)
                break;

            Current = LogFindTransactionLink(Links, LinkCount, Previous);
            if (Steps == LinkCount)
                return STATUS_DATA_ERROR;
        }
    }

    return STATUS_SUCCESS;
}


/*
 * Open one USA-protected RCRD page. The returned Page points at caller-owned
 * Scratch, which must remain alive for the lifetime of the view.
 */
NTSTATUS
NTAPI
NtfsLogfileOpenRecordPage(
    const VOID *Page,
    ULONG PageLength,
    ULONG BytesPerSector,
    ULONG DataOffset,
    VOID *Scratch,
    PNTFS_LOGFILE_RECORD_PAGE_VIEW View)
{
    const UCHAR *Source = (const UCHAR *)Page;
    UCHAR *Bytes = (UCHAR *)Scratch;
    ULONG UsaOffset, UsaCount, SectorCount, UsaBytes, i;
    ULONG NextRecordOffset;
    USHORT UpdateSequence;

    if (!Page || !Scratch || !View ||
        LogBuffersOverlap(Page, Scratch, PageLength) ||
        !LogPowerOfTwo(BytesPerSector) || BytesPerSector < 512 ||
        PageLength < NTFS_LOGFILE_RECORD_PAGE_HEADER_MIN ||
        PageLength % BytesPerSector != 0 ||
        DataOffset < NTFS_LOGFILE_RECORD_PAGE_HEADER_MIN ||
        DataOffset >= PageLength || (DataOffset & 7))
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(View, sizeof(*View));
    if (LOG_U32(Source) != NTFS_LOGFILE_RECORD_PAGE_SIGNATURE)
        return STATUS_DATA_ERROR;

    UsaOffset = LOG_U16(Source + 4);
    UsaCount = LOG_U16(Source + 6);
    SectorCount = PageLength / BytesPerSector;
    if (UsaCount != SectorCount + 1 || (UsaOffset & 1) ||
        UsaOffset < NTFS_LOGFILE_RECORD_PAGE_HEADER_MIN ||
        UsaCount > MAXULONG / sizeof(USHORT))
        return STATUS_DATA_ERROR;

    UsaBytes = UsaCount * sizeof(USHORT);
    if (!LogRange(UsaOffset, UsaBytes, PageLength) ||
        UsaOffset + UsaBytes > BytesPerSector - sizeof(USHORT) ||
        DataOffset < UsaOffset + UsaBytes)
        return STATUS_DATA_ERROR;

    UpdateSequence = LOG_U16(Source + UsaOffset);
    RtlCopyMemory(Bytes, Source, PageLength);
    for (i = 0; i < SectorCount; ++i)
    {
        ULONG Trailer = (i + 1) * BytesPerSector - sizeof(USHORT);
        USHORT Replacement = LOG_U16(Source + UsaOffset + ((i + 1) * sizeof(USHORT)));

        if (LOG_U16(Source + Trailer) != UpdateSequence)
        {
            RtlZeroMemory(Bytes, PageLength);
            return STATUS_DATA_ERROR;
        }

        Bytes[Trailer] = (UCHAR)(Replacement & 0xFF);
        Bytes[Trailer + 1] = (UCHAR)(Replacement >> 8);
    }

    NextRecordOffset = LOG_U16(Bytes + 0x18);
    if (NextRecordOffset < DataOffset ||
        NextRecordOffset > PageLength ||
        (NextRecordOffset & 7))
    {
        RtlZeroMemory(Bytes, PageLength);
        return STATUS_DATA_ERROR;
    }

    View->Page = Bytes;
    View->PageLength = PageLength;
    View->DataOffset = DataOffset;
    View->NextRecordOffset = NextRecordOffset;
    View->LastLsn = LOG_U64(Bytes + 0x08);
    View->Flags = LOG_U32(Bytes + 0x10);
    View->PageCount = LOG_U16(Bytes + 0x14);
    View->PagePosition = LOG_U16(Bytes + 0x16);

    if (View->PageCount == 0 || View->PagePosition >= View->PageCount)
    {
        RtlZeroMemory(Bytes, PageLength);
        RtlZeroMemory(View, sizeof(*View));
        return STATUS_DATA_ERROR;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NtfsLogfileInitializeRecordIterator(
    const NTFS_LOGFILE_RECORD_PAGE_VIEW *Page,
    ULONG RecordHeaderLength,
    ULONG LengthFieldOffset,
    ULONG Alignment,
    BOOLEAN HasClientIndex,
    ULONG ClientIndexFieldOffset,
    PNTFS_LOGFILE_RECORD_ITERATOR Iterator)
{
    if (!Page || !Iterator || !Page->Page ||
        RecordHeaderLength == 0 ||
        LengthFieldOffset > RecordHeaderLength ||
        sizeof(ULONG) > RecordHeaderLength - LengthFieldOffset ||
        !LogPowerOfTwo(Alignment) || Alignment < sizeof(ULONG) ||
        (HasClientIndex &&
         (ClientIndexFieldOffset > RecordHeaderLength ||
          sizeof(USHORT) > RecordHeaderLength - ClientIndexFieldOffset)) ||
        Page->DataOffset > Page->NextRecordOffset ||
        Page->NextRecordOffset > Page->PageLength)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(Iterator, sizeof(*Iterator));
    Iterator->Page = Page;
    Iterator->Cursor = Page->DataOffset;
    Iterator->RecordHeaderLength = RecordHeaderLength;
    Iterator->LengthFieldOffset = LengthFieldOffset;
    Iterator->Alignment = Alignment;
    Iterator->HasClientIndex = HasClientIndex;
    Iterator->ClientIndexFieldOffset = ClientIndexFieldOffset;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NtfsLogfileNextRecord(
    NTFS_LOGFILE_RECORD_ITERATOR *Iterator,
    ULONG ClientCount,
    PNTFS_LOGFILE_RECORD_VIEW Record)
{
    const NTFS_LOGFILE_RECORD_PAGE_VIEW *Page;
    const UCHAR *Bytes;
    ULONG Length, AlignedLength, Remaining;
    ULONG ClientIndex = NTFS_LOGFILE_NO_CLIENT;
    NTFS_LOGFILE_RECORD_FRAGMENT Fragment;

    if (!Iterator || !Record || !Iterator->Page || !Iterator->Page->Page)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(Record, sizeof(*Record));
    Page = Iterator->Page;

    if (Iterator->Cursor == Page->NextRecordOffset)
        return STATUS_NO_MORE_ENTRIES;

    if (Iterator->Cursor > Page->NextRecordOffset)
        return STATUS_DATA_ERROR;

    Remaining = Page->NextRecordOffset - Iterator->Cursor;
    if (Remaining < Iterator->RecordHeaderLength)
        return STATUS_DATA_ERROR;

    Bytes = Page->Page + Iterator->Cursor;
    Length = LOG_U32(Bytes + Iterator->LengthFieldOffset);
    if (Length < Iterator->RecordHeaderLength || Length > Remaining ||
        Length > MAXULONG - (Iterator->Alignment - 1))
        return STATUS_DATA_ERROR;

    AlignedLength = (Length + Iterator->Alignment - 1) &
                    ~(Iterator->Alignment - 1);
    if (AlignedLength > Remaining)
        return STATUS_DATA_ERROR;

    if (Iterator->HasClientIndex)
    {
        ClientIndex = LOG_U16(Bytes + Iterator->ClientIndexFieldOffset);
        if (!NT_SUCCESS(NtfsLogfileValidateClientReference(ClientIndex, ClientCount)))
            return STATUS_DATA_ERROR;
    }

    /* Route every emitted page-local record through the common fragment
     * validator so the iterator and cross-page assembly share bounds rules. */
    Fragment.LogicalOffset = 0;
    Fragment.PageIndex = 0;
    Fragment.PageOffset = Iterator->Cursor;
    Fragment.Length = Length;
    Fragment.PageLength = Page->PageLength;
    if (!NT_SUCCESS(NtfsLogfileValidateRecordFragments(&Fragment, 1, Length)))
        return STATUS_DATA_ERROR;

    Record->Buffer = Bytes;
    Record->Offset = Iterator->Cursor;
    Record->Length = Length;
    Record->HeaderLength = Iterator->RecordHeaderLength;
    Record->ClientIndex = ClientIndex;
    Record->HasClientIndex = Iterator->HasClientIndex;
    Iterator->Cursor += AlignedLength;
    return STATUS_SUCCESS;
}


/*
 * Decode the native LFS record header. The on-disk record header layout is
 * stable across the supported LFS 1.x and 2.0 restart formats; the page
 * framing differs by version, but the 0x30-byte record header does not.
 */
static NTSTATUS
LogDecodeNativeRecordHeader(
    const VOID *Buffer,
    ULONG Available,
    PNTFS_LFS_RECORD_HEADER_VIEW Header)
{
    const UCHAR *Bytes = (const UCHAR *)Buffer;

    if (!Buffer || !Header || Available < NTFS_LFS_RECORD_HEADER_SIZE)
        return STATUS_DATA_ERROR;

    RtlZeroMemory(Header, sizeof(*Header));
    Header->ThisLsn = LOG_U64(Bytes + 0x00);
    Header->ClientPreviousLsn = LOG_U64(Bytes + 0x08);
    Header->ClientUndoNextLsn = LOG_U64(Bytes + 0x10);
    Header->ClientDataLength = LOG_U32(Bytes + 0x18);
    Header->ClientSequenceNumber = LOG_U16(Bytes + 0x1C);
    Header->ClientIndex = LOG_U16(Bytes + 0x1E);
    Header->RecordType = LOG_U32(Bytes + 0x20);
    Header->TransactionId = LOG_U32(Bytes + 0x24);
    Header->Flags = LOG_U16(Bytes + 0x28);

    /* The alignment word at 0x2A and trailing quadword padding are reserved. */
    if (Header->ThisLsn == 0 ||
        Header->ClientDataLength > MAXULONG - NTFS_LFS_RECORD_HEADER_SIZE ||
        (Header->Flags & ~NTFS_LFS_RECORD_MULTI_PAGE) != 0)
        return STATUS_DATA_ERROR;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NtfsLogfileDecodeRecordHeaderV1(
    const VOID *Buffer,
    ULONG Available,
    PNTFS_LFS_RECORD_HEADER_VIEW Header)
{
    return LogDecodeNativeRecordHeader(Buffer, Available, Header);
}

NTSTATUS
NTAPI
NtfsLogfileDecodeRecordHeaderV2(
    const VOID *Buffer,
    ULONG Available,
    PNTFS_LFS_RECORD_HEADER_VIEW Header)
{
    return LogDecodeNativeRecordHeader(Buffer, Available, Header);
}

NTSTATUS
NTAPI
NtfsLogfileAssembleRecord(
    const NTFS_LOGFILE_RECORD_PAGE_VIEW *Pages,
    ULONG PageCount,
    ULONG FirstPageIndex,
    ULONG FirstPageOffset,
    NTFS_LFS_FORMAT Format,
    ULONG RecordHeaderLength,
    ULONG ClientCount,
    VOID *Scratch,
    ULONG ScratchLength,
    NTFS_LOGFILE_RECORD_FRAGMENT *Fragments,
    ULONG FragmentCapacity,
    PNTFS_LOGFILE_ASSEMBLED_RECORD Record)
{
    const NTFS_LOGFILE_RECORD_PAGE_VIEW *FirstPage;
    const UCHAR *FirstBytes;
    NTFS_LFS_RECORD_HEADER_VIEW Header;
    ULONG HeaderLength, TotalLength, Remaining, Copied, FragmentCount;
    ULONG PageIndex, PageOffset, PageSteps;
    NTSTATUS Status;

    if (!Pages || PageCount == 0 || FirstPageIndex >= PageCount ||
        !Scratch || !Fragments || FragmentCapacity == 0 || !Record ||
        (Format != NtfsLfsVersion1 && Format != NtfsLfsVersion2) ||
        RecordHeaderLength < NTFS_LFS_RECORD_HEADER_SIZE ||
        (RecordHeaderLength & 7) || ClientCount == 0)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(Record, sizeof(*Record));
    FirstPage = &Pages[FirstPageIndex];
    if (!FirstPage->Page || FirstPageOffset < FirstPage->DataOffset ||
        FirstPageOffset > FirstPage->NextRecordOffset ||
        FirstPage->NextRecordOffset > FirstPage->PageLength ||
        FirstPage->NextRecordOffset - FirstPageOffset < NTFS_LFS_RECORD_HEADER_SIZE)
        return STATUS_DATA_ERROR;

    FirstBytes = FirstPage->Page + FirstPageOffset;
    if (Format == NtfsLfsVersion1)
        Status = NtfsLogfileDecodeRecordHeaderV1(
            FirstBytes, FirstPage->NextRecordOffset - FirstPageOffset, &Header);
    else
        Status = NtfsLogfileDecodeRecordHeaderV2(
            FirstBytes, FirstPage->NextRecordOffset - FirstPageOffset, &Header);
    if (!NT_SUCCESS(Status))
        return Status;

    if (RecordHeaderLength > MAXULONG - Header.ClientDataLength)
        return STATUS_DATA_ERROR;
    HeaderLength = RecordHeaderLength;
    TotalLength = HeaderLength + Header.ClientDataLength;
    if (TotalLength == 0 || TotalLength > ScratchLength)
        return STATUS_BUFFER_TOO_SMALL;

    if (!NT_SUCCESS(NtfsLogfileValidateClientReference(Header.ClientIndex, ClientCount)))
        return STATUS_DATA_ERROR;

    Remaining = TotalLength;
    Copied = 0;
    FragmentCount = 0;
    PageIndex = FirstPageIndex;
    PageOffset = FirstPageOffset;

    /*
     * Walk the circular page array. Every continuation starts at that page's
     * DataOffset; page positions must advance modulo the declared I/O count.
     * A strict PageCount bound prevents malformed metadata from looping.
     */
    for (PageSteps = 0; Remaining != 0; ++PageSteps)
    {
        const NTFS_LOGFILE_RECORD_PAGE_VIEW *Page;
        ULONG Available, Take;

        if (PageSteps >= PageCount || FragmentCount >= FragmentCapacity)
            return STATUS_BUFFER_TOO_SMALL;

        Page = &Pages[PageIndex];
        if (!Page->Page || Page->PageCount == 0 ||
            Page->PagePosition >= Page->PageCount ||
            Page->DataOffset > Page->NextRecordOffset ||
            Page->NextRecordOffset > Page->PageLength)
            return STATUS_DATA_ERROR;

        if (PageSteps != 0)
        {
            const NTFS_LOGFILE_RECORD_PAGE_VIEW *Previous;
            ULONG ExpectedPosition;
            Previous = &Pages[(PageIndex + PageCount - 1) % PageCount];
            if (!Previous->Page || Previous->PageCount == 0 ||
                Previous->PagePosition >= Previous->PageCount ||
                Page->PageCount != Previous->PageCount)
                return STATUS_DATA_ERROR;
            ExpectedPosition = ((ULONG)Previous->PagePosition + 1) %
                               (ULONG)Previous->PageCount;
            if (Page->PagePosition != ExpectedPosition)
                return STATUS_DATA_ERROR;
            PageOffset = Page->DataOffset;
        }

        if (PageOffset < Page->DataOffset || PageOffset > Page->NextRecordOffset)
            return STATUS_DATA_ERROR;
        Available = Page->NextRecordOffset - PageOffset;
        if (Available == 0)
            return STATUS_DATA_ERROR;
        Take = (Remaining < Available) ? Remaining : Available;

        Fragments[FragmentCount].LogicalOffset = Copied;
        Fragments[FragmentCount].PageIndex = PageIndex;
        Fragments[FragmentCount].PageOffset = PageOffset;
        Fragments[FragmentCount].Length = Take;
        Fragments[FragmentCount].PageLength = Page->PageLength;
        ++FragmentCount;

        RtlCopyMemory((UCHAR *)Scratch + Copied, Page->Page + PageOffset, Take);
        Copied += Take;
        Remaining -= Take;

        if (Remaining != 0)
        {
            if ((Header.Flags & NTFS_LFS_RECORD_MULTI_PAGE) == 0)
                return STATUS_DATA_ERROR;
            PageIndex = (PageIndex + 1) % PageCount;
        }
    }

    if (FragmentCount > 1 && (Header.Flags & NTFS_LFS_RECORD_MULTI_PAGE) == 0)
        return STATUS_DATA_ERROR;

    Status = NtfsLogfileValidateRecordFragments(Fragments, FragmentCount, TotalLength);
    if (!NT_SUCCESS(Status))
        return Status;

    Record->Buffer = (const UCHAR *)Scratch;
    Record->Length = TotalLength;
    Record->HeaderLength = HeaderLength;
    Record->FirstPageIndex = FirstPageIndex;
    Record->FirstPageOffset = FirstPageOffset;
    Record->FragmentCount = FragmentCount;
    Record->Header = Header;
    return STATUS_SUCCESS;
}
