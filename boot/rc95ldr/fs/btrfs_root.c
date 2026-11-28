#include "rc95ldr_btrfs.h"
#include <string.h>

#define BTRFS_ROOT_TREE_OBJECTID 1ull
#define BTRFS_FS_TREE_OBJECTID 5ull
#define BTRFS_ROOT_ITEM_KEY 132u
#define BTRFS_TREE_HEADER_SIZE 101u
#define BTRFS_LEAF_ITEM_SIZE 25u
#define BTRFS_NODE_PTR_SIZE 33u
#define BTRFS_ROOT_ITEM_MIN_SIZE 239u
#define BTRFS_MAX_TREE_NODES 128u

static int
BtrfsRootAddU64(uint64_t A, uint64_t B, uint64_t *R)
{
    if (B > UINT64_MAX - A) return 0;
    *R = A + B;
    return 1;
}

static RC95LDR_STATUS
BtrfsRootReadNode(RC95LDR_BTRFS_VOLUME *V, uint64_t Logical, uint8_t *Buffer)
{
    uint64_t Physical;
    uint32_t BlockSize = V->PartitionDevice.BlockSize;
    uint32_t NodeSize  = V->Superblock.NodeSize;
    uint32_t I;

    for (I = 0; I < V->ChunkCount; ++I)
    {
        const RC95LDR_BTRFS_CHUNK *C = &V->Chunks[I];
        uint64_t End;
        if (!BtrfsRootAddU64(C->Logical, C->Length, &End))
            return Rc95LdrCorruptData;
        if (Logical >= C->Logical && Logical <= End &&
            NodeSize <= End - Logical)
        {
            if (!BtrfsRootAddU64(C->Physical, Logical - C->Logical, &Physical))
                return Rc95LdrIntegerOverflow;
            if (!BlockSize || NodeSize % BlockSize || Physical % BlockSize)
                return Rc95LdrUnsupported;
            return Rc95LdrReadBlocks(&V->PartitionDevice,
                Physical / BlockSize, NodeSize / BlockSize, Buffer, NodeSize);
        }
    }
    return Rc95LdrNotFound;
}

RC95LDR_STATUS
Rc95LdrBtrfsLoadRootTree(RC95LDR_BTRFS_VOLUME *V,
                         uint8_t *Scratch, size_t ScratchLength)
{
    uint64_t Pending[BTRFS_MAX_TREE_NODES];
    uint64_t PendingGen[BTRFS_MAX_TREE_NODES];
    uint8_t PendingLevel[BTRFS_MAX_TREE_NODES];
    uint64_t Visited[BTRFS_MAX_TREE_NODES];
    uint32_t PendingCount = 0, VisitedCount = 0, I, NodeSize;
    uint64_t FoundRoot = 0, FoundGeneration = 0;
    uint8_t FoundLevel = 0;
    int Found = 0;

    if (!V || !V->Mounted || !V->ChunkTreeLoaded || !Scratch)
        return Rc95LdrInvalidParameter;
    NodeSize = V->Superblock.NodeSize;
    if (ScratchLength < NodeSize) return Rc95LdrBufferTooSmall;
    V->RootTreeLoaded = 0;
    V->FsTreeRoot = 0;
    V->FsTreeGeneration = 0;
    V->FsTreeLevel = 0;

    Pending[PendingCount] = V->Superblock.Root;
    PendingGen[PendingCount] = 0;
    PendingLevel[PendingCount] = V->Superblock.RootLevel;
    ++PendingCount;

    while (PendingCount)
    {
        uint64_t Bytenr, ExpectedGen;
        uint8_t ExpectedLevel, Level;
        uint32_t Count;
        --PendingCount;
        Bytenr = Pending[PendingCount];
        ExpectedGen = PendingGen[PendingCount];
        ExpectedLevel = PendingLevel[PendingCount];

        for (I = 0; I < VisitedCount; ++I)
            if (Visited[I] == Bytenr) return Rc95LdrCorruptData;
        if (VisitedCount >= BTRFS_MAX_TREE_NODES) return Rc95LdrBufferTooSmall;
        Visited[VisitedCount++] = Bytenr;

        {
            RC95LDR_STATUS S = BtrfsRootReadNode(V, Bytenr, Scratch);
            if (S != Rc95LdrSuccess) return S;
        }
        if (Rc95LdrReadU32(Scratch) !=
                Rc95LdrVeneerCrc32c(Scratch + 32u, NodeSize - 32u) ||
            memcmp(Scratch + 32u, V->Superblock.FsId, 16u) != 0 ||
            Rc95LdrReadU64(Scratch + 48u) != Bytenr ||
            Rc95LdrReadU64(Scratch + 88u) != BTRFS_ROOT_TREE_OBJECTID ||
            Rc95LdrReadU64(Scratch + 80u) > V->Superblock.Generation)
            return Rc95LdrCorruptData;

        Count = Rc95LdrReadU32(Scratch + 96u);
        Level = Scratch[100u];
        if (Level > 8u || Level != ExpectedLevel ||
            (ExpectedGen && Rc95LdrReadU64(Scratch + 80u) != ExpectedGen) ||
            Count > (NodeSize - BTRFS_TREE_HEADER_SIZE) /
                    (Level ? BTRFS_NODE_PTR_SIZE : BTRFS_LEAF_ITEM_SIZE))
            return Rc95LdrCorruptData;

        if (!Level)
        {
            uint32_t PreviousData = NodeSize;
            uint64_t PrevObject = 0, PrevOffset = 0;
            uint8_t PrevType = 0;
            int HavePrev = 0;
            for (I = 0; I < Count; ++I)
            {
                const uint8_t *E = Scratch + BTRFS_TREE_HEADER_SIZE +
                                   I * BTRFS_LEAF_ITEM_SIZE;
                uint64_t Object = Rc95LdrReadU64(E);
                uint8_t Type = E[8];
                uint64_t KeyOffset = Rc95LdrReadU64(E + 9);
                uint32_t DataOffset = Rc95LdrReadU32(E + 17);
                uint32_t DataSize = Rc95LdrReadU32(E + 21);
                uint64_t DataEnd;
                if (HavePrev && (Object < PrevObject ||
                    (Object == PrevObject && Type < PrevType) ||
                    (Object == PrevObject && Type == PrevType &&
                     KeyOffset <= PrevOffset)))
                    return Rc95LdrCorruptData;
                HavePrev = 1; PrevObject = Object; PrevType = Type;
                PrevOffset = KeyOffset;
                if (DataOffset < BTRFS_TREE_HEADER_SIZE +
                                  Count * BTRFS_LEAF_ITEM_SIZE ||
                    !BtrfsRootAddU64(DataOffset, DataSize, &DataEnd) ||
                    DataEnd > NodeSize || DataEnd > PreviousData)
                    return Rc95LdrCorruptData;
                PreviousData = DataOffset;

                if (Object == BTRFS_FS_TREE_OBJECTID &&
                    Type == BTRFS_ROOT_ITEM_KEY && KeyOffset == 0)
                {
                    const uint8_t *R = Scratch + DataOffset;
                    uint64_t Generation, RootBytenr;
                    uint8_t RootLevel;
                    if (DataSize < BTRFS_ROOT_ITEM_MIN_SIZE)
                        return Rc95LdrCorruptData;
                    Generation = Rc95LdrReadU64(R + 160u);
                    RootBytenr = Rc95LdrReadU64(R + 176u);
                    RootLevel = R[238u];
                    if (!Generation || Generation > V->Superblock.Generation ||
                        !RootBytenr || RootBytenr % NodeSize || RootLevel > 8u)
                        return Rc95LdrCorruptData;
                    if (Found && (FoundRoot != RootBytenr ||
                        FoundGeneration != Generation || FoundLevel != RootLevel))
                        return Rc95LdrCorruptData;
                    Found = 1; FoundRoot = RootBytenr;
                    FoundGeneration = Generation; FoundLevel = RootLevel;
                }
            }
        }
        else
        {
            uint64_t PrevObject = 0, PrevOffset = 0;
            uint8_t PrevType = 0;
            int HavePrev = 0;
            for (I = 0; I < Count; ++I)
            {
                const uint8_t *E = Scratch + BTRFS_TREE_HEADER_SIZE +
                                   I * BTRFS_NODE_PTR_SIZE;
                uint64_t Object = Rc95LdrReadU64(E);
                uint8_t Type = E[8];
                uint64_t KeyOffset = Rc95LdrReadU64(E + 9);
                uint64_t Child = Rc95LdrReadU64(E + 17);
                uint64_t ChildGen = Rc95LdrReadU64(E + 25);
                if (HavePrev && (Object < PrevObject ||
                    (Object == PrevObject && Type < PrevType) ||
                    (Object == PrevObject && Type == PrevType &&
                     KeyOffset <= PrevOffset)))
                    return Rc95LdrCorruptData;
                HavePrev = 1; PrevObject = Object; PrevType = Type;
                PrevOffset = KeyOffset;
                if (!Child || Child % NodeSize || !ChildGen ||
                    ChildGen > V->Superblock.Generation ||
                    PendingCount >= BTRFS_MAX_TREE_NODES)
                    return Rc95LdrCorruptData;
                Pending[PendingCount] = Child;
                PendingGen[PendingCount] = ChildGen;
                PendingLevel[PendingCount] = (uint8_t)(Level - 1u);
                ++PendingCount;
            }
        }
    }

    if (!Found) return Rc95LdrNotFound;
    V->FsTreeRoot = FoundRoot;
    V->FsTreeGeneration = FoundGeneration;
    V->FsTreeLevel = FoundLevel;
    V->RootTreeLoaded = 1;
    return Rc95LdrSuccess;
}

#define BTRFS_FS_TREE_OBJECTID 5ull

static RC95LDR_STATUS
BtrfsFindFsTreeItem(RC95LDR_BTRFS_VOLUME *V,
                           uint64_t WantedObject,
                           uint8_t WantedType,
                           uint64_t WantedOffset,
                           uint8_t *Scratch,
                           size_t ScratchLength,
                           void *ItemBuffer,
                           size_t ItemBufferLength,
                           size_t *ItemLength,
                           int Predecessor,
                           uint64_t *FoundOffset)
{
    uint64_t Pending[BTRFS_MAX_TREE_NODES];
    uint64_t PendingGen[BTRFS_MAX_TREE_NODES];
    uint8_t PendingLevel[BTRFS_MAX_TREE_NODES];
    uint64_t Visited[BTRFS_MAX_TREE_NODES];
    uint32_t PendingCount = 0, VisitedCount = 0, I, NodeSize;
    int Found = 0;
    uint32_t FoundDataOffset = 0, FoundDataSize = 0;
    uint64_t BestOffset = 0;

    if (!V || !V->Mounted || !V->RootTreeLoaded || !Scratch ||
        !ItemLength || (!ItemBuffer && ItemBufferLength) ||
        (Predecessor && !FoundOffset))
        return Rc95LdrInvalidParameter;
    *ItemLength = 0;
    NodeSize = V->Superblock.NodeSize;
    if (ScratchLength < NodeSize) return Rc95LdrBufferTooSmall;

    Pending[PendingCount] = V->FsTreeRoot;
    PendingGen[PendingCount] = V->FsTreeGeneration;
    PendingLevel[PendingCount] = V->FsTreeLevel;
    ++PendingCount;

    while (PendingCount)
    {
        uint64_t Bytenr, ExpectedGen;
        uint8_t ExpectedLevel, Level;
        uint32_t Count;
        --PendingCount;
        Bytenr = Pending[PendingCount];
        ExpectedGen = PendingGen[PendingCount];
        ExpectedLevel = PendingLevel[PendingCount];
        for (I = 0; I < VisitedCount; ++I)
            if (Visited[I] == Bytenr) return Rc95LdrCorruptData;
        if (VisitedCount >= BTRFS_MAX_TREE_NODES)
            return Rc95LdrBufferTooSmall;
        Visited[VisitedCount++] = Bytenr;

        {
            RC95LDR_STATUS S = BtrfsRootReadNode(V, Bytenr, Scratch);
            if (S != Rc95LdrSuccess) return S;
        }
        if (Rc95LdrReadU32(Scratch) !=
                Rc95LdrVeneerCrc32c(Scratch + 32u, NodeSize - 32u) ||
            memcmp(Scratch + 32u, V->Superblock.FsId, 16u) != 0 ||
            Rc95LdrReadU64(Scratch + 48u) != Bytenr ||
            Rc95LdrReadU64(Scratch + 88u) != BTRFS_FS_TREE_OBJECTID ||
            Rc95LdrReadU64(Scratch + 80u) != ExpectedGen)
            return Rc95LdrCorruptData;

        Count = Rc95LdrReadU32(Scratch + 96u);
        Level = Scratch[100u];
        if (Level > 8u || Level != ExpectedLevel ||
            Count > (NodeSize - BTRFS_TREE_HEADER_SIZE) /
                    (Level ? BTRFS_NODE_PTR_SIZE : BTRFS_LEAF_ITEM_SIZE))
            return Rc95LdrCorruptData;

        if (!Level)
        {
            uint32_t PreviousData = NodeSize;
            uint64_t PrevObject = 0, PrevOffset = 0;
            uint8_t PrevType = 0;
            int HavePrev = 0;
            for (I = 0; I < Count; ++I)
            {
                const uint8_t *E = Scratch + BTRFS_TREE_HEADER_SIZE +
                                   I * BTRFS_LEAF_ITEM_SIZE;
                uint64_t Object = Rc95LdrReadU64(E);
                uint8_t Type = E[8];
                uint64_t Offset = Rc95LdrReadU64(E + 9);
                uint32_t DataOffset = Rc95LdrReadU32(E + 17);
                uint32_t DataSize = Rc95LdrReadU32(E + 21);
                uint64_t DataEnd;
                if (HavePrev && (Object < PrevObject ||
                    (Object == PrevObject && Type < PrevType) ||
                    (Object == PrevObject && Type == PrevType &&
                     Offset <= PrevOffset)))
                    return Rc95LdrCorruptData;
                HavePrev = 1; PrevObject = Object; PrevType = Type;
                PrevOffset = Offset;
                if (DataOffset < BTRFS_TREE_HEADER_SIZE +
                                  Count * BTRFS_LEAF_ITEM_SIZE ||
                    !BtrfsRootAddU64(DataOffset, DataSize, &DataEnd) ||
                    DataEnd > NodeSize || DataEnd > PreviousData)
                    return Rc95LdrCorruptData;
                PreviousData = DataOffset;
                if (Object == WantedObject && Type == WantedType &&
                    (Predecessor ? Offset <= WantedOffset :
                                   Offset == WantedOffset))
                {
                    if (!Predecessor)
                    {
                        *ItemLength = DataSize;
                        if (DataSize > ItemBufferLength)
                            return Rc95LdrBufferTooSmall;
                        if (DataSize && !ItemBuffer)
                            return Rc95LdrInvalidParameter;
                        if (DataSize) memcpy(ItemBuffer, Scratch + DataOffset, DataSize);
                        return Rc95LdrSuccess;
                    }
                    Found = 1;
                    BestOffset = Offset;
                    FoundDataOffset = DataOffset;
                    FoundDataSize = DataSize;
                }
                if (Object > WantedObject ||
                    (Object == WantedObject && Type > WantedType) ||
                    (Object == WantedObject && Type == WantedType &&
                     Offset > WantedOffset))
                {
                    if (!Predecessor) return Rc95LdrNotFound;
                    break;
                }
            }
            if (Predecessor && Found)
            {
                *ItemLength = FoundDataSize;
                *FoundOffset = BestOffset;
                if (FoundDataSize > ItemBufferLength)
                    return Rc95LdrBufferTooSmall;
                if (FoundDataSize && !ItemBuffer)
                    return Rc95LdrInvalidParameter;
                if (FoundDataSize)
                    memcpy(ItemBuffer, Scratch + FoundDataOffset, FoundDataSize);
                return Rc95LdrSuccess;
            }
        }
        else
        {
            uint64_t PrevObject = 0, PrevOffset = 0;
            uint64_t SelectedChild = 0, SelectedGeneration = 0;
            uint8_t PrevType = 0;
            int HavePrev = 0;
            for (I = 0; I < Count; ++I)
            {
                const uint8_t *E = Scratch + BTRFS_TREE_HEADER_SIZE +
                                   I * BTRFS_NODE_PTR_SIZE;
                uint64_t Object = Rc95LdrReadU64(E);
                uint8_t Type = E[8];
                uint64_t Offset = Rc95LdrReadU64(E + 9);
                uint64_t Child = Rc95LdrReadU64(E + 17);
                uint64_t ChildGen = Rc95LdrReadU64(E + 25);
                int NotGreater;
                if (HavePrev && (Object < PrevObject ||
                    (Object == PrevObject && Type < PrevType) ||
                    (Object == PrevObject && Type == PrevType &&
                     Offset <= PrevOffset)))
                    return Rc95LdrCorruptData;
                HavePrev = 1; PrevObject = Object; PrevType = Type;
                PrevOffset = Offset;
                if (!Child || Child % NodeSize || !ChildGen ||
                    ChildGen > V->FsTreeGeneration)
                    return Rc95LdrCorruptData;
                NotGreater = (Object < WantedObject) ||
                    (Object == WantedObject && Type < WantedType) ||
                    (Object == WantedObject && Type == WantedType &&
                     Offset <= WantedOffset);
                if (NotGreater)
                {
                    SelectedChild = Child;
                    SelectedGeneration = ChildGen;
                }
            }
            if (!SelectedChild) return Rc95LdrNotFound;
            if (PendingCount >= BTRFS_MAX_TREE_NODES)
                return Rc95LdrBufferTooSmall;
            Pending[PendingCount] = SelectedChild;
            PendingGen[PendingCount] = SelectedGeneration;
            PendingLevel[PendingCount] = (uint8_t)(Level - 1u);
            ++PendingCount;
        }
    }
    return Rc95LdrNotFound;
}

RC95LDR_STATUS
Rc95LdrBtrfsFindFsTreeItem(RC95LDR_BTRFS_VOLUME *V,
                           uint64_t WantedObject, uint8_t WantedType,
                           uint64_t WantedOffset, uint8_t *Scratch,
                           size_t ScratchLength, void *ItemBuffer,
                           size_t ItemBufferLength, size_t *ItemLength)
{
    return BtrfsFindFsTreeItem(V, WantedObject, WantedType, WantedOffset,
            Scratch, ScratchLength, ItemBuffer, ItemBufferLength, ItemLength,
            0, NULL);
}

RC95LDR_STATUS
Rc95LdrBtrfsFindFsTreeItemAtOrBefore(RC95LDR_BTRFS_VOLUME *V,
                                     uint64_t WantedObject,
                                     uint8_t WantedType,
                                     uint64_t WantedOffset,
                                     uint8_t *Scratch,
                                     size_t ScratchLength,
                                     void *ItemBuffer,
                                     size_t ItemBufferLength,
                                     size_t *ItemLength,
                                     uint64_t *FoundOffset)
{
    if (!FoundOffset) return Rc95LdrInvalidParameter;
    *FoundOffset = 0;
    return BtrfsFindFsTreeItem(V, WantedObject, WantedType, WantedOffset,
            Scratch, ScratchLength, ItemBuffer, ItemBufferLength, ItemLength,
            1, FoundOffset);
}



/*
 * Find the least key strictly greater than (ObjectId, KeyType, KeyOffset).
 * This deliberately walks the bounded tree rather than assuming that the
 * next key is in the same leaf. That is required for extent boundaries at
 * leaf transitions. The caller owns the NodeSize scratch buffer.
 */
RC95LDR_STATUS
Rc95LdrBtrfsFindFsTreeItemAfter(RC95LDR_BTRFS_VOLUME *V,
                               uint64_t ObjectId,
                               uint8_t KeyType,
                               uint64_t KeyOffset,
                               uint8_t *Scratch,
                               size_t ScratchLength,
                               void *ItemBuffer,
                               size_t ItemBufferLength,
                               size_t *ItemLength,
                               uint64_t *FoundOffset)
{
    uint64_t Pending[BTRFS_MAX_TREE_NODES];
    uint64_t PendingGen[BTRFS_MAX_TREE_NODES];
    uint8_t PendingLevel[BTRFS_MAX_TREE_NODES];
    uint64_t Visited[BTRFS_MAX_TREE_NODES];
    uint32_t PendingCount = 0, VisitedCount = 0, I, NodeSize;
    uint64_t BestOffset = UINT64_MAX;
    size_t BestLength = 0;
    int Found = 0;

    if (!V || !V->Mounted || !V->RootTreeLoaded || !Scratch ||
        !ItemLength || !FoundOffset || (!ItemBuffer && ItemBufferLength))
        return Rc95LdrInvalidParameter;
    *ItemLength = 0;
    *FoundOffset = 0;
    NodeSize = V->Superblock.NodeSize;
    if (ScratchLength < NodeSize)
        return Rc95LdrBufferTooSmall;

    Pending[PendingCount] = V->FsTreeRoot;
    PendingGen[PendingCount] = V->FsTreeGeneration;
    PendingLevel[PendingCount] = V->FsTreeLevel;
    ++PendingCount;

    while (PendingCount)
    {
        uint64_t Bytenr, ExpectedGen;
        uint8_t ExpectedLevel, Level;
        uint32_t Count;
        --PendingCount;
        Bytenr = Pending[PendingCount];
        ExpectedGen = PendingGen[PendingCount];
        ExpectedLevel = PendingLevel[PendingCount];

        for (I = 0; I < VisitedCount; ++I)
            if (Visited[I] == Bytenr)
                return Rc95LdrCorruptData;
        if (VisitedCount >= BTRFS_MAX_TREE_NODES)
            return Rc95LdrBufferTooSmall;
        Visited[VisitedCount++] = Bytenr;

        {
            RC95LDR_STATUS S = BtrfsRootReadNode(V, Bytenr, Scratch);
            if (S != Rc95LdrSuccess) return S;
        }
        if (Rc95LdrReadU32(Scratch) !=
                Rc95LdrVeneerCrc32c(Scratch + 32u, NodeSize - 32u) ||
            memcmp(Scratch + 32u, V->Superblock.FsId, 16u) != 0 ||
            Rc95LdrReadU64(Scratch + 48u) != Bytenr ||
            Rc95LdrReadU64(Scratch + 88u) != BTRFS_FS_TREE_OBJECTID ||
            Rc95LdrReadU64(Scratch + 80u) != ExpectedGen)
            return Rc95LdrCorruptData;

        Count = Rc95LdrReadU32(Scratch + 96u);
        Level = Scratch[100u];
        if (Level > 8u || Level != ExpectedLevel ||
            Count > (NodeSize - BTRFS_TREE_HEADER_SIZE) /
                    (Level ? BTRFS_NODE_PTR_SIZE : BTRFS_LEAF_ITEM_SIZE))
            return Rc95LdrCorruptData;

        if (!Level)
        {
            uint32_t PreviousData = NodeSize;
            uint64_t PrevObject = 0, PrevOffset = 0;
            uint8_t PrevType = 0;
            int HavePrev = 0;
            for (I = 0; I < Count; ++I)
            {
                const uint8_t *E = Scratch + BTRFS_TREE_HEADER_SIZE +
                                   I * BTRFS_LEAF_ITEM_SIZE;
                uint64_t Object = Rc95LdrReadU64(E);
                uint8_t Type = E[8];
                uint64_t Offset = Rc95LdrReadU64(E + 9);
                uint32_t DataOffset = Rc95LdrReadU32(E + 17);
                uint32_t DataSize = Rc95LdrReadU32(E + 21);
                uint64_t DataEnd;
                if (HavePrev && (Object < PrevObject ||
                    (Object == PrevObject && Type < PrevType) ||
                    (Object == PrevObject && Type == PrevType &&
                     Offset <= PrevOffset)))
                    return Rc95LdrCorruptData;
                HavePrev = 1; PrevObject = Object; PrevType = Type;
                PrevOffset = Offset;
                if (DataOffset < BTRFS_TREE_HEADER_SIZE +
                                  Count * BTRFS_LEAF_ITEM_SIZE ||
                    !BtrfsRootAddU64(DataOffset, DataSize, &DataEnd) ||
                    DataEnd > NodeSize || DataEnd > PreviousData)
                    return Rc95LdrCorruptData;
                PreviousData = DataOffset;

                if (Object == ObjectId && Type == KeyType &&
                    Offset > KeyOffset && (!Found || Offset < BestOffset))
                {
                    BestOffset = Offset;
                    BestLength = DataSize;
                    Found = 1;
                    *ItemLength = DataSize;
                    *FoundOffset = Offset;
                    /*
                     * Keep scanning: a later visited leaf may contain a
                     * smaller successor than this one. A too-small caller
                     * buffer is reported only after the globally least key
                     * has been selected.
                     */
                    if (DataSize <= ItemBufferLength && DataSize && ItemBuffer)
                        memcpy(ItemBuffer, Scratch + DataOffset, DataSize);
                }
            }
        }
        else
        {
            uint64_t PrevObject = 0, PrevOffset = 0;
            uint8_t PrevType = 0;
            int HavePrev = 0;
            for (I = 0; I < Count; ++I)
            {
                const uint8_t *E = Scratch + BTRFS_TREE_HEADER_SIZE +
                                   I * BTRFS_NODE_PTR_SIZE;
                uint64_t Object = Rc95LdrReadU64(E);
                uint8_t Type = E[8];
                uint64_t Offset = Rc95LdrReadU64(E + 9);
                uint64_t Child = Rc95LdrReadU64(E + 17);
                uint64_t ChildGen = Rc95LdrReadU64(E + 25);
                if (HavePrev && (Object < PrevObject ||
                    (Object == PrevObject && Type < PrevType) ||
                    (Object == PrevObject && Type == PrevType &&
                     Offset <= PrevOffset)))
                    return Rc95LdrCorruptData;
                HavePrev = 1; PrevObject = Object; PrevType = Type;
                PrevOffset = Offset;
                if (!Child || Child % NodeSize || !ChildGen ||
                    ChildGen > V->FsTreeGeneration)
                    return Rc95LdrCorruptData;
                if (PendingCount >= BTRFS_MAX_TREE_NODES)
                    return Rc95LdrBufferTooSmall;
                Pending[PendingCount] = Child;
                PendingGen[PendingCount] = ChildGen;
                PendingLevel[PendingCount] = (uint8_t)(Level - 1u);
                ++PendingCount;
            }
        }
    }

    if (!Found)
        return Rc95LdrNotFound;
    *ItemLength = BestLength;
    *FoundOffset = BestOffset;
    if (BestLength > ItemBufferLength)
        return Rc95LdrBufferTooSmall;
    if (BestLength && !ItemBuffer)
        return Rc95LdrInvalidParameter;
    return Rc95LdrSuccess;
}

#define BTRFS_INODE_ITEM_KEY 1u
#define BTRFS_INODE_ITEM_SIZE 160u
#define BTRFS_S_IFMT 0170000u
#define BTRFS_S_IFREG 0100000u
#define BTRFS_S_IFDIR 0040000u
#define BTRFS_S_IFLNK 0120000u

RC95LDR_STATUS
Rc95LdrBtrfsReadInode(RC95LDR_BTRFS_VOLUME *V,
                      uint64_t InodeId,
                      uint8_t *Scratch,
                      size_t ScratchLength,
                      RC95LDR_BTRFS_INODE *Inode)
{
    uint8_t Item[BTRFS_INODE_ITEM_SIZE];
    size_t ItemLength = 0;
    uint32_t Type;
    RC95LDR_STATUS S;

    if (!V || !Inode || !InodeId)
        return Rc95LdrInvalidParameter;
    memset(Inode, 0, sizeof(*Inode));
    S = Rc95LdrBtrfsFindFsTreeItem(V, InodeId, BTRFS_INODE_ITEM_KEY, 0,
                                  Scratch, ScratchLength, Item, sizeof(Item),
                                  &ItemLength);
    if (S != Rc95LdrSuccess) return S;
    if (ItemLength != sizeof(Item)) return Rc95LdrCorruptData;

    Inode->Generation = Rc95LdrReadU64(Item);
    Inode->TransactionId = Rc95LdrReadU64(Item + 8u);
    Inode->Size = Rc95LdrReadU64(Item + 16u);
    Inode->AllocatedBytes = Rc95LdrReadU64(Item + 24u);
    Inode->BlockGroup = Rc95LdrReadU64(Item + 32u);
    Inode->LinkCount = Rc95LdrReadU32(Item + 40u);
    Inode->UserId = Rc95LdrReadU32(Item + 44u);
    Inode->GroupId = Rc95LdrReadU32(Item + 48u);
    Inode->Mode = Rc95LdrReadU32(Item + 52u);
    Inode->Flags = Rc95LdrReadU64(Item + 64u);
    Inode->Sequence = Rc95LdrReadU64(Item + 72u);

    Type = Inode->Mode & BTRFS_S_IFMT;
    if (!Inode->Generation || Inode->Generation > V->FsTreeGeneration ||
        (Type != BTRFS_S_IFREG && Type != BTRFS_S_IFDIR &&
         Type != BTRFS_S_IFLNK))
    {
        memset(Inode, 0, sizeof(*Inode));
        return Rc95LdrCorruptData;
    }
    return Rc95LdrSuccess;
}


#define BTRFS_DIR_ITEM_KEY 84u
#define BTRFS_DIR_INDEX_KEY 96u
#define BTRFS_DIR_ITEM_HEADER_SIZE 30u

static uint32_t
BtrfsNameHash(const uint8_t *Name, size_t Length)
{
    uint32_t Crc = 0xfffffffeu;
    size_t I;
    for (I = 0; I < Length; ++I)
    {
        unsigned Bit;
        Crc ^= Name[I];
        for (Bit = 0; Bit < 8u; ++Bit)
            Crc = (Crc >> 1) ^ ((Crc & 1u) ? 0x82f63b78u : 0u);
    }
    return Crc;
}

RC95LDR_STATUS
Rc95LdrBtrfsFindDirectoryEntry(RC95LDR_BTRFS_VOLUME *V,
                              uint64_t DirectoryId,
                              const uint8_t *Name,
                              size_t NameLength,
                              uint8_t *Scratch,
                              size_t ScratchLength,
                              uint64_t *InodeId,
                              uint8_t *FileType,
                              uint64_t *LocationOffset)
{
    uint8_t Bucket[4096];
    size_t BucketLength = 0, Offset = 0;
    uint32_t Hash;
    RC95LDR_STATUS S;

    if (!V || !DirectoryId || !Name || !NameLength || NameLength > 255u ||
        !Scratch || !InodeId || !FileType || !LocationOffset)
        return Rc95LdrInvalidParameter;
    *InodeId = 0;
    *FileType = 0;
    *LocationOffset = 0;

    Hash = BtrfsNameHash(Name, NameLength);
    S = Rc95LdrBtrfsFindFsTreeItem(V, DirectoryId, BTRFS_DIR_ITEM_KEY,
                                   Hash, Scratch, ScratchLength,
                                   Bucket, sizeof(Bucket), &BucketLength);
    if (S != Rc95LdrSuccess) return S;

    while (Offset < BucketLength)
    {
        const uint8_t *Entry;
        uint16_t DataLength, EntryNameLength;
        uint64_t RecordLength, RecordEnd;
        uint64_t LocationObject, KeyOffset;
        uint8_t LocationType, EntryType;
        size_t NameOffset;

        if (BucketLength - Offset < BTRFS_DIR_ITEM_HEADER_SIZE)
            return Rc95LdrCorruptData;
        Entry = Bucket + Offset;
        LocationObject = Rc95LdrReadU64(Entry);
        LocationType = Entry[8];
        KeyOffset = Rc95LdrReadU64(Entry + 9);
        DataLength = (uint16_t)(Entry[25] | ((uint16_t)Entry[26] << 8));
        EntryNameLength = (uint16_t)(Entry[27] | ((uint16_t)Entry[28] << 8));
        EntryType = Entry[29];

        /* Btrfs directory types follow DT_*: zero is a valid unknown type,
           and 1..8 are regular, directory, device, FIFO, socket, symlink,
           and extended-attribute variants. Values above 8 are malformed. */
        if (!LocationObject ||
            (LocationType != BTRFS_INODE_ITEM_KEY &&
             LocationType != BTRFS_ROOT_ITEM_KEY) ||
            EntryType > 8u ||
            (uint64_t)EntryNameLength + (uint64_t)DataLength >
                (uint64_t)(BucketLength - Offset - BTRFS_DIR_ITEM_HEADER_SIZE))
            return Rc95LdrCorruptData;

        RecordLength = BTRFS_DIR_ITEM_HEADER_SIZE +
                       (uint64_t)EntryNameLength + (uint64_t)DataLength;
        if (!BtrfsRootAddU64(Offset, RecordLength, &RecordEnd) ||
            RecordEnd > BucketLength)
            return Rc95LdrCorruptData;
        NameOffset = Offset + BTRFS_DIR_ITEM_HEADER_SIZE;

        if (EntryNameLength == NameLength &&
            memcmp(Bucket + NameOffset, Name, NameLength) == 0)
        {
            /* Root-item locations represent subvolume entries, not inode
               numbers in this filesystem tree. Do not reinterpret them as
               ordinary files or directories. */
            if (LocationType == BTRFS_ROOT_ITEM_KEY)
                return Rc95LdrUnsupported;
            if (KeyOffset != 0u || DataLength != 0u)
                return Rc95LdrCorruptData;

            *InodeId = LocationObject;
            *FileType = EntryType;
            *LocationOffset = KeyOffset;
            return Rc95LdrSuccess;
        }
        Offset = (size_t)RecordEnd;
    }
    return Rc95LdrNotFound;
}

#define BTRFS_FT_REG_FILE 1u
#define BTRFS_FT_DIR 2u
#define BTRFS_FT_SYMLINK 7u

static RC95LDR_STATUS
BtrfsDirectoryTypeStatus(uint8_t DirectoryType, uint32_t Mode)
{
    uint32_t Type = Mode & BTRFS_S_IFMT;

    if ((DirectoryType == BTRFS_FT_REG_FILE && Type == BTRFS_S_IFREG) ||
        (DirectoryType == BTRFS_FT_DIR && Type == BTRFS_S_IFDIR) ||
        (DirectoryType == BTRFS_FT_SYMLINK && Type == BTRFS_S_IFLNK))
        return Rc95LdrSuccess;

    /* These are valid Btrfs directory-entry types, but this read-only
       path resolver intentionally handles only regular files, directories,
       and symlinks. A disagreement between two supported types is corrupt. */
    if (DirectoryType == 0u ||
        (DirectoryType >= 3u && DirectoryType <= 6u) ||
        DirectoryType == 8u)
        return Rc95LdrUnsupported;
    return Rc95LdrCorruptData;
}

RC95LDR_STATUS
Rc95LdrBtrfsResolvePath(RC95LDR_BTRFS_VOLUME *V,
                        uint64_t StartInodeId,
                        const uint8_t *Path,
                        size_t PathLength,
                        uint8_t *Scratch,
                        size_t ScratchLength,
                        uint64_t *ResolvedInodeId,
                        RC95LDR_BTRFS_INODE *ResolvedInode)
{
    size_t Position = 0;
    int RequireDirectory = 0;
    uint64_t CurrentId = StartInodeId;
    RC95LDR_BTRFS_INODE Current;
    RC95LDR_STATUS S;

    if (!V || !StartInodeId || (!Path && PathLength) || !Scratch ||
        !ResolvedInodeId || !ResolvedInode || PathLength > 4096u)
        return Rc95LdrInvalidParameter;
    *ResolvedInodeId = 0;
    memset(ResolvedInode, 0, sizeof(*ResolvedInode));
    if (PathLength && memchr(Path, '\0', PathLength) != NULL)
        return Rc95LdrInvalidParameter;
    RequireDirectory = PathLength && Path[PathLength - 1u] == '/';

    S = Rc95LdrBtrfsReadInode(V, CurrentId, Scratch, ScratchLength, &Current);
    if (S != Rc95LdrSuccess) return S;

    /* A leading slash is rooted at StartInodeId, not at a global subvolume. */
    while (Position < PathLength && Path[Position] == '/') ++Position;
    while (Position < PathLength)
    {
        size_t Start = Position, Length;
        uint64_t NextId, LocationOffset;
        uint8_t DirectoryType;
        RC95LDR_BTRFS_INODE Next;

        while (Position < PathLength && Path[Position] != '/') ++Position;
        Length = Position - Start;
        if (!Length || Length > 255u)
            return Rc95LdrUnsupported;
        if (Length == 1u && Path[Start] == '.')
        {
            if ((Current.Mode & BTRFS_S_IFMT) != BTRFS_S_IFDIR)
                return Rc95LdrNotFound;
            while (Position < PathLength && Path[Position] == '/') ++Position;
            continue;
        }
        if (Length == 2u && Path[Start] == '.' && Path[Start + 1u] == '.')
        {
            if ((Current.Mode & BTRFS_S_IFMT) != BTRFS_S_IFDIR)
                return Rc95LdrNotFound;
            /* The default subvolume root's parent is itself. Resolving a
               nested directory's parent needs an inode-ref index and is
               deliberately rejected rather than guessed. */
            if (CurrentId != StartInodeId)
                return Rc95LdrUnsupported;
            while (Position < PathLength && Path[Position] == '/') ++Position;
            continue;
        }
        if ((Current.Mode & BTRFS_S_IFMT) != BTRFS_S_IFDIR)
            return Rc95LdrNotFound;

        S = Rc95LdrBtrfsFindDirectoryEntry(V, CurrentId, Path + Start,
                Length, Scratch, ScratchLength, &NextId, &DirectoryType,
                &LocationOffset);
        if (S != Rc95LdrSuccess) return S;
        S = Rc95LdrBtrfsReadInode(V, NextId, Scratch, ScratchLength, &Next);
        if (S != Rc95LdrSuccess) return S;
        S = BtrfsDirectoryTypeStatus(DirectoryType, Next.Mode);
        if (S != Rc95LdrSuccess)
            return S;

        CurrentId = NextId;
        Current = Next;
        while (Position < PathLength && Path[Position] == '/') ++Position;
    }

    if (RequireDirectory &&
        (Current.Mode & BTRFS_S_IFMT) != BTRFS_S_IFDIR)
        return Rc95LdrNotFound;
    *ResolvedInodeId = CurrentId;
    *ResolvedInode = Current;
    return Rc95LdrSuccess;
}

#define BTRFS_EXTENT_DATA_KEY 108u
#define BTRFS_FILE_EXTENT_HEADER_SIZE 21u
#define BTRFS_FILE_EXTENT_REGULAR_SIZE 53u

RC95LDR_STATUS
Rc95LdrBtrfsReadFileExtent(RC95LDR_BTRFS_VOLUME *V,
                           uint64_t InodeId,
                           uint64_t FileOffset,
                           uint8_t *Scratch,
                           size_t ScratchLength,
                           RC95LDR_BTRFS_FILE_EXTENT *Extent,
                           void *ItemBuffer,
                           size_t ItemBufferLength,
                           void *InlineBuffer,
                           size_t InlineBufferLength)
{
    const uint8_t *Item = (const uint8_t *)ItemBuffer;
    size_t ItemLength = 0;
    uint64_t End, DiskStart, ExtentKeyOffset;
    RC95LDR_STATUS S;

    if (!V || !InodeId || !Scratch || !Extent || !ItemBuffer ||
        !ItemBufferLength || (!InlineBuffer && InlineBufferLength))
        return Rc95LdrInvalidParameter;
    memset(Extent, 0, sizeof(*Extent));
    S = Rc95LdrBtrfsFindFsTreeItemAtOrBefore(V, InodeId,
            BTRFS_EXTENT_DATA_KEY, FileOffset, Scratch, ScratchLength,
            ItemBuffer, ItemBufferLength, &ItemLength, &ExtentKeyOffset);
    if (S != Rc95LdrSuccess) return S;
    if (ItemLength < BTRFS_FILE_EXTENT_HEADER_SIZE)
        return Rc95LdrCorruptData;

    Extent->FileOffset = ExtentKeyOffset;
    Extent->Generation = Rc95LdrReadU64(Item);
    Extent->RamBytes = Rc95LdrReadU64(Item + 8u);
    if (!Extent->Generation || Extent->Generation > V->FsTreeGeneration ||
        Item[16] != 0u || Item[17] != 0u ||
        Rc95LdrReadU16(Item + 18u) != 0u)
        return Rc95LdrUnsupported;

    Extent->Type = Item[20];
    if (Extent->Type == 0u)
    {
        size_t PayloadLength = ItemLength - BTRFS_FILE_EXTENT_HEADER_SIZE;
        if ((uint64_t)PayloadLength != Extent->RamBytes ||
            !BtrfsRootAddU64(ExtentKeyOffset, (uint64_t)PayloadLength, &End))
            return Rc95LdrCorruptData;
        if (FileOffset < ExtentKeyOffset || FileOffset >= End)
            return Rc95LdrNotFound; /* a genuine sparse gap after this item */
        Extent->NumBytes = PayloadLength;
        Extent->InlineLength = PayloadLength;
        if (PayloadLength > InlineBufferLength)
            return Rc95LdrBufferTooSmall;
        if (PayloadLength && !InlineBuffer)
            return Rc95LdrInvalidParameter;
        if (PayloadLength)
            memcpy(InlineBuffer, Item + BTRFS_FILE_EXTENT_HEADER_SIZE,
                   PayloadLength);
        return Rc95LdrSuccess;
    }

    if (Extent->Type != 1u && Extent->Type != 2u)
        return Rc95LdrUnsupported;
    if (ItemLength != BTRFS_FILE_EXTENT_REGULAR_SIZE)
        return Rc95LdrCorruptData;

    Extent->DiskBytenr = Rc95LdrReadU64(Item + 21u);
    Extent->DiskNumBytes = Rc95LdrReadU64(Item + 29u);
    Extent->ExtentOffset = Rc95LdrReadU64(Item + 37u);
    Extent->NumBytes = Rc95LdrReadU64(Item + 45u);
    if (!Extent->NumBytes || !BtrfsRootAddU64(ExtentKeyOffset,
            Extent->NumBytes, &End) || !Extent->DiskBytenr ||
        !Extent->DiskNumBytes ||
        !BtrfsRootAddU64(Extent->DiskBytenr, Extent->ExtentOffset,
                         &DiskStart) ||
        Extent->ExtentOffset > Extent->DiskNumBytes ||
        Extent->NumBytes > Extent->DiskNumBytes - Extent->ExtentOffset ||
        Extent->RamBytes < Extent->NumBytes)
    {
        memset(Extent, 0, sizeof(*Extent));
        return Rc95LdrCorruptData;
    }
    if (FileOffset < ExtentKeyOffset || FileOffset >= End)
    {
        memset(Extent, 0, sizeof(*Extent));
        return Rc95LdrNotFound; /* predecessor exists, but cursor is in a hole */
    }
    return Rc95LdrSuccess;
}

RC95LDR_STATUS
Rc95LdrBtrfsReadExtentData(RC95LDR_BTRFS_VOLUME *V,
                           const RC95LDR_BTRFS_FILE_EXTENT *E,
                           uint64_t FileOffset,
                           size_t Length,
                           uint8_t *Scratch,
                           size_t ScratchLength,
                           void *Output)
{
    uint64_t ExtentEnd, Delta, Logical;
    uint32_t BlockSize;
    size_t Done = 0;
    uint8_t *Out = (uint8_t *)Output;

    if (!V || !E || (!Output && Length) || !Scratch)
        return Rc95LdrInvalidParameter;
    if (E->Type == 0u)
        return Rc95LdrUnsupported; /* inline bytes are returned by ReadFileExtent */
    if (E->Type != 1u && E->Type != 2u)
        return Rc95LdrUnsupported;
    if (!BtrfsRootAddU64(E->FileOffset, E->NumBytes, &ExtentEnd) ||
        FileOffset < E->FileOffset || FileOffset > ExtentEnd ||
        (uint64_t)Length > ExtentEnd - FileOffset)
        return Rc95LdrOutOfBounds;
    if (!Length) return Rc95LdrSuccess;
    if (E->Type == 2u)
    {
        memset(Output, 0, Length);
        return Rc95LdrSuccess;
    }

    BlockSize = V->PartitionDevice.BlockSize;
    if (!BlockSize || ScratchLength < BlockSize)
        return Rc95LdrBufferTooSmall;
    Delta = FileOffset - E->FileOffset;
    if (!E->DiskBytenr || !E->DiskNumBytes ||
        E->ExtentOffset > E->DiskNumBytes)
        return Rc95LdrCorruptData;
    if (!BtrfsRootAddU64(E->DiskBytenr, E->ExtentOffset, &Logical) ||
        !BtrfsRootAddU64(Logical, Delta, &Logical))
        return Rc95LdrIntegerOverflow;
    if (Delta > E->DiskNumBytes - E->ExtentOffset ||
        (uint64_t)Length > E->DiskNumBytes - E->ExtentOffset - Delta)
        return Rc95LdrOutOfBounds;

    while (Done < Length)
    {
        uint64_t SectorLogical = Logical - (Logical % BlockSize);
        uint64_t Physical, SectorPhysical, PhysicalEnd;
        size_t InBlock = (size_t)(Logical % BlockSize);
        size_t Copy = BlockSize - InBlock;
        RC95LDR_STATUS S;
        if (Copy > Length - Done) Copy = Length - Done;
        /*
         * Keep the logical cursor bounded even on hostile extent metadata.
         * The initial range check covers the file and disk extents, while this
         * explicit addition check protects the per-sector cursor update.
         */
        if ((uint64_t)Copy > UINT64_MAX - Logical)
            return Rc95LdrIntegerOverflow;
        S = Rc95LdrBtrfsMapLogical(V, SectorLogical, BlockSize, &Physical);
        if (S != Rc95LdrSuccess) return S;
        if (Physical % BlockSize)
            return Rc95LdrCorruptData;
        SectorPhysical = Physical;
        if (!BtrfsRootAddU64(SectorPhysical, BlockSize, &PhysicalEnd) ||
            PhysicalEnd > V->Superblock.DeviceBytes)
            return Rc95LdrOutOfBounds;
        S = Rc95LdrReadBlocks(&V->PartitionDevice,
                SectorPhysical / BlockSize, 1u, Scratch, BlockSize);
        if (S != Rc95LdrSuccess) return S;
        memcpy(Out + Done, Scratch + InBlock, Copy);
        Done += Copy;
        Logical += Copy;
    }
    return Rc95LdrSuccess;
}
