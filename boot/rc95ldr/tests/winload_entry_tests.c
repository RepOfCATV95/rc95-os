#include "../winload/include/rc95ldrentry.h"
#include <stdio.h>
#include <string.h>

static unsigned Builds, Releases;
static int BuildMode;
static uint32_t LastGraphicsDisabled, LastGraphicsPolicyValid;

static RC95LDR_STATUS BuildRequest(void *Context, RC95LDR_WINLOAD_REQUEST *Request)
{
    RC95LDR_BOOT_FIRMWARE *Firmware = (RC95LDR_BOOT_FIRMWARE *)Context;
    Builds++;
    Request->Firmware = (BuildMode == 2)
        ? (*Firmware == Rc95LdrFirmwareBios ? Rc95LdrFirmwareUefi : Rc95LdrFirmwareBios)
        : *Firmware;
    Request->OsFamily = BuildMode == 1 ? Rc95LdrOsLiberNt : Rc95LdrOsRc95;
    Request->ResumePolicy = Rc95LdrResumeDisabled;
    return BuildMode ? Rc95LdrSuccess : Rc95LdrNotFound;
}

static void ReleaseRequest(void *Context, RC95LDR_WINLOAD_REQUEST *Request)
{
    (void)Context;
    LastGraphicsDisabled=Request->GraphicsModeDisabled;
    LastGraphicsPolicyValid=Request->GraphicsModePolicyValid;
    Releases++;
}

static void Initialize(RC95LDR_BOOT_FIRMWARE Firmware,
                        RC95LDR_BOOT_CONTEXT *Boot,
                        RC95LDR_WINLOAD_PLATFORM *Platform,
                        RC95LDR_WINLOAD_STARTUP *Startup,
                        RC95LDR_WINLOAD_RESULT *Result)
{
    memset(Boot, 0, sizeof(*Boot));
    memset(Platform, 0, sizeof(*Platform));
    memset(Startup, 0, sizeof(*Startup));
    memset(Result, 0xA5, sizeof(*Result));

    Platform->StructureSize = offsetof(RC95LDR_WINLOAD_PLATFORM, TextUi);
    Platform->Version = RC95LDR_WINLOAD_ABI_VERSION;
    Startup->StructureSize = sizeof(*Startup);
    Startup->Version = RC95LDR_STARTUP_ABI_VERSION;
    Startup->Firmware = Firmware;
    Startup->Context = &Startup->Firmware;
    Startup->BootContext = Boot;
    Startup->Platform = Platform;
    Startup->BuildBootRequest = BuildRequest;
    Startup->ReleaseBootRequest = ReleaseRequest;
}

static RC95LDR_STATUS Dispatch(RC95LDR_BOOT_FIRMWARE Firmware,
                               const RC95LDR_WINLOAD_STARTUP *Startup,
                               RC95LDR_WINLOAD_RESULT *Result)
{
    return Firmware == Rc95LdrFirmwareBios
        ? Rc95LdrBiosWinloadEntry(Startup, Result)
        : Rc95LdrUefiWinloadEntry(Startup, Result);
}

static int RunBuildFailure(RC95LDR_BOOT_FIRMWARE Firmware)
{
    RC95LDR_BOOT_CONTEXT Boot;
    RC95LDR_WINLOAD_PLATFORM Platform;
    RC95LDR_WINLOAD_STARTUP Startup;
    RC95LDR_WINLOAD_RESULT Result;
    RC95LDR_STATUS Status;

    Initialize(Firmware, &Boot, &Platform, &Startup, &Result);
    Builds = Releases = 0;
    BuildMode = 0;
    Status = Dispatch(Firmware, &Startup, &Result);

    return Status == Rc95LdrNotFound &&
           Builds == 1 && Releases == 1 &&
           Boot.CurrentStage == Rc95LdrStageStartup &&
           Boot.LastError.Status == Rc95LdrNotFound &&
           Boot.LastError.Detail == 0x200u &&
           Result.StructureSize == sizeof(Result) &&
           Result.Version == RC95LDR_WINLOAD_ABI_VERSION &&
           Result.KernelEntryPoint == 0 &&
           Result.LoaderBlockAddress == 0;
}

/* A successful firmware request must reach the common WINLOAD policy layer. */
static int RunFirmwareMismatch(RC95LDR_BOOT_FIRMWARE Firmware)
{
    RC95LDR_BOOT_CONTEXT Boot;
    RC95LDR_WINLOAD_PLATFORM Platform;
    RC95LDR_WINLOAD_STARTUP Startup;
    RC95LDR_WINLOAD_RESULT Result;
    RC95LDR_STATUS Status;

    Initialize(Firmware, &Boot, &Platform, &Startup, &Result);
    Builds = Releases = 0;
    BuildMode = 2;
    Status = Dispatch(Firmware, &Startup, &Result);

    return Status == Rc95LdrInvalidParameter &&
           Builds == 1 && Releases == 1 &&
           Boot.CurrentStage == Rc95LdrStageStartup &&
           Boot.LastError.Status == Rc95LdrInvalidParameter &&
           Boot.LastError.Detail == 0x201u &&
           Result.StructureSize == sizeof(Result) &&
           Result.Version == RC95LDR_WINLOAD_ABI_VERSION;
}

static int RunCommonWinloadDispatch(RC95LDR_BOOT_FIRMWARE Firmware)
{
    RC95LDR_BOOT_CONTEXT Boot;
    RC95LDR_WINLOAD_PLATFORM Platform;
    RC95LDR_WINLOAD_STARTUP Startup;
    RC95LDR_WINLOAD_RESULT Result;
    RC95LDR_STATUS Status;

    Initialize(Firmware, &Boot, &Platform, &Startup, &Result);
    Builds = Releases = 0;
    BuildMode = 1;
    Status = Dispatch(Firmware, &Startup, &Result);

    return Status == Rc95LdrUnsupported &&
           Builds == 1 && Releases == 1 &&
           Boot.CurrentStage == Rc95LdrStageImageLoad &&
           Boot.LastError.Status == Rc95LdrUnsupported &&
           Boot.LastError.Detail == 0x4C4E0001u &&
           Result.StructureSize == sizeof(Result) &&
           Result.Version == RC95LDR_WINLOAD_ABI_VERSION;
}


static int RunGraphicsPolicyPropagation(RC95LDR_BOOT_FIRMWARE Firmware,
                                        uint32_t Disabled)
{
    RC95LDR_BOOT_CONTEXT Boot;
    RC95LDR_WINLOAD_PLATFORM Platform;
    RC95LDR_WINLOAD_STARTUP Startup;
    RC95LDR_WINLOAD_RESULT Result;
    RC95LDR_WINDOWS_INSTALLATION Installation;
    RC95LDR_STATUS Status;

    Initialize(Firmware,&Boot,&Platform,&Startup,&Result);
    memset(&Installation,0,sizeof(Installation));
    Installation.GraphicsModeDisabled=Disabled;
    Startup.SelectedInstallation=&Installation;
    Builds=Releases=0;
    LastGraphicsDisabled=0xFFFFFFFFu;
    LastGraphicsPolicyValid=0;
    BuildMode=1; /* Reach common WINLOAD dispatch, then fail closed for LiberNT. */
    Status=Dispatch(Firmware,&Startup,&Result);
    return Status==Rc95LdrUnsupported && Builds==1 && Releases==1 &&
        LastGraphicsDisabled==Disabled && LastGraphicsPolicyValid==1u;
}

int main(void)
{
    if (!RunBuildFailure(Rc95LdrFirmwareBios) ||
        !RunBuildFailure(Rc95LdrFirmwareUefi)) {
        puts("WINLOAD firmware entry build-failure cleanup failed");
        return 1;
    }
    if (!RunFirmwareMismatch(Rc95LdrFirmwareBios) ||
        !RunFirmwareMismatch(Rc95LdrFirmwareUefi)) {
        puts("WINLOAD entry accepted a cross-firmware boot request");
        return 1;
    }
    if (!RunCommonWinloadDispatch(Rc95LdrFirmwareBios) ||
        !RunCommonWinloadDispatch(Rc95LdrFirmwareUefi)) {
        puts("WINLOAD firmware entry did not reach common policy");
        return 1;
    }
    if (!RunGraphicsPolicyPropagation(Rc95LdrFirmwareBios,0u) ||
        !RunGraphicsPolicyPropagation(Rc95LdrFirmwareUefi,1u)) {
        puts("WINLOAD entry did not propagate the exact selected BCD graphics policy");
        return 1;
    }
    puts("WINLOAD firmware entry dispatch tests passed");
    return 0;
}
