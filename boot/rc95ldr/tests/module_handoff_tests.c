#include "rc95ldrmodule.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>

static RC95LDR_STATUS Transfer(void *Context, uint64_t Entry,
                               const RC95LDR_MODULE_HANDOFF *Handoff)
{
    unsigned *Calls = (unsigned *)Context;
    (void)Handoff;
    assert(Entry == 0x100100u);
    ++*Calls;
    return Rc95LdrSuccess;
}

static RC95LDR_MODULE_HANDOFF Valid(void)
{
    RC95LDR_MODULE_HANDOFF H;
    memset(&H, 0, sizeof(H));
    H.Signature = RC95LDR_MODULE_SIGNATURE;
    H.StructureSize = sizeof(H);
    H.Version = RC95LDR_MODULE_HANDOFF_VERSION;
    H.ModuleKind = Rc95LdrModuleVeneer;
    H.Firmware = Rc95LdrModuleBios;
    H.Architecture = Rc95LdrModuleI386;
    H.ImageBase = 0x100000u;
    H.ImageSize = 0x20000u;
    H.EntryPoint = 0x100100u;
    H.Services = 0x101000u;
    H.ServicesSize = 0x100u;
    return H;
}

int main(void)
{
    RC95LDR_MODULE_HANDOFF H = Valid();
    unsigned Calls = 0;

    assert(Rc95LdrValidateModuleHandoff(&H, sizeof(H),
        Rc95LdrModuleVeneer, Rc95LdrModuleBios,
        Rc95LdrModuleI386) == Rc95LdrSuccess);
    assert(Rc95LdrInvokeModule(&H, sizeof(H), Rc95LdrModuleVeneer,
        Rc95LdrModuleBios, Rc95LdrModuleI386, Transfer,
        &Calls) == Rc95LdrSuccess);
    assert(Calls == 1);

    assert(Rc95LdrValidateModuleHandoff(&H, sizeof(H) - 1,
        Rc95LdrModuleVeneer, Rc95LdrModuleBios,
        Rc95LdrModuleI386) == Rc95LdrInvalidParameter);
    H.EntryPoint = UINT64_MAX - 4u;
    assert(Rc95LdrValidateModuleHandoff(&H, sizeof(H),
        Rc95LdrModuleVeneer, Rc95LdrModuleBios,
        Rc95LdrModuleI386) == Rc95LdrOutOfBounds);
    H = Valid(); H.ImageSize = UINT64_MAX;
    assert(Rc95LdrValidateModuleHandoff(&H, sizeof(H),
        Rc95LdrModuleVeneer, Rc95LdrModuleBios,
        Rc95LdrModuleI386) == Rc95LdrOutOfBounds);
    H = Valid(); H.Services = H.ImageBase + H.ImageSize - 8u;
    H.ServicesSize = 16u;
    assert(Rc95LdrValidateModuleHandoff(&H, sizeof(H),
        Rc95LdrModuleVeneer, Rc95LdrModuleBios,
        Rc95LdrModuleI386) == Rc95LdrOutOfBounds);
    H = Valid(); H.Reserved = 1u;
    assert(Rc95LdrValidateModuleHandoff(&H, sizeof(H),
        Rc95LdrModuleVeneer, Rc95LdrModuleBios,
        Rc95LdrModuleI386) == Rc95LdrInvalidParameter);
    H = Valid(); H.Flags = 0x80000000u;
    assert(Rc95LdrValidateModuleHandoff(&H, sizeof(H),
        Rc95LdrModuleVeneer, Rc95LdrModuleBios,
        Rc95LdrModuleI386) == Rc95LdrInvalidParameter);
    H = Valid(); H.ModuleKind = Rc95LdrModuleWinload;
    assert(Rc95LdrInvokeModule(&H, sizeof(H), Rc95LdrModuleVeneer,
        Rc95LdrModuleBios, Rc95LdrModuleI386, Transfer,
        &Calls) == Rc95LdrInvalidParameter);
    assert(Calls == 1);
    return 0;
}
