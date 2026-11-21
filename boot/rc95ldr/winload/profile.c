#include "rc95ldrprofile.h"
#include <string.h>

static int Rc95LdrValidFirmware(RC95LDR_BOOT_FIRMWARE Firmware)
{
    return Firmware==Rc95LdrFirmwareBios || Firmware==Rc95LdrFirmwareUefi;
}

static int Rc95LdrIsLegacyNtVersion(uint32_t Major,uint32_t Minor)
{
    return (Major==4 && Minor==0) ||
           (Major==5 && Minor<=2);
}

static int Rc95LdrIsModernWindowsVersion(uint32_t Major,uint32_t Minor,
                                        uint32_t Build)
{
    if (Major==6) return Minor<=3;
    if (Major==10 && Minor==0) return Build>=10240;
    return 0;
}

RC95LDR_STATUS Rc95LdrSelectOsProfile(const RC95LDR_OS_IDENTITY *Identity,
                                      RC95LDR_OS_PROFILE *Profile)
{
    RC95LDR_OS_PROFILE p;
    if (!Identity || !Profile ||
        Identity->StructureSize<sizeof(*Identity) ||
        Identity->Version!=RC95LDR_OS_PROFILE_VERSION ||
        !Rc95LdrValidFirmware(Identity->Firmware))
        return Rc95LdrInvalidParameter;

    memset(&p,0,sizeof(p));
    p.StructureSize=sizeof(p);
    p.Version=RC95LDR_OS_PROFILE_VERSION;
    p.OsFamily=Identity->OsFamily;
    p.Firmware=Identity->Firmware;
    p.BootEntry=Identity->BootEntry;
    p.OsMajorVersion=Identity->OsMajorVersion;
    p.OsMinorVersion=Identity->OsMinorVersion;
    p.OsBuildNumber=Identity->OsBuildNumber;
    p.Flags=Identity->Flags;

    switch (Identity->OsFamily) {
    case Rc95LdrOsWindowsNtLegacy:
        if (Identity->BootEntry!=Rc95LdrEntryNtldr ||
            Identity->Firmware!=Rc95LdrFirmwareBios ||
            !Rc95LdrIsLegacyNtVersion(Identity->OsMajorVersion,
                                      Identity->OsMinorVersion))
            return Rc95LdrUnsupported;
        p.KernelAbi=Rc95LdrKernelAbiNtLegacy;
        p.LoaderBlockKind=Rc95LdrLoaderBlockNtLegacy;
        break;

    case Rc95LdrOsReactOS:
        if (Identity->BootEntry!=Rc95LdrEntryReactOs ||
            Identity->OsMajorVersion || Identity->OsMinorVersion ||
            Identity->OsBuildNumber)
            return Rc95LdrUnsupported;
        p.KernelAbi=Rc95LdrKernelAbiReactOs;
        p.LoaderBlockKind=Rc95LdrLoaderBlockReactOs;
        break;

    case Rc95LdrOsWindowsModern:
        if (Identity->BootEntry!=Rc95LdrEntryWindowsWinload ||
            !Rc95LdrIsModernWindowsVersion(Identity->OsMajorVersion,
                Identity->OsMinorVersion,Identity->OsBuildNumber))
            return Rc95LdrUnsupported;
        p.KernelAbi=Rc95LdrKernelAbiWindowsModern;
        p.LoaderBlockKind=Rc95LdrLoaderBlockWindowsModern;
        if (Identity->Firmware==Rc95LdrFirmwareUefi)
            p.ResumeApplication=Rc95LdrResumeApplicationWinresumeEfi;
        else
            p.ResumeApplication=Rc95LdrResumeApplicationWinresumeExe;
        break;

    case Rc95LdrOsLiberNt:
        /* LiberNT ships a winload implementation with an ABI that RC95LDR
           does not support, under either BIOS or UEFI firmware. */
        return Rc95LdrUnsupported;

    case Rc95LdrOsRc95:
        if (Identity->BootEntry!=Rc95LdrEntryRc95Winload ||
            Identity->OsMajorVersion || Identity->OsMinorVersion ||
            Identity->OsBuildNumber)
            return Rc95LdrUnsupported;
        p.KernelAbi=Rc95LdrKernelAbiRc95;
        p.LoaderBlockKind=Rc95LdrLoaderBlockRc95;
        p.ResumeApplication=Rc95LdrResumeApplicationRc95Winload;
        break;

    default:
        return Rc95LdrUnsupported; /* Includes any LiberNT identity. */
    }

    *Profile=p;
    return Rc95LdrSuccess;
}
