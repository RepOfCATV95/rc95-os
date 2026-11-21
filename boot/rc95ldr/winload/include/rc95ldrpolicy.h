#ifndef _RC95LDR_POLICY_H_
#define _RC95LDR_POLICY_H_
#include "rc95bcddevice.h"
#ifdef __cplusplus
extern "C" {
#endif

#define RC95LDR_MAX_SYSTEM_ROOT_CHARS 260u

typedef struct _RC95LDR_BCD_BOOT_CANDIDATE {
    RC95BCD_GUID ObjectId;
    uint32_t DisplayOrder;
    uint8_t IsDefault;
    uint8_t IsWindowsLoader;
    uint8_t IsEnabled;
    uint8_t Reserved;
    RC95BCD_ELEMENT_VIEW DeviceElementStorage;
    RC95BCD_ELEMENT_VIEW OsDeviceElementStorage;
    const RC95BCD_ELEMENT_VIEW *DeviceElement;
    const RC95BCD_ELEMENT_VIEW *OsDeviceElement;
    uint16_t LoaderPathStorage[RC95LDR_MAX_SYSTEM_ROOT_CHARS];
    const uint16_t *LoaderPathUtf16;
    uint32_t LoaderPathCharacters;
    uint16_t SystemRootStorage[RC95LDR_MAX_SYSTEM_ROOT_CHARS];
    const uint16_t *SystemRootUtf16;
    uint32_t SystemRootCharacters;
} RC95LDR_BCD_BOOT_CANDIDATE;

typedef struct _RC95LDR_WINDOWS_INSTALLATION {
    RC95BCD_GUID BootEntryId;
    size_t PartitionIndex;
    size_t OsPartitionIndex;
    RC95LDR_PARTITION_INFO BootPartition;
    RC95LDR_PARTITION_INFO OsPartition;
    uint16_t LoaderPathUtf16[RC95LDR_MAX_SYSTEM_ROOT_CHARS];
    uint32_t LoaderPathCharacters;
    uint16_t SystemRootUtf16[RC95LDR_MAX_SYSTEM_ROOT_CHARS];
    uint32_t SystemRootCharacters;
} RC95LDR_WINDOWS_INSTALLATION;

/* Selects the enabled Windows loader by valid default first, then display
 * order. device resolves independently to a supported partition; osdevice
 * must resolve uniquely to NTFS. The selected record is the storage contract
 * passed to winload. */
RC95LDR_STATUS Rc95LdrSelectWindowsInstallation(
    const RC95LDR_PARTITION_INFO *Partitions,
    size_t PartitionCount,
    const RC95LDR_BCD_BOOT_CANDIDATE *Candidates,
    size_t CandidateCount,
    RC95LDR_WINDOWS_INSTALLATION *Installation);

#ifdef __cplusplus
}
#endif
#endif
