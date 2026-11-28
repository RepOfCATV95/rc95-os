#include "include/rc95ldrbiosbtrfsboot.h"
#include "include/rc95ldrbiosstartup.h"
#include <stddef.h>
#include <string.h>

static RC95LDR_STATUS
BiosBtrfsSelectCandidate(void *Opaque,const RC95BCD_HIVE *Hive,
    const RC95LDR_BCD_BOOT_CANDIDATE *Candidates,size_t Count,
    const RC95LDR_BCD_BOOT_CANDIDATE **Selected)
{
    RC95LDR_BIOS_BTRFS_BOOT_CONFIG *Config=(RC95LDR_BIOS_BTRFS_BOOT_CONFIG*)Opaque;
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
BiosBtrfsBuild(void *Opaque, RC95LDR_WINLOAD_REQUEST *Request)
{
    RC95LDR_BIOS_BTRFS_WINLOAD_CONTEXT *Flow =
        (RC95LDR_BIOS_BTRFS_WINLOAD_CONTEXT *)Opaque;
    if (!Flow || !Flow->BuildBootRequest || !Request)
        return Rc95LdrInvalidParameter;
    return Flow->BuildBootRequest(Flow, Request);
}

static void
BiosBtrfsRelease(void *Opaque, RC95LDR_WINLOAD_REQUEST *Request)
{
    RC95LDR_BIOS_BTRFS_WINLOAD_CONTEXT *Flow =
        (RC95LDR_BIOS_BTRFS_WINLOAD_CONTEXT *)Opaque;
    if (Flow && Flow->ReleaseBootRequest)
        Flow->ReleaseBootRequest(Flow, Request);
}

/* This entrypoint is intentionally Btrfs-only. Do not run the generic NTFS
 * validator with the Btrfs callback's unrelated Context pointer. */
static RC95LDR_STATUS
BiosBtrfsRejectNtfs(void *Opaque, const RC95LDR_NTFS_VOLUME *Volume,
                    const RC95LDR_WINDOWS_INSTALLATION *Installation)
{
    (void)Opaque;
    (void)Volume;
    (void)Installation;
    return Rc95LdrUnsupported;
}

RC95LDR_STATUS
Rc95LdrBiosStartWindowsFromBtrfs(
    const RC95LDR_BIOS_BLOCK_CONTEXT *BlockContext,
    const RC95LDR_BIOS_DISK *Disk,
    const RC95LDR_TEXTUI *TextUi,
    const RC95LDR_WINLOAD_STARTUP *Startup,
    RC95LDR_BIOS_BTRFS_BOOT_CONFIG *Config,
    RC95LDR_WINLOAD_RESULT *Result)
{
    RC95LDR_BIOS_DISK_DISCOVERY Discovery;
    RC95LDR_BIOS_DISCOVERY_OPS DiscoveryOps;
    RC95LDR_WINLOAD_STARTUP BoundStartup;
    RC95LDR_WINLOAD_PLATFORM BoundPlatform;
    RC95LDR_BIOS_BTRFS_WINLOAD_CONTEXT Flow;
    RC95LDR_NTFS_VOLUME UnusedNtfsVolume;
    RC95LDR_STATUS Status, CleanupStatus;
    uint32_t PreviousFsKind = 0, PreviousFsCapabilities = 0;
    size_t PlatformCopySize;
    int AttestationBound = 0;

    if (!BlockContext || !Disk || !Startup || !Config || !Result ||
        !Config->DiscoveryScratch || !Config->DiscoveryScratchLength ||
        !Config->Partitions || !Config->PartitionCapacity ||
        !Config->Candidates || !Config->CandidateCapacity ||
        !Config->HiveBuffer || !Config->HiveCapacity ||
        !Config->ItemBuffer || !Config->ItemBufferLength ||
        !Config->InlineBuffer || !Config->InlineBufferLength ||
        !Config->StagingBuffer || !Config->StagingBufferLength ||
        (!Config->ValidateWindowsVolume &&
         (!Config->Validation.ProbeBuffer ||
          Config->Validation.ProbeCapacity < 4096u)) ||
        Startup->StructureSize < sizeof(*Startup) ||
        Startup->Version != RC95LDR_STARTUP_ABI_VERSION ||
        Startup->Firmware != Rc95LdrFirmwareBios ||
        !Startup->BootContext || !Startup->Platform ||
        !Startup->BuildBootRequest || !Startup->ReleaseBootRequest)
        return Rc95LdrInvalidParameter;

    if (Startup->Platform->StructureSize <
        offsetof(RC95LDR_WINLOAD_PLATFORM, BootFileSystemCapabilities) +
        sizeof(Startup->Platform->BootFileSystemCapabilities))
        return Rc95LdrUnsupported;

    memset(&BoundPlatform, 0, sizeof(BoundPlatform));
    PlatformCopySize = Startup->Platform->StructureSize;
    if (PlatformCopySize > sizeof(BoundPlatform))
        PlatformCopySize = sizeof(BoundPlatform);
    memcpy(&BoundPlatform, Startup->Platform, PlatformCopySize);
    BoundPlatform.StructureSize = (uint32_t)PlatformCopySize;

    memset(&Discovery, 0, sizeof(Discovery));
    memset(&DiscoveryOps, 0, sizeof(DiscoveryOps));
    memset(&Flow, 0, sizeof(Flow));
    memset(&UnusedNtfsVolume, 0, sizeof(UnusedNtfsVolume));
    memset(&Config->MountedVolume, 0, sizeof(Config->MountedVolume));
    memset(&Config->Installation, 0, sizeof(Config->Installation));
    Config->CandidateCount = 0;
    Config->BcdPartitionIndex = (size_t)-1;
    Config->Mounted = 0;

    Discovery.BlockContext = BlockContext;
    Discovery.Disk = Disk;
    Discovery.Scratch = (uint8_t *)Config->DiscoveryScratch;
    Discovery.ScratchLength = Config->DiscoveryScratchLength;

    DiscoveryOps.Context = Config->ValidateWindowsVolume ?
        Config->ValidationContext : &Config->Validation;
    DiscoveryOps.ValidateWindowsVolume = BiosBtrfsRejectNtfs;
    DiscoveryOps.TextUi = TextUi;
    DiscoveryOps.BtrfsWindowsVolume = &Config->MountedVolume;
    DiscoveryOps.BtrfsItemBuffer = Config->ItemBuffer;
    DiscoveryOps.BtrfsItemBufferLength = Config->ItemBufferLength;
    DiscoveryOps.BtrfsInlineBuffer = Config->InlineBuffer;
    DiscoveryOps.BtrfsInlineBufferLength = Config->InlineBufferLength;
    DiscoveryOps.BtrfsStagingBuffer = Config->StagingBuffer;
    DiscoveryOps.BtrfsStagingBufferLength = Config->StagingBufferLength;
    DiscoveryOps.ValidateBtrfsWindowsVolume =
        Config->ValidateWindowsVolume ? Config->ValidateWindowsVolume :
        Rc95LdrBiosValidateBtrfsWindowsVolume;
    if(Config->MenuStartup){
        DiscoveryOps.SelectionContext=Config;
        DiscoveryOps.SelectBootCandidate=BiosBtrfsSelectCandidate;
    }

    Status = Rc95LdrDiscoverBiosWindows(
        &Discovery, &DiscoveryOps,
        Config->Partitions, Config->PartitionCapacity,
        Config->Candidates, Config->CandidateCapacity,
        &Config->CandidateCount, Config->HiveBuffer, Config->HiveCapacity,
        &Config->Installation, &UnusedNtfsVolume,
        &Config->BcdPartitionIndex);
    if (Status != Rc95LdrSuccess)
        goto Cleanup;
    if (!Config->MountedVolume.Btrfs.Mounted ||
        !Config->MountedVolume.ProviderScopeActive ||
        Config->MountedVolume.Volume.FileSystem !=
            &Config->MountedVolume.FileSystem) {
        if (UnusedNtfsVolume.Mounted)
            Rc95LdrBiosUnmountNtfs(&UnusedNtfsVolume);
        Status = Rc95LdrUnsupported;
        goto Cleanup;
    }
    Config->Mounted = 1u;

    Flow.CallerContext = Startup->Context;
    Flow.SelectedInstallation = &Config->Installation;
    Flow.WindowsVolume = &Config->MountedVolume.Volume;
    Flow.BuildBootRequest = Startup->BuildBootRequest;
    Flow.ReleaseBootRequest = Startup->ReleaseBootRequest;

    if (Startup->Platform->BuildNativePreparation ==
            Rc95LdrRc95NativeBuildPreparation) {
        RC95LDR_RC95_NATIVE_PLATFORM_CONTEXT *NativeProvider =
            (RC95LDR_RC95_NATIVE_PLATFORM_CONTEXT *)
                Startup->Platform->NativePreparationContext;
        if (!NativeProvider || !NativeProvider->Input ||
            !NativeProvider->Result ||
            NativeProvider->Input->StructureSize <
                sizeof(RC95LDR_RC95_NATIVE_PROVIDER_INPUT) ||
            NativeProvider->Input->Version != RC95LDR_RC95_PROVIDER_VERSION ||
            !NativeProvider->Input->Profile ||
            NativeProvider->Input->Profile->Firmware != Rc95LdrFirmwareBios) {
            Status = Rc95LdrInvalidParameter;
            goto Cleanup;
        }
        Flow.NativeProvider = NativeProvider;
        Flow.PreviousNativeProviderInput = NativeProvider->Input;
        Flow.NativeProviderInput = *NativeProvider->Input;
        Flow.NativeProviderInput.SelectedInstallation = &Config->Installation;
        Flow.NativeProviderInput.BootFileProvider =
            &Config->MountedVolume.Provider;
        Flow.NativeProviderInput.WinloadPlatform = &BoundPlatform;
        NativeProvider->Input = &Flow.NativeProviderInput;
    }

    PreviousFsKind = BoundPlatform.BootFileSystemKind;
    PreviousFsCapabilities = BoundPlatform.BootFileSystemCapabilities;
    BoundPlatform.BootFileSystemKind = Rc95LdrWinloadFsBtrfs;
    BoundPlatform.BootFileSystemCapabilities =
        RC95LDR_WINLOAD_FS_CAP_READ_ONLY |
        RC95LDR_WINLOAD_FS_CAP_BTRFS_NATIVE;
    AttestationBound = 1;

    BoundStartup = *Startup;
    BoundStartup.Platform = &BoundPlatform;
    BoundStartup.Context = &Flow;
    BoundStartup.BuildBootRequest = BiosBtrfsBuild;
    BoundStartup.ReleaseBootRequest = BiosBtrfsRelease;
    BoundStartup.SelectedInstallation = &Config->Installation;

    Status = Rc95LdrBiosWinloadEntry(&BoundStartup, Result);

Cleanup:
    if (AttestationBound) {
        BoundPlatform.BootFileSystemKind = PreviousFsKind;
        BoundPlatform.BootFileSystemCapabilities = PreviousFsCapabilities;
    }
    if (Flow.NativeProvider && Flow.PreviousNativeProviderInput)
        Flow.NativeProvider->Input = Flow.PreviousNativeProviderInput;

    if (UnusedNtfsVolume.Mounted)
        Rc95LdrBiosUnmountNtfs(&UnusedNtfsVolume);
    if (Config->MountedVolume.ProviderScopeActive) {
        CleanupStatus = Rc95LdrBiosEndBtrfsFileProvider(
            &Config->MountedVolume);
        if (Status == Rc95LdrSuccess)
            Status = CleanupStatus;
    }
    if (Config->MountedVolume.Btrfs.Mounted)
        Rc95LdrBiosUnmountBtrfs(&Config->MountedVolume);
    Config->Mounted = 0;
    return Status;
}
