#include "rc95ldrpolicy.h"
#include <string.h>
#include <stdio.h>

static void put32(uint8_t *p, uint32_t v)
{ p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24); }
static void put64(uint8_t *p, uint64_t v)
{ put32(p,(uint32_t)v); put32(p+4,(uint32_t)(v>>32)); }

int main(void)
{
    uint8_t mbrBoot[12]={0}, mbrOs[12]={0};
    RC95BCD_ELEMENT_VIEW bootView, osView;
    RC95LDR_BCD_BOOT_CANDIDATE entries[2];
    RC95LDR_PARTITION_INFO parts[2];
    RC95LDR_WINDOWS_INSTALLATION install;
    uint16_t root[]={'\\','W','i','n','d','o','w','s',0};
    uint16_t loader[]={'\\','W','i','n','d','o','w','s','\\','S','y','s','t','e','m','3','2','\\','w','i','n','l','o','a','d','.','e','f','i',0};

    put32(mbrBoot,0x12345678); put64(mbrBoot+4,1048576);
    put32(mbrOs,0x12345678); put64(mbrOs+4,2097152);
    memset(&bootView,0,sizeof(bootView)); bootView.Kind=Rc95BcdElementDevice;
    bootView.DeviceType=RC95BCD_DEVICE_TYPE_PARTITION;
    bootView.DeviceData=mbrBoot; bootView.DeviceDataLength=sizeof(mbrBoot);
    memset(&osView,0,sizeof(osView)); osView.Kind=Rc95BcdElementDevice;
    osView.DeviceType=RC95BCD_DEVICE_TYPE_PARTITION;
    osView.DeviceData=mbrOs; osView.DeviceDataLength=sizeof(mbrOs);

    memset(parts,0,sizeof(parts));
    parts[0].Style=Rc95LdrPartitionMbr; parts[0].FileSystem=Rc95LdrFsNtfs;
    parts[0].MbrDiskSignature=0x12345678; parts[0].StartLba=2048;
    parts[0].BlockSize=512; parts[0].BlockCount=100000;
    parts[1]=parts[0]; parts[1].StartLba=4096;
    parts[0].FileSystem=Rc95LdrFsFat32; /* ESP/boot files may be FAT32. */

    memset(entries,0,sizeof(entries));
    entries[0].IsWindowsLoader=1; entries[0].IsEnabled=1;
    entries[0].DisplayOrder=0; entries[0].IsDefault=1;
    entries[0].DeviceElement=&bootView; entries[0].OsDeviceElement=&osView;
    entries[0].SystemRootUtf16=root; entries[0].SystemRootCharacters=8;
    entries[0].LoaderPathUtf16=loader; entries[0].LoaderPathCharacters=29;
    if (Rc95LdrSelectWindowsInstallation(parts,2,entries,1,&install)!=Rc95LdrSuccess)
        return 1;
    if (install.PartitionIndex!=0 || install.OsPartitionIndex!=1 ||
        install.SystemRootCharacters!=8 || install.SystemRootUtf16[8]!=0 ||
        install.LoaderPathCharacters!=29 || install.LoaderPathUtf16[29]!=0)
        return 1;

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
    puts("RC95LDR BCD policy tests passed");
    return 0;
}
