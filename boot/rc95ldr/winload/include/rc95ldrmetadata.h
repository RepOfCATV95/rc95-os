#ifndef _RC95LDR_METADATA_H_
#define _RC95LDR_METADATA_H_
#include "rc95ldrloaderblock.h"
#ifdef __cplusplus
extern "C" {
#endif
#define RC95LDR_METADATA_VERSION 1u
#define RC95LDR_ARC_DISK_MAX 32u
typedef struct _RC95LDR_NLS_INPUT {
    const void *AnsiData;uint32_t AnsiSize;
    const void *OemData;uint32_t OemSize;
    const void *UnicodeData;uint32_t UnicodeSize;
} RC95LDR_NLS_INPUT;
typedef struct _RC95LDR_ARC_DISK_INPUT {
    uint32_t Signature,Checksum;
    uint8_t ValidPartitionTable,ExtendedInt13,IsGpt,Reserved;
    uint8_t GptSignature[16];
    const char *ArcName;
} RC95LDR_ARC_DISK_INPUT;
typedef struct _RC95LDR_METADATA_INPUT {
    uint32_t StructureSize,Version;
    RC95LDR_NLS_INPUT Nls;
    const RC95LDR_ARC_DISK_INPUT *Disks;
    uint32_t DiskCount;
    void *Buffer;size_t BufferSize;
} RC95LDR_METADATA_INPUT;
typedef struct _RC95LDR_METADATA_RESULT {
    RC95LDR_NT_PTR NlsData,ArcDiskInformation;
    size_t BytesUsed;
} RC95LDR_METADATA_RESULT;
RC95LDR_STATUS Rc95LdrBuildLoaderMetadata(const RC95LDR_METADATA_INPUT*,RC95LDR_METADATA_RESULT*);
#ifdef __cplusplus
}
#endif
#endif
