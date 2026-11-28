#ifndef _RC95LDR_UEFI_DISCOVERY_H_
#define _RC95LDR_UEFI_DISCOVERY_H_
#include "rc95ldrpolicy.h"
#include "rc95ldrtextui.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _RC95LDR_UEFI_DISCOVERY_OPS {
    void *Context;
    RC95LDR_STATUS (*EnumeratePartitions)(void*, RC95LDR_PARTITION_INFO*, size_t, size_t*);
    RC95LDR_STATUS (*MountVolume)(void*, const RC95LDR_PARTITION_INFO*, RC95LDR_FILESYSTEM_KIND, RC95LDR_VOLUME*);
    void (*UnmountVolume)(void*, RC95LDR_VOLUME*);
    RC95LDR_STATUS (*CollectBootCandidates)(void*, const RC95BCD_HIVE*,
        RC95LDR_BCD_BOOT_CANDIDATE*, size_t, size_t*);
    RC95LDR_STATUS (*ValidateWindowsVolume)(void*, const RC95LDR_VOLUME*,
        const RC95LDR_WINDOWS_INSTALLATION*);
    const RC95LDR_TEXTUI *TextUi; /* Optional BCD discovery diagnostics. */
    RC95LDR_STATUS (*AllocatePool)(void*, size_t, void**);
    void (*FreePool)(void*, void*);
    /* Optional interactive selector. The returned pointer must identify one
       of Candidates; NULL is never interpreted as an implicit default. */
    RC95LDR_STATUS (*SelectBootCandidate)(void*, const RC95BCD_HIVE*,
        const RC95LDR_BCD_BOOT_CANDIDATE*, size_t,
        const RC95LDR_BCD_BOOT_CANDIDATE**);
    void *SelectionContext; /* Optional context for SelectBootCandidate only. */
} RC95LDR_UEFI_DISCOVERY_OPS;

/* Enumerates all FAT32 GPT ESPs. Caller owns scratch buffers. On success
 * WindowsVolume remains mounted and must be unmounted by the caller. */
RC95LDR_STATUS Rc95LdrDiscoverUefiWindows(
    const RC95LDR_UEFI_DISCOVERY_OPS*, RC95LDR_PARTITION_INFO*, size_t,
    RC95LDR_BCD_BOOT_CANDIDATE*, size_t, size_t*, void*, size_t,
    RC95LDR_WINDOWS_INSTALLATION*, RC95LDR_VOLUME*, size_t*);
#ifdef __cplusplus
}
#endif
#endif
