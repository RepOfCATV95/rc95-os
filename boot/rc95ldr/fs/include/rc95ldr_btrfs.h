#ifndef _RC95LDR_BTRFS_H_
#define _RC95LDR_BTRFS_H_

#include "rc95ldr.h"
#include "veneer.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Read-only Btrfs volume binding.
 *
 * Keep this object at a stable address until unmounted: PartitionDevice's
 * DeviceContext points back to it. The underlying block-device context
 * remains owned by the platform.
 *
 * A successful mount validates the superblock and binds the partition-relative
 * block window. After loading both trees, initialize the caller-owned read-only
 * filesystem context and use Rc95LdrBtrfsBindVolumeWithFileSystem().
 */
#define RC95LDR_BTRFS_MAX_CHUNKS 128u

typedef struct _RC95LDR_BTRFS_CHUNK
{
    uint64_t Logical;
    uint64_t Length;
    uint64_t Physical;
    uint64_t Type;
    uint64_t DeviceId;
} RC95LDR_BTRFS_CHUNK;

typedef struct _RC95LDR_BTRFS_INODE
{
    uint64_t Generation;
    uint64_t TransactionId;
    uint64_t Size;
    uint64_t AllocatedBytes;
    uint64_t BlockGroup;
    uint64_t Flags;
    uint64_t Sequence;
    uint32_t LinkCount;
    uint32_t UserId;
    uint32_t GroupId;
    uint32_t Mode;
} RC95LDR_BTRFS_INODE;

typedef struct _RC95LDR_BTRFS_FILE_EXTENT
{
    uint64_t FileOffset;
    uint64_t Generation;
    uint64_t RamBytes;
    uint64_t DiskBytenr;
    uint64_t DiskNumBytes;
    uint64_t ExtentOffset;
    uint64_t NumBytes;
    uint8_t Type; /* 0 inline, 1 regular, 2 preallocated */
    size_t InlineLength;
} RC95LDR_BTRFS_FILE_EXTENT;

typedef struct _RC95LDR_BTRFS_VOLUME
{
    RC95LDR_BLOCK_DEVICE DeviceStorage;
    RC95LDR_BLOCK_DEVICE PartitionDevice;
    uint64_t PartitionStartLba;
    uint64_t PartitionBlockCount;
    uint64_t SelectedSuperblockOffset;
    RC95LDR_BTRFS_SUPERBLOCK Superblock;
    RC95LDR_BTRFS_CHUNK Chunks[RC95LDR_BTRFS_MAX_CHUNKS];
    uint32_t ChunkCount;
    uint64_t FsTreeRoot;
    uint64_t FsTreeGeneration;
    uint8_t FsTreeLevel;
    uint8_t ChunkTreeLoaded;
    uint8_t RootTreeLoaded;
    uint8_t Mounted;
} RC95LDR_BTRFS_VOLUME;

RC95LDR_STATUS
Rc95LdrBtrfsMountPartition(
    RC95LDR_BTRFS_VOLUME *Volume,
    const RC95LDR_BLOCK_DEVICE *Device,
    uint64_t PartitionStartLba,
    uint64_t PartitionBlockCount,
    uint8_t *Scratch,
    size_t ScratchLength);

void Rc95LdrBtrfsUnmount(RC95LDR_BTRFS_VOLUME *Volume);

/* Load and validate the chunk tree using the validated SYSTEM chunk array
 * as the bootstrap address map. Scratch must be at least NodeSize bytes. */
RC95LDR_STATUS
Rc95LdrBtrfsLoadChunkTree(
    RC95LDR_BTRFS_VOLUME *Volume,
    uint8_t *Scratch,
    size_t ScratchLength);

/* Locate and validate the default filesystem tree (subvolume ID 5) in
 * the Btrfs root tree. Requires a successfully loaded chunk tree. */
RC95LDR_STATUS
Rc95LdrBtrfsLoadRootTree(
    RC95LDR_BTRFS_VOLUME *Volume,
    uint8_t *Scratch,
    size_t ScratchLength);

/* Find an item in the selected default filesystem tree (subvolume 5).
 * ItemBuffer receives the raw item payload. On Rc95LdrBufferTooSmall,
 * ItemLength is set to the required payload size. Scratch must be NodeSize. */
RC95LDR_STATUS
Rc95LdrBtrfsFindFsTreeItem(
    RC95LDR_BTRFS_VOLUME *Volume,
    uint64_t ObjectId,
    uint8_t KeyType,
    uint64_t KeyOffset,
    uint8_t *Scratch,
    size_t ScratchLength,
    void *ItemBuffer,
    size_t ItemBufferLength,
    size_t *ItemLength);

/* Read and validate an inode item from the selected default filesystem tree. */
RC95LDR_STATUS
Rc95LdrBtrfsFindFsTreeItemAtOrBefore(
    RC95LDR_BTRFS_VOLUME *Volume,
    uint64_t ObjectId,
    uint8_t KeyType,
    uint64_t KeyOffset,
    uint8_t *Scratch,
    size_t ScratchLength,
    void *ItemBuffer,
    size_t ItemBufferLength,
    size_t *ItemLength,
    uint64_t *FoundOffset);

/* Find the least matching key strictly after KeyOffset. The search is
 * bounded by BTRFS_MAX_TREE_NODES and crosses leaf boundaries safely. */
RC95LDR_STATUS
Rc95LdrBtrfsFindFsTreeItemAfter(
    RC95LDR_BTRFS_VOLUME *Volume,
    uint64_t ObjectId,
    uint8_t KeyType,
    uint64_t KeyOffset,
    uint8_t *Scratch,
    size_t ScratchLength,
    void *ItemBuffer,
    size_t ItemBufferLength,
    size_t *ItemLength,
    uint64_t *FoundOffset);

RC95LDR_STATUS
Rc95LdrBtrfsReadInode(
    RC95LDR_BTRFS_VOLUME *Volume,
    uint64_t InodeId,
    uint8_t *Scratch,
    size_t ScratchLength,
    RC95LDR_BTRFS_INODE *Inode);


/* Resolve one exact UTF-8 name in a directory's DIR_ITEM collision bucket.
 * Name matching is byte-exact; case folding and path traversal are separate
 * policy layers. The caller supplies NodeSize scratch storage. */
RC95LDR_STATUS
Rc95LdrBtrfsFindDirectoryEntry(
    RC95LDR_BTRFS_VOLUME *Volume,
    uint64_t DirectoryId,
    const uint8_t *Name,
    size_t NameLength,
    uint8_t *Scratch,
    size_t ScratchLength,
    uint64_t *InodeId,
    uint8_t *FileType,
    uint64_t *LocationOffset);

/* Resolve a slash-separated path relative to StartInodeId. Leading slashes
 * are accepted; dot/dot-dot components and symlink following are rejected.
 * The caller supplies NodeSize scratch storage. */
RC95LDR_STATUS
Rc95LdrBtrfsResolvePath(
    RC95LDR_BTRFS_VOLUME *Volume,
    uint64_t StartInodeId,
    const uint8_t *Path,
    size_t PathLength,
    uint8_t *Scratch,
    size_t ScratchLength,
    uint64_t *ResolvedInodeId,
    RC95LDR_BTRFS_INODE *ResolvedInode);

/* Decode one EXTENT_DATA item at the exact file-offset key. Inline data
 * is copied to InlineBuffer; regular/preallocated extents return validated
 * disk mapping metadata. Compression and encryption are rejected. */
/* Read a byte range from a validated regular/preallocated extent.
 * Scratch must hold one device block; Inline extents are supplied separately. */
RC95LDR_STATUS
Rc95LdrBtrfsReadExtentData(
    RC95LDR_BTRFS_VOLUME *Volume,
    const RC95LDR_BTRFS_FILE_EXTENT *Extent,
    uint64_t FileOffset,
    size_t Length,
    uint8_t *Scratch,
    size_t ScratchLength,
    void *Output);

RC95LDR_STATUS
Rc95LdrBtrfsReadFileExtent(
    RC95LDR_BTRFS_VOLUME *Volume,
    uint64_t InodeId,
    uint64_t FileOffset,
    uint8_t *Scratch,
    size_t ScratchLength,
    RC95LDR_BTRFS_FILE_EXTENT *Extent,
    void *ItemBuffer,
    size_t ItemBufferLength,
    void *InlineBuffer,
    size_t InlineBufferLength);

/* Translate a logical filesystem byte address to a partition-relative
 * physical byte address. Requires a successfully loaded chunk tree. */
RC95LDR_STATUS
Rc95LdrBtrfsMapLogical(
    const RC95LDR_BTRFS_VOLUME *Volume,
    uint64_t Logical,
    uint64_t Length,
    uint64_t *Physical);

const RC95LDR_BTRFS_SUPERBLOCK *
Rc95LdrBtrfsGetSuperblock(const RC95LDR_BTRFS_VOLUME *Volume);


#define RC95LDR_BTRFS_FILE_HANDLES 8u
typedef struct _RC95LDR_BTRFS_FILE_HANDLE
{
    uint64_t InodeId;
    RC95LDR_BTRFS_INODE Inode;
    uintptr_t Generation;
    uint8_t InUse;
} RC95LDR_BTRFS_FILE_HANDLE;

/* Caller-owned work buffers keep the boot filesystem allocation-free.
 * Scratch must hold NodeSize; ItemBuffer and InlineBuffer should each be
 * at least NodeSize bytes. StagingBuffer is the transaction workspace:
 * every nonempty ReadAt must fit in StagingBufferLength. It must not overlap
 * the caller's ReadAt destination. A read is committed to the destination
 * only after every requested byte has been obtained successfully. The
 * context and buffers outlive open handles. */
typedef struct _RC95LDR_BTRFS_FILESYSTEM_CONTEXT
{
    RC95LDR_BTRFS_VOLUME *Volume;
    uint8_t *Scratch;
    size_t ScratchLength;
    void *ItemBuffer;
    size_t ItemBufferLength;
    void *InlineBuffer;
    size_t InlineBufferLength;
    void *StagingBuffer;
    size_t StagingBufferLength;
    uintptr_t NextHandleGeneration;
    RC95LDR_BTRFS_FILE_HANDLE Handles[RC95LDR_BTRFS_FILE_HANDLES];
} RC95LDR_BTRFS_FILESYSTEM_CONTEXT;

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
    RC95LDR_FILESYSTEM *FileSystem);

RC95LDR_STATUS
Rc95LdrBtrfsBindVolumeWithFileSystem(
    RC95LDR_BTRFS_VOLUME *Btrfs,
    RC95LDR_VOLUME *Volume,
    const RC95LDR_FILESYSTEM *FileSystem);

RC95LDR_STATUS
Rc95LdrBtrfsBindVolume(
    RC95LDR_BTRFS_VOLUME *Btrfs,
    RC95LDR_VOLUME *Volume);

#ifdef __cplusplus
}
#endif

#endif
