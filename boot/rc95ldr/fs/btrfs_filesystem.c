#include "rc95ldr_btrfs.h"
#include <string.h>

#define BTRFS_DEFAULT_ROOT_INODE 256ull
#define BTRFS_INODE_MODE_MASK 0170000u
#define BTRFS_INODE_MODE_REGULAR 0100000u
#define BTRFS_EXTENT_DATA_KEY 108u
#define BTRFS_HANDLE_INDEX_BITS 4u
#define BTRFS_HANDLE_INDEX_MASK ((((uintptr_t)1u) << BTRFS_HANDLE_INDEX_BITS) - 1u)

static int
BtrfsFsAddU64(uint64_t A, uint64_t B, uint64_t *Result);

static void *
BtrfsEncodeHandle(size_t Index, uintptr_t Generation)
{
    return (void *)((Generation << BTRFS_HANDLE_INDEX_BITS) |
                    (uintptr_t)(Index + 1u));
}

static RC95LDR_BTRFS_FILE_HANDLE *
BtrfsFindHandle(RC95LDR_BTRFS_FILESYSTEM_CONTEXT *Context, void *Opaque)
{
    uintptr_t Value = (uintptr_t)Opaque;
    uintptr_t SlotCode, Generation;
    size_t Index;
    RC95LDR_BTRFS_FILE_HANDLE *Slot;
    if (!Context || !Value) return NULL;
    SlotCode = Value & BTRFS_HANDLE_INDEX_MASK;
    Generation = Value >> BTRFS_HANDLE_INDEX_BITS;
    if (!SlotCode || SlotCode > RC95LDR_BTRFS_FILE_HANDLES || !Generation)
        return NULL;
    Index = (size_t)(SlotCode - 1u);
    Slot = &Context->Handles[Index];
    if (!Slot->InUse || Slot->Generation != Generation)
        return NULL;
    return Slot;
}

static RC95LDR_STATUS
BtrfsFsOpenRead(void *Opaque, const char *Path, void **Handle, uint64_t *Size)
{
    RC95LDR_BTRFS_FILESYSTEM_CONTEXT *Context = Opaque;
    RC95LDR_BTRFS_FILE_HANDLE *Slot = NULL;
    RC95LDR_BTRFS_INODE Inode;
    uint64_t InodeId;
    size_t I, Length;
    RC95LDR_STATUS S;

    if (!Context || !Context->Volume || !Path || !*Path || !Handle || !Size)
        return Rc95LdrInvalidParameter;
    *Handle = NULL;
    *Size = 0;
    Length = strlen(Path);
    S = Rc95LdrBtrfsResolvePath(Context->Volume, BTRFS_DEFAULT_ROOT_INODE,
            (const uint8_t *)Path, Length, Context->Scratch,
            Context->ScratchLength, &InodeId, &Inode);
    if (S != Rc95LdrSuccess) return S;
    if ((Inode.Mode & BTRFS_INODE_MODE_MASK) != BTRFS_INODE_MODE_REGULAR)
        return Rc95LdrUnsupported;

    for (I = 0; I < RC95LDR_BTRFS_FILE_HANDLES; ++I)
        if (!Context->Handles[I].InUse) { Slot = &Context->Handles[I]; break; }
    if (!Slot) return Rc95LdrOutOfMemory;

    Context->NextHandleGeneration = (Context->NextHandleGeneration + 1u) &
        (UINTPTR_MAX >> BTRFS_HANDLE_INDEX_BITS);
    if (!Context->NextHandleGeneration)
        Context->NextHandleGeneration = 1u;
    Slot->InodeId = InodeId;
    Slot->Inode = Inode;
    Slot->Generation = Context->NextHandleGeneration;
    Slot->InUse = 1;
    *Size = Inode.Size;
    *Handle = BtrfsEncodeHandle((size_t)(Slot - Context->Handles),
                                Slot->Generation);
    return Rc95LdrSuccess;
}

static RC95LDR_STATUS
BtrfsFsReadAt(void *Opaque, void *Handle, uint64_t Offset,
              void *Buffer, size_t Length)
{
    RC95LDR_BTRFS_FILESYSTEM_CONTEXT *Context = Opaque;
    RC95LDR_BTRFS_FILE_HANDLE *Slot = BtrfsFindHandle(Context, Handle);
    uint8_t *Output;
    uint64_t End, Cursor = Offset;
    size_t Remaining = Length;

    if (!Slot || (!Buffer && Length)) return Rc95LdrInvalidParameter;
    if (!Length) return Offset <= Slot->Inode.Size ?
        Rc95LdrSuccess : Rc95LdrOutOfBounds;
    if (!Context->StagingBuffer) return Rc95LdrInvalidParameter;
    if (Length > Context->StagingBufferLength)
        return Rc95LdrBufferTooSmall;
    Output = (uint8_t *)Context->StagingBuffer;
    if (Offset > Slot->Inode.Size ||
        (uint64_t)Length > Slot->Inode.Size - Offset ||
        !BtrfsFsAddU64(Offset, (uint64_t)Length, &End))
        return Rc95LdrOutOfBounds;

    while (Remaining)
    {
        RC95LDR_BTRFS_FILE_EXTENT Extent;
        RC95LDR_STATUS S = Rc95LdrBtrfsReadFileExtent(Context->Volume,
            Slot->InodeId, Cursor, Context->Scratch, Context->ScratchLength,
            &Extent, Context->ItemBuffer, Context->ItemBufferLength,
            Context->InlineBuffer, Context->InlineBufferLength);

        if (S == Rc95LdrNotFound)
        {
            uint64_t NextOffset = 0;
            size_t NextLength = 0;
            S = Rc95LdrBtrfsFindFsTreeItemAfter(Context->Volume,
                Slot->InodeId, BTRFS_EXTENT_DATA_KEY, Cursor,
                Context->Scratch, Context->ScratchLength,
                Context->ItemBuffer, Context->ItemBufferLength,
                &NextLength, &NextOffset);
            if (S == Rc95LdrNotFound)
                NextOffset = End;
            else if (S != Rc95LdrSuccess)
                return S;
            if (NextOffset <= Cursor) return Rc95LdrCorruptData;
            {
                uint64_t Hole = NextOffset - Cursor;
                size_t Copy = Hole > (uint64_t)Remaining ?
                              Remaining : (size_t)Hole;
                memset(Output, 0, Copy);
                Output += Copy;
                Cursor += Copy;
                Remaining -= Copy;
            }
            continue;
        }
        if (S != Rc95LdrSuccess) return S;

        {
            uint64_t ExtentEnd;
            uint64_t Available;
            size_t Copy;
            if (!BtrfsFsAddU64(Extent.FileOffset, Extent.NumBytes, &ExtentEnd) ||
                ExtentEnd <= Cursor)
                return Rc95LdrCorruptData;
            Available = ExtentEnd - Cursor;
            Copy = Available > (uint64_t)Remaining ?
                   Remaining : (size_t)Available;
            if (Extent.Type == 0u)
            {
                size_t InlineOffset = (size_t)(Cursor - Extent.FileOffset);
                if (InlineOffset > Extent.InlineLength ||
                    Copy > Extent.InlineLength - InlineOffset)
                    return Rc95LdrCorruptData;
                memcpy(Output, (const uint8_t *)Context->InlineBuffer +
                       InlineOffset, Copy);
            }
            else
            {
                S = Rc95LdrBtrfsReadExtentData(Context->Volume, &Extent,
                    Cursor, Copy, Context->Scratch, Context->ScratchLength,
                    Output);
                if (S != Rc95LdrSuccess) return S;
            }
            Output += Copy;
            Cursor += Copy;
            Remaining -= Copy;
        }
    }
    /* Transaction commit: the caller sees bytes only after the full read. */
    memcpy(Buffer, Context->StagingBuffer, Length);
    return Rc95LdrSuccess;
}

static void
BtrfsFsClose(void *Opaque, void *Handle)
{
    RC95LDR_BTRFS_FILESYSTEM_CONTEXT *Context = Opaque;
    RC95LDR_BTRFS_FILE_HANDLE *Slot = BtrfsFindHandle(Context, Handle);
    if (Slot) memset(Slot, 0, sizeof(*Slot));
}

static int
BtrfsFsAddU64(uint64_t A, uint64_t B, uint64_t *Result)
{
    if (B > UINT64_MAX - A) return 0;
    *Result = A + B;
    return 1;
}

RC95LDR_STATUS
Rc95LdrBtrfsInitializeFileSystem(
    RC95LDR_BTRFS_FILESYSTEM_CONTEXT *Context,
    RC95LDR_BTRFS_VOLUME *Volume,
    uint8_t *Scratch,
    size_t ScratchLength,
    void *ItemBuffer,
    size_t ItemBufferLength,
    void *InlineBuffer,
    size_t InlineBufferLength,
    void *StagingBuffer,
    size_t StagingBufferLength,
    RC95LDR_FILESYSTEM *FileSystem)
{
    if (!Context || !Volume || !Volume->Mounted || !Volume->RootTreeLoaded ||
        !Scratch || ScratchLength < Volume->Superblock.NodeSize ||
        !ItemBuffer || ItemBufferLength < Volume->Superblock.NodeSize ||
        !InlineBuffer || InlineBufferLength < Volume->Superblock.NodeSize ||
        !StagingBuffer || !StagingBufferLength || !FileSystem)
        return Rc95LdrInvalidParameter;

    memset(Context, 0, sizeof(*Context));
    Context->Volume = Volume;
    Context->Scratch = Scratch;
    Context->ScratchLength = ScratchLength;
    Context->ItemBuffer = ItemBuffer;
    Context->ItemBufferLength = ItemBufferLength;
    Context->InlineBuffer = InlineBuffer;
    Context->InlineBufferLength = InlineBufferLength;
    Context->StagingBuffer = StagingBuffer;
    Context->StagingBufferLength = StagingBufferLength;

    memset(FileSystem, 0, sizeof(*FileSystem));
    FileSystem->FileSystemContext = Context;
    FileSystem->OpenRead = BtrfsFsOpenRead;
    FileSystem->ReadAt = BtrfsFsReadAt;
    FileSystem->Close = BtrfsFsClose;
    return Rc95LdrSuccess;
}
