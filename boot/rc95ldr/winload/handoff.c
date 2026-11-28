#include "rc95ldrhandoff.h"
#include <stdint.h>
#include <limits.h>
RC95LDR_STATUS Rc95LdrValidateKernelHandoff(const RC95LDR_KERNEL_HANDOFF_CONTEXT *c)
{
    uint64_t end,stackEnd;
    uint32_t requiredAllowed;
    const uint32_t known=RC95LDR_HANDOFF_REQUIRED|RC95LDR_HANDOFF_RAMDISK;
    if(!c||c->Signature!=RC95LDR_HANDOFF_SIGNATURE||
       c->StructureSize<sizeof(*c)||c->Version!=RC95LDR_HANDOFF_VERSION||
       c->OsFamily<Rc95LdrOsWindowsNtLegacy||c->OsFamily>Rc95LdrOsRc95||
       (c->Firmware!=Rc95LdrFirmwareBios&&c->Firmware!=Rc95LdrFirmwareUefi)||
       c->Architecture<Rc95LdrNtArchI386||c->Architecture>Rc95LdrNtArchArm)
        return Rc95LdrInvalidParameter;
    if(!c->KernelEntry||!c->KernelBase||!c->KernelSize||!c->HalBase||
       c->KernelBase>UINT64_MAX-c->KernelSize)
        return Rc95LdrInvalidImage;
    requiredAllowed = (c->OsFamily == Rc95LdrOsRc95) ?
        RC95LDR_HANDOFF_RC95_REQUIRED : RC95LDR_HANDOFF_REQUIRED;
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
       c->OsMajorVersion==4 && c->OsMinorVersion==0)
        requiredAllowed&=~RC95LDR_HANDOFF_LOADER_EXTENSION;
    if(c->Architecture==Rc95LdrNtArchPowerPc||c->Architecture==Rc95LdrNtArchArm)
        requiredAllowed&=~RC95LDR_HANDOFF_PROCESSOR_CONTROL;
    if((c->CompletedRequirements&~known)||(c->RequiredRequirements&~known))return Rc95LdrInvalidParameter;
    if(c->CompletedRequirements&RC95LDR_HANDOFF_RAMDISK) {
        if((c->OsFamily!=Rc95LdrOsWindowsNtLegacy&&c->OsFamily!=Rc95LdrOsWindowsModern)||
           !c->RamdiskPhysicalBase||!c->RamdiskAllocationSize||!c->RamdiskImageLength||
           c->RamdiskPhysicalBase>UINT64_MAX-c->RamdiskAllocationSize||
           c->RamdiskImageOffset>c->RamdiskAllocationSize||
           c->RamdiskImageLength>c->RamdiskAllocationSize-c->RamdiskImageOffset||
           (c->RamdiskPhysicalBase&0xfffu)||(c->RamdiskAllocationSize&0xfffu)||
           c->RamdiskPhysicalBase>UINTPTR_MAX||
           c->RamdiskAllocationSize>UINTPTR_MAX-c->RamdiskPhysicalBase)
            return Rc95LdrInvalidParameter;
    } else if(c->RamdiskPhysicalBase||c->RamdiskAllocationSize||
              c->RamdiskImageOffset||c->RamdiskImageLength)
        return Rc95LdrInvalidParameter;
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
        if (c->Architecture == Rc95LdrNtArchI386 &&
            c->OsFamily == Rc95LdrOsRc95) {
            const RC95LDR_RC95_I386_PROCESSOR_STATE *pc = &c->ProcessorControl.Rc95I386;
            if (!pc->PerCpuDataAddress || !pc->PerCpuDataSize ||
                !pc->SchedulerStateAddress || !pc->SchedulerStateSize ||
                pc->PerCpuDataAddress > UINTPTR_MAX || pc->SchedulerStateAddress > UINTPTR_MAX ||
                pc->PerCpuDataSize > UINTPTR_MAX - pc->PerCpuDataAddress ||
                pc->SchedulerStateSize > UINTPTR_MAX - pc->SchedulerStateAddress ||
                (pc->PerCpuDataAddress & 0xfffu) || (pc->SchedulerStateAddress & 0xfffu) ||
                (pc->PerCpuDataAddress < pc->SchedulerStateAddress + pc->SchedulerStateSize &&
                 pc->SchedulerStateAddress < pc->PerCpuDataAddress + pc->PerCpuDataSize))
                return Rc95LdrInvalidParameter;
        } else if (c->Architecture == Rc95LdrNtArchI386) {
            const RC95LDR_I386_PROCESSOR_CONTROL *pc = &c->ProcessorControl.I386;
            if (!pc->PcrAddress || !pc->PcrSize || !pc->PrcbSize ||
                pc->PcrAddress > UINTPTR_MAX || pc->PcrSize > UINTPTR_MAX - pc->PcrAddress ||
                pc->PrcbOffset > pc->PcrSize || pc->PrcbSize > pc->PcrSize - pc->PrcbOffset ||
                (pc->PcrAddress & 0xfffu) || (pc->PrcbOffset & 3u))
                return Rc95LdrInvalidParameter;
        } else if (c->Architecture == Rc95LdrNtArchAmd64 &&
                   c->OsFamily == Rc95LdrOsRc95) {
            const RC95LDR_RC95_AMD64_PROCESSOR_STATE *pc = &c->ProcessorControl.Rc95Amd64;
            if (!pc->PerCpuDataAddress || !pc->PerCpuDataSize ||
                !pc->SchedulerStateAddress || !pc->SchedulerStateSize ||
                pc->PerCpuDataAddress > UINTPTR_MAX || pc->SchedulerStateAddress > UINTPTR_MAX ||
                pc->PerCpuDataSize > UINTPTR_MAX - pc->PerCpuDataAddress ||
                pc->SchedulerStateSize > UINTPTR_MAX - pc->SchedulerStateAddress ||
                (pc->PerCpuDataAddress & 0xfffu) || (pc->SchedulerStateAddress & 0xfffu) ||
                (pc->PerCpuDataAddress < pc->SchedulerStateAddress + pc->SchedulerStateSize &&
                 pc->SchedulerStateAddress < pc->PerCpuDataAddress + pc->PerCpuDataSize))
                return Rc95LdrInvalidParameter;
        } else if (c->Architecture == Rc95LdrNtArchAmd64) {
            const RC95LDR_AMD64_PROCESSOR_CONTROL *pc = &c->ProcessorControl.Amd64;
            if (!pc->PcrAddress || !pc->PcrSize || !pc->PrcbAddress || !pc->PrcbSize ||
                pc->PcrAddress > UINTPTR_MAX || pc->PrcbAddress > UINTPTR_MAX ||
                pc->PcrSize > UINTPTR_MAX - pc->PcrAddress ||
                pc->PrcbSize > UINTPTR_MAX - pc->PrcbAddress ||
                (pc->PcrAddress & 0xfffu) || (pc->PrcbAddress & 0xfffu) ||
                (pc->PcrAddress < pc->PrcbAddress + pc->PrcbSize &&
                 pc->PrcbAddress < pc->PcrAddress + pc->PcrSize))
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
           !c->FirmwareMemoryMapSize||c->FirmwareMemoryMapDescriptorSize<40u||
           c->FirmwareSystemTable>UINTPTR_MAX||c->FirmwareRuntimeServices>UINTPTR_MAX||
           c->FirmwareMemoryMap>UINTPTR_MAX||
           c->FirmwareMemoryMapSize%c->FirmwareMemoryMapDescriptorSize||
           c->FirmwareMemoryMapSize>UINTPTR_MAX-c->FirmwareMemoryMap)
            return Rc95LdrInvalidParameter;
    }
    /* BIOS has no EFI system/runtime services or EFI descriptor map. These
       fields must not accidentally retain stale values from a prior UEFI
       preparation transaction. */
    if(c->Firmware==Rc95LdrFirmwareBios &&
       (c->FirmwareSystemTable||c->FirmwareRuntimeServices||c->FirmwareMemoryMap||
        c->FirmwareMemoryMapSize||c->FirmwareMemoryMapDescriptorSize))
        return Rc95LdrInvalidParameter;
    if(c->Firmware==Rc95LdrFirmwareBios &&
       (c->CompletedRequirements&RC95LDR_HANDOFF_FIRMWARE_STATE)) {
        if(!c->BiosFirmwareStateAddress || c->BiosFirmwareStateAddress>UINTPTR_MAX ||
           c->BiosFirmwareStateSize<16u ||
           c->BiosFirmwareStateSize>UINTPTR_MAX-c->BiosFirmwareStateAddress ||
           c->BiosFirmwareStateVersion!=1u)
            return Rc95LdrInvalidParameter;
    } else if(c->BiosFirmwareStateAddress || c->BiosFirmwareStateSize ||
              c->BiosFirmwareStateVersion) {
        return Rc95LdrInvalidParameter;
    }
    if(c->Firmware==Rc95LdrFirmwareUefi &&
       (c->BiosFirmwareStateAddress || c->BiosFirmwareStateSize ||
        c->BiosFirmwareStateVersion))
        return Rc95LdrInvalidParameter;
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
