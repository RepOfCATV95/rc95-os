#include "include/veneer.h"
#include <string.h>

#define BTRFS_MAGIC_OFFSET 64u
#define BTRFS_BYTENR_OFFSET 48u
#define BTRFS_CSUM_TYPE_OFFSET 196u
#define BTRFS_SYS_ARRAY_OFFSET 0x32bu
#define BTRFS_DEV_ITEM_OFFSET 201u
#define BTRFS_DEV_TOTAL_BYTES_OFFSET (BTRFS_DEV_ITEM_OFFSET + 40u)
#define BTRFS_DEV_BYTES_USED_OFFSET (BTRFS_DEV_ITEM_OFFSET + 48u)
#define BTRFS_DEV_SECTOR_SIZE_OFFSET (BTRFS_DEV_ITEM_OFFSET + 64u)
#define BTRFS_FIRST_CHUNK_TREE_OBJECTID 256ull
#define BTRFS_ROOT_TREE_DIR_OBJECTID 6ull
#define BTRFS_SUPERBLOCK_MIRROR_1 (64ull * 1024ull * 1024ull)
#define BTRFS_SUPERBLOCK_MIRROR_2 (256ull * 1024ull * 1024ull * 1024ull)
#define BTRFS_INCOMPAT_SUPPORTED 0ull

static int AddU64(uint64_t A, uint64_t B, uint64_t *Out)
{
    if (B > UINT64_MAX - A) return 0;
    *Out = A + B;
    return 1;
}

/* Validate the variable-length system chunk array before any tree reader
 * consumes it. Each entry is disk_key + chunk_item + stripe array.
 * BTRFS_BLOCK_GROUP_SYSTEM is bit 1 (value 2), not METADATA (value 4). */
static RC95LDR_STATUS
ValidateSystemChunkArray(const uint8_t *Raw, uint32_t Length,
                         uint64_t TotalBytes, uint32_t SectorSize,
                         uint64_t DeviceBytes, uint64_t ExpectedDeviceId)
{
    const uint8_t *Cursor = Raw + BTRFS_SYS_ARRAY_OFFSET;
    const uint8_t *End = Cursor + Length;
    uint64_t PreviousLogicalEnd = 0;
    int HavePreviousChunk = 0;
    while (Cursor < End)
    {
        uint64_t Logical, ChunkLength, StripeLength, ChunkType;
        uint64_t LogicalEnd, PhysicalEnd, DeviceId, PhysicalOffset;
        uint16_t StripeCount, SubStripes;
        uint32_t ChunkSectorSize;
        size_t StripeBytes, EntryBytes;
        const uint8_t *Stripe;

        if ((size_t)(End - Cursor) < 17u + 48u)
            return Rc95LdrCorruptData;
        if (Rc95LdrReadU64(Cursor) != BTRFS_FIRST_CHUNK_TREE_OBJECTID ||
            Cursor[8] != 0xe4u) /* first-chunk-tree / BTRFS_CHUNK_ITEM_KEY */
            return Rc95LdrUnsupported;

        Logical = Rc95LdrReadU64(Cursor + 9u);
        ChunkLength = Rc95LdrReadU64(Cursor + 17u);
        StripeLength = Rc95LdrReadU64(Cursor + 33u);
        ChunkType = Rc95LdrReadU64(Cursor + 41u);
        ChunkSectorSize = Rc95LdrReadU32(Cursor + 57u);
        StripeCount = Rc95LdrReadU16(Cursor + 61u);
        SubStripes = Rc95LdrReadU16(Cursor + 63u);

        if (!ChunkLength || !StripeLength ||
            (StripeLength & (StripeLength - 1u)) ||
            StripeLength < SectorSize || (StripeLength % SectorSize) != 0 ||
            ChunkSectorSize != SectorSize ||
            (Logical % SectorSize) != 0 ||
            (ChunkLength % SectorSize) != 0 ||
            !AddU64(Logical, ChunkLength, &LogicalEnd) ||
            LogicalEnd > TotalBytes)
            return Rc95LdrCorruptData;
        if (HavePreviousChunk && Logical < PreviousLogicalEnd)
            return Rc95LdrCorruptData;
        if (ChunkType != 2u || StripeCount != 1u || SubStripes != 0u)
            return Rc95LdrUnsupported;
        if (!Rc95LdrMultiplySize((size_t)StripeCount, 32u, &StripeBytes) ||
            !Rc95LdrAddSize(17u + 48u, StripeBytes, &EntryBytes) ||
            EntryBytes > (size_t)(End - Cursor))
            return Rc95LdrCorruptData;

        Stripe = Cursor + 17u + 48u;
        DeviceId = Rc95LdrReadU64(Stripe);
        PhysicalOffset = Rc95LdrReadU64(Stripe + 8u);
        if (!DeviceId || (ExpectedDeviceId && DeviceId != ExpectedDeviceId) ||
            (PhysicalOffset % SectorSize) != 0 ||
            !AddU64(PhysicalOffset, ChunkLength, &PhysicalEnd))
            return Rc95LdrCorruptData;
        if (DeviceBytes && PhysicalEnd > DeviceBytes)
            return Rc95LdrOutOfBounds;

        PreviousLogicalEnd = LogicalEnd;
        HavePreviousChunk = 1;
        Cursor += EntryBytes;
    }
    return Cursor == End ? Rc95LdrSuccess : Rc95LdrCorruptData;
}

uint32_t
Rc95LdrVeneerCrc32c(const void *Buffer, size_t Length)
{
    const uint8_t *Bytes = (const uint8_t *)Buffer;
    uint32_t Crc = 0xffffffffu;
    size_t I;
    unsigned Bit;
    if (!Buffer && Length) return 0;
    for (I = 0; I < Length; ++I)
    {
        Crc ^= Bytes[I];
        for (Bit = 0; Bit != 8; ++Bit)
            Crc = (Crc >> 1) ^ ((Crc & 1u) ? 0x82f63b78u : 0u);
    }
    return ~Crc;
}

RC95LDR_STATUS
Rc95LdrVeneerValidateSuperblock(const uint8_t *Raw, size_t Length,
                                uint64_t ExpectedBytenr,
                                RC95LDR_BTRFS_SUPERBLOCK *Info)
{
    uint8_t ChecksumInput[RC95LDR_BTRFS_SUPERBLOCK_SIZE - 32u];
    uint32_t StoredCrc, SystemArraySize;
    uint64_t Bytenr, TotalBytes, Devices, DeviceTotalBytes, DeviceBytesUsed;
    uint32_t Sector, Node, Leaf, Stripe;
    uint16_t CsumType;
    uint64_t Magic;

    if (!Raw || !Info) return Rc95LdrInvalidParameter;
    if (Length < RC95LDR_BTRFS_SUPERBLOCK_SIZE) return Rc95LdrBufferTooSmall;
    Magic = Rc95LdrReadU64(Raw + BTRFS_MAGIC_OFFSET);
    if (Magic != RC95LDR_BTRFS_MAGIC) return Rc95LdrInvalidImage;
    Bytenr = Rc95LdrReadU64(Raw + BTRFS_BYTENR_OFFSET);
    if (Bytenr != ExpectedBytenr) return Rc95LdrCorruptData;

    CsumType = Rc95LdrReadU16(Raw + BTRFS_CSUM_TYPE_OFFSET);
    if (CsumType != RC95LDR_BTRFS_CSUM_CRC32C) return Rc95LdrUnsupported;
    StoredCrc = Rc95LdrReadU32(Raw);
    memcpy(ChecksumInput, Raw + 32u, sizeof(ChecksumInput));
    if (Rc95LdrVeneerCrc32c(ChecksumInput, sizeof(ChecksumInput)) != StoredCrc)
        return Rc95LdrCorruptData;

    TotalBytes = Rc95LdrReadU64(Raw + 112u);
    Devices = Rc95LdrReadU64(Raw + 136u);
    Sector = Rc95LdrReadU32(Raw + 144u);
    Node = Rc95LdrReadU32(Raw + 148u);
    Leaf = Rc95LdrReadU32(Raw + 152u);
    Stripe = Rc95LdrReadU32(Raw + 156u);
    SystemArraySize = Rc95LdrReadU32(Raw + 160u);
    DeviceTotalBytes = Rc95LdrReadU64(Raw + BTRFS_DEV_TOTAL_BYTES_OFFSET);
    DeviceBytesUsed = Rc95LdrReadU64(Raw + BTRFS_DEV_BYTES_USED_OFFSET);

    if (!TotalBytes || !Devices || !Sector || !Node || !Leaf || !Stripe || !SystemArraySize ||
        !DeviceTotalBytes || DeviceTotalBytes != TotalBytes ||
        DeviceBytesUsed > DeviceTotalBytes ||
        Rc95LdrReadU32(Raw + BTRFS_DEV_SECTOR_SIZE_OFFSET) != Sector ||
        (Sector & (Sector - 1u)) || (Node & (Node - 1u)) ||
        (Leaf & (Leaf - 1u)) || (Stripe & (Stripe - 1u)) ||
        Sector < 4096u || Sector > 65536u ||
        Node < Sector || Node > 65536u || TotalBytes < Node || Leaf != Node ||
        Stripe < Sector || Stripe > 65536u ||
        SystemArraySize > RC95LDR_BTRFS_MAX_SYSTEM_CHUNK_ARRAY ||
        BTRFS_SYS_ARRAY_OFFSET + SystemArraySize > RC95LDR_BTRFS_SUPERBLOCK_SIZE)
        return Rc95LdrCorruptData;

    /* Boot-time veneer deliberately accepts only single-device, non-RAID
       layouts until a complete device-set/chunk profile implementation exists. */
    if (Devices != 1u) return Rc95LdrUnsupported;
    if (Rc95LdrReadU64(Raw + 188u) & ~BTRFS_INCOMPAT_SUPPORTED)
        return Rc95LdrUnsupported;
    if (Rc95LdrReadU64(Raw + 120u) > TotalBytes ||
        Rc95LdrReadU64(Raw + 128u) != BTRFS_ROOT_TREE_DIR_OBJECTID ||
        !Rc95LdrReadU64(Raw + BTRFS_DEV_ITEM_OFFSET) ||
        !Rc95LdrReadU64(Raw + 72u) ||
        !Rc95LdrReadU64(Raw + 80u) ||
        !Rc95LdrReadU64(Raw + 88u) ||
        Rc95LdrReadU64(Raw + 80u) > TotalBytes - Node ||
        Rc95LdrReadU64(Raw + 88u) > TotalBytes - Node ||
        (Rc95LdrReadU64(Raw + 80u) % Node) != 0 ||
        (Rc95LdrReadU64(Raw + 88u) % Node) != 0 ||
        Raw[198u] > 8u || Raw[199u] > 8u)
        return Rc95LdrCorruptData;
    {
        RC95LDR_STATUS ArrayStatus = ValidateSystemChunkArray(
            Raw, SystemArraySize, TotalBytes, Sector, DeviceTotalBytes,
            Rc95LdrReadU64(Raw + 201u));
        if (ArrayStatus != Rc95LdrSuccess)
            return ArrayStatus;
    }

    memset(Info, 0, sizeof(*Info));
    Info->Bytenr = Bytenr;
    Info->Generation = Rc95LdrReadU64(Raw + 72u);
    Info->Root = Rc95LdrReadU64(Raw + 80u);
    Info->ChunkRoot = Rc95LdrReadU64(Raw + 88u);
    Info->TotalBytes = TotalBytes;
    Info->BytesUsed = Rc95LdrReadU64(Raw + 120u);
    Info->NumDevices = Devices;
    Info->SectorSize = Sector;
    Info->NodeSize = Node;
    Info->LeafSize = Leaf;
    Info->StripeSize = Stripe;
    Info->SystemChunkArraySize = SystemArraySize;
    Info->IncompatFlags = Rc95LdrReadU64(Raw + 188u);
    Info->ChecksumType = CsumType;
    Info->RootLevel = Raw[198u];
    Info->ChunkRootLevel = Raw[199u];
    Info->DeviceId = Rc95LdrReadU64(Raw + BTRFS_DEV_ITEM_OFFSET);
    Info->DeviceBytes = DeviceTotalBytes;
    memcpy(Info->FsId, Raw + 32u, sizeof(Info->FsId));
    memcpy(Info->SystemChunkArray, Raw + BTRFS_SYS_ARRAY_OFFSET,
           SystemArraySize);
    return Rc95LdrSuccess;
}

RC95LDR_STATUS
Rc95LdrVeneerDiscoverBtrfs(const RC95LDR_VENEER_CONTEXT *Context,
                           uint8_t *Scratch, size_t ScratchLength,
                           RC95LDR_BTRFS_SUPERBLOCK *Info,
                           uint64_t *SelectedOffset)
{
    static const uint64_t Mirrors[] = {
        RC95LDR_BTRFS_SUPERBLOCK_OFFSET,
        BTRFS_SUPERBLOCK_MIRROR_1,
        BTRFS_SUPERBLOCK_MIRROR_2
    };
    size_t I;
    RC95LDR_STATUS LastStatus = Rc95LdrNotFound;
    uint64_t PartitionBytes, Lba;
    uint32_t Blocks;
    RC95LDR_STATUS Status;
    RC95LDR_BTRFS_SUPERBLOCK CandidateInfo;

    if (Info != NULL)
        memset(Info, 0, sizeof(*Info));
    if (SelectedOffset != NULL)
        *SelectedOffset = 0;
    if (!Context || !Context->Device || !Context->Device->Read ||
        !Scratch || !Info || !SelectedOffset)
        return Rc95LdrInvalidParameter;
    if (ScratchLength < RC95LDR_BTRFS_SUPERBLOCK_SIZE)
        return Rc95LdrBufferTooSmall;
    if (Context->Device->BlockSize < 512u ||
        (Context->Device->BlockSize & (Context->Device->BlockSize - 1u)) ||
        RC95LDR_BTRFS_SUPERBLOCK_SIZE % Context->Device->BlockSize)
        return Rc95LdrInvalidParameter;
    if (!Context->Device->BlockCount || !Context->PartitionBlockCount ||
        Context->PartitionStartLba >= Context->Device->BlockCount ||
        Context->PartitionBlockCount >
            Context->Device->BlockCount - Context->PartitionStartLba)
        return Rc95LdrOutOfBounds;
    if (Context->PartitionBlockCount >
        UINT64_MAX / Context->Device->BlockSize)
        return Rc95LdrIntegerOverflow;
    PartitionBytes = Context->PartitionBlockCount * Context->Device->BlockSize;

    for (I = 0; I < sizeof(Mirrors) / sizeof(Mirrors[0]); ++I)
    {
        if (Mirrors[I] > PartitionBytes ||
            RC95LDR_BTRFS_SUPERBLOCK_SIZE > PartitionBytes - Mirrors[I])
            continue;
        if (!AddU64(Context->PartitionStartLba,
                    Mirrors[I] / Context->Device->BlockSize, &Lba))
            return Rc95LdrIntegerOverflow;
        Blocks = RC95LDR_BTRFS_SUPERBLOCK_SIZE / Context->Device->BlockSize;
        Status = Rc95LdrReadBlocks(Context->Device, Lba, Blocks,
                                   Scratch, ScratchLength);
        if (Status != Rc95LdrSuccess) { LastStatus = Status; continue; }
        Status = Rc95LdrVeneerValidateSuperblock(
            Scratch, RC95LDR_BTRFS_SUPERBLOCK_SIZE, Mirrors[I], &CandidateInfo);
        if (Status == Rc95LdrSuccess)
        {
            /* With a single-device profile, total_bytes must fit inside the
               selected partition. Do not publish a candidate until all of
               its partition-relative ranges have passed validation. */
            if (CandidateInfo.TotalBytes > PartitionBytes ||
                Rc95LdrReadU64(Scratch + BTRFS_DEV_TOTAL_BYTES_OFFSET) > PartitionBytes)
            {
                LastStatus = Rc95LdrCorruptData;
                continue;
            }
            Status = ValidateSystemChunkArray(Scratch,
                CandidateInfo.SystemChunkArraySize, CandidateInfo.TotalBytes,
                CandidateInfo.SectorSize, PartitionBytes,
                Rc95LdrReadU64(Scratch + BTRFS_DEV_ITEM_OFFSET));
            if (Status != Rc95LdrSuccess)
            {
                LastStatus = Status;
                continue;
            }
            *Info = CandidateInfo;
            *SelectedOffset = Mirrors[I];
            return Rc95LdrSuccess;
        }
        LastStatus = Status;
    }
    return LastStatus;
}
