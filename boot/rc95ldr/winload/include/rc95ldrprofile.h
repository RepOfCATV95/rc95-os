#ifndef _RC95LDR_OSPROFILE_H_
#define _RC95LDR_OSPROFILE_H_
#include "rc95ldrwinload.h"
#ifdef __cplusplus
extern "C" {
#endif

/* These are distinct boot-manager/application routes, not interchangeable
   aliases. Their values intentionally do not encode a Windows version. */
typedef enum _RC95LDR_BOOT_ENTRY_KIND {
    Rc95LdrEntryNtldr=1,       /* {ntldr}: NT 4.0 through Server 2003 */
    Rc95LdrEntryReactOs=2,     /* {rosboot}: ReactOS-specific entry */
    Rc95LdrEntryWindowsWinload=3, /* Vista through Windows 11 */
    Rc95LdrEntryRc95Winload=4  /* RC95-OS-owned loader chain */
} RC95LDR_BOOT_ENTRY_KIND;

typedef enum _RC95LDR_KERNEL_ABI {
    Rc95LdrKernelAbiNtLegacy=1,
    Rc95LdrKernelAbiReactOs=2,
    Rc95LdrKernelAbiWindowsModern=3,
    Rc95LdrKernelAbiRc95=4
} RC95LDR_KERNEL_ABI;

typedef enum _RC95LDR_LOADER_BLOCK_KIND {
    Rc95LdrLoaderBlockNtLegacy=1,
    Rc95LdrLoaderBlockReactOs=2,
    Rc95LdrLoaderBlockWindowsModern=3,
    Rc95LdrLoaderBlockRc95=4
} RC95LDR_LOADER_BLOCK_KIND;

typedef enum _RC95LDR_RESUME_APPLICATION {
    Rc95LdrResumeApplicationNone=0,
    Rc95LdrResumeApplicationWinresumeExe=1,
    Rc95LdrResumeApplicationWinresumeEfi=2,
    Rc95LdrResumeApplicationRc95Winload=3
} RC95LDR_RESUME_APPLICATION;

typedef struct _RC95LDR_OS_IDENTITY {
    uint32_t StructureSize;
    uint32_t Version;
    RC95LDR_OS_FAMILY OsFamily;
    RC95LDR_BOOT_FIRMWARE Firmware;
    RC95LDR_BOOT_ENTRY_KIND BootEntry;
    uint32_t OsMajorVersion;
    uint32_t OsMinorVersion;
    uint32_t OsBuildNumber;
    uint32_t Flags;
} RC95LDR_OS_IDENTITY;

typedef struct _RC95LDR_OS_PROFILE {
    uint32_t StructureSize;
    uint32_t Version;
    RC95LDR_OS_FAMILY OsFamily;
    RC95LDR_BOOT_FIRMWARE Firmware;
    RC95LDR_BOOT_ENTRY_KIND BootEntry;
    RC95LDR_KERNEL_ABI KernelAbi;
    RC95LDR_LOADER_BLOCK_KIND LoaderBlockKind;
    RC95LDR_RESUME_APPLICATION ResumeApplication;
    uint32_t OsMajorVersion;
    uint32_t OsMinorVersion;
    uint32_t OsBuildNumber;
    uint32_t Flags;
} RC95LDR_OS_PROFILE;

#define RC95LDR_OS_PROFILE_VERSION 1u

/* Rejects mismatched entry/family/version/firmware combinations. Windows
   version numbers are NT versions: 4.0, 5.0-5.2, 6.0-6.3 and 10.0.
   ReactOS and RC95-OS use their own identities, not Windows version aliases. */
RC95LDR_STATUS Rc95LdrSelectOsProfile(
    const RC95LDR_OS_IDENTITY *Identity,
    RC95LDR_OS_PROFILE *Profile);

#ifdef __cplusplus
}
#endif
#endif
