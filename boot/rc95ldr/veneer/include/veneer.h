#ifndef _RC95LDR_VENEER_H_
#define _RC95LDR_VENEER_H_
#include "rc95ldr.h"
#ifdef __cplusplus
extern "C" {
#endif

#define RC95LDR_BTRFS_SUPERBLOCK_SIZE 4096u
#define RC95LDR_BTRFS_SUPERBLOCK_OFFSET 65536ull
#define RC95LDR_BTRFS_MAGIC 0x4d5f53665248425full /* "_BHRfS_M" */
#define RC95LDR_BTRFS_CSUM_CRC32C 0u
#define RC95LDR_BTRFS_MAX_SYSTEM_CHUNK_ARRAY 2048u

typedef struct _RC95LDR_BTRFS_SUPERBLOCK {
    uint64_t Bytenr, Generation, Root, ChunkRoot, TotalBytes;
    uint64_t BytesUsed, NumDevices;
    uint32_t SectorSize, NodeSize, LeafSize, StripeSize;
    uint32_t SystemChunkArraySize;
    uint64_t IncompatFlags;
    uint16_t ChecksumType;
    uint8_t RootLevel, ChunkRootLevel;
    uint64_t DeviceId, DeviceBytes;
    uint8_t FsId[16];
    uint8_t SystemChunkArray[RC95LDR_BTRFS_MAX_SYSTEM_CHUNK_ARRAY];
} RC95LDR_BTRFS_SUPERBLOCK;

typedef struct _RC95LDR_VENEER_CONTEXT {
    const RC95LDR_BLOCK_DEVICE *Device;
    uint64_t PartitionStartLba;
    uint64_t PartitionBlockCount;
} RC95LDR_VENEER_CONTEXT;

/* The veneer is read-only. All I/O is partition-relative and goes through
 * RC95LDR's checked block-device contract. */

uint32_t Rc95LdrVeneerCrc32c(const void *Buffer, size_t Length);
RC95LDR_STATUS Rc95LdrVeneerValidateSuperblock(
    const uint8_t *Raw, size_t Length, uint64_t ExpectedBytenr,
    RC95LDR_BTRFS_SUPERBLOCK *Info);
RC95LDR_STATUS Rc95LdrVeneerDiscoverBtrfs(
    const RC95LDR_VENEER_CONTEXT *Context,
    uint8_t *Scratch, size_t ScratchLength,
    RC95LDR_BTRFS_SUPERBLOCK *Info,
    uint64_t *SelectedSuperblockOffset);

#ifdef __cplusplus
}
#endif
#endif
