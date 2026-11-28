#include "include/rc95ldruefiwinloadflow.h"
#include <stddef.h>

RC95LDR_STATUS
Rc95LdrUefiSetWinloadProviderAttestation(
    RC95LDR_WINLOAD_PLATFORM *Platform,
    const RC95LDR_WINDOWS_INSTALLATION *Installation,
    int IsSelectedOsVolume)
{
    size_t KindRequired,CapabilitiesRequired;
    if (!Platform||!Installation) return Rc95LdrInvalidParameter;
    KindRequired=offsetof(RC95LDR_WINLOAD_PLATFORM,BootFileSystemKind)+
        sizeof(Platform->BootFileSystemKind);
    CapabilitiesRequired=offsetof(RC95LDR_WINLOAD_PLATFORM,BootFileSystemCapabilities)+
        sizeof(Platform->BootFileSystemCapabilities);
    if (Platform->StructureSize<KindRequired) {
        if (IsSelectedOsVolume&&Installation->OsPartition.FileSystem==Rc95LdrFsBtrfs)
            return Rc95LdrUnsupported;
        return Rc95LdrSuccess;
    }
    if (!IsSelectedOsVolume) {
        Platform->BootFileSystemKind=Rc95LdrWinloadFsUnknown;
        if (Platform->StructureSize>=CapabilitiesRequired)
            Platform->BootFileSystemCapabilities=0;
        return Rc95LdrSuccess;
    }
    if (Installation->OsPartition.FileSystem==Rc95LdrFsBtrfs&&
        Platform->StructureSize<CapabilitiesRequired)
        return Rc95LdrUnsupported;
    switch (Installation->OsPartition.FileSystem) {
    case Rc95LdrFsFat32:
        Platform->BootFileSystemKind=Rc95LdrWinloadFsFat32;
        if (Platform->StructureSize>=CapabilitiesRequired)
            Platform->BootFileSystemCapabilities=0;
        break;
    case Rc95LdrFsNtfs:
        Platform->BootFileSystemKind=Rc95LdrWinloadFsNtfs;
        if (Platform->StructureSize>=CapabilitiesRequired)
            Platform->BootFileSystemCapabilities=0;
        break;
    case Rc95LdrFsBtrfs:
        Platform->BootFileSystemKind=Rc95LdrWinloadFsBtrfs;
        Platform->BootFileSystemCapabilities=
            RC95LDR_WINLOAD_FS_CAP_READ_ONLY|
            RC95LDR_WINLOAD_FS_CAP_BTRFS_NATIVE;
        break;
    default:
        Platform->BootFileSystemKind=Rc95LdrWinloadFsUnknown;
        if (Platform->StructureSize>=CapabilitiesRequired)
            Platform->BootFileSystemCapabilities=0;
        break;
    }
    return Rc95LdrSuccess;
}
