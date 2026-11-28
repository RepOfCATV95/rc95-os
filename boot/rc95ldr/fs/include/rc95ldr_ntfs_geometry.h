#ifndef _RC95LDR_NTFS_GEOMETRY_H_
#define _RC95LDR_NTFS_GEOMETRY_H_
#include "rc95ldr.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct _RC95LDR_NTFS_GEOMETRY {
    uint32_t BytesPerSector;
    uint32_t SectorsPerCluster;
    uint32_t BytesPerCluster;
    uint64_t TotalSectors;
    uint64_t ClusterCount;
    uint64_t MftLcn;
    uint64_t MftMirrorLcn;
    uint32_t FileRecordBytes;
    uint32_t IndexBufferBytes;
    uint64_t VolumeSerial;
} RC95LDR_NTFS_GEOMETRY;
/* Parse and validate one NTFS boot sector against its selected partition.
 * This is geometry validation only; it does not mount NTFS or read the MFT. */
RC95LDR_STATUS Rc95LdrNtfsParseGeometry(
    const void *BootSector,
    size_t BootSectorLength,
    uint32_t DeviceBlockSize,
    uint64_t PartitionBlockCount,
    RC95LDR_NTFS_GEOMETRY *Geometry);
#ifdef __cplusplus
}
#endif
#endif
