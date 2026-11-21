#include "rc95ldrhandoff.h"
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
    c->ArchitectureState=0x640000;c->CpuState=0x650000;
    c->CompletedRequirements=RC95LDR_HANDOFF_REQUIRED;
    c->RequiredRequirements=RC95LDR_HANDOFF_REQUIRED;
}
static void TestCompleteContext(void)
{
    RC95LDR_KERNEL_HANDOFF_CONTEXT c;InitContext(&c);
    assert(Rc95LdrValidateKernelHandoff(&c)==Rc95LdrSuccess);
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
    assert(Rc95LdrValidateKernelHandoff(&c)==Rc95LdrOutOfBounds);
}
int main(void)
{
    TestCompleteContext();TestMissingNlsRejected();
    TestIncompleteRequirementsRejected();TestPointerOverflowRejected();
    return 0;
}
