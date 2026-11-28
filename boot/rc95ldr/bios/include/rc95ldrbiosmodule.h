#ifndef _RC95LDR_BIOS_MODULE_H_
#define _RC95LDR_BIOS_MODULE_H_

#include "rc95ldrmodule.h"
#include "rc95ldrpe.h"
#include "rc95ldrwinload.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Loads a PE32/PE32+ module from the active BIOS file provider, validates and
 * maps it into a platform-owned page allocation, then transfers control via
 * the architecture-specific callback. Imports are rejected unless the image
 * has no import directory; no implicit or guessed import resolver is used.
 * A returning transfer means the child has finished or failed, so its image
 * allocation is released before this routine returns. */
RC95LDR_STATUS RC95LDR_MODULE_ABI Rc95LdrBiosNativeModuleTransfer(
    void *Context, uint64_t EntryPoint,
    const RC95LDR_MODULE_HANDOFF *Handoff);

RC95LDR_STATUS Rc95LdrBiosLoadAndInvokeNativeModule(
    const RC95LDR_WINLOAD_PLATFORM *Platform,
    const uint16_t *Path,
    uint32_t ModuleKind,
    uint32_t Architecture,
    const RC95LDR_MODULE_HANDOFF *ParentHandoff,
    size_t ParentHandoffSize);

RC95LDR_STATUS Rc95LdrBiosLoadAndInvokeModule(
    const RC95LDR_WINLOAD_PLATFORM *Platform,
    const uint16_t *Path,
    uint32_t ModuleKind,
    uint32_t Architecture,
    const RC95LDR_MODULE_HANDOFF *ParentHandoff,
    size_t ParentHandoffSize,
    RC95LDR_MODULE_TRANSFER Transfer,
    void *TransferContext);

#ifdef __cplusplus
}
#endif
#endif
