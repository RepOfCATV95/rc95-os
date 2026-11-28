#ifndef _RC95LDR_FAT32_H_
#define _RC95LDR_FAT32_H_
#include "rc95ldr.h"
#ifdef __cplusplus
extern "C" {
#endif
#define RC95LDR_FAT32_MAX_OPEN_FILES 8u
#define RC95LDR_FAT32_MAX_SECTOR_SIZE 4096u
typedef struct _RC95LDR_FAT32_FILE {
    uint32_t FirstCluster;
    uint32_t FileSize;
    uint8_t InUse;
} RC95LDR_FAT32_FILE;
/* The mounted object is self-referential through PartitionDevice and
 * Interface. Keep it at a stable address until unmounted; do not copy/move it. */
typedef struct _RC95LDR_FAT32 {
    /* Snapshot the descriptor; DeviceContext remains caller-owned. */
    RC95LDR_BLOCK_DEVICE DeviceStorage;
    const RC95LDR_BLOCK_DEVICE *Device;
    RC95LDR_BLOCK_DEVICE PartitionDevice;
    uint64_t PartitionStartLba;
    uint64_t PartitionBlockCount;
    uint32_t BytesPerSector;
    uint32_t SectorsPerCluster;
    uint32_t ReservedSectors;
    uint32_t FsInfoSector;
    uint32_t BackupBootSector;
    uint32_t FatStartSector;
    uint32_t FatSectors;
    uint32_t RootCluster;
    uint32_t DataStartSector;
    uint32_t ClusterCount;
    uint32_t FatCount;
    uint32_t ActiveFat;
    uint8_t FatMirroringEnabled;
    uint8_t Mounted;
    RC95LDR_FAT32_FILE Files[RC95LDR_FAT32_MAX_OPEN_FILES];
    RC95LDR_FILESYSTEM Interface;
} RC95LDR_FAT32;
RC95LDR_STATUS Rc95LdrFat32Mount(RC95LDR_FAT32*,const RC95LDR_BLOCK_DEVICE*,uint64_t);
RC95LDR_STATUS Rc95LdrFat32MountPartition(RC95LDR_FAT32*,const RC95LDR_BLOCK_DEVICE*,uint64_t,uint64_t);
void Rc95LdrFat32Unmount(RC95LDR_FAT32*);
const RC95LDR_FILESYSTEM *Rc95LdrFat32GetInterface(RC95LDR_FAT32*);
RC95LDR_STATUS Rc95LdrFat32BindVolume(RC95LDR_FAT32*,RC95LDR_VOLUME*);
/* Deletes a root-relative or volume-relative file only when the block device
 * provides write support. Directory removal and read-only media are rejected. */
RC95LDR_STATUS Rc95LdrFat32DeleteFile(RC95LDR_FAT32*,const char*);
#ifdef __cplusplus
}
#endif
#endif
