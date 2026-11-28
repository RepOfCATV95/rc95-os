#include "rc95ldrhandoff.h"
#include "rc95ldrmemorymap.h"
#include <assert.h>
#include <string.h>
static void InitContext(RC95LDR_KERNEL_HANDOFF_CONTEXT *c)
{
    memset(c,0,sizeof(*c));
    c->Signature=RC95LDR_HANDOFF_SIGNATURE;
    c->StructureSize=sizeof(*c);
    c->Version=RC95LDR_HANDOFF_VERSION;
    c->OsFamily=Rc95LdrOsRc95;
    c->Firmware=Rc95LdrFirmwareBios;
#if defined(_ARM_)
    c->Architecture=Rc95LdrNtArchArm;c->MachineType=0x1c0;
#elif defined(_PPC_) || defined(_M_PPC)
    c->Architecture=Rc95LdrNtArchPowerPc;c->MachineType=0x1f0;
#elif UINTPTR_MAX > UINT32_MAX
    c->Architecture=Rc95LdrNtArchAmd64;c->MachineType=0x8664;
#else
    c->Architecture=Rc95LdrNtArchI386;c->MachineType=0x14c;
#endif
    c->KernelBase=0x100000;c->KernelEntry=0x101000;c->KernelSize=0x20000;
    c->HalBase=0x200000;c->LoaderBlockAddress=0x300000;c->LoaderBlockSize=0x2000;
    c->MemoryRangeCount=1;c->StackBase=0x400000;c->StackSize=0x8000;
    c->PageTableRoot=0x500000;c->ConfigurationRoot=0x600000;c->NlsData=0x610000;
    c->ArcDiskInformation=0x620000;c->LoaderExtension=0x630000;
    c->ArchitectureState=0x640000;c->CpuState=0x650000;c->RegisterFrameAddress=0x660000;c->RegisterFrameSize=0x100;
    c->CompletedRequirements=RC95LDR_HANDOFF_RC95_REQUIRED;
    c->RequiredRequirements=RC95LDR_HANDOFF_RC95_REQUIRED;
    c->BiosFirmwareStateAddress=0x670000;
    c->BiosFirmwareStateSize=16;
    c->BiosFirmwareStateVersion=1;
    c->LoaderExtension=0;
#if UINTPTR_MAX > UINT32_MAX
    if (c->OsFamily == Rc95LdrOsRc95) {
        c->ProcessorControl.Rc95Amd64.PerCpuDataAddress=0x700000;
        c->ProcessorControl.Rc95Amd64.PerCpuDataSize=0x1000;
        c->ProcessorControl.Rc95Amd64.SchedulerStateAddress=0x702000;
        c->ProcessorControl.Rc95Amd64.SchedulerStateSize=0x1000;
    } else {
        c->ProcessorControl.Amd64.PcrAddress=0x700000;
        c->ProcessorControl.Amd64.PcrSize=0x1000;
        c->ProcessorControl.Amd64.PrcbAddress=0x702000;
        c->ProcessorControl.Amd64.PrcbSize=0x1000;
    }
#else
    if (c->OsFamily == Rc95LdrOsRc95) {
        c->ProcessorControl.Rc95I386.PerCpuDataAddress=0x700000;
        c->ProcessorControl.Rc95I386.PerCpuDataSize=0x1000;
        c->ProcessorControl.Rc95I386.SchedulerStateAddress=0x702000;
        c->ProcessorControl.Rc95I386.SchedulerStateSize=0x1000;
    } else {
        c->ProcessorControl.I386.PcrAddress=0x700000;
        c->ProcessorControl.I386.PcrSize=0x2000;
        c->ProcessorControl.I386.PrcbOffset=0x100;
        c->ProcessorControl.I386.PrcbSize=0x100;
    }
#endif
#if defined(_ARM_) || defined(_PPC_) || defined(_M_PPC)
    c->CompletedRequirements&=~RC95LDR_HANDOFF_PROCESSOR_CONTROL;
    c->RequiredRequirements&=~RC95LDR_HANDOFF_PROCESSOR_CONTROL;
#endif
}
static void TestCompleteContext(void)
{
    RC95LDR_KERNEL_HANDOFF_CONTEXT c;InitContext(&c);
    assert(Rc95LdrValidateKernelHandoff(&c)==Rc95LdrSuccess);
}
static void TestRc95RejectsNtProcessorControl(void)
{
    RC95LDR_KERNEL_HANDOFF_CONTEXT c;
    InitContext(&c);
#if UINTPTR_MAX > UINT32_MAX
    c.ProcessorControl.Rc95Amd64.PerCpuDataAddress = 0;
    c.ProcessorControl.Rc95Amd64.PerCpuDataSize = 0;
    c.ProcessorControl.Rc95Amd64.SchedulerStateAddress = 0;
    c.ProcessorControl.Rc95Amd64.SchedulerStateSize = 0;
    c.ProcessorControl.Amd64.PcrAddress = 0x700000;
    c.ProcessorControl.Amd64.PcrSize = 0x1000;
    c.ProcessorControl.Amd64.PrcbAddress = 0x702000;
    c.ProcessorControl.Amd64.PrcbSize = 0x1000;
#else
    c.ProcessorControl.Rc95I386.PerCpuDataAddress = 0;
    c.ProcessorControl.Rc95I386.PerCpuDataSize = 0;
    c.ProcessorControl.Rc95I386.SchedulerStateAddress = 0;
    c.ProcessorControl.Rc95I386.SchedulerStateSize = 0;
    c.ProcessorControl.I386.PcrAddress = 0x700000;
    c.ProcessorControl.I386.PcrSize = 0x2000;
    c.ProcessorControl.I386.PrcbOffset = 0x100;
    c.ProcessorControl.I386.PrcbSize = 0x100;
#endif
    assert(Rc95LdrValidateKernelHandoff(&c) == Rc95LdrInvalidParameter);
}
static void TestWindowsNtUsesNtProcessorControl(void)
{
    RC95LDR_KERNEL_HANDOFF_CONTEXT c;
    InitContext(&c);
    c.OsFamily = Rc95LdrOsWindowsNtLegacy;
    c.OsMajorVersion = 5;
    c.CompletedRequirements=RC95LDR_HANDOFF_REQUIRED;
    c.RequiredRequirements=RC95LDR_HANDOFF_REQUIRED;
    c.LoaderExtension=0;
    c.OsMinorVersion = 1;
#if UINTPTR_MAX > UINT32_MAX
    c.ProcessorControl.Amd64.PcrAddress = 0x700000;
    c.ProcessorControl.Amd64.PcrSize = 0x1000;
    c.ProcessorControl.Amd64.PrcbAddress = 0x702000;
    c.ProcessorControl.Amd64.PrcbSize = 0x1000;
#else
    c.ProcessorControl.I386.PcrAddress = 0x700000;
    c.ProcessorControl.I386.PcrSize = 0x2000;
    c.ProcessorControl.I386.PrcbOffset = 0x100;
    c.ProcessorControl.I386.PrcbSize = 0x100;
#endif
    assert(Rc95LdrValidateKernelHandoff(&c) == Rc95LdrSuccess);
}
static void TestMissingNlsRejected(void)
{
    RC95LDR_KERNEL_HANDOFF_CONTEXT c;InitContext(&c);c.NlsData=0;
    assert(Rc95LdrValidateKernelHandoff(&c)==Rc95LdrInvalidParameter);
}
static void TestIncompleteRequirementsRejected(void)
{
    RC95LDR_KERNEL_HANDOFF_CONTEXT c;InitContext(&c);
    c.CompletedRequirements&=~RC95LDR_HANDOFF_CPU_STATE;
    assert(Rc95LdrValidateKernelHandoff(&c)==Rc95LdrUnsupported);
}
static void TestPointerOverflowRejected(void)
{
    RC95LDR_KERNEL_HANDOFF_CONTEXT c;InitContext(&c);
    c.KernelBase=(uint64_t)UINTPTR_MAX-0x100u;c.KernelEntry=c.KernelBase+0x10u;c.KernelSize=0x200u;
#if UINTPTR_MAX == UINT64_MAX
    assert(Rc95LdrValidateKernelHandoff(&c)==Rc95LdrInvalidImage);
#else
    assert(Rc95LdrValidateKernelHandoff(&c)==Rc95LdrOutOfBounds);
#endif
}
static void TestBiosRequiresSnapshot(void)
{
    RC95LDR_KERNEL_HANDOFF_CONTEXT c;
    InitContext(&c);
    c.BiosFirmwareStateAddress=0;
    c.BiosFirmwareStateSize=0;
    c.BiosFirmwareStateVersion=0;
    c.CompletedRequirements&=~RC95LDR_HANDOFF_FIRMWARE_STATE;
    assert(Rc95LdrValidateKernelHandoff(&c)==Rc95LdrUnsupported);
}
static void TestBiosRejectsStaleEfiState(void)
{
    RC95LDR_KERNEL_HANDOFF_CONTEXT c;
    InitContext(&c);
    c.FirmwareSystemTable=0x700000;
    assert(Rc95LdrValidateKernelHandoff(&c)==Rc95LdrInvalidParameter);
}
static void TestUefiRejectsMalformedMemoryMap(void)
{
    RC95LDR_KERNEL_HANDOFF_CONTEXT c;
    InitContext(&c);
    c.Firmware=Rc95LdrFirmwareUefi;
    c.BiosFirmwareStateAddress=0;
    c.BiosFirmwareStateSize=0;
    c.BiosFirmwareStateVersion=0;
    c.FirmwareSystemTable=0x710000;
    c.FirmwareRuntimeServices=0x711000;
    c.FirmwareMemoryMap=0x712000;
    c.FirmwareMemoryMapSize=255;
    c.FirmwareMemoryMapDescriptorSize=48;
    assert(Rc95LdrValidateKernelHandoff(&c)==Rc95LdrInvalidParameter);
    c.FirmwareMemoryMapSize=240;
    c.FirmwareMemoryMapDescriptorSize=32;
    assert(Rc95LdrValidateKernelHandoff(&c)==Rc95LdrInvalidParameter);
    c.FirmwareMemoryMapDescriptorSize=48;
    c.FirmwareMemoryMap=UINTPTR_MAX-128u;
    assert(Rc95LdrValidateKernelHandoff(&c)==Rc95LdrInvalidParameter);
}

static void TestWindowsRamdiskHandoff(void)
{
    RC95LDR_KERNEL_HANDOFF_CONTEXT c;
    InitContext(&c);
    c.OsFamily=Rc95LdrOsWindowsModern;
    c.OsMajorVersion=10;
    c.CompletedRequirements=RC95LDR_HANDOFF_REQUIRED;
    c.RequiredRequirements=RC95LDR_HANDOFF_REQUIRED;
    c.LoaderExtension=0;
    c.OsMinorVersion=0;
    c.CompletedRequirements|=RC95LDR_HANDOFF_RAMDISK;
#if UINTPTR_MAX > UINT32_MAX
    c.ProcessorControl.Amd64.PcrAddress=0x700000;
    c.ProcessorControl.Amd64.PcrSize=0x1000;
    c.ProcessorControl.Amd64.PrcbAddress=0x702000;
    c.ProcessorControl.Amd64.PrcbSize=0x1000;
#else
    c.ProcessorControl.I386.PcrAddress=0x700000;
    c.ProcessorControl.I386.PcrSize=0x2000;
    c.ProcessorControl.I386.PrcbOffset=0x100;
    c.ProcessorControl.I386.PrcbSize=0x100;
#endif
    c.RamdiskPhysicalBase=0x800000;
    c.RamdiskAllocationSize=0x20000;
    c.RamdiskImageOffset=0x1000;
    c.RamdiskImageLength=0x18000;
    assert(Rc95LdrValidateKernelHandoff(&c)==Rc95LdrSuccess);
    c.RamdiskImageLength=0x30000;
    assert(Rc95LdrValidateKernelHandoff(&c)==Rc95LdrInvalidParameter);
}

static void TestRamdiskAllocationType(void)
{
    RC95LDR_NATIVE_ALLOCATION_SET set;
    RC95LDR_MEMORY_RESERVATION_SET reservations;
    RC95LDR_FIRMWARE_MEMORY_RANGE firmware;
    RC95LDR_MEMORYMAP_RESULT map;
    size_t i;
    int found=0;
    memset(&set,0,sizeof(set));
    memset(&reservations,0,sizeof(reservations));
    memset(&firmware,0,sizeof(firmware));
    firmware.Base=0x100000;
    firmware.Length=0x1000000;
    firmware.Kind=Rc95LdrFirmwareMemoryAvailable;
    assert(Rc95LdrTrackNativeAllocation(&set,RC95LDR_ALLOC_RAMDISK,
        0x800000,0x20000)==Rc95LdrSuccess);
    assert(set.Count==1 && set.Entries[0].MemoryType==LoaderXIPRom);
    /* Physical allocations are page-granular; reject ranges that would
       otherwise be rounded into ownership of a neighbouring page. */
    assert(Rc95LdrTrackNativeAllocation(&set,RC95LDR_ALLOC_RAMDISK,
        0x820001,0x1000)==Rc95LdrInvalidParameter);
    assert(set.Count==1);
    assert(Rc95LdrTrackNativeAllocation(&set,RC95LDR_ALLOC_RAMDISK,
        0x810000,0x10000)==Rc95LdrCorruptData);
    assert(Rc95LdrReserveMemoryRange(&reservations,set.Entries[0].PhysicalBase,
        set.Entries[0].Length,set.Entries[0].MemoryType)==Rc95LdrSuccess);
    assert(Rc95LdrBuildFinalMemoryMap(&firmware,1,reservations.Entries,
        reservations.Count,&map)==Rc95LdrSuccess);
    for(i=0;i<map.RangeCount;++i)
        if(map.Ranges[i].Type==LoaderXIPRom &&
           map.Ranges[i].BasePage==0x800000/4096u &&
           map.Ranges[i].PageCount==0x20000/4096u)
            found=1;
    assert(found);
}

int main(void)
{
    TestCompleteContext();TestRc95RejectsNtProcessorControl();TestWindowsNtUsesNtProcessorControl();TestMissingNlsRejected();
    TestIncompleteRequirementsRejected();TestPointerOverflowRejected();
    TestWindowsRamdiskHandoff();TestRamdiskAllocationType();TestBiosRequiresSnapshot();TestBiosRejectsStaleEfiState();TestUefiRejectsMalformedMemoryMap();
    return 0;
}
