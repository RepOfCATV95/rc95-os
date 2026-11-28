#include "include/rc95ldruefiimageboot.h"
#include "include/rc95ldruefientry.h"
#include <string.h>

static int UefiArchitectureMatches(uint32_t Architecture)
{
#if defined(_M_IX86) || defined(__i386__)
    return Architecture == Rc95LdrModuleI386;
#elif defined(_M_AMD64) || defined(_M_X64) || \
      defined(_AMD64_) || defined(__x86_64__) || defined(__amd64__)
    return Architecture == Rc95LdrModuleAmd64;
#else
    (void)Architecture;
    return 0;
#endif
}

RC95LDR_STATUS
Rc95LdrUefiValidateImageBootConfig(
    const RC95LDR_UEFI_IMAGE_BOOT_CONFIG *C,
    uint32_t NativeArchitecture)
{
    if (!C || C->Signature != RC95LDR_UEFI_IMAGE_BOOT_CONFIG_SIGNATURE ||
        C->StructureSize < sizeof(*C) ||
        C->Version != RC95LDR_UEFI_IMAGE_BOOT_CONFIG_VERSION ||
        C->Architecture != NativeArchitecture ||
        !UefiArchitectureMatches(NativeArchitecture) ||
        !C->Partitions || !C->PartitionCapacity ||
        !C->Candidates || !C->CandidateCapacity ||
        !C->HiveBuffer || !C->HiveCapacity ||
        C->Startup.StructureSize < sizeof(C->Startup) ||
        C->Startup.Version != RC95LDR_STARTUP_ABI_VERSION ||
        C->Startup.Firmware != Rc95LdrFirmwareUefi ||
        !C->Startup.BootContext || !C->Startup.Platform ||
        !C->Startup.BuildBootRequest || !C->Startup.ReleaseBootRequest)
        return Rc95LdrInvalidParameter;
    return Rc95LdrSuccess;
}

static EFI_STATUS UefiStatusFromLoaderStatus(RC95LDR_STATUS Status)
{
    switch (Status)
    {
    case Rc95LdrSuccess: return EFI_SUCCESS;
    case Rc95LdrInvalidParameter: return EFI_INVALID_PARAMETER;
    case Rc95LdrOutOfMemory: return EFI_OUT_OF_RESOURCES;
    case Rc95LdrNotFound: return EFI_NOT_FOUND;
    case Rc95LdrUnsupported: return EFI_UNSUPPORTED;
    case Rc95LdrBufferTooSmall: return EFI_BUFFER_TOO_SMALL;
    default: return EFI_LOAD_ERROR;
    }
}

EFI_STATUS EFIAPI
Rc95LdrUefiImageEntry(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    RC95LDR_UEFI_IMAGE_CONTEXT Image;
    RC95LDR_UEFI_NATIVE_DISCOVERY_CONTEXT Discovery;
    RC95LDR_STATUS Status;
    uint32_t Architecture;

#if defined(_M_IX86) || defined(__i386__)
    Architecture = Rc95LdrModuleI386;
#elif defined(_M_AMD64) || defined(_M_X64) || \
      defined(_AMD64_) || defined(__x86_64__) || defined(__amd64__)
    Architecture = Rc95LdrModuleAmd64;
#else
    (void)ImageHandle;
    (void)SystemTable;
    return EFI_UNSUPPORTED;
#endif

    Status = Rc95LdrUefiValidateImageBootConfig(
        &Rc95LdrUefiImageBootConfiguration, Architecture);
    if (Status != Rc95LdrSuccess)
        return EFI_INVALID_PARAMETER;

    memset(&Image, 0, sizeof(Image));
    memset(&Discovery, 0, sizeof(Discovery));
    Status = Rc95LdrUefiInitializeImage(ImageHandle, SystemTable, &Image);
    if (Status != Rc95LdrSuccess)
        return UefiStatusFromLoaderStatus(Status);

    /*
     * NativeDiscoverAndStartWindows owns the full firmware transaction:
     * Block I/O + Device Path inventory, GPT/MBR enumeration, ESP/BCD
     * resolution, selected NTFS mount, provider scope, WINLOAD, and reverse
     * cleanup. A successful kernel handoff is non-returning.
     */
    Status = Rc95LdrUefiNativeDiscoverAndStartWindows(
        &Discovery, &Image, Architecture,
        Rc95LdrUefiImageBootConfiguration.Partitions,
        Rc95LdrUefiImageBootConfiguration.PartitionCapacity,
        Rc95LdrUefiImageBootConfiguration.Candidates,
        Rc95LdrUefiImageBootConfiguration.CandidateCapacity,
        &Rc95LdrUefiImageBootConfiguration.CandidateCount,
        Rc95LdrUefiImageBootConfiguration.HiveBuffer,
        Rc95LdrUefiImageBootConfiguration.HiveCapacity,
        &Rc95LdrUefiImageBootConfiguration.Startup,
        &Rc95LdrUefiImageBootConfiguration.Result,
        &Rc95LdrUefiImageBootConfiguration.SelectedInstallation,
        &Rc95LdrUefiImageBootConfiguration.SelectedEspIndex,
        Rc95LdrUefiImageBootConfiguration.TextUi);

    /* This code is reached only when discovery/WINLOAD returned an error or
       an explicitly recoverable status. Native discovery destroys its own
       mounts and firmware inventory before returning. */
    return UefiStatusFromLoaderStatus(Status);
}
