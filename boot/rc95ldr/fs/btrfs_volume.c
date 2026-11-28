#include "rc95ldr_btrfs.h"
#include <string.h>

static RC95LDR_STATUS
BtrfsPartitionRead(void *Context,
                   uint64_t Lba,
                   uint32_t BlockCount,
                   void *Buffer,
                   size_t BufferLength)
{
    RC95LDR_BTRFS_VOLUME *Volume = (RC95LDR_BTRFS_VOLUME *)Context;
    uint64_t PhysicalLba;

    if (!Volume || !Volume->Mounted || !Volume->DeviceStorage.Read ||
        (!Buffer && BufferLength))
        return Rc95LdrInvalidParameter;

    if (Lba > Volume->PartitionBlockCount ||
        (uint64_t)BlockCount > Volume->PartitionBlockCount - Lba)
        return Rc95LdrOutOfBounds;

    if (Volume->PartitionStartLba > UINT64_MAX - Lba)
        return Rc95LdrIntegerOverflow;

    PhysicalLba = Volume->PartitionStartLba + Lba;
    return Rc95LdrReadBlocks(&Volume->DeviceStorage, PhysicalLba,
                             BlockCount, Buffer, BufferLength);
}

RC95LDR_STATUS
Rc95LdrBtrfsMountPartition(RC95LDR_BTRFS_VOLUME *Volume,
                           const RC95LDR_BLOCK_DEVICE *Device,
                           uint64_t PartitionStartLba,
                           uint64_t PartitionBlockCount,
                           uint8_t *Scratch,
                           size_t ScratchLength)
{
    RC95LDR_VENEER_CONTEXT Context;
    RC95LDR_BTRFS_SUPERBLOCK Superblock;
    uint64_t SelectedOffset, PartitionBytes, VolumeBlocks;
    RC95LDR_STATUS Status;

    if (!Volume)
        return Rc95LdrInvalidParameter;

    /* A failed remount must never leave an earlier volume binding live. */
    memset(Volume, 0, sizeof(*Volume));
    if (!Device || !Device->Read || !Scratch)
        return Rc95LdrInvalidParameter;

    memset(&Superblock, 0, sizeof(Superblock));
    SelectedOffset = 0;

    if (!Device->BlockSize ||
        (Device->BlockSize & (Device->BlockSize - 1u)) ||
        !PartitionBlockCount ||
        PartitionStartLba >= Device->BlockCount ||
        PartitionBlockCount > Device->BlockCount - PartitionStartLba)
        return Rc95LdrInvalidParameter;

    if (PartitionBlockCount > UINT64_MAX / Device->BlockSize)
        return Rc95LdrIntegerOverflow;
    PartitionBytes = PartitionBlockCount * Device->BlockSize;

    /* Snapshot the callback table so later edits to the caller's descriptor
       cannot redirect a mounted volume. The platform context remains owned
       by the caller and must outlive this mount. */
    Volume->DeviceStorage = *Device;
    Volume->PartitionStartLba = PartitionStartLba;

    Context.Device = &Volume->DeviceStorage;
    Context.PartitionStartLba = PartitionStartLba;
    Context.PartitionBlockCount = PartitionBlockCount;

    Status = Rc95LdrVeneerDiscoverBtrfs(&Context, Scratch, ScratchLength,
                                       &Superblock, &SelectedOffset);
    if (Status != Rc95LdrSuccess)
    {
        memset(Volume, 0, sizeof(*Volume));
        return Status;
    }

    if (!Superblock.TotalBytes ||
        Superblock.TotalBytes > PartitionBytes ||
        (Superblock.TotalBytes % Device->BlockSize) != 0)
    {
        memset(Volume, 0, sizeof(*Volume));
        return Rc95LdrCorruptData;
    }

    VolumeBlocks = Superblock.TotalBytes / Device->BlockSize;
    if (!VolumeBlocks || VolumeBlocks > PartitionBlockCount)
    {
        memset(Volume, 0, sizeof(*Volume));
        return Rc95LdrCorruptData;
    }

    Volume->PartitionBlockCount = VolumeBlocks;
    Volume->SelectedSuperblockOffset = SelectedOffset;
    Volume->Superblock = Superblock;
    Volume->PartitionDevice.DeviceContext = Volume;
    Volume->PartitionDevice.BlockCount = VolumeBlocks;
    Volume->PartitionDevice.BlockSize = Device->BlockSize;
    Volume->PartitionDevice.Read = BtrfsPartitionRead;
    /* Btrfs boot access is read-only even if the platform device is writable. */
    Volume->PartitionDevice.Write = NULL;
    Volume->Mounted = 1;
    return Rc95LdrSuccess;
}

void
Rc95LdrBtrfsUnmount(RC95LDR_BTRFS_VOLUME *Volume)
{
    if (Volume)
        memset(Volume, 0, sizeof(*Volume));
}

const RC95LDR_BTRFS_SUPERBLOCK *
Rc95LdrBtrfsGetSuperblock(const RC95LDR_BTRFS_VOLUME *Volume)
{
    return Volume && Volume->Mounted ? &Volume->Superblock : NULL;
}

RC95LDR_STATUS
Rc95LdrBtrfsBindVolume(RC95LDR_BTRFS_VOLUME *Btrfs,
                       RC95LDR_VOLUME *Volume)
{
    if (!Btrfs || !Volume || !Btrfs->Mounted ||
        !Btrfs->PartitionDevice.Read || !Btrfs->PartitionDevice.BlockCount)
        return Rc95LdrInvalidParameter;

    Volume->BlockDevice = &Btrfs->PartitionDevice;
    /* Plain binding exposes block I/O only; use BindVolumeWithFileSystem
       after loading the root tree and initializing its caller-owned context. */
    Volume->FileSystem = NULL;
    return Rc95LdrSuccess;
}


RC95LDR_STATUS
Rc95LdrBtrfsBindVolumeWithFileSystem(RC95LDR_BTRFS_VOLUME *Btrfs,
                                    RC95LDR_VOLUME *Volume,
                                    const RC95LDR_FILESYSTEM *FileSystem)
{
    RC95LDR_STATUS S;
    if (!FileSystem || !FileSystem->OpenRead || !FileSystem->ReadAt ||
        !FileSystem->Close)
        return Rc95LdrInvalidParameter;
    S = Rc95LdrBtrfsBindVolume(Btrfs, Volume);
    if (S != Rc95LdrSuccess) return S;
    if (!Btrfs->RootTreeLoaded)
    {
        Volume->FileSystem = NULL;
        return Rc95LdrInvalidParameter;
    }
    Volume->FileSystem = FileSystem;
    return Rc95LdrSuccess;
}


#define BTRFS_FIRST_CHUNK_TREE_OBJECTID 256ull
#define BTRFS_CHUNK_TREE_OBJECTID 3ull
#define BTRFS_CHUNK_ITEM_KEY 0xe4u
#define BTRFS_TREE_HEADER_SIZE 101u
#define BTRFS_LEAF_ITEM_SIZE 25u
#define BTRFS_NODE_PTR_SIZE 33u
#define BTRFS_CHUNK_ITEM_FIXED_SIZE 48u
#define BTRFS_STRIPE_SIZE 32u
#define BTRFS_BLOCK_GROUP_MASK 7ull

static int
BtrfsAddU64(uint64_t A, uint64_t B, uint64_t *Result)
{
    if (B > UINT64_MAX - A)
        return 0;
    *Result = A + B;
    return 1;
}

static RC95LDR_STATUS
BtrfsInsertChunk(RC95LDR_BTRFS_VOLUME *Volume,
                 const RC95LDR_BTRFS_CHUNK *Chunk)
{
    uint64_t End, ExistingEnd, PhysicalEnd;
    uint32_t I, InsertAt;

    if (!Chunk->Length || !BtrfsAddU64(Chunk->Logical, Chunk->Length, &End) ||
        End > Volume->Superblock.TotalBytes ||
        !BtrfsAddU64(Chunk->Physical, Chunk->Length, &PhysicalEnd) ||
        PhysicalEnd > (uint64_t)Volume->PartitionDevice.BlockCount *
                      Volume->PartitionDevice.BlockSize)
        return Rc95LdrOutOfBounds;

    InsertAt = 0;
    while (InsertAt < Volume->ChunkCount &&
           Volume->Chunks[InsertAt].Logical < Chunk->Logical)
        ++InsertAt;

    if (InsertAt < Volume->ChunkCount &&
        Volume->Chunks[InsertAt].Logical == Chunk->Logical)
    {
        const RC95LDR_BTRFS_CHUNK *Old = &Volume->Chunks[InsertAt];
        return Old->Length == Chunk->Length &&
               Old->Physical == Chunk->Physical &&
               Old->DeviceId == Chunk->DeviceId &&
               Old->Type == Chunk->Type ? Rc95LdrSuccess : Rc95LdrCorruptData;
    }

    if (InsertAt && BtrfsAddU64(Volume->Chunks[InsertAt - 1].Logical,
                               Volume->Chunks[InsertAt - 1].Length,
                               &ExistingEnd) &&
        ExistingEnd > Chunk->Logical)
        return Rc95LdrCorruptData;
    if (InsertAt < Volume->ChunkCount &&
        BtrfsAddU64(Chunk->Logical, Chunk->Length, &ExistingEnd) &&
        ExistingEnd > Volume->Chunks[InsertAt].Logical)
        return Rc95LdrCorruptData;
    if (Volume->ChunkCount >= RC95LDR_BTRFS_MAX_CHUNKS)
        return Rc95LdrBufferTooSmall;

    for (I = Volume->ChunkCount; I > InsertAt; --I)
        Volume->Chunks[I] = Volume->Chunks[I - 1];
    Volume->Chunks[InsertAt] = *Chunk;
    ++Volume->ChunkCount;
    return Rc95LdrSuccess;
}

static RC95LDR_STATUS
BtrfsLoadSystemChunks(RC95LDR_BTRFS_VOLUME *Volume)
{
    const uint8_t *Cursor = Volume->Superblock.SystemChunkArray;
    const uint8_t *End = Cursor + Volume->Superblock.SystemChunkArraySize;
    while (Cursor < End)
    {
        RC95LDR_BTRFS_CHUNK Chunk;
        uint16_t Stripes;
        size_t EntrySize;
        const uint8_t *Stripe;

        if ((size_t)(End - Cursor) < 17u + BTRFS_CHUNK_ITEM_FIXED_SIZE)
            return Rc95LdrCorruptData;
        if (Rc95LdrReadU64(Cursor) != BTRFS_FIRST_CHUNK_TREE_OBJECTID ||
            Cursor[8] != BTRFS_CHUNK_ITEM_KEY)
            return Rc95LdrUnsupported;

        memset(&Chunk, 0, sizeof(Chunk));
        Chunk.Logical = Rc95LdrReadU64(Cursor + 9u);
        Chunk.Length = Rc95LdrReadU64(Cursor + 17u);
        Chunk.Type = Rc95LdrReadU64(Cursor + 17u + 24u);
        Stripes = Rc95LdrReadU16(Cursor + 17u + 44u);
        if (Rc95LdrReadU64(Cursor + 17u + 8u) != 2u ||
            !Stripes || Stripes != 1u ||
            Rc95LdrReadU16(Cursor + 17u + 46u) != 0u ||
            (Chunk.Type & BTRFS_BLOCK_GROUP_MASK) == 0 ||
            (Chunk.Type & ~BTRFS_BLOCK_GROUP_MASK) != 0)
            return Rc95LdrUnsupported;
        if (!Rc95LdrAddSize(17u + BTRFS_CHUNK_ITEM_FIXED_SIZE,
                            BTRFS_STRIPE_SIZE, &EntrySize) ||
            EntrySize > (size_t)(End - Cursor))
            return Rc95LdrCorruptData;

        Stripe = Cursor + 17u + BTRFS_CHUNK_ITEM_FIXED_SIZE;
        Chunk.DeviceId = Rc95LdrReadU64(Stripe);
        Chunk.Physical = Rc95LdrReadU64(Stripe + 8u);
        if (Chunk.DeviceId != Volume->Superblock.DeviceId)
            return Rc95LdrUnsupported;
        if ((Chunk.Logical % Volume->Superblock.SectorSize) ||
            (Chunk.Physical % Volume->Superblock.SectorSize) ||
            (Chunk.Length % Volume->Superblock.SectorSize) ||
            Rc95LdrReadU32(Cursor + 17u + 40u) !=
                Volume->Superblock.SectorSize)
            return Rc95LdrCorruptData;

        {
            RC95LDR_STATUS Status = BtrfsInsertChunk(Volume, &Chunk);
            if (Status != Rc95LdrSuccess)
                return Status;
        }
        Cursor += EntrySize;
    }
    return Cursor == End ? Rc95LdrSuccess : Rc95LdrCorruptData;
}

static RC95LDR_STATUS
BtrfsMapLogicalInternal(const RC95LDR_BTRFS_VOLUME *Volume,
                        uint64_t Logical, uint64_t Length,
                        uint64_t *Physical)
{
    uint32_t I;
    uint64_t End, ChunkEnd;
    if (!Volume || !Volume->Mounted || !Physical || !Length)
        return Rc95LdrInvalidParameter;
    if (!BtrfsAddU64(Logical, Length, &End) ||
        End > Volume->Superblock.TotalBytes)
        return Rc95LdrOutOfBounds;
    for (I = 0; I < Volume->ChunkCount; ++I)
    {
        const RC95LDR_BTRFS_CHUNK *Chunk = &Volume->Chunks[I];
        if (!BtrfsAddU64(Chunk->Logical, Chunk->Length, &ChunkEnd))
            return Rc95LdrCorruptData;
        if (Logical >= Chunk->Logical && End <= ChunkEnd)
        {
            if (!BtrfsAddU64(Chunk->Physical,
                             Logical - Chunk->Logical, Physical))
                return Rc95LdrIntegerOverflow;
            return Rc95LdrSuccess;
        }
    }
    return Rc95LdrNotFound;
}

RC95LDR_STATUS
Rc95LdrBtrfsMapLogical(const RC95LDR_BTRFS_VOLUME *Volume,
                       uint64_t Logical, uint64_t Length,
                       uint64_t *Physical)
{
    if (!Volume || !Volume->ChunkTreeLoaded)
        return Rc95LdrInvalidParameter;
    return BtrfsMapLogicalInternal(Volume, Logical, Length, Physical);
}

static RC95LDR_STATUS
BtrfsReadLogicalNode(RC95LDR_BTRFS_VOLUME *Volume,
                     uint64_t Logical, uint8_t *Buffer)
{
    uint64_t Physical;
    uint32_t BlockSize = Volume->PartitionDevice.BlockSize;
    uint32_t NodeSize = Volume->Superblock.NodeSize;
    RC95LDR_STATUS Status;

    if (!BlockSize || NodeSize % BlockSize ||
        (Logical % BlockSize) != 0)
        return Rc95LdrUnsupported;
    Status = BtrfsMapLogicalInternal(Volume, Logical, NodeSize, &Physical);
    if (Status != Rc95LdrSuccess)
        return Status;
    if ((Physical % BlockSize) != 0)
        return Rc95LdrCorruptData;
    return Rc95LdrReadBlocks(&Volume->PartitionDevice,
                             Physical / BlockSize,
                             NodeSize / BlockSize,
                             Buffer, NodeSize);
}

static RC95LDR_STATUS
BtrfsParseChunkItem(RC95LDR_BTRFS_VOLUME *Volume,
                    const uint8_t *Item, uint32_t ItemSize,
                    uint64_t Logical)
{
    RC95LDR_BTRFS_CHUNK Chunk;
    uint16_t Stripes;
    const uint8_t *Stripe;
    uint64_t PhysicalEnd;

    if (ItemSize < BTRFS_CHUNK_ITEM_FIXED_SIZE + BTRFS_STRIPE_SIZE)
        return Rc95LdrCorruptData;
    memset(&Chunk, 0, sizeof(Chunk));
    Chunk.Logical = Logical;
    Chunk.Length = Rc95LdrReadU64(Item);
    Chunk.Type = Rc95LdrReadU64(Item + 24u);
    Stripes = Rc95LdrReadU16(Item + 44u);
    if (Rc95LdrReadU64(Item + 8u) != 2u ||
        Stripes != 1u || Rc95LdrReadU16(Item + 46u) != 0u ||
        ItemSize != BTRFS_CHUNK_ITEM_FIXED_SIZE + BTRFS_STRIPE_SIZE ||
        (Chunk.Type & BTRFS_BLOCK_GROUP_MASK) == 0 ||
        (Chunk.Type & ~BTRFS_BLOCK_GROUP_MASK) != 0)
        return Rc95LdrUnsupported;
    {
        uint64_t StripeLength = Rc95LdrReadU64(Item + 16u);
        if (!StripeLength || (StripeLength & (StripeLength - 1u)) ||
            StripeLength < Volume->Superblock.SectorSize ||
            (StripeLength % Volume->Superblock.SectorSize) != 0 ||
            Rc95LdrReadU32(Item + 40u) != Volume->Superblock.SectorSize ||
            (Chunk.Length % Volume->Superblock.SectorSize) != 0 ||
            (Logical % Volume->Superblock.SectorSize) != 0)
            return Rc95LdrCorruptData;
    }

    Stripe = Item + BTRFS_CHUNK_ITEM_FIXED_SIZE;
    Chunk.DeviceId = Rc95LdrReadU64(Stripe);
    Chunk.Physical = Rc95LdrReadU64(Stripe + 8u);
    if (Chunk.DeviceId != Volume->Superblock.DeviceId)
        return Rc95LdrUnsupported;
    if ((Chunk.Physical % Volume->Superblock.SectorSize) != 0 ||
        !BtrfsAddU64(Chunk.Physical, Chunk.Length, &PhysicalEnd) ||
        PhysicalEnd > Volume->Superblock.DeviceBytes)
        return Rc95LdrOutOfBounds;
    return BtrfsInsertChunk(Volume, &Chunk);
}

RC95LDR_STATUS
Rc95LdrBtrfsLoadChunkTree(RC95LDR_BTRFS_VOLUME *Volume,
                          uint8_t *Scratch, size_t ScratchLength)
{
    uint64_t Pending[RC95LDR_BTRFS_MAX_CHUNKS];
    uint64_t PendingGeneration[RC95LDR_BTRFS_MAX_CHUNKS];
    uint8_t PendingLevel[RC95LDR_BTRFS_MAX_CHUNKS];
    uint64_t Visited[RC95LDR_BTRFS_MAX_CHUNKS];
    uint32_t PendingCount = 0, VisitedCount = 0;
    uint32_t NodeSize, I;
    RC95LDR_STATUS Status;

    if (!Volume || !Volume->Mounted || !Scratch)
        return Rc95LdrInvalidParameter;
    NodeSize = Volume->Superblock.NodeSize;
    if (ScratchLength < NodeSize)
        return Rc95LdrBufferTooSmall;

    /* Rebuild from the validated bootstrap SYSTEM chunks on every attempt. */
    memset(Volume->Chunks, 0, sizeof(Volume->Chunks));
    Volume->ChunkCount = 0;
    Volume->ChunkTreeLoaded = 0;
    Status = BtrfsLoadSystemChunks(Volume);
    if (Status != Rc95LdrSuccess)
        return Status;

    Pending[PendingCount] = Volume->Superblock.ChunkRoot;
    PendingGeneration[PendingCount] = 0;
    PendingLevel[PendingCount] = Volume->Superblock.ChunkRootLevel;
    ++PendingCount;
    while (PendingCount)
    {
        uint64_t Bytenr;
        uint64_t ExpectedGeneration;
        uint8_t ExpectedLevel;
        uint32_t ItemCount;
        uint8_t Level;

        --PendingCount;
        Bytenr = Pending[PendingCount];
        ExpectedGeneration = PendingGeneration[PendingCount];
        ExpectedLevel = PendingLevel[PendingCount];

        for (I = 0; I < VisitedCount; ++I)
            if (Visited[I] == Bytenr)
                return Rc95LdrCorruptData;
        if (VisitedCount >= RC95LDR_BTRFS_MAX_CHUNKS)
            return Rc95LdrBufferTooSmall;
        Visited[VisitedCount++] = Bytenr;

        Status = BtrfsReadLogicalNode(Volume, Bytenr, Scratch);
        if (Status != Rc95LdrSuccess)
            return Status;
        if (Rc95LdrReadU32(Scratch) !=
            Rc95LdrVeneerCrc32c(Scratch + 32u, NodeSize - 32u) ||
            memcmp(Scratch + 32u, Volume->Superblock.FsId, 16u) != 0 ||
            Rc95LdrReadU64(Scratch + 48u) != Bytenr ||
            Rc95LdrReadU64(Scratch + 88u) != BTRFS_CHUNK_TREE_OBJECTID ||
            Rc95LdrReadU64(Scratch + 80u) > Volume->Superblock.Generation)
            return Rc95LdrCorruptData;

        ItemCount = Rc95LdrReadU32(Scratch + 96u);
        Level = Scratch[100u];
        if (Level > 8u || Level != ExpectedLevel ||
            (ExpectedGeneration && Rc95LdrReadU64(Scratch + 80u) != ExpectedGeneration) ||
            ItemCount > (NodeSize - BTRFS_TREE_HEADER_SIZE) /
            (Level ? BTRFS_NODE_PTR_SIZE : BTRFS_LEAF_ITEM_SIZE))
            return Rc95LdrCorruptData;
        if (Bytenr == Volume->Superblock.ChunkRoot &&
            Level != Volume->Superblock.ChunkRootLevel)
            return Rc95LdrCorruptData;

        if (Level == 0u)
        {
            uint32_t PreviousOffset = NodeSize;
            uint64_t PreviousObject = 0, PreviousOffsetKey = 0;
            uint8_t PreviousType = 0;
            int HavePrevious = 0;
            for (I = 0; I < ItemCount; ++I)
            {
                const uint8_t *Entry = Scratch + BTRFS_TREE_HEADER_SIZE +
                                       I * BTRFS_LEAF_ITEM_SIZE;
                uint64_t ObjectId = Rc95LdrReadU64(Entry);
                uint8_t KeyType = Entry[8u];
                uint64_t KeyOffset = Rc95LdrReadU64(Entry + 9u);
                uint32_t DataOffset = Rc95LdrReadU32(Entry + 17u);
                uint32_t DataSize = Rc95LdrReadU32(Entry + 21u);
                uint64_t DataEnd;

                if (HavePrevious &&
                    (ObjectId < PreviousObject ||
                     (ObjectId == PreviousObject && KeyType < PreviousType) ||
                     (ObjectId == PreviousObject && KeyType == PreviousType &&
                      KeyOffset <= PreviousOffsetKey)))
                    return Rc95LdrCorruptData;
                PreviousObject = ObjectId;
                PreviousType = KeyType;
                PreviousOffsetKey = KeyOffset;
                HavePrevious = 1;

                if (DataOffset < BTRFS_TREE_HEADER_SIZE +
                                 ItemCount * BTRFS_LEAF_ITEM_SIZE ||
                    !BtrfsAddU64(DataOffset, DataSize, &DataEnd) ||
                    DataEnd > NodeSize || DataEnd > PreviousOffset)
                    return Rc95LdrCorruptData;
                PreviousOffset = DataOffset;

                if (ObjectId == BTRFS_FIRST_CHUNK_TREE_OBJECTID &&
                    KeyType == BTRFS_CHUNK_ITEM_KEY)
                {
                    Status = BtrfsParseChunkItem(Volume, Scratch + DataOffset,
                                                 DataSize, KeyOffset);
                    if (Status != Rc95LdrSuccess)
                        return Status;
                }
            }
        }
        else
        {
            uint64_t PreviousObject = 0, PreviousOffsetKey = 0;
            uint8_t PreviousType = 0;
            int HavePrevious = 0;
            for (I = 0; I < ItemCount; ++I)
            {
                const uint8_t *Entry = Scratch + BTRFS_TREE_HEADER_SIZE +
                                       I * BTRFS_NODE_PTR_SIZE;
                uint64_t ObjectId = Rc95LdrReadU64(Entry);
                uint8_t KeyType = Entry[8u];
                uint64_t KeyOffset = Rc95LdrReadU64(Entry + 9u);
                uint64_t Child = Rc95LdrReadU64(Entry + 17u);
                uint64_t ChildGeneration = Rc95LdrReadU64(Entry + 25u);

                if (HavePrevious &&
                    (ObjectId < PreviousObject ||
                     (ObjectId == PreviousObject && KeyType < PreviousType) ||
                     (ObjectId == PreviousObject && KeyType == PreviousType &&
                      KeyOffset <= PreviousOffsetKey)))
                    return Rc95LdrCorruptData;
                PreviousObject = ObjectId;
                PreviousType = KeyType;
                PreviousOffsetKey = KeyOffset;
                HavePrevious = 1;

                if (!Child || (Child % Volume->Superblock.NodeSize) != 0 ||
                    !ChildGeneration || ChildGeneration > Volume->Superblock.Generation ||
                    PendingCount >= RC95LDR_BTRFS_MAX_CHUNKS)
                    return Rc95LdrCorruptData;
                Pending[PendingCount] = Child;
                PendingGeneration[PendingCount] = ChildGeneration;
                PendingLevel[PendingCount] = (uint8_t)(Level - 1u);
                ++PendingCount;
            }
        }
    }

    if (!Volume->ChunkCount)
        return Rc95LdrCorruptData;
    Volume->ChunkTreeLoaded = 1;
    return Rc95LdrSuccess;
}
