#include "include/rc95ldrbiosstartup.h"
#include "include/rc95ldrbioscd.h"
#if defined(RC95LDR_BIOS_NATIVE_AMD64)
#include "include/rc95ldrbiosamd64handoff.h"
#endif
#include <string.h>
#include <stddef.h>

static RC95LDR_STATUS
Rc95LdrBiosRestartSystem(void *Context)
{
    RC95LDR_BIOS_SERVICES *Services=(RC95LDR_BIOS_SERVICES*)Context;
    RC95LDR_BIOS_REGISTERS Registers;
    RC95LDR_STATUS Status;
    if(!Services || Rc95LdrBiosValidateServices(Services)!=Rc95LdrSuccess ||
       !Services->InvokeInterrupt) return Rc95LdrInvalidParameter;
    memset(&Registers,0,sizeof(Registers));
    /* INT 19h re-enters the BIOS bootstrap loader. A conforming BIOS normally
       does not return; report failure if firmware unexpectedly does. */
    Status=Services->InvokeInterrupt(Services->Context,0x19u,&Registers);
    return Status==Rc95LdrSuccess ? Rc95LdrIoError : Status;
}

static RC95LDR_STATUS
BiosMenuEnumerateModes(void *Context,RC95LDR_FIRMWARE_VIDEO_MODE *Modes,
    size_t Capacity,size_t *Count)
{
    RC95LDR_BIOS_STARTUP *S=(RC95LDR_BIOS_STARTUP *)Context;
    if(!S)return Rc95LdrInvalidParameter;
    return Rc95LdrBiosEnumerateFirmwareModes(&S->VideoContext,Modes,Capacity,Count);
}
static RC95LDR_STATUS
BiosMenuSetMode(void *Context,uint32_t ModeId)
{
    RC95LDR_BIOS_STARTUP *S=(RC95LDR_BIOS_STARTUP *)Context;
    if(!S)return Rc95LdrInvalidParameter;
    return Rc95LdrBiosSetFirmwareVideoMode(&S->VideoContext,ModeId);
}
static RC95LDR_STATUS
BiosMenuMapFramebuffer(void *Context,uint64_t Base,uint64_t Size,void **Mapping)
{
    RC95LDR_BIOS_STARTUP *S=(RC95LDR_BIOS_STARTUP *)Context;
    if(!S)return Rc95LdrInvalidParameter;
    return Rc95LdrBiosMapFirmwareFramebuffer(&S->VideoContext,Base,Size,Mapping);
}
static RC95LDR_STATUS
BiosMenuReadKey(void *Context,uint32_t *Key)
{
    RC95LDR_BIOS_STARTUP *S=(RC95LDR_BIOS_STARTUP *)Context;
    if(!S)return Rc95LdrInvalidParameter;
    return Rc95LdrBiosReadKey(&S->TextContext,Key);
}

RC95LDR_STATUS
Rc95LdrBiosBindFirmwareMenuOps(RC95LDR_BIOS_STARTUP *Startup,
                               RC95LDR_FIRMWARE_MENU_OPS *MenuOps)
{
    RC95LDR_STATUS Status;
    if (!Startup || !MenuOps ||
        Startup->StructureSize < sizeof(*Startup) ||
        Startup->Version != RC95LDR_BIOS_STARTUP_VERSION ||
        MenuOps->StructureSize < sizeof(*MenuOps) ||
        MenuOps->Version != RC95LDR_FIRMWARE_MENU_VERSION)
        return Rc95LdrInvalidParameter;

    /* The menu writes the BCD value for the entry actually selected. Keep
       this pointer attached to caller-owned startup storage until dispatch. */
    /* A host/test caller may supply its own complete firmware operations.
       Preserve that provider; native BIOS services are installed only when
       the caller leaves the adapter slots empty. */
    if(Startup->BiosServices){
        Status=Rc95LdrBiosValidateServices(Startup->BiosServices);
        if(Status!=Rc95LdrSuccess)return Status;
        if(!MenuOps->EnumerateModes&&!MenuOps->SetMode&&
           !MenuOps->MapFramebuffer&&!MenuOps->ReadKey&&
           !MenuOps->TextUi&&!MenuOps->TextUiExtension){
            if(!Startup->TextContext.Services){
                Status=Rc95LdrBiosInitializeTextUi(Startup->BiosServices,
                    &Startup->TextContext,&Startup->TextUi,&Startup->TextUiExtension);
                if(Status!=Rc95LdrSuccess)return Status;
            }
            if(!Startup->VideoContext.Text.Services){
                Status=Rc95LdrBiosInitializeVideo(Startup->BiosServices,
                    &Startup->VideoContext,&Startup->VideoPlatform);
                if(Status!=Rc95LdrSuccess)return Status;
            }
            MenuOps->Context=Startup;
            MenuOps->EnumerateModes=BiosMenuEnumerateModes;
            MenuOps->SetMode=BiosMenuSetMode;
            MenuOps->MapFramebuffer=BiosMenuMapFramebuffer;
            MenuOps->ReadKey=BiosMenuReadKey;
            MenuOps->TextUi=&Startup->TextUi;
            MenuOps->TextUiExtension=&Startup->TextUiExtension;
        }else if(!MenuOps->EnumerateModes||!MenuOps->SetMode||
                 !MenuOps->MapFramebuffer||!MenuOps->ReadKey||
                 !MenuOps->TextUi||!MenuOps->TextUiExtension){
            return Rc95LdrInvalidParameter;
        }
        if(!MenuOps->RestartSystem){
            MenuOps->RestartContext=Startup->BiosServices;
            MenuOps->RestartSystem=Rc95LdrBiosRestartSystem;
        }
    }
    MenuOps->GraphicsModeDisabledOutput = &Startup->GraphicsModeDisabled;
    return Rc95LdrSuccess;
}

/* Keep binding and menu invocation together so the ordering cannot be
 * accidentally reversed by the BIOS boot coordinator. */
RC95LDR_STATUS
Rc95LdrBiosRunCandidateMenu(
    RC95LDR_BIOS_STARTUP *Startup,
    RC95LDR_FIRMWARE_MENU_OPS *MenuOps,
    const RC95BCD_HIVE *Hive,
    const RC95LDR_BCD_BOOT_CANDIDATE *Candidates,
    size_t CandidateCount,
    const char *Title,
    const char *const *Items,
    const RC95LDR_BCD_BOOT_CANDIDATE **Selected)
{
    RC95LDR_STATUS Status;
    if (!Startup || !MenuOps || !Candidates || !CandidateCount ||
        !Items || !Selected)
        return Rc95LdrInvalidParameter;
    Status = Rc95LdrBiosBindFirmwareMenuOps(Startup, MenuOps);
    if (Status != Rc95LdrSuccess)
        return Status;
    return Rc95LdrRunBiosCandidateMenu(MenuOps, Hive, Candidates,
        CandidateCount, Title, Items, Selected);
}

#if defined(RC95LDR_BIOS_NATIVE_I386) || defined(RC95LDR_BIOS_NATIVE_AMD64)
RC95LDR_STATUS
Rc95LdrPrepareNativePlatform(RC95LDR_BIOS_STARTUP *Startup,
                             RC95LDR_WINLOAD_RESULT *Result)
{
    RC95LDR_STATUS Status;
    RC95LDR_WINLOAD_STARTUP WinloadStartup;

    if (!Startup || !Result ||
        Startup->StructureSize < sizeof(*Startup) ||
        Startup->Version != RC95LDR_BIOS_STARTUP_VERSION ||
        !Startup->BiosServices || !Startup->MemoryReservations ||
        !Startup->FirmwareMemoryRanges || !Startup->FirmwareMemoryCapacity ||
        !Startup->BuildBootRequest || !Startup->ReleaseBootRequest ||
        Startup->Platform.StructureSize < offsetof(RC95LDR_WINLOAD_PLATFORM, TextUi) ||
        Startup->Platform.Version != RC95LDR_WINLOAD_ABI_VERSION)
        return Rc95LdrInvalidParameter;

    Startup->FirmwareMemoryCount = 0;
    memset(&Startup->Disk, 0, sizeof(Startup->Disk));
    memset(&Startup->BlockContext, 0, sizeof(Startup->BlockContext));
    memset(&Startup->BlockDevice, 0, sizeof(Startup->BlockDevice));
    memset(&Startup->ElTorito, 0, sizeof(Startup->ElTorito));
    memset(&Startup->IsoVolume, 0, sizeof(Startup->IsoVolume));
    memset(&Startup->FileProviderScope, 0, sizeof(Startup->FileProviderScope));
    Startup->IsoMounted = 0;
    memset(&Startup->TextContext, 0, sizeof(Startup->TextContext));
    memset(&Startup->VideoContext, 0, sizeof(Startup->VideoContext));
    memset(&Startup->VideoPlatform, 0, sizeof(Startup->VideoPlatform));
    memset(&Startup->Framebuffer, 0, sizeof(Startup->Framebuffer));
    memset(&Startup->TextUi, 0, sizeof(Startup->TextUi));
    memset(&Startup->TextUiExtension, 0, sizeof(Startup->TextUiExtension));

    /* The reservation is committed before the gateway can be installed or
       any BIOS scratch buffer can be used by the memory-map collector. */
#if defined(RC95LDR_BIOS_NATIVE_I386)
    /* The i386 first-stage coordinator has already installed the gateway
       and committed its low-memory reservations before entering this ABI. */
    Status = Rc95LdrBiosValidateServices(Startup->BiosServices);
    if (Status != Rc95LdrSuccess) goto Cleanup;
#elif defined(RC95LDR_BIOS_NATIVE_AMD64)
    /*
     * AMD64 BIOS execution must arrive in long mode through a provider-owned
     * transition/thunk. Long mode cannot execute INT 13h/15h/16h directly.
     * Require a complete caller-supplied interrupt bridge and low-memory map;
     * never pretend that the i386 real-mode gateway works in long mode.
     */
    Status = Rc95LdrBiosValidateServices(Startup->BiosServices);
    if (Status != Rc95LdrSuccess) goto Cleanup;
    /*
     * BIOS has no ExitBootServices equivalent. The caller has already
     * transitioned into the native long-mode environment; stop issuing BIOS
     * requests and use the common validated AMD64 handoff after WINLOAD has
     * completed its OS-specific preparation.
     */
    if (Startup->Platform.StructureSize <
            offsetof(RC95LDR_WINLOAD_PLATFORM, ExitFirmwareAndValidatedHandoff) +
            sizeof(Startup->Platform.ExitFirmwareAndValidatedHandoff))
        { Status = Rc95LdrUnsupported; goto Cleanup; }
    if (!Startup->Platform.ExitFirmwareAndValidatedHandoff)
        Startup->Platform.ExitFirmwareAndValidatedHandoff =
            Rc95LdrBiosAmd64TransferHandoff;
    if (Startup->Platform.StructureSize >=
            offsetof(RC95LDR_WINLOAD_PLATFORM, ExitFirmwareContext) +
            sizeof(Startup->Platform.ExitFirmwareContext) &&
        !Startup->Platform.ExitFirmwareContext)
        Startup->Platform.ExitFirmwareContext = Startup->Context;
    if (Startup->BiosServices->ScratchPhysical >= 0x100000u ||
        Startup->BiosServices->ScratchLength == 0 ||
        Startup->BiosServices->ScratchLength >
            0x100000u - Startup->BiosServices->ScratchPhysical)
        { Status = Rc95LdrOutOfBounds; goto Cleanup; }
#else
    Status = Rc95LdrUnsupported;
    goto Cleanup;
#endif

    Status = Rc95LdrBiosCollectMemoryMap(
        Startup->BiosServices, Startup->FirmwareMemoryRanges,
        Startup->FirmwareMemoryCapacity, &Startup->FirmwareMemoryCount);
    if (Status != Rc95LdrSuccess) goto Cleanup;

    /* Prefer the native ISO9660 provider when this is an El Torito
       no-emulation boot. Emulated El Torito media remain visible through the
       ordinary EDD disk path; non-CD hard-disk boots are unchanged. */
    Status = Rc95LdrBiosMountElToritoIso(
        Startup->BiosServices, Startup->BiosServices->BootDrive,
        &Startup->ElTorito, &Startup->Disk, &Startup->BlockContext,
        &Startup->BlockDevice, &Startup->IsoVolume);
    if (Status == Rc95LdrSuccess)
        Startup->IsoMounted = 1;
    else if (Status == Rc95LdrNotFound ||
             (Status == Rc95LdrUnsupported &&
              Startup->ElTorito.MediaType != 0))
    {
        Status = Rc95LdrBiosProbeDisk(
            Startup->BiosServices, Startup->BiosServices->BootDrive,
            &Startup->Disk);
        if (Status != Rc95LdrSuccess) goto Cleanup;
        Status = Rc95LdrBiosInitializeBlockDevice(
            Startup->BiosServices, &Startup->Disk,
            &Startup->BlockContext, &Startup->BlockDevice);
        if (Status != Rc95LdrSuccess) goto Cleanup;

        /* Some BIOSes omit INT 13h 4B01h even for a valid El Torito disc.
           A 2048-byte EDD device can still be recognized by its ISO PVD. */
        if (Startup->Disk.BytesPerSector ==
            RC95LDR_ISO9660_LOGICAL_BLOCK_SIZE)
        {
            Status = Rc95LdrIso9660Mount(
                &Startup->IsoVolume, &Startup->BlockDevice);
            if (Status == Rc95LdrSuccess)
                Startup->IsoMounted = 1;
            else if (Status != Rc95LdrInvalidImage &&
                     Status != Rc95LdrNotFound)
                goto Cleanup;
        }
    }
    else
        goto Cleanup;

    Status = Rc95LdrBiosInitializeTextUi(
        Startup->BiosServices, &Startup->TextContext,
        &Startup->TextUi, &Startup->TextUiExtension);
    if (Status != Rc95LdrSuccess) goto Cleanup;

    /* Initialize the native RC95LDR VBE adapter. The BCD-derived
       graphicsmodedisabled policy is BIOS-only; otherwise request exact XGA
       1024x768. A missing VBE mode falls back to the native 80x50 console. */
    Status = Rc95LdrBiosInitializeVideo(Startup->BiosServices,
        &Startup->VideoContext, &Startup->VideoPlatform);
    if (Status != Rc95LdrSuccess) goto Cleanup;
    {
        RC95LDR_BGFX_CONFIG VideoConfig;
        memset(&VideoConfig, 0, sizeof(VideoConfig));
        VideoConfig.StructureSize = sizeof(VideoConfig);
        VideoConfig.Version = RC95LDR_BGFX_VERSION;
        VideoConfig.GraphicsModeDisabled = Startup->GraphicsModeDisabled;
        VideoConfig.PreferredWidth = RC95LDR_BGFX_DEFAULT_WIDTH;
        VideoConfig.PreferredHeight = RC95LDR_BGFX_DEFAULT_HEIGHT;
        Status = Rc95LdrBgfxInitialize(&Startup->VideoPlatform,
            &VideoConfig, RC95LDR_BGFX_FIRMWARE_BIOS,
            &Startup->Framebuffer);
        if (Status == Rc95LdrUnsupported &&
            Startup->GraphicsModeDisabled)
            Status = Rc95LdrSuccess; /* 80x50 text mode was selected. */
        else if (Status == Rc95LdrUnsupported || Status == Rc95LdrNotFound ||
                 Status == Rc95LdrIoError)
            Status = Rc95LdrBiosSetTextMode(&Startup->TextContext, 80u, 50u);
        if (Status != Rc95LdrSuccess) goto Cleanup;
    }

    /* Bind the RC95-OS provider into WINLOAD before copying the startup
       descriptor. Its callbacks and dedicated context are RC95-owned; no
       FreeLoader loader-block structures or construction routines are used. */
    Startup->Platform.TextUi = &Startup->TextUi;
    if (Startup->NativeProvider) {
        if (!Startup->NativeProvider->Input ||
            !Startup->NativeProvider->Input->Profile ||
            Startup->NativeProvider->Input->Profile->Firmware !=
                Rc95LdrFirmwareBios) {
            Status = Rc95LdrInvalidParameter;
            goto Cleanup;
        }
        Status = Rc95LdrAttachRc95NativeProvider(
            &Startup->Platform, Startup->NativeProvider);
        if (Status != Rc95LdrSuccess) goto Cleanup;
    }

    memset(&WinloadStartup, 0, sizeof(WinloadStartup));
    WinloadStartup.StructureSize = sizeof(WinloadStartup);
    WinloadStartup.Version = RC95LDR_STARTUP_ABI_VERSION;
    WinloadStartup.Firmware = Rc95LdrFirmwareBios;
    WinloadStartup.Context = Startup->Context;
    WinloadStartup.BootContext = &Startup->BootContext;
    WinloadStartup.Platform = &Startup->Platform;
    WinloadStartup.BuildBootRequest = Startup->BuildBootRequest;
    WinloadStartup.ReleaseBootRequest = Startup->ReleaseBootRequest;

    if (Startup->IsoMounted)
    {
        const RC95LDR_FILE_PROVIDER *Provider =
            Rc95LdrIso9660GetFileProvider(&Startup->IsoVolume);
        if (!Provider) { Status = Rc95LdrInvalidParameter; goto Cleanup; }
        Status = Rc95LdrBeginFileProviderScope(
            Provider, &Startup->FileProviderScope);
        if (Status != Rc95LdrSuccess) goto Cleanup;
        Status = Rc95LdrBiosWinloadEntry(&WinloadStartup, Result);
    }
    else
    {
        /* Hard-disk boots must select exactly one BCD-backed native
           filesystem transaction; never build a request against an
           unspecified or ambiguous volume. */
        if (!!Startup->NtfsBoot == !!Startup->BtrfsBoot) {
            Status = Rc95LdrInvalidParameter;
            goto Cleanup;
        }
        /* A menu is opt-in, but partial configuration is rejected rather
           than silently reverting to automatic default selection. */
        if(Startup->FirmwareMenuOps || Startup->FirmwareMenuTitle ||
           Startup->FirmwareMenuItems){
            if(!Startup->FirmwareMenuOps){
                Status=Rc95LdrInvalidParameter;
                goto Cleanup;
            }
        }
        if (Startup->NtfsBoot){
            Startup->NtfsBoot->MenuStartup =
                Startup->FirmwareMenuOps ? Startup : NULL;
            Status = Rc95LdrBiosStartWindowsFromNtfs(
                &Startup->BlockContext, &Startup->Disk, &Startup->TextUi,
                &WinloadStartup, Startup->NtfsBoot, Result);
        }else{
            Startup->BtrfsBoot->MenuStartup =
                Startup->FirmwareMenuOps ? Startup : NULL;
            Status = Rc95LdrBiosStartWindowsFromBtrfs(
                &Startup->BlockContext, &Startup->Disk, &Startup->TextUi,
                &WinloadStartup, Startup->BtrfsBoot, Result);
        }
    }

Cleanup:
    /*
     * Unwind in reverse acquisition order. The provider scope may still own
     * file handles when WINLOAD fails; close it before unmounting its volume.
     * Preserve the first operational error, but surface cleanup failure when
     * the boot operation itself otherwise succeeded.
     */
    if (Startup->FileProviderScope.Active)
    {
        RC95LDR_STATUS ScopeStatus =
            Rc95LdrEndFileProviderScope(&Startup->FileProviderScope);
        if (Status == Rc95LdrSuccess)
            Status = ScopeStatus;
    }
    if (Startup->IsoMounted)
    {
        Rc95LdrIso9660Unmount(&Startup->IsoVolume);
        Startup->IsoMounted = 0;
    }
    return Status;
}
#else
/* Host-side BIOS service tests use an injected InvokeInterrupt implementation. */
RC95LDR_STATUS
Rc95LdrPrepareNativePlatform(RC95LDR_BIOS_STARTUP *Startup,
                             RC95LDR_WINLOAD_RESULT *Result)
{
    if (!Startup || !Result) return Rc95LdrInvalidParameter;
    return Rc95LdrUnsupported;
}
#endif
