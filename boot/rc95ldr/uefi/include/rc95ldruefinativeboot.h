#ifndef _RC95LDR_UEFI_NATIVE_BOOT_H_
#define _RC95LDR_UEFI_NATIVE_BOOT_H_

#include "rc95ldruefinativediscovery.h"
#include "rc95ldruefiwinloadflow.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Production one-call UEFI path: initialize firmware services, enumerate
 * GPT/MBR partitions, mount ESP and selected NTFS volume, parse BCD, validate
 * Windows boot files, bind the NTFS file provider, and enter WINLOAD.
 *
 * Context and all discovery scratch buffers are caller-owned. The context is
 * destroyed on every returning path. A successful kernel handoff does not
 * return; all resources needed after ExitBootServices are transferred by the
 * existing WINLOAD firmware-handoff path.
 */
RC95LDR_STATUS Rc95LdrUefiNativeDiscoverAndStartWindows(
    RC95LDR_UEFI_NATIVE_DISCOVERY_CONTEXT *Context,
    const RC95LDR_UEFI_IMAGE_CONTEXT *Image,
    uint32_t Architecture,
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
    size_t *SelectedEspIndex,
    const RC95LDR_TEXTUI *TextUi);

#ifdef __cplusplus
}
#endif
#endif
