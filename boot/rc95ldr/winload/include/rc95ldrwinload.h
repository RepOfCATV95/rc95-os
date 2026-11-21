#ifndef _RC95LDR_WINLOAD_H_
#define _RC95LDR_WINLOAD_H_
#include "rc95ldr.h"
#include "rc95ldrtextui.h"
#ifdef __cplusplus
extern "C" {
#endif
#define RC95LDR_WINLOAD_ABI_VERSION 2u
typedef enum _RC95LDR_BOOT_FIRMWARE { Rc95LdrFirmwareBios=1, Rc95LdrFirmwareUefi=2 } RC95LDR_BOOT_FIRMWARE;
typedef enum _RC95LDR_OS_FAMILY {
    Rc95LdrOsWindowsNtLegacy=1, /* NT 4.0 through Server 2003 */
    Rc95LdrOsReactOS=2,
    Rc95LdrOsWindowsModern=3,   /* Vista through Windows 11 */
    Rc95LdrOsRc95=4,
    Rc95LdrOsLiberNt=5           /* CATV 95 LiberNT fork: unsupported */
} RC95LDR_OS_FAMILY;
typedef enum _RC95LDR_RESUME_POLICY {
    Rc95LdrResumeDisabled=0,
    Rc95LdrResumeViaWinresume=1, /* Windows Vista+ UEFI resume application */
    Rc95LdrResumeInsideWinload=2 /* RC95-OS only */
} RC95LDR_RESUME_POLICY;
typedef struct _RC95LDR_WINLOAD_FILE { void *FileContext; uint64_t FileSize; } RC95LDR_WINLOAD_FILE;
typedef struct _RC95LDR_WINLOAD_MEMORY_RANGE {
    uint64_t Base; uint64_t Length; uint32_t Type; uint32_t Attributes;
} RC95LDR_WINLOAD_MEMORY_RANGE;
typedef struct _RC95LDR_WINLOAD_PLATFORM {
    uint32_t StructureSize; uint32_t Version; void *Context;
    RC95LDR_STATUS (*OpenFile)(void *, const uint16_t *, RC95LDR_WINLOAD_FILE *);
    RC95LDR_STATUS (*ReadFileAt)(void *, const RC95LDR_WINLOAD_FILE *, uint64_t, void *, size_t);
    void (*CloseFile)(void *, RC95LDR_WINLOAD_FILE *);
    /* Returns a physical address that is identity-mapped and writable until
       handoff. Size is bytes; Alignment is a power-of-two byte alignment. */
    RC95LDR_STATUS (*AllocatePages)(void *, uint64_t, uint64_t, uint64_t *);
    void (*FreePages)(void *, uint64_t, uint64_t);
    RC95LDR_STATUS (*GetMemoryMap)(void *, RC95LDR_WINLOAD_MEMORY_RANGE *, size_t, size_t *);
    /* Must not return on success. UEFI implementation exits boot services first. */
    RC95LDR_STATUS (*ExitFirmwareAndHandoff)(void *, uint64_t, uint64_t, uint64_t);
    /* Optional firmware text console. NULL keeps headless operation. This
       tail field is size-gated so older v2 platform initializers remain valid. */
    const RC95LDR_TEXTUI *TextUi;
} RC95LDR_WINLOAD_PLATFORM;
typedef struct _RC95LDR_WINLOAD_REQUEST {
    uint32_t StructureSize; uint32_t Version;
    RC95LDR_OS_FAMILY OsFamily;
    RC95LDR_BOOT_FIRMWARE Firmware;
    RC95LDR_RESUME_POLICY ResumePolicy;
    uint32_t Flags;
    const uint16_t *SystemRoot;
    const uint16_t *KernelPath;
    const uint16_t *HalPath;
    const uint16_t *BootDriverDirectory;
    const uint16_t *SystemHivePath;
    void *BootConfiguration;
    /* Explicit boot route and NT version prevent cross-family ABI guessing. */
    uint32_t BootEntryKind;
    uint32_t OsMajorVersion;
    uint32_t OsMinorVersion;
    uint32_t OsBuildNumber;
} RC95LDR_WINLOAD_REQUEST;
typedef struct _RC95LDR_WINLOAD_RESULT {
    uint32_t StructureSize; uint32_t Version;
    uint64_t KernelEntryPoint; uint64_t KernelImageBase;
    uint64_t KernelImageSize; uint64_t LoaderBlockAddress;
    uint64_t LoaderBlockSize; uint64_t ResumeEntryPoint;
    uint32_t ImageCount; uint32_t Flags;
} RC95LDR_WINLOAD_RESULT;
/*
 * Unified firmware-neutral OS preparation entry point. OS family selects the
 * loader-block/handoff profile; firmware supplies platform services.
 * NT legacy ({ntldr}) and ReactOS ({rosboot}) remain separate compatibility
 * dispatches, never implicit aliases for the Vista+ profile. ABI v2 requires
 * an explicit boot-entry kind and OS version identity, checked by profile.c.
 *
 * PE validation/mapping and initial kernel mapping are implemented. The
 * routine still returns Rc95LdrUnsupported after mapping until dependencies,
 * profile-specific loader blocks, and firmware handoff are complete.
 */
RC95LDR_STATUS LoadAndBootWindows(RC95LDR_BOOT_CONTEXT *BootContext,
    const RC95LDR_WINLOAD_PLATFORM *Platform,
    const RC95LDR_WINLOAD_REQUEST *Request,
    RC95LDR_WINLOAD_RESULT *Result);
#ifdef __cplusplus
}
#endif
#endif
