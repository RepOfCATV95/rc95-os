#include "rc95ldrmodule.h"
#include <assert.h>
#include <string.h>

static RC95LDR_STATUS RC95LDR_MODULE_ABI Open(void *C, const uint16_t *P, RC95LDR_MODULE_FILE *F)
{ (void)C; (void)P; (void)F; return Rc95LdrSuccess; }
static RC95LDR_STATUS RC95LDR_MODULE_ABI Read(void *C, const RC95LDR_MODULE_FILE *F, uint64_t O, void *B, size_t N)
{ (void)C; (void)F; (void)O; (void)B; (void)N; return Rc95LdrSuccess; }
static void RC95LDR_MODULE_ABI Close(void *C, RC95LDR_MODULE_FILE *F)
{ (void)C; (void)F; }
static RC95LDR_STATUS RC95LDR_MODULE_ABI Allocate(void *C, uint64_t N, uint64_t A, uint64_t *P)
{ (void)C; (void)N; (void)A; (void)P; return Rc95LdrSuccess; }
static void RC95LDR_MODULE_ABI Free(void *C, uint64_t P, uint64_t N)
{ (void)C; (void)P; (void)N; }
static RC95LDR_STATUS RC95LDR_MODULE_ABI Validate(void *C, uint64_t P, uint64_t V, uint64_t N, uint64_t A)
{ (void)C; (void)P; (void)V; (void)N; (void)A; return Rc95LdrSuccess; }
static RC95LDR_STATUS RC95LDR_MODULE_ABI Quiesce(void *C)
{ (void)C; return Rc95LdrSuccess; }

int main(void)
{
    RC95LDR_MODULE_SERVICES S;
    RC95LDR_MODULE_HANDOFF H;
    memset(&S, 0, sizeof(S));
    S.Signature = RC95LDR_MODULE_SERVICES_SIGNATURE;
    S.StructureSize = sizeof(S);
    S.Version = RC95LDR_MODULE_SERVICES_VERSION;
    S.Firmware = Rc95LdrModuleBios;
    S.Architecture = Rc95LdrModuleI386;
    S.OpenFile = Open; S.ReadFileAt = Read; S.CloseFile = Close;
    S.AllocatePages = Allocate; S.FreePages = Free;
    S.ValidateAllocationExtent = Validate; S.Quiesce = Quiesce;
    assert(Rc95LdrValidateModuleServices(&S, sizeof(S), Rc95LdrModuleBios,
        Rc95LdrModuleI386) == Rc95LdrSuccess);
    assert(Rc95LdrValidateModuleServices(&S, sizeof(S) - 1u,
        Rc95LdrModuleBios, Rc95LdrModuleI386) == Rc95LdrInvalidParameter);
    S.ReadFileAt = NULL;
    assert(Rc95LdrValidateModuleServices(&S, sizeof(S), Rc95LdrModuleBios,
        Rc95LdrModuleI386) == Rc95LdrInvalidParameter);
    S.ReadFileAt = Read;
    S.Quiesce = NULL;
    assert(Rc95LdrValidateModuleServices(&S, sizeof(S), Rc95LdrModuleBios,
        Rc95LdrModuleI386) == Rc95LdrInvalidParameter);
    S.Quiesce = Quiesce;
    S.Version++;
    assert(Rc95LdrValidateModuleServices(&S, sizeof(S), Rc95LdrModuleBios,
        Rc95LdrModuleI386) == Rc95LdrInvalidParameter);
    S.Version = RC95LDR_MODULE_SERVICES_VERSION;
    S.Architecture = Rc95LdrModuleAmd64;
    assert(Rc95LdrValidateModuleServices(&S, sizeof(S), Rc95LdrModuleBios,
        Rc95LdrModuleI386) == Rc95LdrInvalidParameter);

    memset(&H, 0, sizeof(H));
    H.Signature = RC95LDR_MODULE_SIGNATURE;
    H.StructureSize = sizeof(H);
    H.Version = RC95LDR_MODULE_HANDOFF_VERSION;
    H.ModuleKind = Rc95LdrModuleVeneer;
    H.Firmware = Rc95LdrModuleBios;
    H.Architecture = Rc95LdrModuleI386;
    H.ImageBase = 0x100000;
    H.ImageSize = 0x20000;
    H.EntryPoint = 0x100100;
    H.Services = (uint64_t)(uintptr_t)&S;
    H.ServicesSize = sizeof(S);
    assert(Rc95LdrValidateModuleHandoff(&H, sizeof(H), Rc95LdrModuleVeneer,
        Rc95LdrModuleBios, Rc95LdrModuleI386) == Rc95LdrSuccess);
    H.Services = UINT64_MAX - 3u;
    H.ServicesSize = 16u;
    assert(Rc95LdrValidateModuleHandoff(&H, sizeof(H), Rc95LdrModuleVeneer,
        Rc95LdrModuleBios, Rc95LdrModuleI386) == Rc95LdrOutOfBounds);
    return 0;
}
