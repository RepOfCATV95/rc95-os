#ifndef _RC95LDR_UEFI_WINLOAD_FLOW_H_
#define _RC95LDR_UEFI_WINLOAD_FLOW_H_

#include "rc95ldrdiscoveryuefi.h"
/* Use the canonical shared WINLOAD ABI header explicitly; this avoids
 * basename collisions with firmware-specific entry headers in SDK trees. */
#include "../../winload/include/rc95ldrentry.h"
#include "rc95ldr_volumeprovider.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * BuildBootRequest and ReleaseBootRequest receive this context during the
 * synchronous boot transaction. The selected installation and mounted OS
 * volume remain valid until WINLOAD returns (a successful handoff never does).
 */
typedef struct _RC95LDR_UEFI_WINLOAD_FLOW_CONTEXT {
    void *CallerContext;
    const RC95LDR_WINDOWS_INSTALLATION *SelectedInstallation;
    /* Exact volume backing the active file-provider scope (OS volume or WIM). */
    const RC95LDR_VOLUME *FileProviderVolume;
    RC95LDR_BUILD_BOOT_REQUEST BuildBootRequest;
    RC95LDR_RELEASE_BOOT_REQUEST ReleaseBootRequest;
} RC95LDR_UEFI_WINLOAD_FLOW_CONTEXT;

/* Set WINLOAD's size-guarded filesystem attestation to match the provider
 * actually bound for this transaction. IsSelectedOsVolume is false for WIM. */
RC95LDR_STATUS Rc95LdrUefiSetWinloadProviderAttestation(
    RC95LDR_WINLOAD_PLATFORM *Platform,
    const RC95LDR_WINDOWS_INSTALLATION *Installation,
    int IsSelectedOsVolume);

/* Parse ramdisk= into a root-relative WIM path and canonical options GUID.
 * The comma-delimited GUID is mandatory for this API. */
RC95LDR_STATUS Rc95LdrUefiParseBcdRamdiskTarget(
    const RC95LDR_WINDOWS_INSTALLATION *Installation,
    char *Path, size_t PathCapacity, char GuidText[39]);
/* Resolve that GUID as an Objects\\{GUID} key in the supplied, already-loaded
 * BCD hive. The hive image must be the same image used for candidate discovery. */
RC95LDR_STATUS Rc95LdrUefiResolveBcdRamdiskOptions(
    const RC95LDR_WINDOWS_INSTALLATION *Installation,
    const void *HiveImage, size_t HiveImageSize,
    uint32_t *ObjectCell);
#define RC95LDR_SDI_MAX_SECTIONS 8u
typedef enum _RC95LDR_SDI_RAMDISK_KIND {
    Rc95LdrSdiRamdiskNone=0, Rc95LdrSdiRamdiskPartition=1, Rc95LdrSdiRamdiskDisk=2
} RC95LDR_SDI_RAMDISK_KIND;
typedef struct _RC95LDR_SDI_SECTION {
    uint32_t Type, Attributes;
    uint64_t Offset, Size;
} RC95LDR_SDI_SECTION;
typedef struct _RC95LDR_SDI_IMAGE_INFO {
    uint32_t MemoryType, PageAlignmentFactor, SectionCount, RamdiskKind;
    uint8_t RuntimeGuid[16], RamdiskPartitionType, Reserved[7];
    uint64_t BootCodeOffset, BootCodeSize, RamdiskOffset, RamdiskSize;
    RC95LDR_SDI_SECTION Sections[RC95LDR_SDI_MAX_SECTIONS];
} RC95LDR_SDI_IMAGE_INFO;
RC95LDR_STATUS Rc95LdrUefiParseSdiImage(
    const void *Image, size_t ImageSize, RC95LDR_SDI_IMAGE_INFO *Info);
RC95LDR_STATUS Rc95LdrUefiValidateSdiImage(
    const void *Image, size_t ImageSize);
/* Compatibility path-only parser. */
RC95LDR_STATUS Rc95LdrUefiParseBcdWimPath(
    const RC95LDR_WINDOWS_INSTALLATION *Installation, char *Path, size_t Capacity);

/*
 * Discover the ESP/BCD candidate, mount the exact selected OS volume, bind
 * its read-only file provider, and invoke the shared WINLOAD coordinator.
 * All discovery scratch storage is caller-owned. The OS volume is unmounted
 * on every return path. A successful final kernel handoff does not return.
 *
 * Startup->BuildBootRequest and Startup->ReleaseBootRequest are called with
 * RC95LDR_UEFI_WINLOAD_FLOW_CONTEXT as their Context argument.
 */
RC95LDR_STATUS
Rc95LdrUefiDiscoverAndStartWindows(
    const RC95LDR_UEFI_DISCOVERY_OPS *DiscoveryOps,
    RC95LDR_PARTITION_INFO *Partitions,
    size_t PartitionCapacity,
    RC95LDR_BCD_BOOT_CANDIDATE *Candidates,
    size_t CandidateCapacity,
    size_t *CandidateCount,
    void *HiveBuffer,
    size_t HiveCapacity,
    const RC95LDR_WINLOAD_STARTUP *Startup,
    RC95LDR_WINLOAD_RESULT *Result,
    RC95LDR_WINDOWS_INSTALLATION *SelectedInstallation,
    size_t *SelectedEspIndex);

#ifdef __cplusplus
}
#endif
#endif
