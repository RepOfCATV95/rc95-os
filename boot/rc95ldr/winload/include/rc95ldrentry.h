#ifndef _RC95LDR_WINLOAD_ENTRY_H_
#define _RC95LDR_WINLOAD_ENTRY_H_

#include "rc95ldrwinload.h"
#include "rc95ldrpolicy.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RC95LDR_STARTUP_ABI_VERSION 2u

typedef RC95LDR_STATUS (*RC95LDR_BUILD_BOOT_REQUEST)(
    void *Context, RC95LDR_WINLOAD_REQUEST *Request);
typedef void (*RC95LDR_RELEASE_BOOT_REQUEST)(
    void *Context, RC95LDR_WINLOAD_REQUEST *Request);

typedef struct _RC95LDR_WINLOAD_STARTUP
{
    uint32_t StructureSize;
    uint32_t Version;
    RC95LDR_BOOT_FIRMWARE Firmware;
    void *Context;
    RC95LDR_BOOT_CONTEXT *BootContext;
    const RC95LDR_WINLOAD_PLATFORM *Platform;
    RC95LDR_BUILD_BOOT_REQUEST BuildBootRequest;
    RC95LDR_RELEASE_BOOT_REQUEST ReleaseBootRequest;
    /* Exact BCD-selected installation; used to propagate graphics policy. */
    const RC95LDR_WINDOWS_INSTALLATION *SelectedInstallation;
    /* UEFI AMD64 provider-owned context created before WINLOAD starts. It
     * contains the Loaded Image, tracked final-map pages and allocation set. */
    void *FirmwareHandoffContext;
} RC95LDR_WINLOAD_STARTUP;

/* Shared executable entry coordinator. The firmware frontend owns request
 * discovery and platform lifetime; policy and OS preparation remain common. */
RC95LDR_STATUS Rc95LdrWinloadStart(
    const RC95LDR_WINLOAD_STARTUP *Startup,
    RC95LDR_WINLOAD_RESULT *Result);

/* Native firmware entry shims call these after constructing and validating
 * their firmware-specific startup context. */
RC95LDR_STATUS Rc95LdrBiosWinloadEntry(
    const RC95LDR_WINLOAD_STARTUP *Startup,
    RC95LDR_WINLOAD_RESULT *Result);
RC95LDR_STATUS Rc95LdrUefiWinloadEntry(
    const RC95LDR_WINLOAD_STARTUP *Startup,
    RC95LDR_WINLOAD_RESULT *Result);

#ifdef __cplusplus
}
#endif
#endif
