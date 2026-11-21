#pragma once
#include <rc95ldruedisk.h>
#include <rc95ldrdiscoveryuefi.h>
#include <rc95ldr_fat32.h>
#include <rc95ldrfs.h>

#define RC95LDR_UEFI_MOUNT_SLOTS 4u

typedef struct _RC95LDR_FREELDR_UEFI_MOUNT {
    RC95LDR_UEFI_BLOCK_IO BlockIo;
    RC95LDR_BLOCK_DEVICE BlockDevice;
    RC95LDR_FAT32 Fat32;
    RC95LDR_FREELDR_VOLUME Ntfs;
    RC95LDR_VOLUME Volume;
    uint8_t Kind;
    uint8_t InUse;
} RC95LDR_FREELDR_UEFI_MOUNT;

typedef struct _RC95LDR_FREELDR_UEFI_CONTEXT {
    RC95LDR_UEFI_FIRMWARE_OPS Firmware;
    void *Scratch;
    size_t ScratchLength;
    RC95LDR_FREELDR_UEFI_MOUNT Mounts[RC95LDR_UEFI_MOUNT_SLOTS];
} RC95LDR_FREELDR_UEFI_CONTEXT;

/* Returns firmware protocol adapters after UefiInitializeBootDevices. */
RC95LDR_STATUS
Rc95LdrFreeldrUefiGetFirmwareOps(RC95LDR_UEFI_FIRMWARE_OPS *Ops);

/* Builds discovery callbacks using firmware GPT enumeration, the read-only
 * FAT32 adapter for ESPs and FreeLoader ARC's NTFS filesystem dispatch. */
RC95LDR_STATUS
Rc95LdrFreeldrUefiInitializeDiscovery(
    RC95LDR_FREELDR_UEFI_CONTEXT *Context,
    void *Scratch,
    size_t ScratchLength,
    RC95LDR_UEFI_DISCOVERY_OPS *DiscoveryOps);

RC95LDR_STATUS
Rc95LdrFreeldrUefiReadBlockDevice(
    void *Context,uint64_t Lba,uint32_t BlockCount,void *Buffer,size_t BufferLength);

RC95LDR_STATUS
Rc95LdrFreeldrUefiBuildArcPartitionPath(
    const RC95LDR_PARTITION_INFO *Partition,char *Buffer,size_t BufferLength);

/* Convenience entry point: initializes the firmware/filesystem callbacks and
 * executes ESP -> BCD -> NTFS discovery. Call only after
 * UefiInitializeBootDevices; all scratch arrays/buffers remain caller-owned. */
RC95LDR_STATUS
Rc95LdrFreeldrUefiDiscoverWindows(
    RC95LDR_FREELDR_UEFI_CONTEXT *Context,
    void *Scratch, size_t ScratchLength,
    RC95LDR_PARTITION_INFO *Partitions, size_t PartitionCapacity,
    RC95LDR_BCD_BOOT_CANDIDATE *Candidates, size_t CandidateCapacity,
    void *HiveBuffer, size_t HiveCapacity,
    RC95LDR_WINDOWS_INSTALLATION *Installation,
    RC95LDR_VOLUME *WindowsVolume,
    size_t *EspPartitionIndex);
