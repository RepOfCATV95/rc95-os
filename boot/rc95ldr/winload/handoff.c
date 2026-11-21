#include "rc95ldrhandoff.h"
#include <stdint.h>
#include <limits.h>
RC95LDR_STATUS Rc95LdrValidateKernelHandoff(const RC95LDR_KERNEL_HANDOFF_CONTEXT *c)
{
    uint64_t end,stackEnd;
    uint32_t requiredAllowed=RC95LDR_HANDOFF_REQUIRED;
    const uint32_t known=RC95LDR_HANDOFF_REQUIRED;
    if(!c||c->Signature!=RC95LDR_HANDOFF_SIGNATURE||
       c->StructureSize<sizeof(*c)||c->Version!=RC95LDR_HANDOFF_VERSION||
       c->OsFamily<Rc95LdrOsWindowsNtLegacy||c->OsFamily>Rc95LdrOsRc95||
       (c->Firmware!=Rc95LdrFirmwareBios&&c->Firmware!=Rc95LdrFirmwareUefi)||
       c->Architecture<Rc95LdrNtArchI386||c->Architecture>Rc95LdrNtArchArm)
        return Rc95LdrInvalidParameter;
    if(!c->KernelEntry||!c->KernelBase||!c->KernelSize||!c->HalBase||
       c->KernelBase>UINT64_MAX-c->KernelSize)
        return Rc95LdrInvalidImage;
    end=c->KernelBase+c->KernelSize;
    if(c->KernelEntry<c->KernelBase||c->KernelEntry>=end)
        return Rc95LdrInvalidImage;
    if(c->KernelEntry>UINTPTR_MAX||c->KernelBase>UINTPTR_MAX||
       c->KernelSize>UINTPTR_MAX-c->KernelBase||c->HalBase>UINTPTR_MAX||
       !c->LoaderBlockAddress||!c->LoaderBlockSize||
       c->LoaderBlockAddress>UINTPTR_MAX||
       c->LoaderBlockSize>UINTPTR_MAX-c->LoaderBlockAddress||
       c->LoaderBlockAddress>UINT64_MAX-c->LoaderBlockSize)
        return Rc95LdrOutOfBounds;
    if(c->Architecture==Rc95LdrNtArchI386||c->Architecture==Rc95LdrNtArchPowerPc){
        if(sizeof(void*)!=4)return Rc95LdrUnsupported;
    }else if(c->Architecture==Rc95LdrNtArchAmd64){
        if(sizeof(void*)!=8)return Rc95LdrUnsupported;
    }else if(c->Architecture==Rc95LdrNtArchArm){
#if defined(_ARM_)
        if(sizeof(void*)!=4)return Rc95LdrUnsupported;
#else
        return Rc95LdrUnsupported;
#endif
    }else return Rc95LdrUnsupported;
    if(c->OsFamily==Rc95LdrOsWindowsNtLegacy &&
       c->NtMajorVersion==4 && c->NtMinorVersion==0)
        requiredAllowed&=~RC95LDR_HANDOFF_LOADER_EXTENSION;
    if(c->Architecture==Rc95LdrNtArchPowerPc||c->Architecture==Rc95LdrNtArchArm)
        requiredAllowed&=~RC95LDR_HANDOFF_PROCESSOR_CONTROL;
    if((c->CompletedRequirements&~known)||(c->RequiredRequirements&~known))return Rc95LdrInvalidParameter;
    if((c->CompletedRequirements&RC95LDR_HANDOFF_MEMORY_MAP)&&!c->MemoryRangeCount)
        return Rc95LdrInvalidParameter;
    if((c->CompletedRequirements&RC95LDR_HANDOFF_CONFIGURATION_TREE)&&!c->ConfigurationRoot)
        return Rc95LdrInvalidParameter;
    if((c->CompletedRequirements&RC95LDR_HANDOFF_CONFIGURATION_TREE)&&c->ConfigurationRoot>UINTPTR_MAX)
        return Rc95LdrOutOfBounds;
    if((c->CompletedRequirements&RC95LDR_HANDOFF_NLS)&&!c->NlsData)
        return Rc95LdrInvalidParameter;
    if((c->CompletedRequirements&RC95LDR_HANDOFF_NLS)&&c->NlsData>UINTPTR_MAX)
        return Rc95LdrOutOfBounds;
    if((c->CompletedRequirements&RC95LDR_HANDOFF_ARC_DISK_INFO)&&!c->ArcDiskInformation)
        return Rc95LdrInvalidParameter;
    if((c->CompletedRequirements&RC95LDR_HANDOFF_ARC_DISK_INFO)&&c->ArcDiskInformation>UINTPTR_MAX)
        return Rc95LdrOutOfBounds;
    if((c->CompletedRequirements&RC95LDR_HANDOFF_LOADER_EXTENSION)&&!c->LoaderExtension)
        return Rc95LdrInvalidParameter;
    if((c->CompletedRequirements&RC95LDR_HANDOFF_LOADER_EXTENSION)&&c->LoaderExtension>UINTPTR_MAX)
        return Rc95LdrOutOfBounds;
    if((c->CompletedRequirements&RC95LDR_HANDOFF_ARCH_STATE)&&!c->ArchitectureState)
        return Rc95LdrInvalidParameter;
    if((c->CompletedRequirements&RC95LDR_HANDOFF_STACK)&&
       (!c->StackBase||!c->StackSize||c->StackBase>UINT64_MAX-c->StackSize))
        return Rc95LdrInvalidParameter;
    if(c->CompletedRequirements&RC95LDR_HANDOFF_STACK){
        stackEnd=c->StackBase+c->StackSize;
        if(c->StackBase>UINTPTR_MAX||stackEnd-1>UINTPTR_MAX)return Rc95LdrOutOfBounds;
    }
    if((c->CompletedRequirements&RC95LDR_HANDOFF_PAGE_TABLES)&&!c->PageTableRoot)
        return Rc95LdrInvalidParameter;
    if(c->CompletedRequirements&RC95LDR_HANDOFF_PAGE_TABLES)
        if(c->PageTableRoot>UINTPTR_MAX)return Rc95LdrOutOfBounds;
    if(c->CompletedRequirements&RC95LDR_HANDOFF_ARCH_STATE)
        if(c->ArchitectureState>UINTPTR_MAX)return Rc95LdrOutOfBounds;
    if(c->CompletedRequirements&RC95LDR_HANDOFF_PROCESSOR_CONTROL) {
        if(c->Architecture==Rc95LdrNtArchI386) {
            const RC95LDR_I386_PROCESSOR_CONTROL *pc=&c->ProcessorControl.I386;
            if(!pc->PcrAddress||!pc->PcrSize||!pc->PrcbSize||
               pc->PcrAddress>UINTPTR_MAX||pc->PcrSize>UINTPTR_MAX-pc->PcrAddress||
               pc->PrcbOffset>pc->PcrSize||pc->PrcbSize>pc->PcrSize-pc->PrcbOffset||
               (pc->PcrAddress&0xfffu)||(pc->PrcbOffset&3u))
                return Rc95LdrInvalidParameter;
        } else if(c->Architecture==Rc95LdrNtArchAmd64) {
            const RC95LDR_AMD64_PROCESSOR_CONTROL *pc=&c->ProcessorControl.Amd64;
            if(!pc->PcrAddress||!pc->PcrSize||!pc->PrcbAddress||!pc->PrcbSize||
               pc->PcrAddress>UINTPTR_MAX||pc->PrcbAddress>UINTPTR_MAX||
               pc->PcrSize>UINTPTR_MAX-pc->PcrAddress||
               pc->PrcbSize>UINTPTR_MAX-pc->PrcbAddress||
               (pc->PcrAddress&0xfffu)||(pc->PrcbAddress&0xfffu)||
               (pc->PcrAddress<pc->PrcbAddress+pc->PrcbSize&&
                pc->PrcbAddress<pc->PcrAddress+pc->PcrSize))
                return Rc95LdrInvalidParameter;
        } else return Rc95LdrInvalidParameter;
    }
    if((c->CompletedRequirements&RC95LDR_HANDOFF_REGISTER_FRAME)&&(!c->RegisterFrameAddress||!c->RegisterFrameSize))return Rc95LdrInvalidParameter;
    if(c->RegisterFrameAddress>UINTPTR_MAX||
       c->RegisterFrameSize>UINTPTR_MAX-c->RegisterFrameAddress)return Rc95LdrOutOfBounds;
    if((c->Architecture==Rc95LdrNtArchI386||c->Architecture==Rc95LdrNtArchAmd64) &&
       !(c->CompletedRequirements&RC95LDR_HANDOFF_PROCESSOR_CONTROL))
        return Rc95LdrUnsupported;
    if((c->Architecture==Rc95LdrNtArchPowerPc||c->Architecture==Rc95LdrNtArchArm) &&
       (c->CompletedRequirements&RC95LDR_HANDOFF_PROCESSOR_CONTROL))
        return Rc95LdrInvalidParameter;
    if((c->CompletedRequirements&RC95LDR_HANDOFF_CPU_STATE)&&!c->CpuState)
        return Rc95LdrInvalidParameter;
    if(c->CompletedRequirements&RC95LDR_HANDOFF_CPU_STATE)
        if(c->CpuState>UINTPTR_MAX)return Rc95LdrOutOfBounds;
    if(c->Firmware==Rc95LdrFirmwareUefi &&
       (c->CompletedRequirements&RC95LDR_HANDOFF_FIRMWARE_STATE)){
        if(!c->FirmwareSystemTable||!c->FirmwareRuntimeServices||!c->FirmwareMemoryMap||
           !c->FirmwareMemoryMapSize||!c->FirmwareMemoryMapDescriptorSize||
           c->FirmwareSystemTable>UINTPTR_MAX||c->FirmwareRuntimeServices>UINTPTR_MAX||
           c->FirmwareMemoryMap>UINTPTR_MAX||
           c->FirmwareMemoryMapSize%c->FirmwareMemoryMapDescriptorSize)
            return Rc95LdrInvalidParameter;
    }
    if((c->CompletedRequirements&c->RequiredRequirements)!=c->RequiredRequirements)
        return Rc95LdrUnsupported;
    if(c->RequiredRequirements!=requiredAllowed)
        return Rc95LdrInvalidParameter;
    if(requiredAllowed&RC95LDR_HANDOFF_LOADER_EXTENSION) {
        if(!(c->CompletedRequirements&RC95LDR_HANDOFF_LOADER_EXTENSION))
            return Rc95LdrUnsupported;
    } else if(c->LoaderExtension ||
              (c->CompletedRequirements&RC95LDR_HANDOFF_LOADER_EXTENSION))
        return Rc95LdrInvalidParameter;
    return Rc95LdrSuccess;
}
