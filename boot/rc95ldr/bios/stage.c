#include "include/rc95ldrbiosstage.h"
#include <string.h>

/*
 * Protected-mode stage coordinator. The linked-in provider is responsible
 * for concrete filesystem, allocator, memory-map, native-preparation and
 * handoff callbacks. No FreeLoader symbol or compatibility adapter is used.
 */
RC95LDR_STATUS
Rc95LdrBiosStageMain(uint8_t BootDrive,
                     RC95LDR_BIOS_STARTUP *Startup,
                     const RC95LDR_BIOS_PLATFORM_PROVIDER *Provider)
{
    RC95LDR_STATUS Status;
    int BuildAttempted = 0;
    size_t MinimumProviderSize =
        offsetof(RC95LDR_BIOS_PLATFORM_PROVIDER, BuildPlatform) +
        sizeof(Provider->BuildPlatform);

    if (!Startup || !Provider ||
        Provider->StructureSize < MinimumProviderSize ||
        (Provider->Version != RC95LDR_BIOS_PLATFORM_PROVIDER_VERSION &&
         Provider->Version != RC95LDR_BIOS_PLATFORM_PROVIDER_VERSION_LEGACY) ||
        !Provider->BuildPlatform)
        return Rc95LdrInvalidParameter;

    memset(Startup, 0, sizeof(*Startup));
    Startup->StructureSize = sizeof(*Startup);
    Startup->Version = RC95LDR_BIOS_STARTUP_VERSION;

    /* BuildPlatform may allocate before it reports an error. Pair every
       attempted build with the optional version-2 release callback. */
    BuildAttempted = 1;
    Status = Provider->BuildPlatform(BootDrive, Startup, Provider->Context);
    if (Status != Rc95LdrSuccess)
        goto Cleanup;

    if (!Startup->BiosServices ||
        Startup->BiosServices->BootDrive != BootDrive ||
        !Startup->MemoryReservations ||
        !Startup->FirmwareMemoryRanges ||
        !Startup->FirmwareMemoryCapacity ||
        !Startup->BuildBootRequest ||
        !Startup->ReleaseBootRequest ||
        !Startup->Platform.OpenFile ||
        !Startup->Platform.ReadFileAt ||
        !Startup->Platform.CloseFile ||
        !Startup->Platform.AllocatePages ||
        !Startup->Platform.FreePages ||
        !Startup->Platform.ValidateAllocationExtent ||
        !Startup->Platform.GetMemoryMap ||
        !Startup->Platform.BuildNativePreparation ||
        !Startup->Platform.ReleaseNativePreparation ||
        !Startup->Platform.ExitFirmwareAndHandoff)
    {
        Status = Rc95LdrInvalidParameter;
        goto Cleanup;
    }

#if defined(RC95LDR_BIOS_NATIVE_I386)
    Status = Rc95LdrBiosPrepareNativePlatform(
        Startup->BiosServices, Startup->MemoryReservations);
    if (Status == Rc95LdrSuccess)
        Status = Rc95LdrBiosInvokeNativeEntry(Startup);
#elif defined(RC95LDR_BIOS_NATIVE_AMD64)
    /* AMD64 uses the provider's long-mode BIOS thunk rather than the i386
       real-mode gateway, but its low-memory scratch window and the reserved
       gateway/packet window must still be excluded from the allocator map. */
    Status = Rc95LdrBiosAddMemoryReservations(
        Startup->BiosServices, Startup->MemoryReservations);
    if (Status == Rc95LdrSuccess)
        Status = Rc95LdrBiosInvokeNativeEntry(Startup);
#else
    Status = Rc95LdrUnsupported;
#endif

Cleanup:
    if (BuildAttempted &&
        Provider->Version >= RC95LDR_BIOS_PLATFORM_PROVIDER_VERSION &&
        Provider->StructureSize >= sizeof(*Provider) &&
        Provider->ReleasePlatform)
        Provider->ReleasePlatform(BootDrive, Startup, Provider->Context);
    return Status;
}
