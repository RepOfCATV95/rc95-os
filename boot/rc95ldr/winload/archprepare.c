#include "rc95ldrarchprepare.h"
#include <stdint.h>
#include <limits.h>
#include <string.h>

static RC95LDR_STATUS
Rc95LdrEnableI386Nxe(void)
{
#if defined(_MSC_VER) && defined(_M_IX86)
    __asm {
        mov ecx, 0C0000080h
        rdmsr
        or eax, 0800h
        wrmsr
    }
    return Rc95LdrSuccess;
#elif defined(__i386__)
    {
        uint32_t low, high;
        const uint32_t efer = 0xC0000080u;
        __asm__ __volatile__("rdmsr"
            : "=a"(low), "=d"(high) : "c"(efer));
        low |= 0x00000800u;
        __asm__ __volatile__("wrmsr"
            : : "a"(low), "d"(high), "c"(efer) : "memory");
    }
    return Rc95LdrSuccess;
#else
    return Rc95LdrUnsupported;
#endif
}

RC95LDR_STATUS Rc95LdrPrepareArchitectureHandoff(
    const RC95LDR_ARCH_HANDOFF_INPUT *i,RC95LDR_KERNEL_HANDOFF_CONTEXT *c)
{
    uint64_t end,alignment,frameAddress;size_t frameSize;
    RC95LDR_ARCH_REGISTER_FRAME *frame;
    if(!i||!c||i->StructureSize<sizeof(*i)||i->Version!=RC95LDR_ARCH_HANDOFF_INPUT_VERSION||
       i->Architecture!=c->Architecture||!i->StackBase||!i->StackSize||!i->StackPointer||
       !i->PageTableRoot||!i->ArchitectureState||!i->CpuState||
       !i->KernelEntry||!i->LoaderBlockArgument||!i->RegisterFrameBuffer||
       i->EnableNx > 1u || i->ReservedNx != 0u ||
       (i->EnableNx && i->Architecture != Rc95LdrNtArchI386 &&
        i->Architecture != Rc95LdrNtArchAmd64) ||
       i->StackBase>UINT64_MAX-i->StackSize)
        return Rc95LdrInvalidParameter;
    switch(i->Architecture){
    case Rc95LdrNtArchI386:
        if(sizeof(void*)!=4)return Rc95LdrUnsupported;
        alignment=4;frameSize=sizeof(RC95LDR_I386_REGISTER_FRAME);break;
    case Rc95LdrNtArchAmd64:
        if(sizeof(void*)!=8)return Rc95LdrUnsupported;
        alignment=16;frameSize=sizeof(RC95LDR_AMD64_REGISTER_FRAME);break;
    case Rc95LdrNtArchPowerPc:
        if(sizeof(void*)!=4)return Rc95LdrUnsupported;
        alignment=16;frameSize=sizeof(RC95LDR_POWERPC_REGISTER_FRAME);break;
    case Rc95LdrNtArchArm:
#if defined(_ARM_)
        if(sizeof(void*)!=4)return Rc95LdrUnsupported;
        alignment=8;frameSize=sizeof(RC95LDR_ARM_REGISTER_FRAME);break;
#else
        return Rc95LdrUnsupported;
#endif
    default:return Rc95LdrUnsupported;
    }
    if(i->RegisterFrameCapacity<frameSize)return Rc95LdrBufferTooSmall;
    /* I386 PAE has no separate cold-transfer stub. Enable NXE here, after
       the protected page tables have been built but before firmware transfer
       can activate them. AMD64 enables NXE in its final CR3-switch stub. */
    if (i->EnableNx && i->Architecture == Rc95LdrNtArchI386) {
        RC95LDR_STATUS NxStatus = Rc95LdrEnableI386Nxe();
        if (NxStatus != Rc95LdrSuccess) return NxStatus;
    }
    if(((uintptr_t)i->RegisterFrameBuffer&(alignment-1))||
       i->KernelEntry>UINTPTR_MAX||i->LoaderBlockArgument>UINTPTR_MAX)return Rc95LdrOutOfBounds;
    end=i->StackBase+i->StackSize;
    if(i->StackBase>UINTPTR_MAX||end-1>UINTPTR_MAX||i->StackPointer<i->StackBase||
       i->StackPointer>end||i->PageTableRoot>UINTPTR_MAX||
       i->ArchitectureState>UINTPTR_MAX||i->CpuState>UINTPTR_MAX)
        return Rc95LdrOutOfBounds;
    if((i->StackBase&(alignment-1))||(end&(alignment-1))||
       (i->StackPointer&(alignment-1))||(i->ArchitectureState&(alignment-1))||
       (i->CpuState&(alignment-1))||(i->PageTableRoot&0xfffu))
        return Rc95LdrInvalidParameter;
    if (i->Architecture == Rc95LdrNtArchI386 &&
        c->OsFamily == Rc95LdrOsRc95) {
        const RC95LDR_RC95_I386_PROCESSOR_STATE *pc = &i->ProcessorControl.Rc95I386;
        if (!pc->PerCpuDataAddress || !pc->PerCpuDataSize ||
            !pc->SchedulerStateAddress || !pc->SchedulerStateSize ||
            pc->PerCpuDataAddress > UINTPTR_MAX || pc->SchedulerStateAddress > UINTPTR_MAX ||
            pc->PerCpuDataSize > UINTPTR_MAX - pc->PerCpuDataAddress ||
            pc->SchedulerStateSize > UINTPTR_MAX - pc->SchedulerStateAddress ||
            (pc->PerCpuDataAddress & 0xfffu) || (pc->SchedulerStateAddress & 0xfffu) ||
            (pc->PerCpuDataAddress < pc->SchedulerStateAddress + pc->SchedulerStateSize &&
             pc->SchedulerStateAddress < pc->PerCpuDataAddress + pc->PerCpuDataSize))
            return Rc95LdrInvalidParameter;
    } else if (i->Architecture == Rc95LdrNtArchI386) {
        const RC95LDR_I386_PROCESSOR_CONTROL *pc = &i->ProcessorControl.I386;
        if (!pc->PcrAddress || !pc->PcrSize || !pc->PrcbSize ||
            pc->PcrAddress > UINTPTR_MAX || pc->PcrSize > UINTPTR_MAX - pc->PcrAddress ||
            pc->PrcbOffset > pc->PcrSize || pc->PrcbSize > pc->PcrSize - pc->PrcbOffset ||
            (pc->PcrAddress & 0xfffu) || (pc->PrcbOffset & 3u))
            return Rc95LdrInvalidParameter;
    } else if (i->Architecture == Rc95LdrNtArchAmd64 &&
               c->OsFamily == Rc95LdrOsRc95) {
        const RC95LDR_RC95_AMD64_PROCESSOR_STATE *pc = &i->ProcessorControl.Rc95Amd64;
        if (!pc->PerCpuDataAddress || !pc->PerCpuDataSize ||
            !pc->SchedulerStateAddress || !pc->SchedulerStateSize ||
            pc->PerCpuDataAddress > UINTPTR_MAX || pc->SchedulerStateAddress > UINTPTR_MAX ||
            pc->PerCpuDataSize > UINTPTR_MAX - pc->PerCpuDataAddress ||
            pc->SchedulerStateSize > UINTPTR_MAX - pc->SchedulerStateAddress ||
            (pc->PerCpuDataAddress & 0xfffu) || (pc->SchedulerStateAddress & 0xfffu) ||
            (pc->PerCpuDataAddress < pc->SchedulerStateAddress + pc->SchedulerStateSize &&
             pc->SchedulerStateAddress < pc->PerCpuDataAddress + pc->PerCpuDataSize))
            return Rc95LdrInvalidParameter;
    } else if (i->Architecture == Rc95LdrNtArchAmd64) {
        const RC95LDR_AMD64_PROCESSOR_CONTROL *pc = &i->ProcessorControl.Amd64;
        if (!pc->PcrAddress || !pc->PcrSize || !pc->PrcbAddress || !pc->PrcbSize ||
            pc->PcrAddress > UINTPTR_MAX || pc->PrcbAddress > UINTPTR_MAX ||
            pc->PcrSize > UINTPTR_MAX - pc->PcrAddress ||
            pc->PrcbSize > UINTPTR_MAX - pc->PrcbAddress ||
            (pc->PcrAddress & 0xfffu) || (pc->PrcbAddress & 0xfffu) ||
            (pc->PcrAddress < pc->PrcbAddress + pc->PrcbSize &&
             pc->PrcbAddress < pc->PcrAddress + pc->PcrSize))
            return Rc95LdrInvalidParameter;
    }
    frame=(RC95LDR_ARCH_REGISTER_FRAME*)i->RegisterFrameBuffer;
    memset(frame,0,frameSize);
    switch(i->Architecture){
    case Rc95LdrNtArchI386:
        frame->I386.Eax=(uint32_t)i->GeneralRegisters[0];frame->I386.Ebx=(uint32_t)i->GeneralRegisters[1];
        frame->I386.Ecx=(uint32_t)i->GeneralRegisters[2];frame->I386.Edx=(uint32_t)i->GeneralRegisters[3];
        frame->I386.Esi=(uint32_t)i->GeneralRegisters[4];frame->I386.Edi=(uint32_t)i->GeneralRegisters[5];
        frame->I386.Ebp=(uint32_t)i->GeneralRegisters[6];
        frame->I386.Eip=(uint32_t)i->KernelEntry;frame->I386.Esp=(uint32_t)i->StackPointer;
        frame->I386.EFlags=i->StatusRegister?i->StatusRegister:0x2u;
        frame->I386.Cs=0x08;frame->I386.Ss=0x10;frame->I386.Ds=0x10;
        frame->I386.Es=0x10;frame->I386.Fs=0x30;frame->I386.Gs=0x10;
        frame->I386.Eax=(uint32_t)i->LoaderBlockArgument;break;
    case Rc95LdrNtArchAmd64:
        memcpy(&frame->Amd64.Rax,i->GeneralRegisters,16*sizeof(uint64_t));
        frame->Amd64.Rip=i->KernelEntry;frame->Amd64.Rsp=i->StackPointer;
        frame->Amd64.RFlags=i->StatusRegister?i->StatusRegister:0x2u;
        frame->Amd64.Rcx=i->LoaderBlockArgument;
        frame->Amd64.Cs=0x10;frame->Amd64.Ss=0x18;break;
    case Rc95LdrNtArchPowerPc:
        {unsigned n;for(n=0;n<32;n++)frame->PowerPc.Gpr[n]=(uint32_t)i->GeneralRegisters[n];}
        frame->PowerPc.Nip=(uint32_t)i->KernelEntry;frame->PowerPc.Msr=i->StatusRegister;
        frame->PowerPc.Gpr[1]=(uint32_t)i->StackPointer;
        frame->PowerPc.Gpr[3]=(uint32_t)i->LoaderBlockArgument;break;
    case Rc95LdrNtArchArm:
        memcpy(frame->Arm.R,i->GeneralRegisters,16*sizeof(uint32_t));
        frame->Arm.R[0]=(uint32_t)i->LoaderBlockArgument;
        frame->Arm.R[13]=(uint32_t)i->StackPointer;
        frame->Arm.R[15]=(uint32_t)i->KernelEntry;
        frame->Arm.Cpsr=i->StatusRegister;break;
    default:return Rc95LdrUnsupported;
    }
    frameAddress=(uint64_t)(uintptr_t)i->RegisterFrameBuffer;
    if(frameSize>UINTPTR_MAX-frameAddress)return Rc95LdrOutOfBounds;
    c->StackBase=i->StackBase;c->StackSize=i->StackSize;c->PageTableRoot=i->PageTableRoot;
    c->ArchitectureState=i->ArchitectureState;c->CpuState=i->CpuState;
    c->ProcessorControl=i->ProcessorControl;
    c->RegisterFrameAddress=frameAddress;c->RegisterFrameSize=(uint32_t)frameSize;
    c->CompletedRequirements|=RC95LDR_HANDOFF_STACK|RC95LDR_HANDOFF_PAGE_TABLES|
        RC95LDR_HANDOFF_ARCH_STATE|RC95LDR_HANDOFF_CPU_STATE|
        RC95LDR_HANDOFF_REGISTER_FRAME;
    if(i->Architecture==Rc95LdrNtArchI386||i->Architecture==Rc95LdrNtArchAmd64)
        c->CompletedRequirements|=RC95LDR_HANDOFF_PROCESSOR_CONTROL;
    return Rc95LdrSuccess;
}
