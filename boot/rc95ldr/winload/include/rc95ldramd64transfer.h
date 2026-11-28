#ifndef _RC95LDR_AMD64_TRANSFER_H_
#define _RC95LDR_AMD64_TRANSFER_H_

#include "rc95ldrarchprepare.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Validates the complete, OS-profile-specific handoff before entering the
 * native AMD64 transfer stub. On success this routine never returns.
 *
 * The caller must already have installed a long-mode GDT with the target
 * kernel's expected selectors and must ensure that the register frame,
 * target stack, kernel entry and loader block are mapped by PageTableRoot.
 * The frame's RCX must identify LoaderBlockAddress and its RIP must identify
 * KernelEntry. Firmware teardown (including UEFI ExitBootServices) is owned
 * by the firmware-specific WINLOAD platform callback before this function
 * is invoked.
 */
RC95LDR_STATUS Rc95LdrAmd64TransferKernel(
    const RC95LDR_KERNEL_HANDOFF_CONTEXT *Handoff);

#ifdef __cplusplus
}
#endif
#endif
