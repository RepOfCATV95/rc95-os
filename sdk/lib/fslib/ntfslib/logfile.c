#if defined(NTFS_LOGFILE_KERNEL_BUILD)
#include <ntifs.h>
#else
#include "ntfslib.h"
#endif
#include "logfile.h"

typedef char NTFS_LOGFILE_CLIENT_ENTRY_SIZE_CHECK[
    (sizeof(NTFS_LOGFILE_CLIENT_ENTRY_VIEW) ==
        NTFS_LOGFILE_CLIENT_ENTRY_SIZE) ? 1 : -1];

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

/* Validate independently sized caller buffers without overflowing addresses. */
static BOOLEAN
LogBufferRangesOverlap(
    const VOID *First,
    ULONG FirstLength,
    const VOID *Second,
    ULONG SecondLength)
{
    ULONG_PTR A = (ULONG_PTR)First;
    ULONG_PTR B = (ULONG_PTR)Second;

    if (FirstLength == 0 || SecondLength == 0)
        return FALSE;
    if (!First || !Second ||
        A > ~(ULONG_PTR)0 - FirstLength ||
        B > ~(ULONG_PTR)0 - SecondLength)
        return TRUE;

    return A < B + SecondLength && B < A + FirstLength;
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
    RestartOffset = LOG_U16(Source + 0x18);
    if (UsaCount != SectorCount + 1 || (UsaOffset & 1) ||
        UsaOffset < NTFS_LOGFILE_RESTART_HEADER_MIN ||
        UsaCount > MAXULONG / sizeof(USHORT))
        return STATUS_DATA_ERROR;

    UsaBytes = UsaCount * sizeof(USHORT);
    if (!LogRange(UsaOffset, UsaBytes, PageLength) ||
        UsaOffset + UsaBytes > BytesPerSector - sizeof(USHORT) ||
        RestartOffset < NTFS_LOGFILE_RESTART_HEADER_MIN ||
        UsaOffset + UsaBytes > RestartOffset)
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
    USHORT FreeList, InUseList;
    ULONG SequenceBits;
    USHORT AreaLength16, ClientOffset16, HeaderLength, PageDataOffset;

    if (!Restart || !Area || !Restart->Page ||
        Restart->RestartOffset >= Restart->PageLength)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(Area, sizeof(*Area));
    if (!LogPowerOfTwo(Restart->LogPageSize))
        return STATUS_DATA_ERROR;

    Offset = Restart->RestartOffset;
    Available = Restart->PageLength - Offset;
    if (Available < NTFS_LOGFILE_RESTART_AREA_MIN)
        return STATUS_DATA_ERROR;

    Bytes = Restart->Page + Offset;
    /* Restart-area fields are read by fixed offsets, never native structure casts. */
    /*
     * RESTART_AREA uses 16-bit client counts/list heads and 32-bit sequence
     * width; the fixed offsets below are part of the on-disk format.
     */
    Clients = LOG_U16(Bytes + 0x08);
    FreeList = LOG_U16(Bytes + 0x0A);
    InUseList = LOG_U16(Bytes + 0x0C);
    Flags = LOG_U16(Bytes + 0x0E);
    SequenceBits = LOG_U32(Bytes + 0x10);
    AreaLength16 = LOG_U16(Bytes + 0x14);
    ClientOffset16 = LOG_U16(Bytes + 0x16);
    AreaLength = AreaLength16;
    ClientOffset = ClientOffset16;
    HeaderLength = LOG_U16(Bytes + 0x24);
    PageDataOffset = LOG_U16(Bytes + 0x26);

    if (AreaLength < NTFS_LOGFILE_RESTART_AREA_MIN ||
        AreaLength > Available ||
        ClientOffset < NTFS_LOGFILE_RESTART_AREA_MIN ||
        ClientOffset > AreaLength ||
        (ClientOffset & 7) ||
        (Clients > (AreaLength - ClientOffset) / NTFS_LOGFILE_CLIENT_ENTRY_SIZE) ||
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
    Area->Flags = (USHORT)Flags;
    Area->SequenceNumberBits = SequenceBits;
    Area->RestartAreaLength = AreaLength;
    Area->ClientArrayOffset = ClientOffset;
    Area->FileSize = LOG_U64(Bytes + 0x18);
    Area->LastLsnDataLength = LOG_U32(Bytes + 0x20);
    Area->RecordHeaderLength = HeaderLength;
    Area->LogPageDataOffset = PageDataOffset;
    Area->RestartLogOpenCount = LOG_U32(Bytes + 0x28);
    Area->Reserved = LOG_U32(Bytes + 0x2C);

    if (Area->FileSize == 0 ||
        (Area->FileSize & 0x8000000000000000ULL) != 0 ||
        (Area->FileSize % Restart->LogPageSize) != 0 ||
        Area->LastLsnDataLength > Area->FileSize)
    {
        RtlZeroMemory(Area, sizeof(*Area));
        return STATUS_DATA_ERROR;
    }

    return STATUS_SUCCESS;
}


/*
 * Compare two independently validated restart areas. A sequence-width
 * mismatch cannot be ordered safely, and the modular half-range case is
 * deliberately rejected by NtfsLogfileCompareLsn().
 */
NTSTATUS
NTAPI
NtfsLogfileSelectRestartArea(
    const NTFS_LOGFILE_RESTART_AREA_VIEW *First,
    const NTFS_LOGFILE_RESTART_AREA_VIEW *Second,
    PULONG SelectedIndex)
{
    LONG Comparison;
    NTSTATUS Status;

    if (!First || !Second || !SelectedIndex ||
        First->SequenceNumberBits == 0 ||
        First->SequenceNumberBits > 63 ||
        First->SequenceNumberBits != Second->SequenceNumberBits)
        return STATUS_INVALID_PARAMETER;

    Status = NtfsLogfileCompareLsn(
        First->CurrentLsn, Second->CurrentLsn,
        First->SequenceNumberBits, &Comparison);
    if (!NT_SUCCESS(Status))
        return Status;

    *SelectedIndex = (Comparison < 0) ? 1 : 0;
    return STATUS_SUCCESS;
}


/*
 * Decode native 0xA0-byte client records. The client name is inline UTF-16.
 */
NTSTATUS
NTAPI
NtfsLogfileReadClientTable(
    const NTFS_LOGFILE_RESTART_AREA_VIEW *Area,
    NTFS_LOGFILE_CLIENT_ENTRY_VIEW *Entries,
    ULONG EntryCapacity,
    PULONG EntriesRead)
{
    ULONG i, TableBytes;

    if (!Area || !Area->Buffer || !EntriesRead ||
        (!Entries && EntryCapacity != 0))
        return STATUS_INVALID_PARAMETER;

    *EntriesRead = 0;
    if (Area->LogClients > 0xFFFF)
        return STATUS_DATA_ERROR;
    if (Area->LogClients > EntryCapacity)
        return STATUS_BUFFER_TOO_SMALL;
    if (Area->Length < NTFS_LOGFILE_RESTART_AREA_MIN ||
        Area->ClientArrayOffset < NTFS_LOGFILE_RESTART_AREA_MIN ||
        Area->ClientArrayOffset > Area->Length ||
        (Area->ClientArrayOffset & 7) ||
        Area->LogClients > MAXULONG / NTFS_LOGFILE_CLIENT_ENTRY_SIZE)
        return STATUS_DATA_ERROR;

    TableBytes = Area->LogClients * NTFS_LOGFILE_CLIENT_ENTRY_SIZE;
    if (!LogRange(Area->ClientArrayOffset, TableBytes, Area->Length))
        return STATUS_DATA_ERROR;
    for (i = 0; i < Area->LogClients; ++i)
    {
        const UCHAR *Raw = Area->Buffer + Area->ClientArrayOffset +
            i * NTFS_LOGFILE_CLIENT_ENTRY_SIZE;
        USHORT PreviousClient = LOG_U16(Raw + 0x10);
        USHORT NextClient = LOG_U16(Raw + 0x12);
        ULONG NameLength = LOG_U32(Raw + 0x1C);

        if ((PreviousClient != 0xFFFF &&
             PreviousClient >= Area->LogClients) ||
            (NextClient != 0xFFFF && NextClient >= Area->LogClients) ||
            NameLength > NTFS_LOGFILE_CLIENT_NAME_BYTES ||
            (NameLength & 1))
            return STATUS_DATA_ERROR;
    }

    for (i = 0; i < Area->LogClients; ++i)
    {
        const UCHAR *Raw = Area->Buffer + Area->ClientArrayOffset +
            i * NTFS_LOGFILE_CLIENT_ENTRY_SIZE;

        RtlZeroMemory(&Entries[i], sizeof(Entries[i]));
        Entries[i].OldestLsn = LOG_U64(Raw);
        Entries[i].ClientRestartLsn = LOG_U64(Raw + 0x08);
        Entries[i].PreviousClient = LOG_U16(Raw + 0x10);
        Entries[i].NextClient = LOG_U16(Raw + 0x12);
        Entries[i].SequenceNumber = LOG_U16(Raw + 0x14);
        RtlCopyMemory(Entries[i].Reserved, Raw + 0x16,
            sizeof(Entries[i].Reserved));
        Entries[i].ClientNameLength = LOG_U32(Raw + 0x1C);
        RtlCopyMemory(Entries[i].ClientName, Raw + 0x20,
            NTFS_LOGFILE_CLIENT_NAME_BYTES);
    }

    *EntriesRead = Area->LogClients;
    return STATUS_SUCCESS;
}


/*
 * Check the reciprocal links in the restart client's doubly-linked lists.
 * 0xFFFF is the on-disk null link; circular singleton links are permitted.
 */
NTSTATUS
NTAPI
NtfsLogfileValidateClientLinks(
    const NTFS_LOGFILE_CLIENT_ENTRY_VIEW *Entries,
    ULONG ClientCount)
{
    ULONG i;

    /* 0xFFFF is reserved as the null-link sentinel, not a valid index. */
    if (!Entries || ClientCount == 0 || ClientCount > 0xFFFF)
        return STATUS_INVALID_PARAMETER;

    for (i = 0; i < ClientCount; ++i)
    {
        USHORT Previous = Entries[i].PreviousClient;
        USHORT Next = Entries[i].NextClient;

        if ((Previous != 0xFFFF && Previous >= ClientCount) ||
            (Next != 0xFFFF && Next >= ClientCount))
            return STATUS_DATA_ERROR;

        if (Previous != 0xFFFF &&
            Entries[Previous].NextClient != i)
            return STATUS_DATA_ERROR;

        if (Next != 0xFFFF &&
            Entries[Next].PreviousClient != i)
            return STATUS_DATA_ERROR;
    }

    return STATUS_SUCCESS;
}

static NTSTATUS
LogValidateRestartClientList(
    const NTFS_LOGFILE_CLIENT_ENTRY_VIEW *Entries,
    ULONG ClientCount,
    ULONG Head,
    PULONG ListCount)
{
    ULONG Current = (Head == NTFS_LOGFILE_NO_CLIENT) ? 0xFFFF : Head;
    ULONG Previous = 0xFFFF;
    ULONG Steps = 0;

    *ListCount = 0;
    while (Current != 0xFFFF)
    {
        if (Current >= ClientCount || Steps >= ClientCount ||
            Entries[Current].PreviousClient != Previous)
            return STATUS_DATA_ERROR;

        Previous = Current;
        Current = Entries[Current].NextClient;
        ++Steps;
    }

    *ListCount = Steps;
    return STATUS_SUCCESS;
}

static BOOLEAN
LogRestartClientListContains(
    const NTFS_LOGFILE_CLIENT_ENTRY_VIEW *Entries,
    ULONG ClientCount,
    ULONG Head,
    ULONG ClientIndex)
{
    ULONG Current = (Head == NTFS_LOGFILE_NO_CLIENT) ? 0xFFFF : Head;
    ULONG Steps = 0;

    while (Current != 0xFFFF && Steps < ClientCount)
    {
        if (Current == ClientIndex)
            return TRUE;
        if (Current >= ClientCount)
            return FALSE;
        Current = Entries[Current].NextClient;
        ++Steps;
    }

    return FALSE;
}

NTSTATUS
NTAPI
NtfsLogfileValidateClientLists(
    const NTFS_LOGFILE_RESTART_AREA_VIEW *Area,
    const NTFS_LOGFILE_CLIENT_ENTRY_VIEW *Entries,
    ULONG ClientCount)
{
    ULONG FreeCount, InUseCount, i;
    NTSTATUS Status;

    if (!Area || ClientCount > 0xFFFF ||
        ClientCount != Area->LogClients ||
        (ClientCount != 0 && !Entries))
        return STATUS_INVALID_PARAMETER;

    if ((Area->FreeList != NTFS_LOGFILE_NO_CLIENT &&
         Area->FreeList >= ClientCount) ||
        (Area->InUseList != NTFS_LOGFILE_NO_CLIENT &&
         Area->InUseList >= ClientCount))
        return STATUS_DATA_ERROR;

    if (ClientCount == 0)
        return (Area->FreeList == 0xFFFFFFFF &&
                Area->InUseList == 0xFFFFFFFF) ?
            STATUS_SUCCESS : STATUS_DATA_ERROR;

    Status = LogValidateRestartClientList(
        Entries, ClientCount, Area->FreeList, &FreeCount);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = LogValidateRestartClientList(
        Entries, ClientCount, Area->InUseList, &InUseCount);
    if (!NT_SUCCESS(Status))
        return Status;

    if (FreeCount + InUseCount != ClientCount)
        return STATUS_DATA_ERROR;

    for (i = 0; i < ClientCount; ++i)
    {
        BOOLEAN InFree = LogRestartClientListContains(
            Entries, ClientCount, Area->FreeList, i);
        BOOLEAN InUse = LogRestartClientListContains(
            Entries, ClientCount, Area->InUseList, i);

        if (InFree == InUse)
            return STATUS_DATA_ERROR;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NtfsLogfileDecodeNtfsOperation(
    const NTFS_LOGFILE_ASSEMBLED_RECORD *Record,
    NTFS_LOGFILE_NTFS_OPERATION_VIEW *Operation)
{
    const UCHAR *Data;
    ULONG Available;
    ULONG LcnBytes, PayloadOffset;
    NTFS_LOGFILE_NTFS_OPERATION_VIEW Decoded;

    if (!Operation)
        return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(Operation, sizeof(*Operation));

    if (!Record || !Record->Buffer ||
        Record->HeaderLength < NTFS_LFS_RECORD_HEADER_SIZE ||
        Record->HeaderLength > Record->Length)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(&Decoded, sizeof(Decoded));
    Available = Record->Length - Record->HeaderLength;
    if (Record->Header.ClientDataLength > Available ||
        Record->Header.ClientDataLength <
            NTFS_LOGFILE_NTFS_OPERATION_HEADER_SIZE)
        return STATUS_DATA_ERROR;

    Data = Record->Buffer + Record->HeaderLength;
    Decoded.RedoOperation = LOG_U16(Data + 0x00);
    Decoded.UndoOperation = LOG_U16(Data + 0x02);
    Decoded.RedoOffset = LOG_U16(Data + 0x04);
    Decoded.RedoLength = LOG_U16(Data + 0x06);
    Decoded.UndoOffset = LOG_U16(Data + 0x08);
    Decoded.UndoLength = LOG_U16(Data + 0x0A);
    Decoded.TargetAttribute = LOG_U16(Data + 0x0C);
    Decoded.LcnsToFollow = LOG_U16(Data + 0x0E);
    Decoded.RecordOffset = LOG_U16(Data + 0x10);
    Decoded.AttributeOffset = LOG_U16(Data + 0x12);
    Decoded.TargetVcn = LOG_U64(Data + 0x18);
    Decoded.ClientDataLength = Record->Header.ClientDataLength;

    if (Decoded.LcnsToFollow > MAXULONG / sizeof(ULONGLONG))
        return STATUS_DATA_ERROR;
    LcnBytes = Decoded.LcnsToFollow * sizeof(ULONGLONG);
    if (!LogRange(NTFS_LOGFILE_NTFS_OPERATION_HEADER_SIZE,
            LcnBytes, Decoded.ClientDataLength))
        return STATUS_DATA_ERROR;
    PayloadOffset = NTFS_LOGFILE_NTFS_OPERATION_HEADER_SIZE + LcnBytes;

    if ((Decoded.RedoLength != 0 &&
         (Decoded.RedoOffset < PayloadOffset ||
          !LogRange(Decoded.RedoOffset, Decoded.RedoLength,
              Decoded.ClientDataLength))) ||
        (Decoded.UndoLength != 0 &&
         (Decoded.UndoOffset < PayloadOffset ||
          !LogRange(Decoded.UndoOffset, Decoded.UndoLength,
              Decoded.ClientDataLength))))
        return STATUS_DATA_ERROR;

    Decoded.LcnList = Data + NTFS_LOGFILE_NTFS_OPERATION_HEADER_SIZE;
    Decoded.RedoData = (Decoded.RedoLength != 0) ?
        Data + Decoded.RedoOffset : NULL;
    Decoded.UndoData = (Decoded.UndoLength != 0) ?
        Data + Decoded.UndoOffset : NULL;
    *Operation = Decoded;
    return STATUS_SUCCESS;
}


NTSTATUS
NTAPI
NtfsLogfileReadOperationLcn(
    const NTFS_LOGFILE_NTFS_OPERATION_VIEW *Operation,
    ULONG Index,
    PULONGLONG Lcn)
{
    if (!Operation || !Lcn || !Operation->LcnList ||
        Index >= Operation->LcnsToFollow)
        return STATUS_INVALID_PARAMETER;

    *Lcn = LOG_U64(Operation->LcnList + Index * sizeof(ULONGLONG));
    return STATUS_SUCCESS;
}



/*
 * Analyze already-classified transaction records. This deliberately does
 * not infer on-disk operation types: the native client-record decoder must
 * supply Begin/Update/Commit/Abort classifications before this state machine
 * can be used for recovery decisions.
 */
NTSTATUS
NTAPI
NtfsLogfileAnalyzeTransactionEvents(
    const NTFS_LOGFILE_TRANSACTION_EVENT *Events,
    ULONG EventCount,
    ULONG ClientCount,
    NTFS_LOGFILE_TRANSACTION_STATE_VIEW *States,
    ULONG StateCapacity,
    PULONG StatesWritten)
{
    ULONG i, j, Count = 0;
    ULONG EventBytes, StateBytes;
    NTSTATUS Status = STATUS_SUCCESS;

    if ((!Events && EventCount != 0) || !StatesWritten ||
        (!States && StateCapacity != 0) || ClientCount == 0 ||
        EventCount > MAXULONG / sizeof(*Events) ||
        StateCapacity > MAXULONG / sizeof(*States))
        return STATUS_INVALID_PARAMETER;

    EventBytes = EventCount * (ULONG)sizeof(*Events);
    StateBytes = StateCapacity * (ULONG)sizeof(*States);
    if (LogBufferRangesOverlap(
            Events, EventBytes, States, StateBytes) ||
        LogBufferRangesOverlap(
            Events, EventBytes, StatesWritten, (ULONG)sizeof(*StatesWritten)) ||
        LogBufferRangesOverlap(
            States, StateBytes, StatesWritten, (ULONG)sizeof(*StatesWritten)))
        return STATUS_INVALID_PARAMETER;

    *StatesWritten = 0;

    for (i = 0; i < EventCount; ++i)
    {
        const NTFS_LOGFILE_TRANSACTION_EVENT *Event = &Events[i];
        NTFS_LOGFILE_TRANSACTION_STATE_VIEW *State = NULL;

        if (Event->Lsn == 0 || Event->TransactionId == 0 ||
            !NT_SUCCESS(NtfsLogfileValidateClientReference(
                Event->ClientIndex, ClientCount)) ||
            Event->Action < NtfsLogfileTransactionBegin ||
            Event->Action > NtfsLogfileTransactionAbort)
        {
            Status = STATUS_DATA_ERROR;
            goto Failure;
        }

        /*
         * An LFS record has one globally unique LSN, regardless of which
         * client or transaction owns it. Reject duplicate event identities
         * before mutating the transaction state table; otherwise a caller
         * could accidentally account one physical log record twice.
         */
        for (j = 0; j < i; ++j)
        {
            if (Events[j].Lsn == Event->Lsn)
            {
                Status = STATUS_DATA_ERROR;
                goto Failure;
            }
        }

        for (j = 0; j < Count; ++j)
        {
            if (States[j].TransactionId == Event->TransactionId &&
                States[j].ClientIndex == Event->ClientIndex)
            {
                State = &States[j];
                break;
            }
        }

        if (Event->Action == NtfsLogfileTransactionBegin)
        {
            if (State != NULL)
            {
                Status = STATUS_DATA_ERROR;
                goto Failure;
            }
            if (Count == StateCapacity)
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                goto Failure;
            }

            State = &States[Count++];
            RtlZeroMemory(State, sizeof(*State));
            State->TransactionId = Event->TransactionId;
            State->ClientIndex = Event->ClientIndex;
            State->State = NtfsLogfileTransactionActive;
            State->FirstLsn = Event->Lsn;
        }
        else if (State == NULL ||
                 State->State != NtfsLogfileTransactionActive)
        {
            Status = STATUS_DATA_ERROR;
            goto Failure;
        }

        State->LastLsn = Event->Lsn;
        if (State->RecordCount == MAXULONG)
        {
            Status = STATUS_INTEGER_OVERFLOW;
            goto Failure;
        }
        ++State->RecordCount;

        if (Event->Action == NtfsLogfileTransactionCommit)
            State->State = NtfsLogfileTransactionCommitted;
        else if (Event->Action == NtfsLogfileTransactionAbort)
            State->State = NtfsLogfileTransactionAborted;
    }

    *StatesWritten = Count;
    return STATUS_SUCCESS;

Failure:
    if (States && Count != 0)
        RtlZeroMemory(States, Count * sizeof(*States));
    return Status;
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
    ULONG SequenceNumberBits,
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

    /*
     * The iterator is caller-owned and may have been modified after
     * initialization. Revalidate every arithmetic operand before using it;
     * otherwise a zero/non-power-of-two alignment or a forged cursor can
     * bypass the initializer's bounds checks.
     */
    Page = Iterator->Page;
    if (Iterator->RecordHeaderLength == 0 ||
        Iterator->LengthFieldOffset > Iterator->RecordHeaderLength ||
        sizeof(ULONG) > Iterator->RecordHeaderLength - Iterator->LengthFieldOffset ||
        !LogPowerOfTwo(Iterator->Alignment) ||
        Iterator->Alignment < sizeof(ULONG) ||
        (Iterator->HasClientIndex &&
         (Iterator->ClientIndexFieldOffset > Iterator->RecordHeaderLength ||
          sizeof(USHORT) > Iterator->RecordHeaderLength - Iterator->ClientIndexFieldOffset)) ||
        Page->DataOffset > Page->NextRecordOffset ||
        Page->NextRecordOffset > Page->PageLength ||
        Iterator->Cursor < Page->DataOffset ||
        Iterator->Cursor > Page->NextRecordOffset)
        return STATUS_DATA_ERROR;

    RtlZeroMemory(Record, sizeof(*Record));

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
    if (!FirstPage->Page || FirstPage->PageCount != PageCount ||
        FirstPage->PagePosition >= FirstPage->PageCount ||
        FirstPageOffset < FirstPage->DataOffset ||
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
