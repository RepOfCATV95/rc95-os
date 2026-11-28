#include "include/rc95ldrbiosmodule.h"

#if defined(RC95LDR_BIOS_NATIVE_I386) || defined(RC95LDR_BIOS_NATIVE_AMD64)
extern RC95LDR_STATUS RC95LDR_MODULE_ABI Rc95LdrBiosCallModuleEntry(
    uintptr_t EntryPoint, const RC95LDR_MODULE_HANDOFF *Handoff);

RC95LDR_STATUS RC95LDR_MODULE_ABI
Rc95LdrBiosNativeModuleTransfer(void *Context, uint64_t EntryPoint,
                                const RC95LDR_MODULE_HANDOFF *Handoff)
{
    RC95LDR_STATUS Status;
    (void)Context;
    if (!EntryPoint || !Handoff || EntryPoint > (uint64_t)UINTPTR_MAX ||
        EntryPoint != Handoff->EntryPoint ||
        !Handoff->Services || !Handoff->ServicesSize)
        return Rc95LdrInvalidParameter;
#if defined(RC95LDR_BIOS_NATIVE_I386)
    if (Handoff->Architecture != Rc95LdrModuleI386)
        return Rc95LdrUnsupported;
#elif defined(RC95LDR_BIOS_NATIVE_AMD64)
    if (Handoff->Architecture != Rc95LdrModuleAmd64)
        return Rc95LdrUnsupported;
#else
    return Rc95LdrUnsupported;
#endif
    Status = Rc95LdrValidateModuleHandoff(Handoff, sizeof(*Handoff),
        Handoff->ModuleKind, Rc95LdrModuleBios, Handoff->Architecture);
    if (Status != Rc95LdrSuccess)
        return Status;
    Status = Rc95LdrValidateModuleServices(
        (const RC95LDR_MODULE_SERVICES *)(uintptr_t)Handoff->Services,
        (size_t)Handoff->ServicesSize, Rc95LdrModuleBios,
        Handoff->Architecture);
    if (Status != Rc95LdrSuccess)
        return Status;
    return Rc95LdrBiosCallModuleEntry((uintptr_t)EntryPoint, Handoff);
}
#else
RC95LDR_STATUS
Rc95LdrBiosNativeModuleTransfer(void *Context, uint64_t EntryPoint,
                                const RC95LDR_MODULE_HANDOFF *Handoff)
{
    (void)Context;
    (void)EntryPoint;
    (void)Handoff;
    return Rc95LdrUnsupported;
}
#endif

RC95LDR_STATUS
Rc95LdrBiosLoadAndInvokeNativeModule(
    const RC95LDR_WINLOAD_PLATFORM *Platform,
    const uint16_t *Path,
    uint32_t ModuleKind,
    uint32_t Architecture,
    const RC95LDR_MODULE_HANDOFF *ParentHandoff,
    size_t ParentHandoffSize)
{
    return Rc95LdrBiosLoadAndInvokeModule(
        Platform, Path, ModuleKind, Architecture, ParentHandoff,
        ParentHandoffSize, Rc95LdrBiosNativeModuleTransfer, NULL);
}
