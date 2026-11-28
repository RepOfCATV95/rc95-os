#ifndef _RC95LDR_BIOS_AMD64_HANDOFF_H_
#define _RC95LDR_BIOS_AMD64_HANDOFF_H_

#include "rc95ldrhandoff.h"

#ifdef __cplusplus
extern "C" {
#endif

/* BIOS provider callback for the shared WINLOAD validated-handoff slot.
 * The caller must have completed its BIOS-specific transition/teardown and
 * ensured that the target page tables retain all handoff mappings. */
RC95LDR_STATUS Rc95LdrBiosAmd64TransferHandoff(
    void *Context, RC95LDR_KERNEL_HANDOFF_CONTEXT *Handoff);

#ifdef __cplusplus
}
#endif
#endif
