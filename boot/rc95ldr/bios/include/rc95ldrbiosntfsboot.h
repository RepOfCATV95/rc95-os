#ifndef _RC95LDR_BIOS_NTFS_BOOT_H_
#define _RC95LDR_BIOS_NTFS_BOOT_H_

#include "rc95ldrbiosdisk.h"
#include "rc95ldr_ntfs_provider.h"
#include "rc95ldr_volumeprovider.h"
#include "rc95ldrentry.h"
#include "rc95ldrrc95nativeprovider.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Caller-owned resources for a complete BIOS BCD -> NTFS -> WINLOAD
 * transaction. No heap allocation is performed by this path. All buffers
 * must remain valid until Rc95LdrBiosStartWindowsFromNtfs returns.
 */
/*
 * BuildBootRequest/ReleaseBootRequest receive this transaction context. The
 * caller's original context remains available as CallerContext; the exact
 * BCD-selected installation and mounted volume are valid for the callback
 * duration.
 */
typedef struct _RC95LDR_BIOS_NTFS_WINLOAD_CONTEXT {
    void *CallerContext;
    const RC95LDR_WINDOWS_INSTALLATION *SelectedInstallation;
    const RC95LDR_VOLUME *WindowsVolume;
    RC95LDR_BUILD_BOOT_REQUEST BuildBootRequest;
    RC95LDR_RELEASE_BOOT_REQUEST ReleaseBootRequest;
    /* A private input copy binds discovery results without modifying the
       caller template; the provider pointer is restored after WINLOAD. */
    RC95LDR_RC95_NATIVE_PROVIDER_INPUT NativeProviderInput;
    RC95LDR_RC95_NATIVE_PLATFORM_CONTEXT *NativeProvider;
    const RC95LDR_RC95_NATIVE_PROVIDER_INPUT *PreviousNativeProviderInput;
} RC95LDR_BIOS_NTFS_WINLOAD_CONTEXT;

struct _RC95LDR_BIOS_STARTUP;

typedef struct _RC95LDR_BIOS_NTFS_BOOT_CONFIG {
    void *DiscoveryScratch;
    size_t DiscoveryScratchLength;
    RC95LDR_PARTITION_INFO *Partitions;
    size_t PartitionCapacity;
    RC95LDR_BCD_BOOT_CANDIDATE *Candidates;
    size_t CandidateCapacity;
    size_t CandidateCount;
    void *HiveBuffer;
    size_t HiveCapacity;
    RC95LDR_BIOS_NTFS_VALIDATION_CONTEXT Validation;
    void *ProviderWorkspace;
    size_t ProviderWorkspaceLength;
    RC95LDR_NTFS_VOLUME MountedVolume;
    RC95LDR_NTFS_PROVIDER_CONTEXT NtfsProvider;
    RC95LDR_VOLUME BoundVolume;
    RC95LDR_VOLUME_PROVIDER_CONTEXT FileProviderContext;
    RC95LDR_FILE_PROVIDER FileProvider;
    RC95LDR_FILE_PROVIDER_SCOPE FileProviderScope;
    RC95LDR_WINDOWS_INSTALLATION Installation;
    size_t BcdPartitionIndex;
    uint32_t Mounted;
    /* Set by the BIOS coordinator; NULL preserves automatic default boot. */
    struct _RC95LDR_BIOS_STARTUP *MenuStartup;
} RC95LDR_BIOS_NTFS_BOOT_CONFIG;

/*
 * Discover the BCD-selected Windows installation on the BIOS boot disk,
 * mount its exact NTFS partition, bind the native read-only NTFS filesystem
 * provider, and synchronously run the common WINLOAD entry. Resources unwind
 * in reverse order on every returning path.
 */
RC95LDR_STATUS Rc95LdrBiosStartWindowsFromNtfs(
    const RC95LDR_BIOS_BLOCK_CONTEXT *BlockContext,
    const RC95LDR_BIOS_DISK *Disk,
    const RC95LDR_TEXTUI *TextUi,
    const RC95LDR_WINLOAD_STARTUP *Startup,
    RC95LDR_BIOS_NTFS_BOOT_CONFIG *Config,
    RC95LDR_WINLOAD_RESULT *Result);

#ifdef __cplusplus
}
#endif
#endif
