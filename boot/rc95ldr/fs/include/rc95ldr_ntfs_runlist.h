#ifndef _RC95LDR_NTFS_RUNLIST_H_
#define _RC95LDR_NTFS_RUNLIST_H_
#include "rc95ldr.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct _RC95LDR_NTFS_RUN {
    uint64_t Vcn;
    uint64_t Lcn;
    uint64_t ClusterCount;
    uint8_t Sparse;
} RC95LDR_NTFS_RUN;
/* Merge one decoded extent into a VCN-ordered stream. The incoming extent
 * must begin exactly at NextVcn; physical aliases and capacity overflow fail. */
RC95LDR_STATUS Rc95LdrNtfsAppendRunlistExtent(
    const RC95LDR_NTFS_RUN *Extent, size_t ExtentCount,
    uint64_t ExpectedStartingVcn, uint64_t VolumeClusterCount,
    RC95LDR_NTFS_RUN *Runs, size_t RunCapacity,
    size_t *RunCount, uint64_t *NextVcn);
/* Decode NTFS mapping pairs. Output is partition-relative cluster runs.
 * Sparse runs are rejected unless AllowSparse is nonzero. A successful
 * parse requires an explicit zero terminator in the supplied byte range. */
RC95LDR_STATUS Rc95LdrNtfsDecodeRunlist(
    const uint8_t *MappingPairs,
    size_t MappingPairsLength,
    uint64_t StartingVcn,
    uint64_t VolumeClusterCount,
    int AllowSparse,
    RC95LDR_NTFS_RUN *Runs,
    size_t RunCapacity,
    size_t *RunCount,
    uint64_t *NextVcn);
#ifdef __cplusplus
}
#endif
#endif
