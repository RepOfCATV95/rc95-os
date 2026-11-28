#include "rc95ldr.h"
#include <stdint.h>

/* The standalone host suite validates resume parsing and policy, but must
 * never execute a privileged architecture restore trampoline. Production
 * BIOS/UEFI builds link winload/resume_arch.c plus the architecture assembly. */
RC95LDR_STATUS Rc95LdrRestoreProcessorState(
    void *Context,
    uint32_t Machine,
    const void *ProcessorState,
    uint32_t ProcessorStateSize,
    uint32_t Flags,
    uint32_t FxStateOffset,
    uint64_t ResumeFlagPhysicalAddress)
{
    (void)Context;
    (void)Machine;
    (void)ProcessorState;
    (void)ProcessorStateSize;
    (void)Flags;
    (void)FxStateOffset;
    (void)ResumeFlagPhysicalAddress;
    return Rc95LdrUnsupported;
}
