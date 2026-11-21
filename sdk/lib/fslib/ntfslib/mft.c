/*
 * Clean-room NTFS FILE-record parser.
 * Parsing is deliberately read-only; repair/replay belongs in separate layers.
 */
#include "ntfslib.h"
#include "mft.h"

#define MFT_U16(p) ((USHORT)((p)[0] | ((USHORT)(p)[1] << 8)))
#define MFT_U32(p) ((ULONG)((p)[0] | ((ULONG)(p)[1] << 8) | ((ULONG)(p)[2] << 16) | ((ULONG)(p)[3] << 24)))
#define MFT_U64(p) ((ULONGLONG)MFT_U32(p) | ((ULONGLONG)MFT_U32((p) + 4) << 32))

static BOOLEAN
MftIsPowerOfTwo(ULONG Value)
{
    return Value != 0 && (Value & (Value - 1)) == 0;
}

static BOOLEAN
MftRangeValid(ULONG Offset, ULONG Length, ULONG Limit)
{
    return Offset <= Limit && Length <= Limit - Offset;
}

NTSTATUS
NTAPI
NtfsMftOpenRecord(
    _In_reads_bytes_(RecordLength) const VOID *Input,
    _In_ ULONG RecordLength,
    _In_ ULONG SectorSize,
    _Out_writes_bytes_(RecordLength) VOID *Scratch,
    _Out_ PNTFS_MFT_RECORD_VIEW View)
{
    const UCHAR *Source = (const UCHAR *)Input;
    UCHAR *Target = (UCHAR *)Scratch;
    USHORT UsaOffset, UsaCount, UpdateSequence;
    ULONG SectorCount, UsaBytes, BytesInUse, BytesAllocated;
    ULONG FirstAttributeOffset, i;

    if (Input == NULL || Scratch == NULL || Input == Scratch || View == NULL ||
        !MftIsPowerOfTwo(SectorSize) || SectorSize < 512 ||
        RecordLength < NTFS_MFT_FILE_HEADER_SIZE ||
        RecordLength % SectorSize != 0)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(View, sizeof(*View));

    if (MFT_U32(Source) != NTFS_MFT_FILE_SIGNATURE)
        return STATUS_DATA_ERROR;

    UsaOffset = MFT_U16(Source + 4);
    UsaCount = MFT_U16(Source + 6);
    SectorCount = RecordLength / SectorSize;

    if (SectorCount == 0 || SectorCount >= 0xFFFF ||
        UsaCount != SectorCount + 1 ||
        (UsaOffset & 1) != 0 ||
        UsaOffset < NTFS_MFT_FILE_HEADER_SIZE)
        return STATUS_DATA_ERROR;

    UsaBytes = (ULONG)UsaCount * sizeof(USHORT);
    if (!MftRangeValid(UsaOffset, UsaBytes, RecordLength) ||
        UsaOffset + UsaBytes > SectorSize - sizeof(USHORT))
        return STATUS_DATA_ERROR;

    BytesInUse = MFT_U32(Source + 0x18);
    BytesAllocated = MFT_U32(Source + 0x1C);
    FirstAttributeOffset = MFT_U16(Source + 0x14);

    if (BytesAllocated != RecordLength ||
        BytesInUse < NTFS_MFT_FILE_HEADER_SIZE ||
        BytesInUse > BytesAllocated ||
        FirstAttributeOffset < NTFS_MFT_FILE_HEADER_SIZE ||
        (FirstAttributeOffset & 7) != 0 ||
        FirstAttributeOffset >= BytesInUse ||
        FirstAttributeOffset < UsaOffset + UsaBytes)
        return STATUS_DATA_ERROR;

    RtlCopyMemory(Target, Source, RecordLength);
    UpdateSequence = MFT_U16(Target + UsaOffset);

    for (i = 0; i < SectorCount; ++i)
    {
        ULONG Trailer = ((i + 1) * SectorSize) - sizeof(USHORT);
        USHORT Current = MFT_U16(Target + Trailer);
        USHORT Replacement = MFT_U16(Target + UsaOffset + ((i + 1) * 2));

        if (Current != UpdateSequence)
        {
            RtlZeroMemory(Target, RecordLength);
            return STATUS_DATA_ERROR;
        }

        Target[Trailer] = (UCHAR)(Replacement & 0xFF);
        Target[Trailer + 1] = (UCHAR)(Replacement >> 8);
    }

    View->Buffer = Target;
    View->RecordLength = RecordLength;
    View->BytesInUse = BytesInUse;
    View->FirstAttributeOffset = FirstAttributeOffset;
    View->SequenceNumber = MFT_U16(Target + 0x10);
    View->Flags = MFT_U16(Target + 0x16);
    View->RecordNumber = MFT_U32(Target + 0x2C);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NtfsMftDecodeRunlist(
    _In_reads_bytes_(RunlistLength) const UCHAR *Runlist,
    _In_ ULONG RunlistLength,
    _In_ ULONGLONG StartingVcn,
    _Out_writes_to_(RunCapacity, *RunCount) PNTFS_MFT_RUN Runs,
    _In_ ULONG RunCapacity,
    _Out_ PULONG RunCount)
{
    ULONG Offset = 0, Count = 0;
    ULONGLONG Vcn = StartingVcn;
    LONGLONG Lcn = 0;
    BOOLEAN Terminated = FALSE;

    if (Runlist == NULL || RunCount == NULL ||
        (RunCapacity != 0 && Runs == NULL))
        return STATUS_INVALID_PARAMETER;

    *RunCount = 0;
    while (Offset < RunlistLength)
    {
        UCHAR Header = Runlist[Offset++];
        ULONG LengthBytes, OffsetBytes, i;
        ULONGLONG ClusterCount = 0;
        LONGLONG Delta = 0, NewLcn;
        BOOLEAN Sparse;

        if (Header == 0)
        {
            Terminated = TRUE;
            break;
        }

        LengthBytes = Header & 0x0F;
        OffsetBytes = (Header >> 4) & 0x0F;
        if (LengthBytes == 0 || LengthBytes > 8 || OffsetBytes > 8 ||
            !MftRangeValid(Offset, LengthBytes + OffsetBytes, RunlistLength))
            return STATUS_DATA_ERROR;

        for (i = 0; i < LengthBytes; ++i)
            ClusterCount |= (ULONGLONG)Runlist[Offset + i] << (i * 8);
        Offset += LengthBytes;

        if (ClusterCount == 0 || Vcn > ((ULONGLONG)-1) - ClusterCount)
            return STATUS_DATA_ERROR;

        Sparse = (OffsetBytes == 0);
        if (Sparse)
        {
            NewLcn = 0;
        }
        else
        {
            ULONGLONG RawDelta = 0;
            for (i = 0; i < OffsetBytes; ++i)
                RawDelta |= (ULONGLONG)Runlist[Offset + i] << (i * 8);

            /* Sign-extend the little-endian two's-complement LCN delta. */
            if (OffsetBytes < 8 &&
                (Runlist[Offset + OffsetBytes - 1] & 0x80) != 0)
                RawDelta |= (~0ULL) << (OffsetBytes * 8);
            Delta = (LONGLONG)RawDelta;

            if (Delta < 0)
            {
                /* Avoid negating MINLONGLONG directly. */
                ULONGLONG Magnitude = (ULONGLONG)(-(Delta + 1)) + 1;
                if (Magnitude > (ULONGLONG)Lcn)
                    return STATUS_DATA_ERROR;
            }
            else if ((ULONGLONG)Delta >
                     (ULONGLONG)MAXLONGLONG - (ULONGLONG)Lcn)
            {
                return STATUS_DATA_ERROR;
            }

            NewLcn = Lcn + Delta;
            Lcn = NewLcn;
        }
        Offset += OffsetBytes;

        if (Count >= RunCapacity)
        {
            *RunCount = Count + 1;
            return STATUS_BUFFER_TOO_SMALL;
        }

        Runs[Count].StartingVcn = Vcn;
        Runs[Count].ClusterCount = ClusterCount;
        Runs[Count].StartingLcn = Sparse ? 0 : NewLcn;
        Runs[Count].Sparse = Sparse;
        ++Count;
        Vcn += ClusterCount;
    }

    if (!Terminated)
        return STATUS_DATA_ERROR;

    *RunCount = Count;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NtfsMftGetAttribute(
    _In_ const NTFS_MFT_RECORD_VIEW *Record,
    _Inout_ PULONG Cursor,
    _Out_ PNTFS_MFT_ATTRIBUTE_VIEW Attribute)
{
    const UCHAR *Base;
    ULONG Offset, Type, Length;
    UCHAR NonResident, NameLength;
    USHORT NameOffset;

    if (Record == NULL || Cursor == NULL || Attribute == NULL ||
        Record->Buffer == NULL ||
        Record->BytesInUse > Record->RecordLength ||
        Record->FirstAttributeOffset >= Record->BytesInUse)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(Attribute, sizeof(*Attribute));
    Offset = *Cursor;
    if (Offset == 0)
        Offset = Record->FirstAttributeOffset;

    if ((Offset & 7) != 0 || Offset < Record->FirstAttributeOffset ||
        !MftRangeValid(Offset, sizeof(ULONG), Record->BytesInUse))
        return STATUS_DATA_ERROR;

    Base = Record->Buffer + Offset;
    Type = MFT_U32(Base);
    if (Type == NTFS_MFT_ATTRIBUTE_END)
    {
        *Cursor = Offset;
        return STATUS_NO_MORE_ENTRIES;
    }

    if (!MftRangeValid(Offset, 16, Record->BytesInUse))
        return STATUS_DATA_ERROR;

    Length = MFT_U32(Base + 4);
    NonResident = Base[8];
    NameLength = Base[9];
    NameOffset = MFT_U16(Base + 10);

    if (Length < 24 || (Length & 7) != 0 ||
        !MftRangeValid(Offset, Length, Record->BytesInUse) ||
        NonResident > 1)
        return STATUS_DATA_ERROR;

    if (NameLength != 0)
    {
        ULONG NameBytes = (ULONG)NameLength * sizeof(WCHAR);
        ULONG MinimumNameOffset = NonResident ? 64 : 24;
        if (NameOffset < MinimumNameOffset || (NameOffset & 1) != 0 ||
            !MftRangeValid(NameOffset, NameBytes, Length))
            return STATUS_DATA_ERROR;
    }
    else if (NameOffset != 0 && NameOffset >= Length)
    {
        return STATUS_DATA_ERROR;
    }

    Attribute->Buffer = (PUCHAR)Base;
    Attribute->RecordOffset = Offset;
    Attribute->Type = Type;
    Attribute->Length = Length;
    Attribute->NonResident = (BOOLEAN)NonResident;
    Attribute->NameLength = NameLength;
    Attribute->NameOffset = NameOffset;
    Attribute->Flags = MFT_U16(Base + 12);
    Attribute->Instance = MFT_U16(Base + 14);

    if (!NonResident)
    {
        ULONG ValueLength, ValueOffset;
        if (Length < 24)
            return STATUS_DATA_ERROR;
        ValueLength = MFT_U32(Base + 16);
        ValueOffset = MFT_U16(Base + 20);
        if (ValueOffset < 24 || (ValueOffset & 3) != 0 ||
            !MftRangeValid(ValueOffset, ValueLength, Length))
            return STATUS_DATA_ERROR;
        if (NameLength != 0 &&
            ValueOffset < (ULONG)NameOffset + ((ULONG)NameLength * sizeof(WCHAR)))
            return STATUS_DATA_ERROR;
        Attribute->ValueOffset = ValueOffset;
        Attribute->ValueLength = ValueLength;
    }
    else
    {
        ULONG RunsOffset;
        ULONGLONG LowestVcn, HighestVcn;
        if (Length < 64)
            return STATUS_DATA_ERROR;
        LowestVcn = MFT_U64(Base + 16);
        HighestVcn = MFT_U64(Base + 24);
        RunsOffset = MFT_U16(Base + 32);
        if (RunsOffset < 64 || (RunsOffset & 3) != 0 || RunsOffset >= Length ||
            HighestVcn < LowestVcn ||
            (NameLength != 0 &&
             (ULONG)NameOffset + ((ULONG)NameLength * sizeof(WCHAR)) > RunsOffset))
            return STATUS_DATA_ERROR;
        Attribute->DataRunsOffset = RunsOffset;
        Attribute->LowestVcn = LowestVcn;
        Attribute->HighestVcn = HighestVcn;
    }

    if (Offset > MAXULONG - Length)
        return STATUS_DATA_ERROR;
    *Cursor = Offset + Length;
    return STATUS_SUCCESS;
}
