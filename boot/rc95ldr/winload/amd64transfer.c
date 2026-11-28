#include "rc95ldramd64transfer.h"
#include "rc95ldrrc95nativeprovider.h"
#include <stdint.h>
#include <stddef.h>

#if !defined(_M_AMD64) && !defined(_M_X64) && !defined(_AMD64_) && !defined(__x86_64__) && !defined(__amd64__)
#error This source is only valid for native AMD64 builds.
#endif

static int
Rc95LdrAmd64IsCanonical(uint64_t Address)
{
    uint64_t Upper = Address >> 48;
    return ((Address & (UINT64_C(1) << 47)) != 0)
        ? Upper == UINT64_C(0xffff) : Upper == 0;
}

static int
Rc95LdrAmd64IsCanonicalRange(uint64_t Base, uint64_t Size)
{
    if (!Size || Base > UINT64_MAX - (Size - 1u))
        return 0;
    return Rc95LdrAmd64IsCanonical(Base) &&
        Rc95LdrAmd64IsCanonical(Base + Size - 1u);
}

typedef char RC95LDR_AMD64_FRAME_RSP_OFFSET_CHECK[
    (offsetof(RC95LDR_AMD64_REGISTER_FRAME, Rsp) == 56u) ? 1 : -1];
typedef char RC95LDR_AMD64_FRAME_RIP_OFFSET_CHECK[
    (offsetof(RC95LDR_AMD64_REGISTER_FRAME, Rip) == 128u) ? 1 : -1];
typedef char RC95LDR_AMD64_FRAME_SIZE_CHECK[
    (sizeof(RC95LDR_AMD64_REGISTER_FRAME) >= 144u) ? 1 : -1];

/* Implemented by transfer.S; it changes CR3 and abandons the caller forever. */
extern void Rc95LdrAmd64TransferValidated(
    const RC95LDR_AMD64_REGISTER_FRAME *Frame,
    uint64_t PageTableRoot, uint32_t EnableNx);
extern void Rc95LdrAmd64HaltForever(void);

RC95LDR_STATUS
Rc95LdrAmd64TransferKernel(
    const RC95LDR_KERNEL_HANDOFF_CONTEXT *Handoff)
{
    const RC95LDR_AMD64_REGISTER_FRAME *Frame;
    uint64_t StackEnd;
    uint64_t ProcessorStateBase, ProcessorStateSize;
    uint64_t SchedulerStateBase, SchedulerStateSize;
    uint32_t EnableNx = 0;

    if (!Handoff ||
        Handoff->Architecture != Rc95LdrNtArchAmd64 ||
        (Handoff->Firmware != Rc95LdrFirmwareBios &&
         Handoff->Firmware != Rc95LdrFirmwareUefi))
        return Rc95LdrInvalidParameter;

    /* Never allow the transfer stub to become an alternate validation path. */
    {
        RC95LDR_STATUS Status = Rc95LdrValidateKernelHandoff(Handoff);
        if (Status != Rc95LdrSuccess)
            return Status;
    }

    /* RC95-OS's typed CPU record is the source of truth for NX capability.
     * The page-table builder emits NX PTEs only for this AMD64 path, and the
     * transfer stub enables EFER.NXE before installing that CR3. */
    if (Handoff->OsFamily == Rc95LdrOsRc95) {
        const RC95LDR_RC95_CPU_STATE *CpuState;
        if (!Handoff->CpuState || Handoff->CpuState > UINTPTR_MAX)
            return Rc95LdrInvalidParameter;
        CpuState = (const RC95LDR_RC95_CPU_STATE *)(uintptr_t)Handoff->CpuState;
        if (CpuState->Signature != RC95LDR_RC95_CPU_SIGNATURE ||
            CpuState->Size < sizeof(*CpuState) || CpuState->Version != 2u)
            return Rc95LdrInvalidImage;
        EnableNx = ((CpuState->FeatureFlags & RC95LDR_CPU_FEATURE_NX) &&
            (CpuState->FeatureFlags & RC95LDR_CPU_FEATURE_MSR)) ? 1u : 0u;
    }
    /* RC95-OS does not use the Windows KPCR/KPRCB ABI. Its two
     * independently allocated kernel-owned records occupy the same typed
     * processor-control union storage but have distinct RC95 field names. */
    if (Handoff->OsFamily == Rc95LdrOsRc95) {
        ProcessorStateBase =
            Handoff->ProcessorControl.Rc95Amd64.PerCpuDataAddress;
        ProcessorStateSize =
            Handoff->ProcessorControl.Rc95Amd64.PerCpuDataSize;
        SchedulerStateBase =
            Handoff->ProcessorControl.Rc95Amd64.SchedulerStateAddress;
        SchedulerStateSize =
            Handoff->ProcessorControl.Rc95Amd64.SchedulerStateSize;
    } else {
        ProcessorStateBase = Handoff->ProcessorControl.Amd64.PcrAddress;
        ProcessorStateSize = Handoff->ProcessorControl.Amd64.PcrSize;
        SchedulerStateBase = Handoff->ProcessorControl.Amd64.PrcbAddress;
        SchedulerStateSize = Handoff->ProcessorControl.Amd64.PrcbSize;
    }

    if (!(Handoff->CompletedRequirements & RC95LDR_HANDOFF_REGISTER_FRAME) ||
        Handoff->RegisterFrameSize < sizeof(*Frame) ||
        Handoff->RegisterFrameAddress > UINTPTR_MAX ||
        Handoff->PageTableRoot > UINTPTR_MAX ||
        (Handoff->PageTableRoot & UINT64_C(0xfff)) != 0 ||
        (Handoff->RegisterFrameAddress & 0xfu) != 0 ||
        Handoff->RegisterFrameSize > UINTPTR_MAX -
            Handoff->RegisterFrameAddress ||
        !Rc95LdrAmd64IsCanonicalRange(Handoff->RegisterFrameAddress,
            Handoff->RegisterFrameSize) ||
        !Rc95LdrAmd64IsCanonicalRange(Handoff->KernelBase,
            Handoff->KernelSize) ||
        !Rc95LdrAmd64IsCanonical(Handoff->KernelEntry) ||
        !Rc95LdrAmd64IsCanonical(Handoff->HalBase) ||
        !Rc95LdrAmd64IsCanonicalRange(Handoff->LoaderBlockAddress,
            Handoff->LoaderBlockSize) ||
        !Rc95LdrAmd64IsCanonicalRange(Handoff->StackBase,
            Handoff->StackSize) ||
        !Rc95LdrAmd64IsCanonicalRange(ProcessorStateBase,
            ProcessorStateSize) ||
        !Rc95LdrAmd64IsCanonicalRange(SchedulerStateBase,
            SchedulerStateSize))
        return Rc95LdrOutOfBounds;

    StackEnd = Handoff->StackBase + Handoff->StackSize;
    if (!Rc95LdrAmd64IsCanonical(StackEnd - 1u) ||
        Handoff->StackBase > UINT64_MAX - 8u)
        return Rc95LdrOutOfBounds;

    Frame = (const RC95LDR_AMD64_REGISTER_FRAME *)(uintptr_t)
        Handoff->RegisterFrameAddress;

    if (Frame->Rip != Handoff->KernelEntry ||
        Frame->Rcx != Handoff->LoaderBlockAddress ||
        Frame->Cs != 0x10u || Frame->Ss != 0x18u ||
        Frame->Rsp < Handoff->StackBase + 8u ||
        Frame->Rsp > StackEnd ||
        (Frame->Rsp & 0xfu) != 0 ||
        !Rc95LdrAmd64IsCanonical(Frame->Rsp) ||
        (Frame->RFlags & UINT64_C(2)) == 0)
        return Rc95LdrInvalidParameter;

    /*
     * The frame and all target objects must remain mapped after CR3 changes.
     * This is a platform preparation invariant, not something firmware can
     * prove from virtual addresses alone.
     */
    Rc95LdrAmd64TransferValidated(Frame, Handoff->PageTableRoot, EnableNx);

    /* Returning from the target kernel entry violates the native handoff
     * contract. Do not let callers release memory or resume firmware work. */
    Rc95LdrAmd64HaltForever();
    return Rc95LdrUnsupported;
}
