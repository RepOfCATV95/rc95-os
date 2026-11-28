#include "rc95ldrwinload.h"
#include "rc95ldrresume.h"

/*
 * Default architecture callback for RC95LDR WINLOAD platform adapters.
 * All image and processor-state validation is performed by resume.c before
 * this last, deliberately non-returning transition.
 */
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
    (void)ResumeFlagPhysicalAddress;

    if (!ProcessorState)
        return Rc95LdrInvalidParameter;

#if defined(_M_IX86) || defined(_X86_) || defined(__i386__) || defined(i386)
    {
        uint32_t trampolineFlags = 0;
        if (Machine != RC95LDR_HIBER_MACHINE_I386 ||
            ProcessorStateSize != 0x320u)
            return Rc95LdrUnsupported;
        if (Flags & RC95LDR_HIBER_FLAG_HAS_FPU_STATE)
            trampolineFlags |= 0x01u;
        if (Flags & RC95LDR_HIBER_FLAG_HAS_FX_STATE) {
            if (!(Flags & RC95LDR_HIBER_FLAG_HAS_FPU_STATE) ||
                FxStateOffset != 0x320u)
                return Rc95LdrCorruptData;
            trampolineFlags |= 0x02u;
        } else if (FxStateOffset != 0) {
            return Rc95LdrCorruptData;
        }
        Rc95LdrI386ResumeTrampoline(ProcessorState, trampolineFlags);
    }
#elif defined(_M_AMD64) || defined(_M_X64) || defined(_AMD64_) || defined(__x86_64__) || defined(__amd64__)
    if (Machine != RC95LDR_HIBER_MACHINE_AMD64 ||
        ProcessorStateSize != 0x5B0u ||
        (Flags & (RC95LDR_HIBER_FLAG_HAS_FPU_STATE |
                  RC95LDR_HIBER_FLAG_HAS_FX_STATE)) !=
                 (RC95LDR_HIBER_FLAG_HAS_FPU_STATE |
                  RC95LDR_HIBER_FLAG_HAS_FX_STATE) ||
        FxStateOffset != 0x5B0u)
        return Rc95LdrUnsupported;
    Rc95LdrAmd64ResumeTrampoline(ProcessorState, 0x02u);
#else
    (void)Machine;
    (void)ProcessorStateSize;
    (void)Flags;
    (void)FxStateOffset;
    return Rc95LdrUnsupported;
#endif

    /* Reaching this line means the architecture trampoline returned illegally. */
    return Rc95LdrUnsupported;
}
