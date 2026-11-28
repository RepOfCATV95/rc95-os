#ifndef _RC95LDR_BIOS_BTRFS_BOOT_H_
#define _RC95LDR_BIOS_BTRFS_BOOT_H_

#include "rc95ldrbiosdisk.h"
#include "rc95ldrentry.h"
#include "rc95ldrrc95nativeprovider.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _RC95LDR_BIOS_BTRFS_WINLOAD_CONTEXT {
    void *CallerContext;
    const RC95LDR_WINDOWS_INSTALLATION *SelectedInstallation;
    const RC95LDR_VOLUME *WindowsVolume;
    RC95LDR_BUILD_BOOT_REQUEST BuildBootRequest;
    RC95LDR_RELEASE_BOOT_REQUEST ReleaseBootRequest;
    RC95LDR_RC95_NATIVE_PROVIDER_INPUT NativeProviderInput;
    RC95LDR_RC95_NATIVE_PLATFORM_CONTEXT *NativeProvider;
    const RC95LDR_RC95_NATIVE_PROVIDER_INPUT *PreviousNativeProviderInput;
} RC95LDR_BIOS_BTRFS_WINLOAD_CONTEXT;

/* Caller-owned buffers and this object must outlive the WINLOAD transaction. */
struct _RC95LDR_BIOS_STARTUP;

typedef struct _RC95LDR_BIOS_BTRFS_BOOT_CONFIG {
    void *DiscoveryScratch;
    size_t DiscoveryScratchLength;
    RC95LDR_PARTITION_INFO *Partitions;
    size_t PartitionCapacity;
    RC95LDR_BCD_BOOT_CANDIDATE *Candidates;
    size_t CandidateCapacity;
    size_t CandidateCount;
    void *HiveBuffer;
    size_t HiveCapacity;
    void *ItemBuffer;
    size_t ItemBufferLength;
    void *InlineBuffer;
    size_t InlineBufferLength;
    void *StagingBuffer;
    size_t StagingBufferLength;
    /* Optional custom validator/context; NULL selects the built-in validator. */
    void *ValidationContext;
    RC95LDR_BIOS_VALIDATE_BTRFS_WINDOWS_VOLUME ValidateWindowsVolume;
    RC95LDR_BIOS_BTRFS_VALIDATION_CONTEXT Validation;
    RC95LDR_BIOS_BTRFS_VOLUME MountedVolume;
    RC95LDR_WINDOWS_INSTALLATION Installation;
    size_t BcdPartitionIndex;
    uint32_t Mounted;
    /* Set by the BIOS coordinator; NULL preserves automatic default boot. */
    struct _RC95LDR_BIOS_STARTUP *MenuStartup;
} RC95LDR_BIOS_BTRFS_BOOT_CONFIG;

/* Select the BCD-designated Btrfs OS partition, bind its read-only VENEER
 * provider, and synchronously invoke shared WINLOAD. NTFS-selected entries are
 * not accepted by this Btrfs-specific entrypoint. All resources unwind in
 * reverse order if WINLOAD returns. */
RC95LDR_STATUS Rc95LdrBiosStartWindowsFromBtrfs(
    const RC95LDR_BIOS_BLOCK_CONTEXT *BlockContext,
    const RC95LDR_BIOS_DISK *Disk,
    const RC95LDR_TEXTUI *TextUi,
    const RC95LDR_WINLOAD_STARTUP *Startup,
    RC95LDR_BIOS_BTRFS_BOOT_CONFIG *Config,
    RC95LDR_WINLOAD_RESULT *Result);

#ifdef __cplusplus
}
#endif
#endif
