#include "rc95ldruefiwinloadflow.h"
#include <stddef.h>
#include <string.h>

int main(void)
{
    RC95LDR_WINLOAD_PLATFORM Platform;
    RC95LDR_WINDOWS_INSTALLATION Installation;
    const uint32_t Required=(uint32_t)(
        offsetof(RC95LDR_WINLOAD_PLATFORM,BootFileSystemCapabilities)+
        sizeof(Platform.BootFileSystemCapabilities));

    memset(&Platform,0,sizeof(Platform));
    memset(&Installation,0,sizeof(Installation));
    Platform.StructureSize=sizeof(Platform);
    Installation.OsPartition.FileSystem=Rc95LdrFsBtrfs;

    /* Direct OS-volume access is the only path entitled to Btrfs attestation. */
    if (Rc95LdrUefiSetWinloadProviderAttestation(
            &Platform,&Installation,1)!=Rc95LdrSuccess)
        return 1;
    if (Platform.BootFileSystemKind!=Rc95LdrWinloadFsBtrfs||
        Platform.BootFileSystemCapabilities!=
            (RC95LDR_WINLOAD_FS_CAP_READ_ONLY|RC95LDR_WINLOAD_FS_CAP_BTRFS_NATIVE))
        return 2;

    /* A WIM provider is not Btrfs even when the installed OS is on Btrfs. */
    if (Rc95LdrUefiSetWinloadProviderAttestation(
            &Platform,&Installation,0)!=Rc95LdrSuccess)
        return 3;
    if (Platform.BootFileSystemKind!=Rc95LdrWinloadFsUnknown||
        Platform.BootFileSystemCapabilities!=0)
        return 4;

    Installation.OsPartition.FileSystem=Rc95LdrFsNtfs;
    Platform.BootFileSystemKind=Rc95LdrWinloadFsBtrfs;
    Platform.BootFileSystemCapabilities=
        RC95LDR_WINLOAD_FS_CAP_READ_ONLY|RC95LDR_WINLOAD_FS_CAP_BTRFS_NATIVE;
    if (Rc95LdrUefiSetWinloadProviderAttestation(
            &Platform,&Installation,1)!=Rc95LdrSuccess)
        return 5;
    if (Platform.BootFileSystemKind!=Rc95LdrWinloadFsNtfs||
        Platform.BootFileSystemCapabilities!=0)
        return 6;

    /* Old platform ABI: Btrfs direct access must fail closed. */
    Platform.StructureSize=Required-1u;
    Platform.BootFileSystemKind=Rc95LdrWinloadFsBtrfs;
    Platform.BootFileSystemCapabilities=0xffffffffu;
    Installation.OsPartition.FileSystem=Rc95LdrFsBtrfs;
    if (Rc95LdrUefiSetWinloadProviderAttestation(
            &Platform,&Installation,1)!=Rc95LdrUnsupported)
        return 7;
    /* WIM-backed boot remains valid without the optional extension. */
    if (Rc95LdrUefiSetWinloadProviderAttestation(
            &Platform,&Installation,0)!=Rc95LdrSuccess)
        return 8;
    if (Platform.BootFileSystemKind!=Rc95LdrWinloadFsUnknown||
        Platform.BootFileSystemCapabilities!=0xffffffffu)
        return 9;
    return 0;
}
