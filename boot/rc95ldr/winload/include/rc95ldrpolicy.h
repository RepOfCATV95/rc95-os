#ifndef _RC95LDR_POLICY_H_
#define _RC95LDR_POLICY_H_
#include "rc95bcddevice.h"
struct _RC95LDR_WINLOAD_REQUEST;
#ifdef __cplusplus
extern "C" {
#endif

#define RC95LDR_MAX_SYSTEM_ROOT_CHARS 260u
#define RC95LDR_MAX_BCD_ELEMENTS 64u
#define RC95LDR_MAX_BCD_PAYLOAD_BYTES 4096u
#define RC95LDR_MAX_BCD_STRING_CHARS 2048u

typedef struct _RC95LDR_BCD_CANDIDATE_ELEMENT {
    uint32_t ElementId, EncodedType, RegistryValueType;
    RC95BCD_ELEMENT_KIND Kind;
    uint32_t PayloadOffset, PayloadLength, StringOffset, StringCharacters;
    uint64_t IntegerValue;
    uint32_t BooleanValue;
    RC95BCD_GUID ObjectGuid;
    uint32_t ItemCount, DeviceType, AdditionalOptions;
} RC95LDR_BCD_CANDIDATE_ELEMENT;

typedef struct _RC95LDR_BCD_BOOT_CANDIDATE {
    RC95BCD_GUID ObjectId;
    uint32_t ObjectCell; /* BCD Objects\\{GUID} key cell in the source hive. */
    uint32_t DisplayOrder;
    uint8_t IsDefault;
    uint8_t IsWindowsLoader;
    uint8_t IsEnabled;
    uint8_t Reserved;
    uint32_t GraphicsModeDisabled; /* Selected OS-loader BCD Boolean; absent defaults to zero. */
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
    RC95LDR_BCD_CANDIDATE_ELEMENT Elements[RC95LDR_MAX_BCD_ELEMENTS];
    uint32_t ElementCount;
    uint8_t ElementPayloadStorage[RC95LDR_MAX_BCD_PAYLOAD_BYTES];
    uint32_t ElementPayloadBytes;
    uint16_t ElementStringStorage[RC95LDR_MAX_BCD_STRING_CHARS];
    uint32_t ElementStringCharacters;
    uint16_t BootOptionsStorage[RC95LDR_MAX_BCD_STRING_CHARS];
    uint32_t BootOptionsCharacters;
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
    /* Exact OS-loader options copied from the selected BCD object. */
    uint16_t BootOptionsUtf16[RC95LDR_MAX_BCD_STRING_CHARS];
    uint32_t BootOptionsCharacters;
    /* Resolved ramdiskoptions object from the same BCD hive as BootEntryId. */
    char RamdiskOptionsGuid[39];
    uint32_t RamdiskOptionsObjectCell;
    uint8_t HasRamdiskOptions;
    uint8_t HasRamdiskSdi;
    uint8_t RamdiskSdiDeviceIsOsVolume;
    uint8_t ReservedRamdisk;
    /* Decoded BcdLibraryString_RamdiskSdiPath, root-relative to the
       partition named by BcdLibraryDevice_RamdiskSdiDevice. */
    uint16_t RamdiskSdiPathUtf16[260];
    uint32_t RamdiskSdiPathCharacters;
    /* Loader-pool copy owned by the UEFI boot transaction. It remains valid
       through BuildBootRequest/ReleaseBootRequest and is freed on return. */
    void *RamdiskSdiImage;
    size_t RamdiskSdiImageSize;
    /* Parsed Windows SDI section table and file-relative RAM-disk extent. */
    uint32_t RamdiskSdiMemoryType, RamdiskSdiPageAlignmentFactor;
    uint32_t RamdiskSdiSectionCount, RamdiskSdiRamdiskKind;
    uint8_t RamdiskSdiRuntimeGuid[16], RamdiskSdiPartitionType;
    uint8_t ReservedSdiLayout[7];
    uint64_t RamdiskSdiBootCodeOffset, RamdiskSdiBootCodeSize;
    uint64_t RamdiskSdiDataOffset, RamdiskSdiDataSize;
    uint32_t RamdiskSdiSectionTypes[8], RamdiskSdiSectionAttributes[8];
    uint64_t RamdiskSdiSectionOffsets[8], RamdiskSdiSectionSizes[8];
    uint32_t GraphicsModeDisabled; /* 0 = graphics enabled; 1 = text-only. */
} RC95LDR_WINDOWS_INSTALLATION;

/* RC95-OS bootres.dll admission policy; only enabled RC95 graphics may load it. */
int Rc95LdrShouldLoadBootResource(uint32_t OsFamily,
                                  uint32_t GraphicsModePolicyValid,
                                  uint32_t GraphicsModeDisabled);

/* Removes structurally present but unusable loader entries in-place.
 * A retained candidate has decodable partition devices and uniquely resolved
 * boot/NTFS OS partitions. Element views continue to reference the BCD hive. */
RC95LDR_STATUS Rc95LdrFilterWindowsBootCandidates(
    const RC95LDR_PARTITION_INFO *Partitions,
    size_t PartitionCount,
    RC95LDR_BCD_BOOT_CANDIDATE *Candidates,
    size_t *CandidateCount);

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

/* Resolve one exact menu-selected BCD candidate; never reselect the default. */
/* Copy the exact selected object's BCD graphics policy into WINLOAD. */
RC95LDR_STATUS Rc95LdrApplyInstallationGraphicsPolicy(
    const RC95LDR_WINDOWS_INSTALLATION *Installation,
    struct _RC95LDR_WINLOAD_REQUEST *Request);

/* True only when the RC95-OS BCD policy explicitly permits bootres.dll. */
int Rc95LdrShouldLoadBootResource(uint32_t OsFamily,
    uint32_t GraphicsModePolicyValid, uint32_t GraphicsModeDisabled);

RC95LDR_STATUS Rc95LdrResolveWindowsBootCandidate(
    const RC95LDR_PARTITION_INFO *Partitions,
    size_t PartitionCount,
    const RC95LDR_BCD_BOOT_CANDIDATE *Candidate,
    RC95LDR_WINDOWS_INSTALLATION *Installation);

#ifdef __cplusplus
}
#endif
#endif
