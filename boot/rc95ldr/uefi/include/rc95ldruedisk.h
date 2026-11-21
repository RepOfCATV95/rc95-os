#ifndef _RC95LDR_UEDISK_H_
#define _RC95LDR_UEDISK_H_
#include "rc95ldrdiscover.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _RC95LDR_UEFI_MEDIA {
    uint32_t MediaId;
    uint32_t BlockSize;
    uint32_t IoAlign;
    uint64_t LastBlock;
    uint8_t MediaPresent;
    uint8_t RemovableMedia;
    uint8_t LogicalPartition;
    uint8_t ReadOnly;
} RC95LDR_UEFI_MEDIA;

typedef RC95LDR_STATUS (*RC95LDR_UEFI_READ_BLOCKS)(
    void *Protocol, uint32_t MediaId, uint64_t Lba,
    size_t ByteCount, void *Buffer);

typedef struct _RC95LDR_UEFI_BLOCK_IO {
    void *Protocol;
    RC95LDR_UEFI_MEDIA Media;
    RC95LDR_UEFI_READ_BLOCKS ReadBlocks;
} RC95LDR_UEFI_BLOCK_IO;

typedef struct _RC95LDR_UEFI_FIRMWARE_OPS {
    void *Context;
    RC95LDR_STATUS (*GetHandleCount)(void*, size_t*);
    RC95LDR_STATUS (*GetHandleAt)(void*, size_t, void**);
    RC95LDR_STATUS (*OpenBlockIo)(void*, void*, RC95LDR_UEFI_BLOCK_IO*);
    RC95LDR_STATUS (*GetDevicePath)(void*, void*, const uint8_t**, size_t*);
} RC95LDR_UEFI_FIRMWARE_OPS;

/* Parses a UEFI device path and extracts a media hard-drive node when present.
 * GPT signature type 2 returns the unique partition GUID. */
RC95LDR_STATUS Rc95LdrParseUefiDevicePath(
    const uint8_t *Path, size_t PathLength, uint32_t *PartitionNumber,
    uint64_t *StartLba, uint64_t *BlockCount, uint8_t PartitionGuid[16]);

/* Enumerates Block I/O handles, parses GPT on whole-disk handles, identifies
 * FAT32/NTFS filesystems from their boot sectors and associates partition
 * handles by their device-path unique GUID. Scratch is caller-owned. */
RC95LDR_STATUS Rc95LdrUefiEnumerateGptPartitions(
    const RC95LDR_UEFI_FIRMWARE_OPS *Ops,
    RC95LDR_PARTITION_INFO *Partitions, size_t PartitionCapacity,
    size_t *PartitionCount, void *Scratch, size_t ScratchLength);

#ifdef __cplusplus
}
#endif
#endif
