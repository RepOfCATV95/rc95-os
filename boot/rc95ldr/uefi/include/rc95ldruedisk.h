#ifndef _RC95LDR_UEDISK_H_
#define _RC95LDR_UEDISK_H_
#include "rc95ldrdiscover.h"
#include "rc95ldr_fat32.h"
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

typedef RC95LDR_STATUS (*RC95LDR_UEFI_WRITE_BLOCKS)(
    void *Protocol, uint32_t MediaId, uint64_t Lba,
    size_t ByteCount, const void *Buffer);

typedef struct _RC95LDR_UEFI_BLOCK_IO {
    void *Protocol;
    RC95LDR_UEFI_MEDIA Media;
    RC95LDR_UEFI_READ_BLOCKS ReadBlocks;
    RC95LDR_UEFI_WRITE_BLOCKS WriteBlocks;
} RC95LDR_UEFI_BLOCK_IO;

typedef struct _RC95LDR_UEFI_FIRMWARE_OPS {
    void *Context;
    RC95LDR_STATUS (*GetHandleCount)(void*, size_t*);
    RC95LDR_STATUS (*GetHandleAt)(void*, size_t, void**);
    RC95LDR_STATUS (*OpenBlockIo)(void*, void*, RC95LDR_UEFI_BLOCK_IO*);
    RC95LDR_STATUS (*GetDevicePath)(void*, void*, const uint8_t**, size_t*);
} RC95LDR_UEFI_FIRMWARE_OPS;

typedef struct _RC95LDR_UEFI_FAT32_VOLUME {
    RC95LDR_UEFI_BLOCK_IO BlockIo;
    RC95LDR_BLOCK_DEVICE Device;
    RC95LDR_FAT32 Fat32;
    uint32_t Mounted;
} RC95LDR_UEFI_FAT32_VOLUME;

RC95LDR_STATUS Rc95LdrUefiMountFat32Volume(
    const RC95LDR_UEFI_FIRMWARE_OPS *Ops,
    const RC95LDR_PARTITION_INFO *Partition,
    RC95LDR_UEFI_FAT32_VOLUME *Volume,
    RC95LDR_VOLUME *MountedVolume);
void Rc95LdrUefiUnmountFat32Volume(RC95LDR_UEFI_FAT32_VOLUME *Volume);

/* Parses a UEFI hard-drive media node. SignatureType is 1 for an MBR
 * signature and 2 for a GPT unique partition GUID. Signature is the raw
 * 16-byte device-path signature field. */
RC95LDR_STATUS Rc95LdrParseUefiDevicePathEx(
    const uint8_t *Path, size_t PathLength, uint32_t *PartitionNumber,
    uint64_t *StartLba, uint64_t *BlockCount, uint8_t *PartitionFormat,
    uint8_t *SignatureType, uint8_t Signature[16]);

/* Compatibility parser: returns the GPT signature field (zero for MBR). */
RC95LDR_STATUS Rc95LdrParseUefiDevicePath(
    const uint8_t *Path, size_t PathLength, uint32_t *PartitionNumber,
    uint64_t *StartLba, uint64_t *BlockCount, uint8_t PartitionGuid[16]);

/* Verifies that a logical Block I/O handle is exactly the GPT partition
 * recorded by discovery. This prevents a stale/unmatched handle index from
 * being used to open the BCD-selected loader volume. */
RC95LDR_STATUS Rc95LdrUefiValidatePartitionHandle(
    const RC95LDR_PARTITION_INFO *Partition,
    const RC95LDR_UEFI_BLOCK_IO *BlockIo,
    const uint8_t *DevicePath, size_t DevicePathLength);

/* Enumerates Block I/O handles, parses GPT or primary MBR on whole-disk
 * handles, identifies FAT32/NTFS from boot sectors and probes otherwise-
 * unknown partitions for Btrfs through VENEER. Partition handles are
 * associated by complete hard-drive device-path identity. Extended/logical
 * MBR partitions are skipped until EBR-chain validation is implemented.
 * Scratch is caller-owned; Btrfs probing uses a separate aligned local buffer. */
RC95LDR_STATUS Rc95LdrUefiEnumerateGptPartitions(
    const RC95LDR_UEFI_FIRMWARE_OPS *Ops,
    RC95LDR_PARTITION_INFO *Partitions, size_t PartitionCapacity,
    size_t *PartitionCount, void *Scratch, size_t ScratchLength);

/* Enumerates GPT and primary MBR partitions. Extended/logical MBR partitions
 * are deliberately skipped until EBR-chain validation is implemented. */
RC95LDR_STATUS Rc95LdrUefiEnumeratePartitions(
    const RC95LDR_UEFI_FIRMWARE_OPS *Ops,
    RC95LDR_PARTITION_INFO *Partitions, size_t PartitionCapacity,
    size_t *PartitionCount, void *Scratch, size_t ScratchLength);

#ifdef __cplusplus
}
#endif
#endif
