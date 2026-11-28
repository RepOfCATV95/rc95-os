#ifndef _RC95LDR_DISCOVERY_H_
#define _RC95LDR_DISCOVERY_H_
#include "rc95ldr.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef enum _RC95LDR_PARTITION_STYLE {
    Rc95LdrPartitionGpt = 1,
    Rc95LdrPartitionMbr = 2
} RC95LDR_PARTITION_STYLE;

typedef enum _RC95LDR_FILESYSTEM_KIND {
    Rc95LdrFsUnknown = 0,
    Rc95LdrFsFat32 = 1,
    Rc95LdrFsNtfs = 2,
    Rc95LdrFsBtrfs = 3
} RC95LDR_FILESYSTEM_KIND;

typedef struct _RC95LDR_PARTITION_INFO {
    RC95LDR_PARTITION_STYLE Style;
    RC95LDR_FILESYSTEM_KIND FileSystem;
    uint8_t TypeGuid[16];       /* GPT on-disk GUID byte order */
    uint8_t DiskGuid[16];       /* GPT disk GUID from the header */
    uint8_t UniqueGuid[16];     /* GPT on-disk GUID byte order */
    uint32_t MbrDiskSignature;
    uint64_t StartLba;
    uint64_t BlockCount;
    uint32_t BlockSize;
    uint32_t FirmwareHandleIndex;       /* Matching logical partition handle */
    uint32_t ParentDiskHandleIndex;      /* Whole-disk Block I/O handle */
    uint32_t FirmwarePartitionNumber;   /* UEFI hard-drive device-path number */
    uint8_t MbrType;                    /* Zero for GPT; original MBR type byte otherwise. */
} RC95LDR_PARTITION_INFO;

typedef enum _RC95LDR_PARTITION_ID_KIND {
    Rc95LdrPartitionIdGpt = 1,
    Rc95LdrPartitionIdMbr = 2
} RC95LDR_PARTITION_ID_KIND;

typedef struct _RC95LDR_PARTITION_IDENTITY {
    RC95LDR_PARTITION_ID_KIND Kind;
    uint8_t GptDiskGuid[16];
    uint8_t GptUniqueGuid[16];
    uint32_t MbrDiskSignature;
    uint64_t MbrPartitionOffsetBytes;
} RC95LDR_PARTITION_IDENTITY;

/* Locate a GPT ESP by its type GUID; never infer ESP from a FAT label. */
RC95LDR_STATUS Rc95LdrFindEsp(const RC95LDR_PARTITION_INFO*, size_t, size_t*);
/* Resolve the exact partition named by a decoded BCD device descriptor.
 * Ambiguous identities fail closed. RequiredFs may be Rc95LdrFsUnknown. */
RC95LDR_STATUS Rc95LdrResolvePartition(
    const RC95LDR_PARTITION_INFO*, size_t,
    const RC95LDR_PARTITION_IDENTITY*, RC95LDR_FILESYSTEM_KIND,
    size_t*);

#ifdef __cplusplus
}
#endif
#endif
