#include "rc95ldrloaderblock.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>

typedef struct _RC95LDR_BLOCK_LAYOUT {
    size_t Size, LoadList, MemoryList, DriverList, RegistryLength, RegistryBase;
    size_t ArcBoot, ArcHal, NtBoot, NtHal, Options, Extension, ConfigurationRoot, NlsData, ArcDiskInformation, Arch, Firmware;
    size_t PrefixMajor, PrefixMinor, PrefixSize;
    size_t ExtraLists[4]; unsigned ExtraListCount;
    size_t FirmwareVersion, FirmwareRuntime, FirmwareStatus, FirmwareMissed;
    size_t FirmwareList, FirmwareMap, FirmwareMapSize, FirmwareMapDescriptorSize;
} RC95LDR_BLOCK_LAYOUT;

static void InitList(RC95LDR_NT_LIST_ENTRY *h)
{ h->Flink=(RC95LDR_NT_PTR)(uintptr_t)h; h->Blink=(RC95LDR_NT_PTR)(uintptr_t)h; }
static void InsertTail(RC95LDR_NT_LIST_ENTRY *h,RC95LDR_NT_LIST_ENTRY *e)
{
    RC95LDR_NT_LIST_ENTRY *tail=(RC95LDR_NT_LIST_ENTRY *)(uintptr_t)h->Blink;
    e->Flink=(RC95LDR_NT_PTR)(uintptr_t)h;e->Blink=(RC95LDR_NT_PTR)(uintptr_t)tail;
    tail->Flink=(RC95LDR_NT_PTR)(uintptr_t)e;h->Blink=(RC95LDR_NT_PTR)(uintptr_t)e;
}
static int Add(size_t *n,size_t a,size_t *o)
{ if(*n>SIZE_MAX-a)return 0;*o=*n;*n+=a;return 1; }
static int FitsNativePointer(uint64_t value)
{
#if UINTPTR_MAX < UINT64_MAX
    return value<=(uint64_t)UINTPTR_MAX;
#else
    (void)value;return 1;
#endif
}
static size_t AsciiU16Size(const char *s)
{ return (s?strlen(s):0)+1; }
static size_t AsciiSize(const char *s)
{ return (s?strlen(s):0)+1; }
static RC95LDR_NT_PTR PutAnsi(uint8_t *b,size_t *at,const char *s)
{ size_t n=AsciiSize(s);RC95LDR_NT_PTR p=(RC95LDR_NT_PTR)(uintptr_t)(b+*at);if(s)memcpy(b+*at,s,n);else b[*at]=0;*at+=n;return p; }
static RC95LDR_NT_PTR PutU16(uint8_t *b,size_t *at,const char *s)
{
    size_t i,n=s?strlen(s):0;uint16_t *p=(uint16_t *)(void *)(b+*at);
    for (i = 0; i < n; ++i)
        p[i] = (uint8_t)s[i];
    p[n] = 0;
    *at += (n + 1) * sizeof(uint16_t);
    return (RC95LDR_NT_PTR)(uintptr_t)p;
}
static void SetUnicode(RC95LDR_NT_UNICODE_STRING *u,RC95LDR_NT_PTR p,const char *s)
{
    size_t n=s?strlen(s):0;u->Length=(uint16_t)(n*2);u->MaximumLength=(uint16_t)((n+1)*2);u->Buffer=p;
}
static const char *BaseName(const char *s)
{
    const char *p=s?s:"",*last=p,*q;
    for(q=p;*q;q++)if(*q=='/'||*q=='\\')last=q+1;
    return last;
}
static int Layout(RC95LDR_NT_TARGET_VERSION v,RC95LDR_BLOCK_LAYOUT *l)
{
#define SET(T) do { l->Size=sizeof(T); l->LoadList=offsetof(T,LoadOrderListHead); l->MemoryList=offsetof(T,MemoryDescriptorListHead); l->DriverList=offsetof(T,BootDriverListHead); l->RegistryLength=offsetof(T,RegistryLength); l->RegistryBase=offsetof(T,RegistryBase); l->ArcBoot=offsetof(T,ArcBootDeviceName); l->ArcHal=offsetof(T,ArcHalDeviceName); l->NtBoot=offsetof(T,NtBootPathName); l->NtHal=offsetof(T,NtHalPathName); l->Options=offsetof(T,LoadOptions); l->ConfigurationRoot=offsetof(T,ConfigurationRoot); l->NlsData=offsetof(T,NlsData); l->ArcDiskInformation=offsetof(T,ArcDiskInformation); l->Arch=offsetof(T,u); } while(0)
    memset(l,0,sizeof(*l));
    switch(v){
    case Rc95LdrNtVersion4: SET(RC95LDR_NT_LOADER_BLOCK_V4);break;
    case Rc95LdrNtVersion5: SET(RC95LDR_NT_LOADER_BLOCK_V5);l->Extension=offsetof(RC95LDR_NT_LOADER_BLOCK_V5,Extension);break;
    case Rc95LdrNtVersion6: SET(RC95LDR_NT_LOADER_BLOCK_V6);l->Extension=offsetof(RC95LDR_NT_LOADER_BLOCK_V6,Extension);l->Firmware=offsetof(RC95LDR_NT_LOADER_BLOCK_V6,FirmwareInformation);l->FirmwareVersion=l->Firmware+offsetof(RC95LDR_NT_FIRMWARE_INFORMATION,u)+offsetof(RC95LDR_NT_EFI_FIRMWARE_INFORMATION,FirmwareVersion);l->FirmwareRuntime=l->Firmware+offsetof(RC95LDR_NT_FIRMWARE_INFORMATION,u)+offsetof(RC95LDR_NT_EFI_FIRMWARE_INFORMATION,VirtualEfiRuntimeServices);l->FirmwareStatus=l->Firmware+offsetof(RC95LDR_NT_FIRMWARE_INFORMATION,u)+offsetof(RC95LDR_NT_EFI_FIRMWARE_INFORMATION,SetVirtualAddressMapStatus);l->FirmwareMissed=l->Firmware+offsetof(RC95LDR_NT_FIRMWARE_INFORMATION,u)+offsetof(RC95LDR_NT_EFI_FIRMWARE_INFORMATION,MissedMappingsCount);break;
    case Rc95LdrNtVersion61: SET(RC95LDR_NT_LOADER_BLOCK_V61);l->Extension=offsetof(RC95LDR_NT_LOADER_BLOCK_V61,Extension);l->Firmware=offsetof(RC95LDR_NT_LOADER_BLOCK_V61,FirmwareInformation);l->FirmwareVersion=l->Firmware+offsetof(RC95LDR_NT_FIRMWARE_INFORMATION,u)+offsetof(RC95LDR_NT_EFI_FIRMWARE_INFORMATION,FirmwareVersion);l->FirmwareRuntime=l->Firmware+offsetof(RC95LDR_NT_FIRMWARE_INFORMATION,u)+offsetof(RC95LDR_NT_EFI_FIRMWARE_INFORMATION,VirtualEfiRuntimeServices);l->FirmwareStatus=l->Firmware+offsetof(RC95LDR_NT_FIRMWARE_INFORMATION,u)+offsetof(RC95LDR_NT_EFI_FIRMWARE_INFORMATION,SetVirtualAddressMapStatus);l->FirmwareMissed=l->Firmware+offsetof(RC95LDR_NT_FIRMWARE_INFORMATION,u)+offsetof(RC95LDR_NT_EFI_FIRMWARE_INFORMATION,MissedMappingsCount);l->PrefixMajor=offsetof(RC95LDR_NT_LOADER_BLOCK_V61,OsMajorVersion);l->PrefixMinor=offsetof(RC95LDR_NT_LOADER_BLOCK_V61,OsMinorVersion);l->PrefixSize=offsetof(RC95LDR_NT_LOADER_BLOCK_V61,Size);break;
    case Rc95LdrNtVersion62: SET(RC95LDR_NT_LOADER_BLOCK_V62);l->Extension=offsetof(RC95LDR_NT_LOADER_BLOCK_V62,Extension);l->Firmware=offsetof(RC95LDR_NT_LOADER_BLOCK_V62,FirmwareInformation);l->FirmwareVersion=l->Firmware+offsetof(RC95LDR_NT_FIRMWARE_INFORMATION_V62,u)+offsetof(RC95LDR_NT_EFI_FIRMWARE_INFORMATION_V62,FirmwareVersion);l->FirmwareRuntime=l->Firmware+offsetof(RC95LDR_NT_FIRMWARE_INFORMATION_V62,u)+offsetof(RC95LDR_NT_EFI_FIRMWARE_INFORMATION_V62,VirtualEfiRuntimeServices);l->FirmwareStatus=l->Firmware+offsetof(RC95LDR_NT_FIRMWARE_INFORMATION_V62,u)+offsetof(RC95LDR_NT_EFI_FIRMWARE_INFORMATION_V62,SetVirtualAddressMapStatus);l->FirmwareMissed=l->Firmware+offsetof(RC95LDR_NT_FIRMWARE_INFORMATION_V62,u)+offsetof(RC95LDR_NT_EFI_FIRMWARE_INFORMATION_V62,MissedMappingsCount);l->FirmwareList=offsetof(RC95LDR_NT_LOADER_BLOCK_V62,FirmwareInformation)+offsetof(RC95LDR_NT_FIRMWARE_INFORMATION_V62,u)+offsetof(RC95LDR_NT_EFI_FIRMWARE_INFORMATION_V62,FirmwareResourceList);l->PrefixMajor=offsetof(RC95LDR_NT_LOADER_BLOCK_V62,OsMajorVersion);l->PrefixMinor=offsetof(RC95LDR_NT_LOADER_BLOCK_V62,OsMinorVersion);l->PrefixSize=offsetof(RC95LDR_NT_LOADER_BLOCK_V62,Size);l->ExtraLists[0]=offsetof(RC95LDR_NT_LOADER_BLOCK_V62,EarlyLaunchListHead);l->ExtraLists[1]=offsetof(RC95LDR_NT_LOADER_BLOCK_V62,CoreDriverListHead);l->ExtraListCount=2;break;
    case Rc95LdrNtVersion63: SET(RC95LDR_NT_LOADER_BLOCK_V63);l->Extension=offsetof(RC95LDR_NT_LOADER_BLOCK_V63,Extension);l->Firmware=offsetof(RC95LDR_NT_LOADER_BLOCK_V63,FirmwareInformation);l->FirmwareVersion=l->Firmware+offsetof(RC95LDR_NT_FIRMWARE_INFORMATION_V63,u)+offsetof(RC95LDR_NT_EFI_FIRMWARE_INFORMATION_V63,FirmwareVersion);l->FirmwareRuntime=l->Firmware+offsetof(RC95LDR_NT_FIRMWARE_INFORMATION_V63,u)+offsetof(RC95LDR_NT_EFI_FIRMWARE_INFORMATION_V63,VirtualEfiRuntimeServices);l->FirmwareStatus=l->Firmware+offsetof(RC95LDR_NT_FIRMWARE_INFORMATION_V63,u)+offsetof(RC95LDR_NT_EFI_FIRMWARE_INFORMATION_V63,SetVirtualAddressMapStatus);l->FirmwareMissed=l->Firmware+offsetof(RC95LDR_NT_FIRMWARE_INFORMATION_V63,u)+offsetof(RC95LDR_NT_EFI_FIRMWARE_INFORMATION_V63,MissedMappingsCount);l->FirmwareList=offsetof(RC95LDR_NT_LOADER_BLOCK_V63,FirmwareInformation)+offsetof(RC95LDR_NT_FIRMWARE_INFORMATION_V63,u)+offsetof(RC95LDR_NT_EFI_FIRMWARE_INFORMATION_V63,FirmwareResourceList);l->FirmwareMap=offsetof(RC95LDR_NT_LOADER_BLOCK_V63,FirmwareInformation)+offsetof(RC95LDR_NT_FIRMWARE_INFORMATION_V63,u)+offsetof(RC95LDR_NT_EFI_FIRMWARE_INFORMATION_V63,EfiMemoryMap);l->FirmwareMapSize=offsetof(RC95LDR_NT_LOADER_BLOCK_V63,FirmwareInformation)+offsetof(RC95LDR_NT_FIRMWARE_INFORMATION_V63,u)+offsetof(RC95LDR_NT_EFI_FIRMWARE_INFORMATION_V63,EfiMemoryMapSize);l->FirmwareMapDescriptorSize=offsetof(RC95LDR_NT_LOADER_BLOCK_V63,FirmwareInformation)+offsetof(RC95LDR_NT_FIRMWARE_INFORMATION_V63,u)+offsetof(RC95LDR_NT_EFI_FIRMWARE_INFORMATION_V63,EfiMemoryMapDescriptorSize);l->PrefixMajor=offsetof(RC95LDR_NT_LOADER_BLOCK_V63,OsMajorVersion);l->PrefixMinor=offsetof(RC95LDR_NT_LOADER_BLOCK_V63,OsMinorVersion);l->PrefixSize=offsetof(RC95LDR_NT_LOADER_BLOCK_V63,Size);l->ExtraLists[0]=offsetof(RC95LDR_NT_LOADER_BLOCK_V63,EarlyLaunchListHead);l->ExtraLists[1]=offsetof(RC95LDR_NT_LOADER_BLOCK_V63,CoreDriverListHead);l->ExtraListCount=2;break;
    case Rc95LdrNtVersion10: SET(RC95LDR_NT_LOADER_BLOCK_V10);l->Extension=offsetof(RC95LDR_NT_LOADER_BLOCK_V10,Extension);l->Firmware=offsetof(RC95LDR_NT_LOADER_BLOCK_V10,FirmwareInformation);l->FirmwareVersion=l->Firmware+offsetof(RC95LDR_NT_FIRMWARE_INFORMATION_V63,u)+offsetof(RC95LDR_NT_EFI_FIRMWARE_INFORMATION_V63,FirmwareVersion);l->FirmwareRuntime=l->Firmware+offsetof(RC95LDR_NT_FIRMWARE_INFORMATION_V63,u)+offsetof(RC95LDR_NT_EFI_FIRMWARE_INFORMATION_V63,VirtualEfiRuntimeServices);l->FirmwareStatus=l->Firmware+offsetof(RC95LDR_NT_FIRMWARE_INFORMATION_V63,u)+offsetof(RC95LDR_NT_EFI_FIRMWARE_INFORMATION_V63,SetVirtualAddressMapStatus);l->FirmwareMissed=l->Firmware+offsetof(RC95LDR_NT_FIRMWARE_INFORMATION_V63,u)+offsetof(RC95LDR_NT_EFI_FIRMWARE_INFORMATION_V63,MissedMappingsCount);l->FirmwareList=offsetof(RC95LDR_NT_LOADER_BLOCK_V10,FirmwareInformation)+offsetof(RC95LDR_NT_FIRMWARE_INFORMATION_V63,u)+offsetof(RC95LDR_NT_EFI_FIRMWARE_INFORMATION_V63,FirmwareResourceList);l->FirmwareMap=offsetof(RC95LDR_NT_LOADER_BLOCK_V10,FirmwareInformation)+offsetof(RC95LDR_NT_FIRMWARE_INFORMATION_V63,u)+offsetof(RC95LDR_NT_EFI_FIRMWARE_INFORMATION_V63,EfiMemoryMap);l->FirmwareMapSize=offsetof(RC95LDR_NT_LOADER_BLOCK_V10,FirmwareInformation)+offsetof(RC95LDR_NT_FIRMWARE_INFORMATION_V63,u)+offsetof(RC95LDR_NT_EFI_FIRMWARE_INFORMATION_V63,EfiMemoryMapSize);l->FirmwareMapDescriptorSize=offsetof(RC95LDR_NT_LOADER_BLOCK_V10,FirmwareInformation)+offsetof(RC95LDR_NT_FIRMWARE_INFORMATION_V63,u)+offsetof(RC95LDR_NT_EFI_FIRMWARE_INFORMATION_V63,EfiMemoryMapDescriptorSize);l->PrefixMajor=offsetof(RC95LDR_NT_LOADER_BLOCK_V10,OsMajorVersion);l->PrefixMinor=offsetof(RC95LDR_NT_LOADER_BLOCK_V10,OsMinorVersion);l->PrefixSize=offsetof(RC95LDR_NT_LOADER_BLOCK_V10,Size);l->ExtraLists[0]=offsetof(RC95LDR_NT_LOADER_BLOCK_V10,EarlyLaunchListHead);l->ExtraLists[1]=offsetof(RC95LDR_NT_LOADER_BLOCK_V10,CoreDriverListHead);l->ExtraLists[2]=offsetof(RC95LDR_NT_LOADER_BLOCK_V10,CoreExtensionsDriverListHead);l->ExtraLists[3]=offsetof(RC95LDR_NT_LOADER_BLOCK_V10,TpmCoreDriverListHead);l->ExtraListCount=4;break;
    default:return 0;
    }
#undef SET
    return 1;
}
static RC95LDR_STATUS Validate(const RC95LDR_NATIVE_LOADER_INPUT *i,void *b,size_t cap,size_t *used)
{
    if(!i||!b||!used||i->StructureSize<sizeof(*i)||i->Version!=RC95LDR_RC95_LOADER_INPUT_VERSION||
       !i->RegistryBase||!i->RegistryLength||!i->Modules||!i->ModuleCount||
       i->ModuleCount>RC95LDR_MODULE_MAX||i->MemoryRangeCount>RC95LDR_MEMORY_MAX_RANGES||
       (i->MemoryRangeCount&&!i->MemoryRanges)||i->Architecture<Rc95LdrNtArchI386||
       i->Architecture>Rc95LdrNtArchArm)return Rc95LdrInvalidParameter;
    if(i->Architecture==Rc95LdrNtArchI386 && sizeof(void*)!=4)return Rc95LdrUnsupported;
    if(i->Architecture==Rc95LdrNtArchAmd64 && sizeof(void*)!=8)return Rc95LdrUnsupported;
    if(i->Architecture==Rc95LdrNtArchPowerPc && sizeof(void*)!=4)return Rc95LdrUnsupported;
#if !defined(_ARM_)
    if(i->Architecture==Rc95LdrNtArchArm)return Rc95LdrUnsupported;
#else
    if(i->Architecture==Rc95LdrNtArchArm && sizeof(void*)!=4)return Rc95LdrUnsupported;
#endif
    if(i->FirmwareIsUefi>1)return Rc95LdrInvalidParameter;
    if(!FitsNativePointer(i->ConfigurationRoot)||!FitsNativePointer(i->LoaderExtension)||
       !FitsNativePointer(i->NlsData)||!FitsNativePointer(i->ArcDiskInformation))return Rc95LdrOutOfBounds;
    if(!FitsNativePointer(i->FirmwareSystemTable)||!FitsNativePointer(i->VirtualEfiRuntimeServices)||!FitsNativePointer(i->FirmwareMapAddress))return Rc95LdrOutOfBounds;
    if(i->RegistryLength==0)return Rc95LdrInvalidParameter;
    if((i->ArcBootDevice&&strlen(i->ArcBootDevice)>32766u)||(i->ArcHalDevice&&strlen(i->ArcHalDevice)>32766u)||(i->NtBootPath&&strlen(i->NtBootPath)>32766u)||(i->NtHalPath&&strlen(i->NtHalPath)>32766u)||(i->LoadOptions&&strlen(i->LoadOptions)>32766u))return Rc95LdrInvalidParameter;
    {uint32_t n,hasKernel=0,hasHal=0;for(n=0;n<i->ModuleCount;n++){const RC95LDR_LOADED_MODULE*m=&i->Modules[n];if(!m->Path||!m->ImageSize||m->ImageSize>UINT32_MAX||!m->ImageBase||!FitsNativePointer(m->ImageBase)||!FitsNativePointer(m->EntryPoint)||m->ImageBase>UINT64_MAX-m->ImageSize||!FitsNativePointer(m->ImageBase+m->ImageSize)||(m->EntryPoint&&(m->EntryPoint<m->ImageBase||m->EntryPoint>=m->ImageBase+m->ImageSize))||strlen(m->Path)>32766u||(m->RegistryPath&&strlen(m->RegistryPath)>32766u))return Rc95LdrInvalidParameter;if(m->Flags&RC95LDR_MODULE_KERNEL){if(!m->EntryPoint)return Rc95LdrInvalidImage;hasKernel=1;}if(m->Flags&RC95LDR_MODULE_HAL){if(!m->EntryPoint)return Rc95LdrInvalidImage;hasHal=1;}}if(!hasKernel||!hasHal)return Rc95LdrInvalidParameter;}
    {uint32_t n;for(n=0;n<i->MemoryRangeCount;n++){const RC95LDR_MEMORY_RANGE*m=&i->MemoryRanges[n];if(!FitsNativePointer(m->BasePage)||!FitsNativePointer(m->PageCount)||m->BasePage>(uint64_t)UINTPTR_MAX-m->PageCount)return Rc95LdrOutOfBounds;}}
    (void)cap;*used=0;return Rc95LdrSuccess;
}
static RC95LDR_STATUS BuildNt(const RC95LDR_NATIVE_LOADER_INPUT *i,void *buffer,size_t cap,size_t *used)
{
    RC95LDR_BLOCK_LAYOUT l;uint8_t *b=(uint8_t*)buffer;size_t total,mods,mems,drivers,strings,at,j,driverCount=0;
    size_t moduleStride=sizeof(RC95LDR_NT_LDR_DATA_TABLE_ENTRY),memStride=sizeof(RC95LDR_NT_MEMORY_ALLOCATION_DESCRIPTOR),driverStride=sizeof(RC95LDR_NT_BOOT_DRIVER_LIST_ENTRY);
    RC95LDR_NT_LIST_ENTRY *load,*memory,*boot;
    RC95LDR_STATUS st=Validate(i,buffer,cap,used);if(st!=Rc95LdrSuccess)return st;
    if(!Layout(i->NtVersion,&l))return Rc95LdrUnsupported;
    if(i->NtVersion==Rc95LdrNtVersion4&&(i->OsMajorVersion!=4||i->OsMinorVersion!=0))return Rc95LdrInvalidParameter;
    if(i->NtVersion==Rc95LdrNtVersion5&&(i->OsMajorVersion!=5||i->OsMinorVersion>2))return Rc95LdrInvalidParameter;
    if(i->NtVersion==Rc95LdrNtVersion6&&(i->OsMajorVersion!=6||i->OsMinorVersion!=0))return Rc95LdrInvalidParameter;
    if(i->NtVersion==Rc95LdrNtVersion61&&(i->OsMajorVersion!=6||i->OsMinorVersion!=1))return Rc95LdrInvalidParameter;
    if(i->NtVersion==Rc95LdrNtVersion62&&(i->OsMajorVersion!=6||i->OsMinorVersion!=2))return Rc95LdrInvalidParameter;
    if(i->NtVersion==Rc95LdrNtVersion63&&(i->OsMajorVersion!=6||i->OsMinorVersion!=3))return Rc95LdrInvalidParameter;
    if(i->NtVersion==Rc95LdrNtVersion10&&(i->OsMajorVersion!=10||i->OsMinorVersion!=0||i->OsBuildNumber<10240))return Rc95LdrInvalidParameter;
    for(j=0;j<i->ModuleCount;j++){if(!i->Modules[j].ImageBase||!i->Modules[j].ImageSize||i->Modules[j].ImageSize>UINT32_MAX||!i->Modules[j].Path)return Rc95LdrInvalidParameter;if(i->Modules[j].Flags&RC95LDR_MODULE_BOOT_DRIVER)driverCount++;}
    total=l.Size;
    if(!Add(&total,moduleStride*i->ModuleCount,&mods)||!Add(&total,memStride*i->MemoryRangeCount,&mems)||!Add(&total,driverStride*driverCount,&drivers))return Rc95LdrIntegerOverflow;
    strings=0;
    for(j=0;j<i->ModuleCount;j++){size_t n=AsciiU16Size(i->Modules[j].Path)+AsciiU16Size(BaseName(i->Modules[j].Path));if(i->Modules[j].Flags&RC95LDR_MODULE_BOOT_DRIVER)n+=AsciiU16Size(i->Modules[j].Path)+AsciiU16Size(i->Modules[j].RegistryPath);if(strings>SIZE_MAX-n*2)return Rc95LdrIntegerOverflow;strings+=n*2;}
    {const char *ss[5]={i->ArcBootDevice,i->ArcHalDevice,i->NtBootPath,i->NtHalPath,i->LoadOptions};for(j=0;j<5;j++){size_t n=AsciiSize(ss[j]);if(strings>SIZE_MAX-n)return Rc95LdrIntegerOverflow;strings+=n;}}
    if(!Add(&total,strings,&at)||total>cap||total>UINT32_MAX)return total>cap?Rc95LdrBufferTooSmall:Rc95LdrIntegerOverflow;
    memset(b,0,total);
    load=(RC95LDR_NT_LIST_ENTRY*)(void*)(b+l.LoadList);memory=(RC95LDR_NT_LIST_ENTRY*)(void*)(b+l.MemoryList);boot=(RC95LDR_NT_LIST_ENTRY*)(void*)(b+l.DriverList);
    InitList(load);InitList(memory);InitList(boot);for(j=0;j<l.ExtraListCount;j++)InitList((RC95LDR_NT_LIST_ENTRY*)(void*)(b+l.ExtraLists[j]));
    if(l.PrefixSize){*(uint32_t*)(void*)(b+l.PrefixMajor)=i->OsMajorVersion;*(uint32_t*)(void*)(b+l.PrefixMinor)=i->OsMinorVersion;*(uint32_t*)(void*)(b+l.PrefixSize)=(uint32_t)l.Size;}
    *(uint32_t*)(void*)(b+l.RegistryLength)=i->RegistryLength;
    *(RC95LDR_NT_PTR*)(void*)(b+l.RegistryBase)=(RC95LDR_NT_PTR)(uintptr_t)i->RegistryBase;
    if(l.ConfigurationRoot)*(RC95LDR_NT_PTR*)(void*)(b+l.ConfigurationRoot)=i->ConfigurationRoot;
    if(l.NlsData)*(RC95LDR_NT_PTR*)(void*)(b+l.NlsData)=i->NlsData;
    if(l.ArcDiskInformation)*(RC95LDR_NT_PTR*)(void*)(b+l.ArcDiskInformation)=i->ArcDiskInformation;
    if(l.Extension)*(RC95LDR_NT_PTR*)(void*)(b+l.Extension)=i->LoaderExtension;
    if(l.Firmware){
        *(uint32_t*)(void*)(b+l.Firmware)=i->FirmwareIsUefi?1u:0u;
        if(i->FirmwareIsUefi){
            *(uint32_t*)(void*)(b+l.FirmwareVersion)=i->FirmwareVersion;
            *(RC95LDR_NT_PTR*)(void*)(b+l.FirmwareRuntime)=(RC95LDR_NT_PTR)i->VirtualEfiRuntimeServices;
            *(uint32_t*)(void*)(b+l.FirmwareStatus)=0;
            *(uint32_t*)(void*)(b+l.FirmwareMissed)=0;
            if(l.FirmwareList)InitList((RC95LDR_NT_LIST_ENTRY*)(void*)(b+l.FirmwareList));
            if(l.FirmwareMap){
                *(RC95LDR_NT_PTR*)(void*)(b+l.FirmwareMap)=(RC95LDR_NT_PTR)i->FirmwareMapAddress;
                *(uint32_t*)(void*)(b+l.FirmwareMapSize)=i->FirmwareMapSize;
                *(uint32_t*)(void*)(b+l.FirmwareMapDescriptorSize)=i->FirmwareMapDescriptorSize;
            }
        }
    }
    if(i->ArchitectureBlock){
        memcpy(b+l.Arch,i->ArchitectureBlock,sizeof(*i->ArchitectureBlock));
    }else if(i->Architecture==Rc95LdrNtArchI386){
        RC95LDR_NT_I386_LOADER_BLOCK *a=(void*)(b+l.Arch);a->MachineType=i->MachineType;
    }else if(i->Architecture==Rc95LdrNtArchAmd64){
        /* ReactOS exposes no AMD64 union arm; keep its architecture slot zeroed. */
    }else if(i->Architecture==Rc95LdrNtArchPowerPc){
        RC95LDR_NT_PPC_LOADER_BLOCK*a=(void*)(b+l.Arch);a->MachineType=i->MachineType;
    }else if(i->Architecture==Rc95LdrNtArchArm){
#if defined(_ARM_)
        RC95LDR_NT_ARM_LOADER_BLOCK*a=(void*)(b+l.Arch);a->PcrPage=0;
#else
        return Rc95LdrUnsupported;
#endif
    }
    at=total-strings;
    for(j=0;j<i->ModuleCount;j++){
        RC95LDR_NT_LDR_DATA_TABLE_ENTRY *e=(void*)(b+mods+j*moduleStride);
        RC95LDR_NT_PTR full=PutU16(b,&at,i->Modules[j].Path),base=PutU16(b,&at,BaseName(i->Modules[j].Path));
        e->DllBase=(RC95LDR_NT_PTR)i->Modules[j].ImageBase;e->EntryPoint=(RC95LDR_NT_PTR)i->Modules[j].EntryPoint;e->SizeOfImage=(uint32_t)i->Modules[j].ImageSize;e->Flags=0;
        InitList(&e->InMemoryOrderLinks);InitList(&e->InInitializationOrderLinks);
        SetUnicode(&e->FullDllName,full,i->Modules[j].Path);SetUnicode(&e->BaseDllName,base,BaseName(i->Modules[j].Path));
        InsertTail(load,&e->InLoadOrderLinks);
        if(i->Modules[j].Flags&RC95LDR_MODULE_BOOT_DRIVER){
            RC95LDR_NT_BOOT_DRIVER_LIST_ENTRY*d=(void*)(b+drivers);
            const char *rp=i->Modules[j].RegistryPath?i->Modules[j].RegistryPath:"";
            RC95LDR_NT_PTR fp=PutU16(b,&at,i->Modules[j].Path),rpbuf=PutU16(b,&at,rp);
            SetUnicode(&d->FilePath,fp,i->Modules[j].Path);SetUnicode(&d->RegistryPath,rpbuf,rp);d->LdrEntry=(RC95LDR_NT_PTR)(uintptr_t)e;
            InsertTail(boot,&d->Link);drivers+=driverStride;
        }
    }
    for(j=0;j<i->MemoryRangeCount;j++){
        RC95LDR_NT_MEMORY_ALLOCATION_DESCRIPTOR*d=(void*)(b+mems+j*memStride);
        d->MemoryType=i->MemoryRanges[j].Type;d->BasePage=(RC95LDR_NT_PTR)i->MemoryRanges[j].BasePage;d->PageCount=(RC95LDR_NT_PTR)i->MemoryRanges[j].PageCount;InsertTail(memory,&d->ListEntry);
    }
    *(RC95LDR_NT_PTR*)(void*)(b+l.ArcBoot)=PutAnsi(b,&at,i->ArcBootDevice);
    *(RC95LDR_NT_PTR*)(void*)(b+l.ArcHal)=PutAnsi(b,&at,i->ArcHalDevice);
    *(RC95LDR_NT_PTR*)(void*)(b+l.NtBoot)=PutAnsi(b,&at,i->NtBootPath);
    *(RC95LDR_NT_PTR*)(void*)(b+l.NtHal)=PutAnsi(b,&at,i->NtHalPath);
    *(RC95LDR_NT_PTR*)(void*)(b+l.Options)=PutAnsi(b,&at,i->LoadOptions);
    *used=total;return Rc95LdrSuccess;
}
static void InitRc95List(RC95LDR_RC95_LIST_ENTRY*h)
{h->Flink=(RC95LDR_RC95_PTR)(uintptr_t)h;h->Blink=(RC95LDR_RC95_PTR)(uintptr_t)h;}
static void InsertTailRc95(RC95LDR_RC95_LIST_ENTRY*h,RC95LDR_RC95_LIST_ENTRY*e)
{
    RC95LDR_RC95_LIST_ENTRY*tail=(void*)(uintptr_t)h->Blink;
    e->Flink=(RC95LDR_RC95_PTR)(uintptr_t)h;e->Blink=(RC95LDR_RC95_PTR)(uintptr_t)tail;
    tail->Flink=(RC95LDR_RC95_PTR)(uintptr_t)e;h->Blink=(RC95LDR_RC95_PTR)(uintptr_t)e;
}
static void SetRc95Unicode(RC95LDR_RC95_UNICODE_STRING*u,RC95LDR_RC95_PTR p,const char*s)
{
    size_t n=s?strlen(s):0;u->Length=(uint16_t)(n*2);u->MaximumLength=(uint16_t)((n+1)*2);u->Buffer=p;
}
static RC95LDR_STATUS ValidateRc95(const RC95LDR_RC95_LOADER_INPUT*i,void*b,size_t cap,size_t*used)
{
    uint32_t n,hasKernel=0,hasHal=0;
    if(!i||!b||!used||i->StructureSize<sizeof(*i)||i->Version!=RC95LDR_RC95_LOADER_INPUT_VERSION||
       !i->SystemHive||!i->RegistryLength||!i->Modules||!i->ModuleCount||
       i->ModuleCount>RC95LDR_MODULE_MAX||i->MemoryRangeCount>RC95LDR_MEMORY_MAX_RANGES||
       (i->MemoryRangeCount&&!i->MemoryRanges)||i->Architecture<Rc95LdrNtArchI386||
       i->Architecture>Rc95LdrNtArchArm)return Rc95LdrInvalidParameter;
    if((i->Architecture==Rc95LdrNtArchI386||i->Architecture==Rc95LdrNtArchPowerPc)&&sizeof(void*)!=4)return Rc95LdrUnsupported;
    if(i->Architecture==Rc95LdrNtArchAmd64&&sizeof(void*)!=8)return Rc95LdrUnsupported;
    if(i->Architecture==Rc95LdrNtArchArm){
#if defined(_ARM_)
        if(sizeof(void*)!=4)return Rc95LdrUnsupported;
#else
        return Rc95LdrUnsupported;
#endif
    }
    if(i->FirmwareIsUefi>1||!FitsNativePointer(i->FirmwareSystemTable)||
       !FitsNativePointer(i->FirmwareServices)||!FitsNativePointer(i->FirmwareMap)||
       !FitsNativePointer(i->ConfigurationRoot)||!FitsNativePointer(i->NlsData)||
       !FitsNativePointer(i->ArcDiskInformation))return Rc95LdrOutOfBounds;
    /* The RC95 firmware fields describe a live UEFI handoff, not a generic
       firmware label. Reject incomplete maps and stale EFI pointers on BIOS. */
    if(i->FirmwareIsUefi){
        if(!i->FirmwareSystemTable||!i->FirmwareServices||!i->FirmwareMap||
           !i->FirmwareMapSize||!i->FirmwareMapDescriptorSize||
           i->FirmwareMapDescriptorSize<40u||
           i->FirmwareMapSize%i->FirmwareMapDescriptorSize)
            return Rc95LdrInvalidParameter;
        if(i->FirmwareMap>UINTPTR_MAX-i->FirmwareMapSize)
            return Rc95LdrOutOfBounds;
    }else if(i->FirmwareSystemTable||i->FirmwareServices||i->FirmwareMap||
             i->FirmwareMapSize||i->FirmwareMapDescriptorSize)
        return Rc95LdrInvalidParameter;
    if((i->BootDevice&&strlen(i->BootDevice)>32766u)||
       (i->HalDevice&&strlen(i->HalDevice)>32766u)||
       (i->SystemRoot&&strlen(i->SystemRoot)>32766u)||
       (i->LoadOptions&&strlen(i->LoadOptions)>32766u))
        return Rc95LdrInvalidParameter;
    for(n=0;n<i->ModuleCount;n++){
        const RC95LDR_LOADED_MODULE*m=&i->Modules[n];
        if(!m->Path||strlen(m->Path)>32766u||
           (m->RegistryPath&&strlen(m->RegistryPath)>32766u)||
           !m->ImageBase||!m->ImageSize||m->ImageSize>UINT32_MAX||
           !FitsNativePointer(m->ImageBase)||!FitsNativePointer(m->EntryPoint)||
           m->ImageBase>UINT64_MAX-m->ImageSize||
           !FitsNativePointer(m->ImageBase+m->ImageSize)||
           (m->EntryPoint&&(m->EntryPoint<m->ImageBase||m->EntryPoint>=m->ImageBase+m->ImageSize)))
            return Rc95LdrInvalidParameter;
        if(m->Flags&RC95LDR_MODULE_KERNEL){if(!m->EntryPoint)return Rc95LdrInvalidImage;hasKernel=1;}
        if(m->Flags&RC95LDR_MODULE_HAL){if(!m->EntryPoint)return Rc95LdrInvalidImage;hasHal=1;}
    }
    if(!hasKernel||!hasHal)return Rc95LdrInvalidParameter;
    for(n=0;n<i->MemoryRangeCount;n++)if(!FitsNativePointer(i->MemoryRanges[n].BasePage)||!FitsNativePointer(i->MemoryRanges[n].PageCount)||i->MemoryRanges[n].BasePage>(uint64_t)UINTPTR_MAX-i->MemoryRanges[n].PageCount)return Rc95LdrOutOfBounds;
    (void)cap;*used=0;return Rc95LdrSuccess;
}
static RC95LDR_STATUS BuildRc95(const RC95LDR_RC95_LOADER_INPUT*i,void*buffer,size_t cap,size_t*used)
{
    RC95LDR_RC95_NATIVE_LOADER_BLOCK*h=(void*)buffer;
    uint8_t*b=(uint8_t*)buffer;size_t total=sizeof(*h),mo,me,dr,at,j,drivers=0,strings=0;
    RC95LDR_STATUS s=ValidateRc95(i,buffer,cap,used);if(s!=Rc95LdrSuccess)return s;
    for(j=0;j<i->ModuleCount;j++){
        size_t n=(AsciiU16Size(i->Modules[j].Path)+AsciiU16Size(BaseName(i->Modules[j].Path)))*2;
        if(i->Modules[j].Flags&RC95LDR_MODULE_BOOT_DRIVER){n+=(AsciiU16Size(i->Modules[j].Path)+AsciiU16Size(i->Modules[j].RegistryPath))*2;drivers++;}
        if (strings > SIZE_MAX - n)
            return Rc95LdrIntegerOverflow;
        strings += n;
    }
    if(!Add(&total,sizeof(RC95LDR_RC95_MODULE_ENTRY)*i->ModuleCount,&mo)||
       !Add(&total,sizeof(RC95LDR_RC95_MEMORY_DESCRIPTOR)*i->MemoryRangeCount,&me)||
       !Add(&total,sizeof(RC95LDR_RC95_BOOT_DRIVER_ENTRY)*drivers,&dr)||
       !Add(&total,strings,&at))return Rc95LdrIntegerOverflow;
    {const char*ss[4]={i->BootDevice,i->HalDevice,i->SystemRoot,i->LoadOptions};for(j=0;j<4;j++)if(!Add(&total,AsciiU16Size(ss[j])*2,&at))return Rc95LdrIntegerOverflow;}
    if(total>cap||total>UINT32_MAX)return total>cap?Rc95LdrBufferTooSmall:Rc95LdrIntegerOverflow;
    memset(b,0,total);h->Signature=0x4c393552u;h->Size=(uint32_t)total;h->AbiVersion=1;
    h->OsMajorVersion=i->OsMajorVersion;h->OsMinorVersion=i->OsMinorVersion;h->OsBuildNumber=i->OsBuildNumber;
    h->Architecture=i->Architecture;h->MachineType=i->MachineType;h->FirmwareIsUefi=i->FirmwareIsUefi;
    h->FirmwareVersion=i->FirmwareVersion;h->FirmwareSystemTable=(RC95LDR_RC95_PTR)i->FirmwareSystemTable;h->FirmwareServices=(RC95LDR_RC95_PTR)i->FirmwareServices;
    h->FirmwareMap=(RC95LDR_RC95_PTR)i->FirmwareMap;h->FirmwareMapSize=i->FirmwareMapSize;
    h->FirmwareMapDescriptorSize=i->FirmwareMapDescriptorSize;
    h->ConfigurationRoot=i->ConfigurationRoot;h->NlsData=i->NlsData;h->ArcDiskInformation=i->ArcDiskInformation;
    h->RegistryLength=i->RegistryLength;h->SystemHive=(RC95LDR_RC95_PTR)(uintptr_t)i->SystemHive;
    h->ModuleCount=i->ModuleCount;h->MemoryRangeCount=i->MemoryRangeCount;h->BootDriverCount=(uint32_t)drivers;
    for(j=0;j<i->ModuleCount;j++){
        if(i->Modules[j].Flags&RC95LDR_MODULE_KERNEL){h->KernelBase=(RC95LDR_RC95_PTR)i->Modules[j].ImageBase;h->KernelEntry=(RC95LDR_RC95_PTR)i->Modules[j].EntryPoint;}
        if(i->Modules[j].Flags&RC95LDR_MODULE_HAL)h->HalBase=(RC95LDR_RC95_PTR)i->Modules[j].ImageBase;
    }
    InitRc95List(&h->LoadOrderListHead);InitRc95List(&h->MemoryDescriptorListHead);InitRc95List(&h->BootDriverListHead);
    at=total-(AsciiU16Size(i->BootDevice)+AsciiU16Size(i->HalDevice)+AsciiU16Size(i->SystemRoot)+AsciiU16Size(i->LoadOptions))*2;
    for(j=0;j<i->ModuleCount;j++){
        RC95LDR_RC95_MODULE_ENTRY*e=(void*)(b+mo+j*sizeof(*e));
        RC95LDR_RC95_PTR full=PutU16(b,&at,i->Modules[j].Path),base=PutU16(b,&at,BaseName(i->Modules[j].Path));
        e->ImageBase=(RC95LDR_RC95_PTR)i->Modules[j].ImageBase;e->EntryPoint=(RC95LDR_RC95_PTR)i->Modules[j].EntryPoint;
        e->ImageSize=(uint32_t)i->Modules[j].ImageSize;e->Attributes=i->Modules[j].Flags;
        SetRc95Unicode(&e->FullImagePath,full,i->Modules[j].Path);SetRc95Unicode(&e->BaseImageName,base,BaseName(i->Modules[j].Path));
        InsertTailRc95(&h->LoadOrderListHead,&e->LoadOrderLinks);
        if(i->Modules[j].Flags&RC95LDR_MODULE_BOOT_DRIVER){
            RC95LDR_RC95_BOOT_DRIVER_ENTRY*d=(void*)(b+dr);const char*rp=i->Modules[j].RegistryPath?i->Modules[j].RegistryPath:"";
            RC95LDR_RC95_PTR fp=PutU16(b,&at,i->Modules[j].Path),rpbuf=PutU16(b,&at,rp);
            SetRc95Unicode(&d->FilePath,fp,i->Modules[j].Path);SetRc95Unicode(&d->RegistryPath,rpbuf,rp);
            d->ModuleEntry=(RC95LDR_RC95_PTR)(uintptr_t)e;InsertTailRc95(&h->BootDriverListHead,&d->Link);dr+=sizeof(*d);
        }
    }
    for(j=0;j<i->MemoryRangeCount;j++){
        RC95LDR_RC95_MEMORY_DESCRIPTOR*d=(void*)(b+me+j*sizeof(*d));
        d->MemoryType=i->MemoryRanges[j].Type;d->BasePage=(RC95LDR_RC95_PTR)i->MemoryRanges[j].BasePage;
        d->PageCount=(RC95LDR_RC95_PTR)i->MemoryRanges[j].PageCount;InsertTailRc95(&h->MemoryDescriptorListHead,&d->ListEntry);
    }
    h->BootDevice=PutU16(b,&at,i->BootDevice);h->HalDevice=PutU16(b,&at,i->HalDevice);
    h->SystemRoot=PutU16(b,&at,i->SystemRoot);h->LoadOptions=PutU16(b,&at,i->LoadOptions);
    *used=total;return Rc95LdrSuccess;
}
RC95LDR_STATUS Rc95LdrBuildNtLegacyLoaderBlock(const RC95LDR_NATIVE_LOADER_INPUT*i,void*b,size_t c,size_t*u){if(!i||(i->NtVersion!=Rc95LdrNtVersion4&&i->NtVersion!=Rc95LdrNtVersion5))return Rc95LdrUnsupported;return BuildNt(i,b,c,u);}
RC95LDR_STATUS Rc95LdrBuildReactOsLoaderBlock(const RC95LDR_NATIVE_LOADER_INPUT*i,void*b,size_t c,size_t*u){return BuildNt(i,b,c,u);}
RC95LDR_STATUS Rc95LdrBuildWindowsModernLoaderBlock(const RC95LDR_NATIVE_LOADER_INPUT*i,void*b,size_t c,size_t*u){if(!i||i->NtVersion<Rc95LdrNtVersion6)return Rc95LdrUnsupported;return BuildNt(i,b,c,u);}
RC95LDR_STATUS Rc95LdrBuildRc95NativeLoaderBlock(const RC95LDR_RC95_LOADER_INPUT*i,void*b,size_t c,size_t*u){return BuildRc95(i,b,c,u);}
