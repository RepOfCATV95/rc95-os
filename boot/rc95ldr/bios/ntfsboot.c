#include "include/rc95ldrbiosntfsboot.h"
#include "include/rc95ldrbiosstartup.h"
#include <string.h>

static RC95LDR_STATUS
BiosNtfsSelectCandidate(void *Opaque,const RC95BCD_HIVE *Hive,
    const RC95LDR_BCD_BOOT_CANDIDATE *Candidates,size_t Count,
    const RC95LDR_BCD_BOOT_CANDIDATE **Selected)
{
    RC95LDR_BIOS_NTFS_BOOT_CONFIG *Config=(RC95LDR_BIOS_NTFS_BOOT_CONFIG*)Opaque;
    RC95LDR_BIOS_STARTUP *MenuStartup;
    static const char *DefaultItems[RC95LDR_TEXTUI_MAX_MENU_ITEMS]={
        "Windows Boot Entry 1","Windows Boot Entry 2",
        "Windows Boot Entry 3","Windows Boot Entry 4",
        "Windows Boot Entry 5","Windows Boot Entry 6",
        "Windows Boot Entry 7","Windows Boot Entry 8",
        "Windows Boot Entry 9","Windows Boot Entry 10",
        "Windows Boot Entry 11","Windows Boot Entry 12"
    };
    if(!Config || !Config->MenuStartup)return Rc95LdrInvalidParameter;
    MenuStartup=Config->MenuStartup;
    if(!MenuStartup->FirmwareMenuOps)return Rc95LdrInvalidParameter;
    return Rc95LdrBiosRunCandidateMenu(MenuStartup,
        MenuStartup->FirmwareMenuOps,Hive,Candidates,Count,
        MenuStartup->FirmwareMenuTitle?MenuStartup->FirmwareMenuTitle:"RC95-OS Boot Manager",
        MenuStartup->FirmwareMenuItems?MenuStartup->FirmwareMenuItems:DefaultItems,
        Selected);
}
static RC95LDR_STATUS
BiosNtfsBuild(void *Opaque, RC95LDR_WINLOAD_REQUEST *Request)
{
    RC95LDR_BIOS_NTFS_WINLOAD_CONTEXT *Flow = (RC95LDR_BIOS_NTFS_WINLOAD_CONTEXT *)Opaque;
    if (!Flow || !Flow->BuildBootRequest || !Request)
        return Rc95LdrInvalidParameter;
    return Flow->BuildBootRequest(Flow, Request);
}

static void
BiosNtfsRelease(void *Opaque, RC95LDR_WINLOAD_REQUEST *Request)
{
    RC95LDR_BIOS_NTFS_WINLOAD_CONTEXT *Flow = (RC95LDR_BIOS_NTFS_WINLOAD_CONTEXT *)Opaque;
    if (Flow && Flow->ReleaseBootRequest)
        Flow->ReleaseBootRequest(Flow, Request);
}

RC95LDR_STATUS
Rc95LdrBiosStartWindowsFromNtfs(
    const RC95LDR_BIOS_BLOCK_CONTEXT *BlockContext,
    const RC95LDR_BIOS_DISK *Disk,
    const RC95LDR_TEXTUI *TextUi,
    const RC95LDR_WINLOAD_STARTUP *Startup,
    RC95LDR_BIOS_NTFS_BOOT_CONFIG *Config,
    RC95LDR_WINLOAD_RESULT *Result)
{
    RC95LDR_BIOS_DISK_DISCOVERY Discovery;
    RC95LDR_BIOS_DISCOVERY_OPS DiscoveryOps;
    RC95LDR_WINLOAD_STARTUP BoundStartup;
    RC95LDR_BIOS_NTFS_WINLOAD_CONTEXT Flow;
    RC95LDR_STATUS Status, CleanupStatus;
    size_t RequiredWorkspace = 0;

    if (!BlockContext || !Disk || !Startup || !Config || !Result ||
        !Config->DiscoveryScratch || !Config->DiscoveryScratchLength ||
        !Config->Partitions || !Config->PartitionCapacity ||
        !Config->Candidates || !Config->CandidateCapacity ||
        !Config->HiveBuffer || !Config->HiveCapacity ||
        !Config->Validation.Scratch || !Config->Validation.ScratchLength ||
        !Config->Validation.ProbeBuffer || !Config->Validation.ProbeCapacity ||
        !Config->ProviderWorkspace || !Config->ProviderWorkspaceLength ||
        Startup->StructureSize < sizeof(*Startup) ||
        Startup->Version != RC95LDR_STARTUP_ABI_VERSION ||
        Startup->Firmware != Rc95LdrFirmwareBios ||
        !Startup->BootContext || !Startup->Platform ||
        !Startup->BuildBootRequest || !Startup->ReleaseBootRequest)
        return Rc95LdrInvalidParameter;

    memset(&Discovery, 0, sizeof(Discovery));
    memset(&DiscoveryOps, 0, sizeof(DiscoveryOps));
    memset(&Flow, 0, sizeof(Flow));
    memset(&Config->MountedVolume, 0, sizeof(Config->MountedVolume));
    memset(&Config->NtfsProvider, 0, sizeof(Config->NtfsProvider));
    memset(&Config->BoundVolume, 0, sizeof(Config->BoundVolume));
    memset(&Config->FileProviderContext, 0, sizeof(Config->FileProviderContext));
    memset(&Config->FileProvider, 0, sizeof(Config->FileProvider));
    memset(&Config->FileProviderScope, 0, sizeof(Config->FileProviderScope));
    memset(&Config->Installation, 0, sizeof(Config->Installation));
    Config->CandidateCount = 0;
    Config->BcdPartitionIndex = (size_t)-1;
    Config->Mounted = 0;

    Discovery.BlockContext = BlockContext;
    Discovery.Disk = Disk;
    Discovery.Scratch = (uint8_t *)Config->DiscoveryScratch;
    Discovery.ScratchLength = Config->DiscoveryScratchLength;
    DiscoveryOps.Context = &Config->Validation;
    DiscoveryOps.ValidateWindowsVolume = Rc95LdrBiosValidateWindowsVolume;
    DiscoveryOps.TextUi = TextUi;
    if(Config->MenuStartup){
        DiscoveryOps.SelectionContext=Config;
        DiscoveryOps.SelectBootCandidate=BiosNtfsSelectCandidate;
    }

    Status = Rc95LdrDiscoverBiosWindows(
        &Discovery, &DiscoveryOps,
        Config->Partitions, Config->PartitionCapacity,
        Config->Candidates, Config->CandidateCapacity,
        &Config->CandidateCount, Config->HiveBuffer, Config->HiveCapacity,
        &Config->Installation, &Config->MountedVolume,
        &Config->BcdPartitionIndex);
    if (Status != Rc95LdrSuccess)
        goto Cleanup;
    Config->Mounted = Config->MountedVolume.Mounted ? 1u : 0u;
    if (!Config->Mounted) {
        Status = Rc95LdrCorruptData;
        goto Cleanup;
    }

    Status = Rc95LdrNtfsFileSystemWorkspaceSize(
        &Config->MountedVolume, &RequiredWorkspace);
    if (Status != Rc95LdrSuccess)
        goto Cleanup;
    if (Config->ProviderWorkspaceLength < RequiredWorkspace) {
        Status = Rc95LdrBufferTooSmall;
        goto Cleanup;
    }

    Status = Rc95LdrNtfsBindVolume(
        &Config->MountedVolume, &Config->NtfsProvider,
        Config->ProviderWorkspace, Config->ProviderWorkspaceLength,
        &Config->BoundVolume);
    if (Status != Rc95LdrSuccess)
        goto Cleanup;

    Status = Rc95LdrInitializeVolumeFileProvider(
        &Config->BoundVolume, &Config->FileProviderContext,
        &Config->FileProvider);
    if (Status != Rc95LdrSuccess)
        goto Cleanup;

    Status = Rc95LdrBeginFileProviderScope(
        &Config->FileProvider, &Config->FileProviderScope);
    if (Status != Rc95LdrSuccess)
        goto Cleanup;

    Flow.CallerContext = Startup->Context;
    Flow.SelectedInstallation = &Config->Installation;
    Flow.WindowsVolume = &Config->BoundVolume;
    Flow.BuildBootRequest = Startup->BuildBootRequest;
    Flow.ReleaseBootRequest = Startup->ReleaseBootRequest;

    /* Rc95LdrPrepareNativePlatform attached this exact context to the
       WINLOAD platform before entering the BIOS NTFS transaction. Give it a
       transaction-owned input copy so native preparation sees the BCD-selected
       installation and the live, scoped NTFS file provider. */
    if (Startup->Platform->BuildNativePreparation ==
            Rc95LdrRc95NativeBuildPreparation) {
        RC95LDR_RC95_NATIVE_PLATFORM_CONTEXT *NativeProvider =
            (RC95LDR_RC95_NATIVE_PLATFORM_CONTEXT *)
                Startup->Platform->NativePreparationContext;
        if (!NativeProvider || !NativeProvider->Input ||
            !NativeProvider->Result ||
            NativeProvider->Input->StructureSize <
                sizeof(RC95LDR_RC95_NATIVE_PROVIDER_INPUT) ||
            NativeProvider->Input->Version !=
                RC95LDR_RC95_PROVIDER_VERSION ||
            !NativeProvider->Input->Profile ||
            NativeProvider->Input->Profile->Firmware != Rc95LdrFirmwareBios) {
            Status = Rc95LdrInvalidParameter;
            goto Cleanup;
        }
        Flow.NativeProvider = NativeProvider;
        Flow.PreviousNativeProviderInput = NativeProvider->Input;
        Flow.NativeProviderInput = *NativeProvider->Input;
        Flow.NativeProviderInput.SelectedInstallation = &Config->Installation;
        Flow.NativeProviderInput.BootFileProvider = &Config->FileProvider;
        Flow.NativeProviderInput.WinloadPlatform = Startup->Platform;
        NativeProvider->Input = &Flow.NativeProviderInput;
    }

    BoundStartup = *Startup;
    BoundStartup.Context = &Flow;
    BoundStartup.BuildBootRequest = BiosNtfsBuild;
    BoundStartup.ReleaseBootRequest = BiosNtfsRelease;
    BoundStartup.SelectedInstallation = &Config->Installation;

    Status = Rc95LdrBiosWinloadEntry(&BoundStartup, Result);

Cleanup:
    /* Do not leave the provider pointing into this stack-owned transaction
       after WINLOAD returns or an earlier bind step fails. */
    if (Flow.NativeProvider && Flow.PreviousNativeProviderInput)
        Flow.NativeProvider->Input = Flow.PreviousNativeProviderInput;

    /* File handles are owned by the provider scope; close them before
       destroying the filesystem and unmounting the underlying volume. */
    if (Config->FileProviderScope.Active) {
        CleanupStatus = Rc95LdrEndFileProviderScope(
            &Config->FileProviderScope);
        if (Status == Rc95LdrSuccess)
            Status = CleanupStatus;
    }
    if (Config->NtfsProvider.Initialized)
        Rc95LdrNtfsDestroyFileSystem(&Config->NtfsProvider);
    if (Config->MountedVolume.Mounted)
        Rc95LdrBiosUnmountNtfs(&Config->MountedVolume);
    Config->Mounted = 0;
    memset(&Config->BoundVolume, 0, sizeof(Config->BoundVolume));
    memset(&Config->FileProviderContext, 0, sizeof(Config->FileProviderContext));
    memset(&Config->FileProvider, 0, sizeof(Config->FileProvider));
    return Status;
}
