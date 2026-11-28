#include "rc95ldruefiimageboot.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

RC95LDR_UEFI_IMAGE_BOOT_CONFIG Rc95LdrUefiImageBootConfiguration;

static RC95LDR_PARTITION_INFO Partitions[2];
static RC95LDR_BCD_BOOT_CANDIDATE Candidates[2];
static unsigned char Hive[4096];
static RC95LDR_BOOT_CONTEXT BootContext;
static RC95LDR_WINLOAD_PLATFORM Platform;

static RC95LDR_STATUS BuildRequest(void *Context,
                                   RC95LDR_WINLOAD_REQUEST *Request)
{
    (void)Context; (void)Request;
    return Rc95LdrUnsupported;
}
static void ReleaseRequest(void *Context, RC95LDR_WINLOAD_REQUEST *Request)
{
    (void)Context; (void)Request;
}

static void InitializeValid(void)
{
    RC95LDR_UEFI_IMAGE_BOOT_CONFIG *C =
        &Rc95LdrUefiImageBootConfiguration;
    memset(C, 0, sizeof(*C));
    memset(&BootContext, 0, sizeof(BootContext));
    memset(&Platform, 0, sizeof(Platform));
    C->Signature = RC95LDR_UEFI_IMAGE_BOOT_CONFIG_SIGNATURE;
    C->StructureSize = sizeof(*C);
    C->Version = RC95LDR_UEFI_IMAGE_BOOT_CONFIG_VERSION;
#if defined(_M_IX86) || defined(__i386__)
    C->Architecture = Rc95LdrModuleI386;
#else
    C->Architecture = Rc95LdrModuleAmd64;
#endif
    C->Partitions = Partitions;
    C->PartitionCapacity = 2;
    C->Candidates = Candidates;
    C->CandidateCapacity = 2;
    C->HiveBuffer = Hive;
    C->HiveCapacity = sizeof(Hive);
    C->Startup.StructureSize = sizeof(C->Startup);
    C->Startup.Version = RC95LDR_STARTUP_ABI_VERSION;
    C->Startup.Firmware = Rc95LdrFirmwareUefi;
    C->Startup.BootContext = &BootContext;
    C->Startup.Platform = &Platform;
    C->Startup.BuildBootRequest = BuildRequest;
    C->Startup.ReleaseBootRequest = ReleaseRequest;
}

int main(void)
{
    RC95LDR_UEFI_IMAGE_BOOT_CONFIG Saved;
    uint32_t Architecture;
#if defined(_M_IX86) || defined(__i386__)
    Architecture = Rc95LdrModuleI386;
#else
    Architecture = Rc95LdrModuleAmd64;
#endif
    InitializeValid();
    assert(Rc95LdrUefiValidateImageBootConfig(
        &Rc95LdrUefiImageBootConfiguration, Architecture) == Rc95LdrSuccess);

    Saved = Rc95LdrUefiImageBootConfiguration;
    Rc95LdrUefiImageBootConfiguration.Signature = 0;
    assert(Rc95LdrUefiValidateImageBootConfig(
        &Rc95LdrUefiImageBootConfiguration, Architecture) ==
        Rc95LdrInvalidParameter);
    Rc95LdrUefiImageBootConfiguration = Saved;

    Rc95LdrUefiImageBootConfiguration.Version++;
    assert(Rc95LdrUefiValidateImageBootConfig(
        &Rc95LdrUefiImageBootConfiguration, Architecture) ==
        Rc95LdrInvalidParameter);
    Rc95LdrUefiImageBootConfiguration = Saved;

    Rc95LdrUefiImageBootConfiguration.Partitions = NULL;
    assert(Rc95LdrUefiValidateImageBootConfig(
        &Rc95LdrUefiImageBootConfiguration, Architecture) ==
        Rc95LdrInvalidParameter);
    Rc95LdrUefiImageBootConfiguration = Saved;

    Rc95LdrUefiImageBootConfiguration.Startup.Firmware = Rc95LdrFirmwareBios;
    assert(Rc95LdrUefiValidateImageBootConfig(
        &Rc95LdrUefiImageBootConfiguration, Architecture) ==
        Rc95LdrInvalidParameter);
    Rc95LdrUefiImageBootConfiguration = Saved;

    assert(Rc95LdrUefiValidateImageBootConfig(
        &Rc95LdrUefiImageBootConfiguration,
        Architecture == Rc95LdrModuleAmd64
            ? Rc95LdrModuleI386 : Rc95LdrModuleAmd64) ==
        Rc95LdrInvalidParameter);
    puts("RC95LDR UEFI PE entry configuration tests passed");
    return 0;
}
