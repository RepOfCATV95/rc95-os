/*
 * PROJECT:   CATV 95 Kernel
 * LICENSE:   GPL-2.0-or-later
 * PURPOSE:   Validated Btrfs chunk-logical to member-physical mapping helpers.
 *
 * This header intentionally contains no I/O or allocation. It is suitable for
 * PASSIVE_LEVEL preflight and for copying the resulting plan into nonpaged
 * dump-stack state. It does not resolve/pin member devices.
 */
#ifndef _PO_DUMP_BTRFS_MAP_H_
#define _PO_DUMP_BTRFS_MAP_H_

#define PO_BTRFS_BLOCK_RAID0    0x008ULL
#define PO_BTRFS_BLOCK_RAID1    0x010ULL
#define PO_BTRFS_BLOCK_DUP      0x020ULL
#define PO_BTRFS_BLOCK_RAID10   0x040ULL
#define PO_BTRFS_BLOCK_RAID5    0x080ULL
#define PO_BTRFS_BLOCK_RAID6    0x100ULL
#define PO_BTRFS_BLOCK_RAID1C3  0x200ULL
#define PO_BTRFS_BLOCK_RAID1C4  0x400ULL

#define PO_BTRFS_HIBER_MAPPING_VERSION 1
#define PO_BTRFS_HIBER_STRIPE_READONLY 0x00000001
#define PO_BTRFS_HIBER_STRIPE_SEEDING  0x00000002
#define PO_BTRFS_HIBER_STRIPE_MISSING  0x00000004
#define PO_BTRFS_HIBER_MAPPING_FLAG_READONLY 0x00000001
#define PO_BTRFS_HIBER_MAPPING_FLAG_DEGRADED 0x00000002
#define PO_BTRFS_HIBER_MAPPING_FLAG_MISSING  0x00000004

typedef struct _PO_BTRFS_HIBER_STRIPE
{
    ULONGLONG DeviceId;
    UCHAR DeviceUuid[16];
    ULONGLONG PhysicalOffset;
    ULONGLONG DeviceSize;
    ULONG Flags;
    ULONG Reserved;
} PO_BTRFS_HIBER_STRIPE, *PPO_BTRFS_HIBER_STRIPE;

typedef struct _PO_BTRFS_HIBER_MAPPING
{
    ULONG Size;
    ULONG StripeCount;
    ULONGLONG LogicalAddress;
    ULONGLONG SegmentLength;
    ULONGLONG ChunkLogical;
    ULONGLONG ChunkLength;
    ULONGLONG StripeLength;
    ULONGLONG ChunkType;
    ULONG SectorSize;
    USHORT SubStripes;
    USHORT Reserved;
    ULONG Version;
    ULONG HeaderSize;
    ULONG Flags;
    ULONG Reserved2;
    PO_BTRFS_HIBER_STRIPE Stripes[1];
} PO_BTRFS_HIBER_MAPPING, *PPO_BTRFS_HIBER_MAPPING;

/* Keep this private consumer ABI byte-for-byte aligned with btrfsioctl.h. */
C_ASSERT(FIELD_OFFSET(PO_BTRFS_HIBER_STRIPE, DeviceId) == 0);
C_ASSERT(FIELD_OFFSET(PO_BTRFS_HIBER_STRIPE, DeviceUuid) == 8);
C_ASSERT(FIELD_OFFSET(PO_BTRFS_HIBER_STRIPE, PhysicalOffset) == 24);
C_ASSERT(FIELD_OFFSET(PO_BTRFS_HIBER_STRIPE, DeviceSize) == 32);
C_ASSERT(FIELD_OFFSET(PO_BTRFS_HIBER_STRIPE, Flags) == 40);
C_ASSERT(sizeof(PO_BTRFS_HIBER_STRIPE) == 48);
C_ASSERT(FIELD_OFFSET(PO_BTRFS_HIBER_MAPPING, LogicalAddress) == 8);
C_ASSERT(FIELD_OFFSET(PO_BTRFS_HIBER_MAPPING, ChunkLogical) == 24);
C_ASSERT(FIELD_OFFSET(PO_BTRFS_HIBER_MAPPING, ChunkType) == 48);
C_ASSERT(FIELD_OFFSET(PO_BTRFS_HIBER_MAPPING, SectorSize) == 56);
C_ASSERT(FIELD_OFFSET(PO_BTRFS_HIBER_MAPPING, Version) == 64);
C_ASSERT(FIELD_OFFSET(PO_BTRFS_HIBER_MAPPING, Stripes) == 80);

#define PO_BTRFS_HIBER_MAX_TARGETS 4

typedef struct _PO_BTRFS_HIBER_TARGET
{
    ULONG StripeIndex;
    ULONGLONG PhysicalOffset;
    ULONGLONG Length;
} PO_BTRFS_HIBER_TARGET, *PPO_BTRFS_HIBER_TARGET;

static __inline
BOOLEAN
PopDumpBtrfsAddOverflowU64(
    _In_ ULONGLONG A,
    _In_ ULONGLONG B,
    _Out_ PULONGLONG Result)
{
    *Result = A + B;
    return *Result < A;
}

static __inline
BOOLEAN
PopDumpBtrfsMultiplyOverflowU64(
    _In_ ULONGLONG A,
    _In_ ULONGLONG B,
    _Out_ PULONGLONG Result)
{
    if (A != 0 && B > (~(ULONGLONG)0) / A)
        return TRUE;
    *Result = A * B;
    return FALSE;
}

static __inline
NTSTATUS
PopDumpBtrfsValidateMapping(
    _In_reads_bytes_(BufferLength) const VOID *Buffer,
    _In_ ULONG BufferLength,
    _Out_ PPO_BTRFS_HIBER_MAPPING *Mapping)
{
    PPO_BTRFS_HIBER_MAPPING Header;
    ULONGLONG End;
    ULONG RequiredSize;
    ULONG i;

    if (Mapping == NULL || Buffer == NULL)
        return STATUS_INVALID_PARAMETER;
    *Mapping = NULL;

    if (BufferLength < FIELD_OFFSET(PO_BTRFS_HIBER_MAPPING, Stripes))
        return STATUS_FILE_CORRUPT_ERROR;

    Header = (PPO_BTRFS_HIBER_MAPPING)Buffer;
    if (Header->Version != PO_BTRFS_HIBER_MAPPING_VERSION ||
        Header->HeaderSize != FIELD_OFFSET(PO_BTRFS_HIBER_MAPPING, Stripes) ||
        Header->Reserved != 0 || Header->Reserved2 != 0 ||
        (Header->Flags & ~(PO_BTRFS_HIBER_MAPPING_FLAG_READONLY |
                           PO_BTRFS_HIBER_MAPPING_FLAG_DEGRADED |
                           PO_BTRFS_HIBER_MAPPING_FLAG_MISSING)) != 0 ||
        Header->StripeCount == 0 ||
        Header->StripeCount > (MAXULONG -
            FIELD_OFFSET(PO_BTRFS_HIBER_MAPPING, Stripes)) /
            sizeof(PO_BTRFS_HIBER_STRIPE))
        return STATUS_FILE_CORRUPT_ERROR;

    RequiredSize = FIELD_OFFSET(PO_BTRFS_HIBER_MAPPING, Stripes) +
                   Header->StripeCount * sizeof(PO_BTRFS_HIBER_STRIPE);
    if (Header->Size != RequiredSize || Header->Size > BufferLength ||
        Header->SegmentLength == 0 || Header->ChunkLength == 0 ||
        Header->StripeLength == 0 || Header->SectorSize == 0 ||
        (Header->SectorSize & (Header->SectorSize - 1)) != 0 ||
        (Header->LogicalAddress % Header->SectorSize) != 0 ||
        (Header->SegmentLength % Header->SectorSize) != 0 ||
        (Header->ChunkLogical % Header->SectorSize) != 0 ||
        (Header->ChunkLength % Header->SectorSize) != 0 ||
        (Header->StripeLength % Header->SectorSize) != 0 ||
        Header->LogicalAddress < Header->ChunkLogical ||
        PopDumpBtrfsAddOverflowU64(Header->ChunkLogical,
                                  Header->ChunkLength, &End) ||
        Header->LogicalAddress >= End ||
        Header->SegmentLength > End - Header->LogicalAddress)
        return STATUS_FILE_CORRUPT_ERROR;

    for (i = 0; i < Header->StripeCount; ++i)
    {
        const PO_BTRFS_HIBER_STRIPE *Stripe = &Header->Stripes[i];
        UCHAR UuidOr = 0;
        ULONG UuidIndex;

        for (UuidIndex = 0; UuidIndex < sizeof(Stripe->DeviceUuid); ++UuidIndex)
            UuidOr |= Stripe->DeviceUuid[UuidIndex];

        if (Stripe->Reserved != 0 ||
            (Stripe->Flags & ~(PO_BTRFS_HIBER_STRIPE_READONLY |
                               PO_BTRFS_HIBER_STRIPE_SEEDING |
                               PO_BTRFS_HIBER_STRIPE_MISSING)) != 0)
            return STATUS_FILE_CORRUPT_ERROR;

        if (!(Stripe->Flags & PO_BTRFS_HIBER_STRIPE_MISSING))
        {
            if (Stripe->DeviceId == 0 || UuidOr == 0 ||
                Stripe->DeviceSize == 0 ||
                Stripe->PhysicalOffset >= Stripe->DeviceSize ||
                (Stripe->PhysicalOffset % Header->SectorSize) != 0 ||
                (Stripe->DeviceSize % Header->SectorSize) != 0)
                return STATUS_FILE_CORRUPT_ERROR;
        }
    }

    *Mapping = Header;
    return STATUS_SUCCESS;
}

/*
 * Translate one logical byte range within a validated chunk segment into
 * physical member targets. RAID5/6 are deliberately rejected: layout alone
 * is not sufficient to write parity safely.
 */
static __inline
NTSTATUS
PopDumpBtrfsMapLogicalRange(
    _In_ const PO_BTRFS_HIBER_MAPPING *Mapping,
    _In_ ULONGLONG LogicalAddress,
    _In_ ULONGLONG Length,
    _Out_writes_(PO_BTRFS_HIBER_MAX_TARGETS) PPO_BTRFS_HIBER_TARGET Targets,
    _Out_ PULONG TargetCount,
    _Out_ PULONGLONG MappedLength)
{
    ULONGLONG Delta, StripeNumber, StripeOffset, Physical, End;
    ULONGLONG Profile;
    ULONG Count = 0, Index, MirrorCount = 1;
    ULONGLONG MaxLength;
    ULONG i;

    if (Mapping == NULL || Targets == NULL || TargetCount == NULL ||
        MappedLength == NULL)
        return STATUS_INVALID_PARAMETER;

    /* Never leave stale output targets visible after a failed translation. */
    *TargetCount = 0;
    *MappedLength = 0;

    if (Length == 0)
        return STATUS_INVALID_PARAMETER;

    if (Mapping->Flags != 0)
        return STATUS_NOT_SUPPORTED;
    if (Mapping->SectorSize == 0 || Mapping->StripeLength == 0 ||
        Mapping->ChunkLength == 0 || Mapping->SegmentLength == 0 ||
        Mapping->StripeCount == 0 ||
        Mapping->StripeCount > (MAXULONG /
            sizeof(PO_BTRFS_HIBER_STRIPE)))
        return STATUS_FILE_CORRUPT_ERROR;

    if (LogicalAddress < Mapping->LogicalAddress ||
        LogicalAddress - Mapping->LogicalAddress >= Mapping->SegmentLength)
        return STATUS_INVALID_PARAMETER;

    Delta = LogicalAddress - Mapping->ChunkLogical;
    if (Delta >= Mapping->ChunkLength)
        return STATUS_FILE_CORRUPT_ERROR;

    Profile = Mapping->ChunkType &
        (PO_BTRFS_BLOCK_RAID0 | PO_BTRFS_BLOCK_RAID1 |
         PO_BTRFS_BLOCK_DUP | PO_BTRFS_BLOCK_RAID10 |
         PO_BTRFS_BLOCK_RAID5 | PO_BTRFS_BLOCK_RAID6 |
         PO_BTRFS_BLOCK_RAID1C3 | PO_BTRFS_BLOCK_RAID1C4);

    if (Profile != 0 && (Profile & (Profile - 1)) != 0)
        return STATUS_FILE_CORRUPT_ERROR;
    if (Profile == PO_BTRFS_BLOCK_RAID5 || Profile == PO_BTRFS_BLOCK_RAID6)
        return STATUS_NOT_SUPPORTED;

    /* Every supported profile has a strict, known stripe topology. */
    if (Profile == 0 && Mapping->StripeCount != 1)
        return STATUS_NOT_SUPPORTED;
    if (Profile == PO_BTRFS_BLOCK_DUP &&
        (Mapping->StripeCount != 2 ||
         Mapping->Stripes[0].DeviceId != Mapping->Stripes[1].DeviceId ||
         RtlCompareMemory(Mapping->Stripes[0].DeviceUuid,
                          Mapping->Stripes[1].DeviceUuid, 16) != 16))
        return STATUS_NOT_SUPPORTED;
    if (Profile == PO_BTRFS_BLOCK_RAID1 && Mapping->StripeCount != 2)
        return STATUS_NOT_SUPPORTED;
    if (Profile == PO_BTRFS_BLOCK_RAID1C3 && Mapping->StripeCount != 3)
        return STATUS_NOT_SUPPORTED;
    if (Profile == PO_BTRFS_BLOCK_RAID1C4 && Mapping->StripeCount != 4)
        return STATUS_NOT_SUPPORTED;
    if (Profile == PO_BTRFS_BLOCK_RAID0 &&
        (Mapping->StripeCount < 2 || Mapping->SubStripes != 0))
        return STATUS_NOT_SUPPORTED;
    if (Profile == PO_BTRFS_BLOCK_RAID10 &&
        (Mapping->SubStripes < 2 || Mapping->StripeCount < 4 ||
         (Mapping->StripeCount % Mapping->SubStripes) != 0 ||
         Mapping->StripeCount / Mapping->SubStripes < 2))
        return STATUS_NOT_SUPPORTED;

    if ((LogicalAddress % Mapping->SectorSize) != 0 ||
        (Length % Mapping->SectorSize) != 0)
        return STATUS_INVALID_PARAMETER;

    MaxLength = Mapping->SegmentLength -
                (LogicalAddress - Mapping->LogicalAddress);
    if (Length > MaxLength)
        Length = MaxLength;

    StripeOffset = Delta % Mapping->StripeLength;
    StripeNumber = Delta / Mapping->StripeLength;

    if (Profile == PO_BTRFS_BLOCK_RAID0)
    {
        if (Mapping->StripeCount < 2 || Mapping->SubStripes != 0)
            return STATUS_NOT_SUPPORTED;
        Index = (ULONG)(StripeNumber % Mapping->StripeCount);
        if (PopDumpBtrfsMultiplyOverflowU64(
                StripeNumber / Mapping->StripeCount,
                Mapping->StripeLength, &Physical) ||
            PopDumpBtrfsAddOverflowU64(
                Mapping->Stripes[Index].PhysicalOffset, Physical, &Physical) ||
            PopDumpBtrfsAddOverflowU64(Physical, StripeOffset, &Physical))
            return STATUS_INTEGER_OVERFLOW;
        MaxLength = Mapping->StripeLength - StripeOffset;
        if (Length > MaxLength)
            Length = MaxLength;
        Count = 1;
    }
    else if (Profile == PO_BTRFS_BLOCK_RAID10)
    {
        ULONG DataStripes;
        if (Mapping->SubStripes < 2 ||
            Mapping->StripeCount < 4 ||
            (Mapping->StripeCount % Mapping->SubStripes) != 0)
            return STATUS_NOT_SUPPORTED;
        DataStripes = Mapping->StripeCount / Mapping->SubStripes;
        if (DataStripes == 0)
            return STATUS_FILE_CORRUPT_ERROR;
        Index = (ULONG)(StripeNumber % DataStripes) * Mapping->SubStripes;
        if (PopDumpBtrfsMultiplyOverflowU64(
                StripeNumber / DataStripes,
                Mapping->StripeLength, &Physical) ||
            PopDumpBtrfsAddOverflowU64(
                Mapping->Stripes[Index].PhysicalOffset, Physical, &Physical) ||
            PopDumpBtrfsAddOverflowU64(Physical, StripeOffset, &Physical))
            return STATUS_INTEGER_OVERFLOW;
        MirrorCount = Mapping->SubStripes;
        if (MirrorCount > PO_BTRFS_HIBER_MAX_TARGETS)
            return STATUS_NOT_SUPPORTED;
        MaxLength = Mapping->StripeLength - StripeOffset;
        if (Length > MaxLength)
            Length = MaxLength;
        Count = MirrorCount;
    }
    else
    {
        if (Profile == PO_BTRFS_BLOCK_RAID1C3)
            MirrorCount = 3;
        else if (Profile == PO_BTRFS_BLOCK_RAID1C4)
            MirrorCount = 4;
        else if (Profile == PO_BTRFS_BLOCK_RAID1 ||
                 Profile == PO_BTRFS_BLOCK_DUP)
            MirrorCount = 2;
        else if (Profile != 0)
            return STATUS_NOT_SUPPORTED;
        if (Mapping->StripeCount < MirrorCount ||
            MirrorCount > PO_BTRFS_HIBER_MAX_TARGETS)
            return STATUS_FILE_CORRUPT_ERROR;
        Index = 0;
        Count = MirrorCount;
        if (Length > Mapping->ChunkLength - Delta)
            Length = Mapping->ChunkLength - Delta;
    }

    if (Length == 0)
        return STATUS_FILE_CORRUPT_ERROR;

    for (i = 0; i < Count; ++i)
    {
        ULONG StripeIndex = (Profile == PO_BTRFS_BLOCK_RAID0) ?
                            Index : Index + i;
        ULONGLONG TargetOffset;

        if (StripeIndex >= Mapping->StripeCount)
            return STATUS_FILE_CORRUPT_ERROR;

        if (Profile == PO_BTRFS_BLOCK_RAID10 && i != 0)
        {
            if (PopDumpBtrfsMultiplyOverflowU64(
                    StripeNumber / (Mapping->StripeCount /
                                    Mapping->SubStripes),
                    Mapping->StripeLength, &TargetOffset) ||
                PopDumpBtrfsAddOverflowU64(
                    Mapping->Stripes[StripeIndex].PhysicalOffset,
                    TargetOffset, &TargetOffset) ||
                PopDumpBtrfsAddOverflowU64(TargetOffset, StripeOffset,
                                           &TargetOffset))
                return STATUS_INTEGER_OVERFLOW;
        }
        else if (Profile == PO_BTRFS_BLOCK_RAID0 || Profile == PO_BTRFS_BLOCK_RAID10)
            TargetOffset = Physical;
        else
        {
            if (PopDumpBtrfsAddOverflowU64(
                    Mapping->Stripes[StripeIndex].PhysicalOffset,
                    Delta, &TargetOffset))
                return STATUS_INTEGER_OVERFLOW;
        }

        if (Mapping->Stripes[StripeIndex].Flags &
            (PO_BTRFS_HIBER_STRIPE_READONLY |
             PO_BTRFS_HIBER_STRIPE_SEEDING |
             PO_BTRFS_HIBER_STRIPE_MISSING))
            return STATUS_NOT_SUPPORTED;

        if (Mapping->Stripes[StripeIndex].DeviceId == 0 ||
            Mapping->Stripes[StripeIndex].DeviceSize == 0 ||
            PopDumpBtrfsAddOverflowU64(TargetOffset, Length, &End) ||
            End > Mapping->Stripes[StripeIndex].DeviceSize)
            return STATUS_DISK_FULL;

        Targets[i].StripeIndex = StripeIndex;
        Targets[i].PhysicalOffset = TargetOffset;
        Targets[i].Length = Length;
    }

    *TargetCount = Count;
    *MappedLength = Length;
    return STATUS_SUCCESS;
}

#endif /* _PO_DUMP_BTRFS_MAP_H_ */
