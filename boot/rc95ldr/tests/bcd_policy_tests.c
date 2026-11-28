#include "rc95ldrpolicy.h"
#include "rc95ldrwinload.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

static void put32(uint8_t *p, uint32_t v)
{ p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24); }
static void put64(uint8_t *p, uint64_t v)
{ put32(p,(uint32_t)v); put32(p+4,(uint32_t)(v>>32)); }

int main(void)
{
    uint8_t mbrBoot[12]={0}, mbrOs[12]={0}, mbrOs2[12]={0};
    RC95BCD_ELEMENT_VIEW bootView, osView, osView2;
    RC95LDR_BCD_BOOT_CANDIDATE entries[2];
    RC95LDR_PARTITION_INFO parts[3];
    RC95LDR_WINDOWS_INSTALLATION install;
    uint16_t root[]={'\\','W','i','n','d','o','w','s',0};
    uint16_t loader[]={'\\','W','i','n','d','o','w','s','\\','S','y','s','t','e','m','3','2','\\','w','i','n','l','o','a','d','.','e','f','i',0};

    put32(mbrBoot,0x12345678); put64(mbrBoot+4,1048576);
    put32(mbrOs,0x12345678); put64(mbrOs+4,2097152);
    put32(mbrOs2,0x12345678); put64(mbrOs2+4,3145728);
    memset(&bootView,0,sizeof(bootView)); bootView.Kind=Rc95BcdElementDevice;
    bootView.DeviceType=RC95BCD_DEVICE_TYPE_PARTITION;
    bootView.DeviceData=mbrBoot; bootView.DeviceDataLength=sizeof(mbrBoot);
    memset(&osView,0,sizeof(osView)); osView.Kind=Rc95BcdElementDevice;
    osView.DeviceType=RC95BCD_DEVICE_TYPE_PARTITION;
    osView.DeviceData=mbrOs; osView.DeviceDataLength=sizeof(mbrOs);
    memset(&osView2,0,sizeof(osView2)); osView2.Kind=Rc95BcdElementDevice;
    osView2.DeviceType=RC95BCD_DEVICE_TYPE_PARTITION;
    osView2.DeviceData=mbrOs2; osView2.DeviceDataLength=sizeof(mbrOs2);

    memset(parts,0,sizeof(parts));
    parts[0].Style=Rc95LdrPartitionMbr; parts[0].FileSystem=Rc95LdrFsNtfs;
    parts[0].MbrDiskSignature=0x12345678; parts[0].StartLba=2048;
    parts[0].BlockSize=512; parts[0].BlockCount=100000;
    parts[1]=parts[0]; parts[1].StartLba=4096;
    parts[2]=parts[1]; parts[2].StartLba=6144;
    parts[0].FileSystem=Rc95LdrFsFat32; /* ESP/boot files may be FAT32. */

    memset(entries,0,sizeof(entries));
    entries[0].IsWindowsLoader=1; entries[0].IsEnabled=1;
    entries[0].DisplayOrder=0; entries[0].IsDefault=1;
    entries[0].DeviceElement=&bootView; entries[0].OsDeviceElement=&osView;
    entries[0].SystemRootUtf16=root; entries[0].SystemRootCharacters=8;
    entries[0].LoaderPathUtf16=loader; entries[0].LoaderPathCharacters=29;
    entries[0].BootOptionsStorage[0]='-';
    entries[0].BootOptionsStorage[1]='n';
    entries[0].BootOptionsStorage[2]='o';
    entries[0].BootOptionsCharacters=3;
    if (Rc95LdrSelectWindowsInstallation(parts,2,entries,1,&install)!=Rc95LdrSuccess)
        return 1;
    if (install.PartitionIndex!=0 || install.OsPartitionIndex!=1 ||
        install.SystemRootCharacters!=8 || install.SystemRootUtf16[8]!=0 ||
        install.LoaderPathCharacters!=29 || install.LoaderPathUtf16[29]!=0 ||
        install.BootOptionsCharacters!=3 ||
        install.BootOptionsUtf16[0]!='-' ||
        install.BootOptionsUtf16[1]!='n' ||
        install.BootOptionsUtf16[2]!='o' ||
        install.BootOptionsUtf16[3]!=0)
        return 1;

    /* A user-selected non-default BCD object must resolve its own osdevice,
       rather than inheriting the default candidate's NTFS partition. */
    entries[1]=entries[0];
    entries[1].IsDefault=0; entries[1].DisplayOrder=1;
    entries[1].OsDeviceElement=&osView2;
    entries[1].BootOptionsStorage[0]='-';
    entries[1].BootOptionsStorage[1]='s';
    entries[1].BootOptionsStorage[2]='a';
    entries[1].BootOptionsStorage[3]='f';
    entries[1].BootOptionsStorage[4]='e';
    entries[1].BootOptionsCharacters=5;
    if (Rc95LdrResolveWindowsBootCandidate(parts,3,&entries[1],&install)!=Rc95LdrSuccess ||
        install.PartitionIndex!=0 || install.OsPartitionIndex!=2 ||
        install.OsPartition.StartLba!=6144 ||
        install.BootOptionsCharacters!=5 ||
        install.BootOptionsUtf16[4]!='e' ||
        install.BootOptionsUtf16[5]!=0)
        return 1;

    /* A Btrfs osdevice is retained as the exact BCD-selected candidate.
       Filtering must not discard it or substitute another partition; UEFI
       mounts and validates the native read-only provider after selection. */
    parts[2].FileSystem=Rc95LdrFsBtrfs;
    if (Rc95LdrResolveWindowsBootCandidate(parts,3,&entries[1],&install)!=Rc95LdrSuccess ||
        install.OsPartition.FileSystem!=Rc95LdrFsBtrfs ||
        install.OsPartition.StartLba!=6144)
        return 1;
    {
        size_t BtrfsCandidateCount=1;
        RC95LDR_BCD_BOOT_CANDIDATE BtrfsCandidate=entries[1];
        if (Rc95LdrFilterWindowsBootCandidates(parts,3,&BtrfsCandidate,
                &BtrfsCandidateCount)!=Rc95LdrSuccess||
            BtrfsCandidateCount!=1)
            return 1;
    }

    parts[2].FileSystem=Rc95LdrFsUnknown;
    if (Rc95LdrResolveWindowsBootCandidate(parts,3,&entries[1],&install)==Rc95LdrSuccess)
        return 1;
    parts[2].FileSystem=Rc95LdrFsNtfs;

    /* An unterminated or over-capacity BCD option payload is never copied. */
    entries[1].BootOptionsCharacters=RC95LDR_MAX_BCD_STRING_CHARS;
    if (Rc95LdrResolveWindowsBootCandidate(parts,3,&entries[1],&install)==Rc95LdrSuccess)
        return 1;
    entries[1].BootOptionsCharacters=5;

    /* A loader path and SystemRoot must both be absolute volume-relative
       paths. The policy resolver must not silently normalize either one. */
    entries[1].LoaderPathUtf16=loader+1;
    if (Rc95LdrResolveWindowsBootCandidate(parts,3,&entries[1],&install)==Rc95LdrSuccess)
        return 1;
    entries[1].LoaderPathUtf16=loader;
    entries[1].SystemRootUtf16=root+1;
    if (Rc95LdrResolveWindowsBootCandidate(parts,3,&entries[1],&install)==Rc95LdrSuccess)
        return 1;
    entries[1].SystemRootUtf16=root;

    /* Embedded NULs are not a valid BCD options payload. */
    entries[1].BootOptionsStorage[2]=0;
    if (Rc95LdrResolveWindowsBootCandidate(parts,3,&entries[1],&install)==Rc95LdrSuccess)
        return 1;
    entries[1].BootOptionsStorage[2]='a';

    /* GPT payload is disk GUID followed by the unique partition GUID. */
    {
        uint8_t gpt[32];
        RC95BCD_PARTITION_DEVICE decoded;
        size_t i;
        for (i=0;i<sizeof(gpt);++i) gpt[i]=(uint8_t)(i+1);
        bootView.DeviceData=gpt; bootView.DeviceDataLength=sizeof(gpt);
        if (Rc95BcdDecodePartitionDevice(&bootView,&decoded)!=Rc95LdrSuccess ||
            decoded.Encoding!=Rc95BcdPartitionGpt ||
            decoded.Identity.Kind!=Rc95LdrPartitionIdGpt ||
            decoded.Identity.GptDiskGuid[0]!=1 ||
            decoded.Identity.GptUniqueGuid[0]!=17)
            return 1;
        bootView.DeviceType=3; /* BCD file-device payload is not a partition. */
        if (Rc95BcdDecodePartitionDevice(&bootView,&decoded)!=Rc95LdrUnsupported)
            return 1;
        bootView.DeviceType=RC95BCD_DEVICE_TYPE_PARTITION;
        bootView.DeviceData=mbrBoot;
    }

    /* Truncated/unknown device tails must never be treated as a partition. */
    bootView.DeviceDataLength=11;
    if (Rc95LdrSelectWindowsInstallation(parts,2,entries,1,&install)==Rc95LdrSuccess)
        return 1;
    /* The selected candidate's graphics option is copied into the
       WINLOAD request; absent BCD element defaults to graphics enabled. */
    {
        RC95LDR_WINDOWS_INSTALLATION selected;
        RC95LDR_WINLOAD_REQUEST request;
        memset(&selected,0,sizeof(selected));
        memset(&request,0,sizeof(request));
        request.StructureSize=sizeof(request);
        selected.GraphicsModeDisabled=1;
        assert(Rc95LdrApplyInstallationGraphicsPolicy(&selected,&request)==Rc95LdrSuccess);
        assert(request.GraphicsModeDisabled==1);
        assert(request.GraphicsModePolicyValid==1);
        selected.GraphicsModeDisabled=0;
        assert(Rc95LdrApplyInstallationGraphicsPolicy(&selected,&request)==Rc95LdrSuccess);
        assert(request.GraphicsModeDisabled==0);
        selected.GraphicsModeDisabled=2;
        assert(Rc95LdrApplyInstallationGraphicsPolicy(&selected,&request)==Rc95LdrInvalidParameter);
        selected.GraphicsModeDisabled=1;
        request.StructureSize=(uint32_t)offsetof(RC95LDR_WINLOAD_REQUEST,GraphicsModeDisabled);
        assert(Rc95LdrApplyInstallationGraphicsPolicy(&selected,&request)==Rc95LdrBufferTooSmall);
    }
    /* Restore the valid MBR device payload after the truncated-payload test. */
    bootView.DeviceDataLength=sizeof(mbrBoot);
    /* The graphics policy belongs to the exact selected BCD object. Verify
       that text-only selection survives candidate resolution and reaches the
       WINLOAD request, while an absent/zero option keeps graphics enabled. */
    {
        RC95LDR_WINDOWS_INSTALLATION selected;
        RC95LDR_WINLOAD_REQUEST request;
        memset(&request,0,sizeof(request));
        request.StructureSize=sizeof(request);
        entries[1].GraphicsModeDisabled=1;
        assert(Rc95LdrResolveWindowsBootCandidate(parts,3,&entries[1],&selected)==
               Rc95LdrSuccess);
        assert(selected.GraphicsModeDisabled==1);
        assert(Rc95LdrApplyInstallationGraphicsPolicy(&selected,&request)==
               Rc95LdrSuccess);
        assert(request.GraphicsModePolicyValid==1);
        assert(request.GraphicsModeDisabled==1);

        entries[1].GraphicsModeDisabled=0;
        assert(Rc95LdrResolveWindowsBootCandidate(parts,3,&entries[1],&selected)==
               Rc95LdrSuccess);
        assert(selected.GraphicsModeDisabled==0);
        assert(Rc95LdrApplyInstallationGraphicsPolicy(&selected,&request)==
               Rc95LdrSuccess);
        assert(request.GraphicsModePolicyValid==1);
        assert(request.GraphicsModeDisabled==0);

        /* bootres admission is an explicit RC95-only policy decision.
           graphicsmodedisabled=1 must never load/map the optional DLL. */
        assert(Rc95LdrShouldLoadBootResource(Rc95LdrOsRc95,1,0));
        assert(!Rc95LdrShouldLoadBootResource(Rc95LdrOsRc95,1,1));
        assert(!Rc95LdrShouldLoadBootResource(Rc95LdrOsRc95,0,0));
        assert(!Rc95LdrShouldLoadBootResource(Rc95LdrOsWindowsModern,1,0));
        assert(!Rc95LdrShouldLoadBootResource(Rc95LdrOsRc95,2,0));
    }
    puts("RC95LDR BCD policy tests passed");
    return 0;
}
