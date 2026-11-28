#include "rc95ldrloaderblock.h"
#include "rc95ldrnativeprepare.h"
#include "rc95ldrsystemhive.h"
#include "rc95ldrloaderstate.h"
#include "rc95ldrarchprepare.h"
#include "rc95ldrmetadata.h"
#include "rc95ldrplatforminventory.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef union _ALIGNED_BUFFER { RC95LDR_NT_LOADER_BLOCK_V10 Align; uint8_t Bytes[8192]; } ALIGNED_BUFFER;
static void TestNativeLayouts(void)
{
 RC95LDR_LOADED_MODULE m[2]={
  {.ImageBase=0x100000,.ImageSize=0x20000,.EntryPoint=0x101000,
   .PhysicalBase=0x100000,.PhysicalSize=0x20000,.Flags=RC95LDR_MODULE_KERNEL,
   .Path="\\SystemRoot\\system32\\ntoskrnl.exe",.RegistryPath=0},
  {.ImageBase=0x200000,.ImageSize=0x10000,.EntryPoint=0x201000,
   .PhysicalBase=0x200000,.PhysicalSize=0x10000,.Flags=RC95LDR_MODULE_HAL,
   .Path="\\SystemRoot\\system32\\hal.dll",.RegistryPath=0}
 };
 RC95LDR_MEMORY_RANGE mr={0x100,0x200,1,0};RC95LDR_NATIVE_LOADER_INPUT in;ALIGNED_BUFFER b;size_t used=0;uint8_t hive[32]={0};
 memset(&in,0,sizeof(in));in.StructureSize=sizeof(in);in.Version=RC95LDR_NATIVE_LOADER_INPUT_VERSION;in.RegistryBase=hive;in.RegistryLength=sizeof(hive);
 in.Modules=m;in.ModuleCount=2;in.MemoryRanges=&mr;in.MemoryRangeCount=1;
 in.ArcBootDevice="multi(0)disk(0)rdisk(0)partition(1)";in.ArcHalDevice="multi(0)disk(0)rdisk(0)partition(1)";
 in.NtBootPath="\\WINDOWS";in.NtHalPath="\\";in.LoadOptions="/DEBUG";
#if defined(_ARM_)
 in.Architecture=Rc95LdrNtArchArm;in.MachineType=0x1c0;
#elif UINTPTR_MAX > UINT32_MAX
 in.Architecture=Rc95LdrNtArchAmd64;in.MachineType=0x8664;
#else
 in.Architecture=Rc95LdrNtArchI386;in.MachineType=0x14c;
#endif
 in.OsMajorVersion=4;in.NtVersion=Rc95LdrNtVersion4;
 assert(Rc95LdrBuildNtLegacyLoaderBlock(&in,b.Bytes,sizeof(b.Bytes),&used)==Rc95LdrSuccess);
 assert(used>sizeof(RC95LDR_NT_LOADER_BLOCK_V4));
 assert(((RC95LDR_NT_LOADER_BLOCK_V4*)b.Bytes)->LoadOrderListHead.Flink!=0);
 assert(((RC95LDR_NT_LOADER_BLOCK_V4*)b.Bytes)->MemoryDescriptorListHead.Flink!=0);
 in.OsMajorVersion=5;in.OsMinorVersion=2;in.NtVersion=Rc95LdrNtVersion5;
 assert(Rc95LdrBuildNtLegacyLoaderBlock(&in,b.Bytes,sizeof(b.Bytes),&used)==Rc95LdrSuccess);
 assert(((RC95LDR_NT_LOADER_BLOCK_V5*)b.Bytes)->LoadOrderListHead.Flink!=0);
 in.OsMajorVersion=6;in.OsMinorVersion=0;in.NtVersion=Rc95LdrNtVersion6;
 assert(Rc95LdrBuildWindowsModernLoaderBlock(&in,b.Bytes,sizeof(b.Bytes),&used)==Rc95LdrSuccess);
 in.OsMajorVersion=6;in.OsMinorVersion=1;in.NtVersion=Rc95LdrNtVersion61;
 assert(Rc95LdrBuildReactOsLoaderBlock(&in,b.Bytes,sizeof(b.Bytes),&used)==Rc95LdrSuccess);
 assert(((RC95LDR_NT_LOADER_BLOCK_V61*)b.Bytes)->OsMajorVersion==6);
 in.FirmwareIsUefi=1;in.FirmwareSystemTable=(uint64_t)(uintptr_t)hive;in.VirtualEfiRuntimeServices=(uint64_t)(uintptr_t)hive;
 in.FirmwareMapAddress=(uint64_t)(uintptr_t)hive;in.FirmwareMapSize=240;in.FirmwareMapDescriptorSize=48;
 in.OsMajorVersion=6;in.OsMinorVersion=2;in.NtVersion=Rc95LdrNtVersion62;
 assert(Rc95LdrBuildWindowsModernLoaderBlock(&in,b.Bytes,sizeof(b.Bytes),&used)==Rc95LdrSuccess);
 assert(((RC95LDR_NT_LOADER_BLOCK_V62*)b.Bytes)->FirmwareInformation.u.EfiInformation.FirmwareResourceList.Flink==
        (uintptr_t)&((RC95LDR_NT_LOADER_BLOCK_V62*)b.Bytes)->FirmwareInformation.u.EfiInformation.FirmwareResourceList);
 in.OsMajorVersion=6;in.OsMinorVersion=3;in.NtVersion=Rc95LdrNtVersion63;
 assert(Rc95LdrBuildWindowsModernLoaderBlock(&in,b.Bytes,sizeof(b.Bytes),&used)==Rc95LdrSuccess);
 assert(((RC95LDR_NT_LOADER_BLOCK_V63*)b.Bytes)->FirmwareInformation.u.EfiInformation.EfiMemoryMap==(uintptr_t)hive);
 assert(((RC95LDR_NT_LOADER_BLOCK_V63*)b.Bytes)->FirmwareInformation.u.EfiInformation.EfiMemoryMapDescriptorSize==48);
 in.OsMajorVersion=10;in.OsMinorVersion=0;in.OsBuildNumber=10240;in.NtVersion=Rc95LdrNtVersion10;
 assert(Rc95LdrBuildWindowsModernLoaderBlock(&in,b.Bytes,sizeof(b.Bytes),&used)==Rc95LdrSuccess);
 assert(((RC95LDR_NT_LOADER_BLOCK_V10*)b.Bytes)->OsMajorVersion==10);
 assert(((RC95LDR_NT_LOADER_BLOCK_V10*)b.Bytes)->CoreExtensionsDriverListHead.Flink==
        (uintptr_t)&((RC95LDR_NT_LOADER_BLOCK_V10*)b.Bytes)->CoreExtensionsDriverListHead);
 assert(offsetof(RC95LDR_NT_FIRMWARE_INFORMATION,u)%sizeof(void*)==0);
 assert(Rc95LdrBuildRc95NativeLoaderBlock(&in,b.Bytes,sizeof(b.Bytes),&used)==Rc95LdrSuccess);
 assert(((RC95LDR_RC95_NATIVE_LOADER_BLOCK*)b.Bytes)->Signature==0x4c393552u);
 assert(((RC95LDR_RC95_NATIVE_LOADER_BLOCK*)b.Bytes)->FirmwareSystemTable==(uintptr_t)hive);
 assert(((RC95LDR_RC95_NATIVE_LOADER_BLOCK*)b.Bytes)->LoadOrderListHead.Flink==
        (uintptr_t)&((RC95LDR_RC95_NATIVE_LOADER_BLOCK*)b.Bytes)[1]);
 assert(Rc95LdrBuildWindowsModernLoaderBlock(&in,b.Bytes,8,&used)==Rc95LdrBufferTooSmall);
}
static void TestRc95FirmwareValidation(void)
{
    RC95LDR_LOADED_MODULE modules[2]={
        {.ImageBase=0x100000,.ImageSize=0x20000,.EntryPoint=0x101000,
         .Flags=RC95LDR_MODULE_KERNEL,.Path="\\SystemRoot\\system32\\rc95krnl.exe"},
        {.ImageBase=0x200000,.ImageSize=0x10000,.EntryPoint=0x201000,
         .Flags=RC95LDR_MODULE_HAL,.Path="\\SystemRoot\\system32\\hal.dll"}
    };
    RC95LDR_RC95_LOADER_INPUT in;
    uint8_t hive[32]={0},map[256]={0},buffer[8192];
    size_t used=0;
    memset(&in,0,sizeof(in));
    in.StructureSize=sizeof(in);in.Version=RC95LDR_RC95_LOADER_INPUT_VERSION;
    in.Architecture=sizeof(void*)==8?Rc95LdrNtArchAmd64:Rc95LdrNtArchI386;
    in.SystemHive=hive;in.RegistryLength=sizeof(hive);
    in.Modules=modules;in.ModuleCount=2;
    assert(Rc95LdrBuildRc95NativeLoaderBlock(&in,buffer,sizeof(buffer),&used)==Rc95LdrSuccess);
    in.FirmwareIsUefi=1;
    in.FirmwareSystemTable=(uint64_t)(uintptr_t)hive;
    in.FirmwareServices=(uint64_t)(uintptr_t)hive;
    in.FirmwareMap=(uint64_t)(uintptr_t)map;
    in.FirmwareMapSize=240;
    in.FirmwareMapDescriptorSize=48;
    assert(Rc95LdrBuildRc95NativeLoaderBlock(&in,buffer,sizeof(buffer),&used)==Rc95LdrSuccess);
    in.FirmwareMapDescriptorSize=0;
    assert(Rc95LdrBuildRc95NativeLoaderBlock(&in,buffer,sizeof(buffer),&used)==Rc95LdrInvalidParameter);
    in.FirmwareMapDescriptorSize=48;
    in.FirmwareMapSize=239;
    assert(Rc95LdrBuildRc95NativeLoaderBlock(&in,buffer,sizeof(buffer),&used)==Rc95LdrInvalidParameter);
    in.FirmwareMapSize=240;
    in.FirmwareServices=0;
    assert(Rc95LdrBuildRc95NativeLoaderBlock(&in,buffer,sizeof(buffer),&used)==Rc95LdrInvalidParameter);
    in.FirmwareServices=(uint64_t)(uintptr_t)hive;
    in.FirmwareIsUefi=0;
    assert(Rc95LdrBuildRc95NativeLoaderBlock(&in,buffer,sizeof(buffer),&used)==Rc95LdrInvalidParameter);
    in.FirmwareSystemTable=0;in.FirmwareServices=0;in.FirmwareMap=0;
    in.FirmwareMapSize=0;in.FirmwareMapDescriptorSize=0;
    assert(Rc95LdrBuildRc95NativeLoaderBlock(&in,buffer,sizeof(buffer),&used)==Rc95LdrSuccess);
}

static void Put16(uint8_t*p,uint16_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void Put32(uint8_t*p,uint32_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);}
static uint32_t HeaderXor(const uint8_t*p){uint32_t x=0;size_t i;for(i=0;i<0x1fc;i+=4)x^=(uint32_t)p[i]|((uint32_t)p[i+1]<<8)|((uint32_t)p[i+2]<<16)|((uint32_t)p[i+3]<<24);return x==0xffffffffu?0xfffffffeu:(x?x:1u);}
static unsigned HiveEnumerated;
static RC95LDR_STATUS CountHiveSubkey(void *context,const RC95LDR_HIVE_KEY *key)
{
    (void)context;
    assert(key!=NULL);
    ++HiveEnumerated;
    return Rc95LdrSuccess;
}
static void TestDependencyMultiSzValidation(void)
{
    const uint8_t empty[]={0,0,0,0},one[]={'A',0,0,0,0,0};
    const uint8_t two[]={'A',0,'B',0,0,0,0,0};
    const uint8_t embeddedEmpty[]={'A',0,0,0,'B',0,0,0};
    const uint8_t slash[]={'A',0,'/',0,0,0},backslash[]={'A',0,'\\',0,0,0};
    const uint8_t colon[]={'A',0,':',0,0,0},dot[]={'.',0,0,0};
    const uint8_t dotdot[]={'.',0,'.',0,0,0},nonAscii[]={0xe9,0,0,0,0,0};
    const uint8_t missingFinal[]={'A',0,0,0,'B',0};
    const uint8_t nonAsciiBadTail[]={0xe9,0,0,0,'B',0,0,0,'C',0};
    uint8_t overlong[2u*(RC95LDR_HIVE_MAX_NAME_CHARS+1u)+4u];
    size_t overlongIndex;
    for(overlongIndex=0;overlongIndex<RC95LDR_HIVE_MAX_NAME_CHARS+1u;overlongIndex++){
        overlong[overlongIndex*2u]='A';
        overlong[overlongIndex*2u+1u]=0;
    }
    overlong[(RC95LDR_HIVE_MAX_NAME_CHARS+1u)*2u]=0;
    overlong[(RC95LDR_HIVE_MAX_NAME_CHARS+1u)*2u+1u]=0;
    overlong[(RC95LDR_HIVE_MAX_NAME_CHARS+1u)*2u+2u]=0;
    overlong[(RC95LDR_HIVE_MAX_NAME_CHARS+1u)*2u+3u]=0;
    assert(Rc95LdrHiveValidateDependencyMultiSz(empty,sizeof(empty))==Rc95LdrSuccess);
    assert(Rc95LdrHiveValidateDependencyMultiSz(one,sizeof(one))==Rc95LdrUnsupported);
    assert(Rc95LdrHiveValidateDependencyMultiSz(two,sizeof(two))==Rc95LdrUnsupported);
    assert(Rc95LdrHiveValidateDependencyMultiSz(embeddedEmpty,sizeof(embeddedEmpty))==Rc95LdrCorruptData);
    assert(Rc95LdrHiveValidateDependencyMultiSz(slash,sizeof(slash))==Rc95LdrCorruptData);
    assert(Rc95LdrHiveValidateDependencyMultiSz(backslash,sizeof(backslash))==Rc95LdrCorruptData);
    assert(Rc95LdrHiveValidateDependencyMultiSz(colon,sizeof(colon))==Rc95LdrCorruptData);
    assert(Rc95LdrHiveValidateDependencyMultiSz(dot,sizeof(dot))==Rc95LdrCorruptData);
    assert(Rc95LdrHiveValidateDependencyMultiSz(dotdot,sizeof(dotdot))==Rc95LdrCorruptData);
    assert(Rc95LdrHiveValidateDependencyMultiSz(nonAscii,sizeof(nonAscii))==Rc95LdrUnsupported);
    /* Unsupported names must not mask malformed trailing list structure. */
    assert(Rc95LdrHiveValidateDependencyMultiSz(nonAsciiBadTail,sizeof(nonAsciiBadTail))==Rc95LdrCorruptData);
    assert(Rc95LdrHiveValidateDependencyMultiSz(missingFinal,sizeof(missingFinal))==Rc95LdrCorruptData);
    assert(Rc95LdrHiveValidateDependencyMultiSz(overlong,sizeof(overlong))==Rc95LdrCorruptData);
    assert(Rc95LdrHiveValidateDependencyMultiSz(empty,2)==Rc95LdrCorruptData);
    assert(Rc95LdrHiveValidateDependencyMultiSz(NULL,sizeof(empty))==Rc95LdrInvalidParameter);
}
static void TestHiveRootBounds(void)
{
 uint8_t h[0x2000];RC95LDR_SYSTEM_HIVE hive;RC95LDR_HIVE_KEY key;memset(h,0,sizeof(h));
 Put32(h,0x66676572);Put32(h+4,1);Put32(h+8,1);Put32(h+0x14,1);
 Put32(h+0x18,5);Put32(h+0x1c,0);Put32(h+0x20,1);Put32(h+0x24,0x20);Put32(h+0x28,0x1000);
 memcpy(h+0x1000,"hbin",4);Put32(h+0x1008,0x1000);
 /* Root NK at 0x20, with Select and ControlSet001 children. */
 Put32(h+0x1020,0xffffff80u);Put16(h+0x1024,0x6b6e);Put16(h+0x1026,0x20);
 Put32(h+0x1038,2);Put32(h+0x1040,0xa0);Put16(h+0x106c,0);
 /* LI child list. */
 Put32(h+0x10a0,0xfffffff0u);Put16(h+0x10a4,0x696c);Put16(h+0x10a6,2);
 Put32(h+0x10a8,0xb0);Put32(h+0x10ac,0x138);
 /* Select NK with one inline DWORD value named Current = 1. */
 Put32(h+0x10b0,0xffffffa8u);Put16(h+0x10b4,0x6b6e);Put16(h+0x10b6,0x20);
 Put32(h+0x10d8,1);Put32(h+0x10dc,0x108);Put16(h+0x10fc,6);memcpy(h+0x1100,"Select",6);
 Put32(h+0x1108,0xfffffff0u);Put32(h+0x110c,0x118);
 Put32(h+0x1118,0xffffffe0u);Put16(h+0x111c,0x6b76);Put16(h+0x111e,7);
 Put32(h+0x1120,0x80000004u);Put32(h+0x1124,1);Put32(h+0x1128,4);Put16(h+0x112c,1);
 memcpy(h+0x1130,"Current",7);
 /* The resolved active control set. */
 Put32(h+0x1138,0xffffffa0u);Put16(h+0x113c,0x6b6e);Put16(h+0x113e,0x20);
 Put16(h+0x1184,13);memcpy(h+0x1188,"ControlSet001",13);
 Put32(h+0x1fc,HeaderXor(h));
 assert(Rc95LdrOpenSystemHive(h,sizeof(h),&hive)==Rc95LdrSuccess);
 assert(hive.RootCell==0x20);
 /* Cell allocator metadata is untrusted input. Reject free, undersized,
    unaligned and overflowing cell extents before exposing an NK record. */
 Put32(h+0x1020,0x80u);
 assert(Rc95LdrOpenSystemHive(h,sizeof(h),&hive)==Rc95LdrCorruptData);
 Put32(h+0x1020,0x80000000u);
 assert(Rc95LdrOpenSystemHive(h,sizeof(h),&hive)==Rc95LdrCorruptData);
 Put32(h+0x1020,0xfffffff9u);
 assert(Rc95LdrOpenSystemHive(h,sizeof(h),&hive)==Rc95LdrCorruptData);
 Put32(h+0x1020,0xfffff000u);
 assert(Rc95LdrOpenSystemHive(h,sizeof(h),&hive)==Rc95LdrCorruptData);
 Put32(h+0x1020,0xffffff80u);
 assert(Rc95LdrOpenSystemHive(h,sizeof(h),&hive)==Rc95LdrSuccess);
 /* Corrupt hbin geometry must be rejected even if the root cell itself
    remains within the supplied byte buffer. */
 Put32(h+0x1008,0x2000);
 assert(Rc95LdrOpenSystemHive(h,sizeof(h),&hive)==Rc95LdrCorruptData);
 Put32(h+0x1008,0x1000);
 /* A key may have zero stable subkeys and still have volatile children.
    Exercise lookup through the volatile index independently. */
 Put32(h+0x1038,0);       /* Root SubkeyCount */
 Put32(h+0x103c,2);       /* Root VolatileSubkeyCount */
 Put32(h+0x1044,0xa0);    /* Root VolatileSubkeyList */
 assert(Rc95LdrHiveOpenPath(&hive,"Select",&key)==Rc95LdrSuccess);
 /* The NK-declared count must match the complete index tree. A truncated
    leaf and an empty RI node are malformed even if a requested child might
    otherwise be found in an earlier entry. */
 Put16(h+0x10a6,1);
 assert(Rc95LdrHiveOpenPath(&hive,"Select",&key)==Rc95LdrCorruptData);
 Put16(h+0x10a6,2);
 Put16(h+0x10a4,0x6972);Put16(h+0x10a6,0);
 assert(Rc95LdrHiveOpenPath(&hive,"Select",&key)==Rc95LdrCorruptData);
 Put16(h+0x10a4,0x696c);Put16(h+0x10a6,2);
 /* Duplicate a child name in the same index. A lookup must not silently
    choose whichever duplicate happens to be visited first. */
 Put32(h+0x10ac,0xb0);
 assert(Rc95LdrHiveOpenPath(&hive,"Select",&key)==Rc95LdrCorruptData);
 Put32(h+0x10ac,0x138);
 /* Every leaf reference must resolve to an aligned, bounded allocated NK cell. */
 Put32(h+0x10ac,0xffcu);
 assert(Rc95LdrHiveOpenPath(&hive,"Select",&key)==Rc95LdrCorruptData);
 Put32(h+0x10ac,0xb1u);
 assert(Rc95LdrHiveOpenPath(&hive,"Select",&key)==Rc95LdrCorruptData);
 Put32(h+0x10ac,0x138);
 /* Unknown index signatures and entry counts extending past the cell are
    rejected before a partial enumeration can escape. */
 Put16(h+0x10a4,0x1234);
 assert(Rc95LdrHiveOpenPath(&hive,"Select",&key)==Rc95LdrCorruptData);
 Put16(h+0x10a4,0x696c);Put16(h+0x10a6,0xffff);
 assert(Rc95LdrHiveOpenPath(&hive,"Select",&key)==Rc95LdrCorruptData);
 Put16(h+0x10a6,2);
 /* A recursive RI tree may partition one logical subkey list across
    multiple LI leaves. Verify lookup through a nested branch and a sibling. */
 Put32(h+0x10a0,0xffffffe8u);Put16(h+0x10a4,0x6972);Put16(h+0x10a6,2);
 Put32(h+0x10a8,0x200);Put32(h+0x10ac,0x240);
 Put32(h+0x1200,0xfffffff0u);Put16(h+0x1204,0x6972);Put16(h+0x1206,1);
 Put32(h+0x1208,0x220);
 Put32(h+0x1220,0xfffffff0u);Put16(h+0x1224,0x696c);Put16(h+0x1226,1);
 Put32(h+0x1228,0xb0);
 Put32(h+0x1240,0xfffffff0u);Put16(h+0x1244,0x696c);Put16(h+0x1246,1);
 Put32(h+0x1248,0x138);
 assert(Rc95LdrHiveOpenPath(&hive,"Select",&key)==Rc95LdrSuccess);
 assert(Rc95LdrHiveOpenPath(&hive,"ControlSet001",&key)==Rc95LdrSuccess);
 HiveEnumerated=0;
 assert(Rc95LdrHiveEnumerateSubkeys(&hive,&(RC95LDR_HIVE_KEY){.Cell=0x20},CountHiveSubkey,0)==Rc95LdrSuccess);
 assert(HiveEnumerated==2);
 /* A repeated name split across different leaf cells is still ambiguous. */
 Put32(h+0x1248,0xb0);
 assert(Rc95LdrHiveOpenPath(&hive,"Select",&key)==Rc95LdrCorruptData);
 HiveEnumerated=0;
 assert(Rc95LdrHiveEnumerateSubkeys(&hive,&(RC95LDR_HIVE_KEY){.Cell=0x20},CountHiveSubkey,0)==Rc95LdrCorruptData);
 assert(HiveEnumerated==0);
 Put32(h+0x1248,0x138);
 /* Reusing the same leaf index cell in two RI entries is corrupt even when
    the NK count could otherwise be made to match by repeating its entries. */
 Put32(h+0x10a0,0xffffffe8u);Put16(h+0x10a4,0x6972);Put16(h+0x10a6,2);
 Put32(h+0x10a8,0x180);Put32(h+0x10ac,0x180);
 Put32(h+0x1180,0xfffffff0u);Put16(h+0x1184,0x696c);Put16(h+0x1186,1);
 Put32(h+0x1188,0xb0);
 Put32(h+0x1038,2);Put32(h+0x103c,0);
 HiveEnumerated=0;
 assert(Rc95LdrHiveOpenPath(&hive,"Select",&key)==Rc95LdrCorruptData);
 assert(Rc95LdrHiveEnumerateSubkeys(&hive,&(RC95LDR_HIVE_KEY){.Cell=0x20},CountHiveSubkey,0)==Rc95LdrCorruptData);
 assert(HiveEnumerated==0);
 Put32(h+0x1038,0);Put32(h+0x103c,2);
 /* Stable and volatile lists are distinct trees, but their key identities
    share one namespace. A duplicate name across the two trees is ambiguous. */
 Put32(h+0x11e0,0xfffffff0u);Put16(h+0x11e4,0x696c);Put16(h+0x11e6,1);
 Put32(h+0x11e8,0xb0);
 Put32(h+0x1200,0xfffffff0u);Put16(h+0x1204,0x696c);Put16(h+0x1206,1);
 Put32(h+0x1208,0xb0);
 Put32(h+0x1038,1);Put32(h+0x1040,0x1e0);
 Put32(h+0x103c,1);Put32(h+0x1044,0x200);
 assert(Rc95LdrHiveOpenPath(&hive,"Select",&key)==Rc95LdrCorruptData);
 HiveEnumerated=0;
 assert(Rc95LdrHiveEnumerateSubkeys(&hive,&(RC95LDR_HIVE_KEY){.Cell=0x20},CountHiveSubkey,0)==Rc95LdrCorruptData);
 assert(HiveEnumerated==0);
 Put32(h+0x1038,0);Put32(h+0x1040,0);
 Put32(h+0x103c,2);Put32(h+0x1044,0xa0);
 /* RI cycles and nesting beyond the parser's fixed recursion bound fail closed. */
 Put32(h+0x10a8,0xa0);
 assert(Rc95LdrHiveOpenPath(&hive,"Select",&key)==Rc95LdrCorruptData);
 Put32(h+0x10a8,0x180);
 {
  uint32_t node;
  for(node=0;node<9;node++){
   uint32_t off=node==0?0xa0:0x180+(node-1)*0x20;
   uint32_t next=0x180+node*0x20;
   Put32(h+0x1000+off,0xfffffff0u);
   Put16(h+0x1004+off,0x6972);Put16(h+0x1006+off,1);
   Put32(h+0x1008+off,next);
  }
  Put32(h+0x1000+0x180+9*0x20,0xfffffff0u);
  Put16(h+0x1004+0x180+9*0x20,0x696c);
  Put16(h+0x1006+0x180+9*0x20,1);
  Put32(h+0x1008+0x180+9*0x20,0xb0);
  assert(Rc95LdrHiveOpenPath(&hive,"Select",&key)==Rc95LdrCorruptData);
 }
 /* Restore the original single-level LI fixture for value/control-set tests. */
 Put32(h+0x1020,0xffffff80u);
 Put32(h+0x1038,0);Put32(h+0x103c,2);Put32(h+0x1044,0xa0);
 Put32(h+0x10a0,0xfffffff0u);Put16(h+0x10a4,0x696c);Put16(h+0x10a6,2);
 Put32(h+0x10a8,0xb0);Put32(h+0x10ac,0x138);
 /* The deep RI fixture intentionally occupied bytes inside the large
    ControlSet001 NK cell. Restore its key identity before validating the
    complete parent index again. */
 Put32(h+0x1138,0xffffffa0u);Put16(h+0x113c,0x6b6e);Put16(h+0x113e,0x20);
 Put16(h+0x1184,13);memcpy(h+0x1188,"ControlSet001",13);
 {uint32_t current=0;RC95LDR_HIVE_KEY control;
  /* Nested-tree checks reused key for ControlSet001; resolve Select again. */
  assert(Rc95LdrHiveOpenPath(&hive,"Select",&key)==Rc95LdrSuccess);
  assert(Rc95LdrHiveGetDword(&hive,&key,"Current",&current)==Rc95LdrSuccess);
  assert(current==1);
  /* Duplicate VK names are ambiguous even when both list entries reference
     the same cell. The reader must scan the complete value list. */
  Put32(h+0x10d8,2);Put32(h+0x110c,0x118);
  {RC95LDR_HIVE_VALUE duplicate;
   assert(Rc95LdrHiveGetValue(&hive,&key,"Current",&duplicate)==Rc95LdrCorruptData);}
  Put32(h+0x10d8,1);
  /* A VK reference must be an aligned cell offset; do not accept a pointer
     into the middle of an otherwise valid cell. */
  Put32(h+0x110c,0x119u);
  {RC95LDR_HIVE_VALUE malformed;
   assert(Rc95LdrHiveGetValue(&hive,&key,"Current",&malformed)==Rc95LdrCorruptData);}
  Put32(h+0x110c,0x118u);
  /* Exercise the public value APIs directly: inline data must be decoded
     from the VK data field, and malformed descriptors must fail closed. */
  {
   RC95LDR_HIVE_VALUE value,forged;
   uint32_t inlineValue=0;size_t bytesRead=0;
   assert(Rc95LdrHiveGetValue(&hive,&key,"Current",&value)==Rc95LdrSuccess);
   assert(Rc95LdrHiveReadValue(&hive,&value,&inlineValue,sizeof(inlineValue),&bytesRead)==Rc95LdrSuccess);
   assert(bytesRead==sizeof(inlineValue)&&inlineValue==1);
   /* Only VK_COMPRESSED is defined in the value-name flags field. */
   Put16(h+0x112c,0x8001u);
   assert(Rc95LdrHiveGetValue(&hive,&key,"Current",&forged)==Rc95LdrCorruptData);
   Put16(h+0x112c,1u);
   forged=value;forged.Cell=0xfffffffcu;
   assert(Rc95LdrHiveReadValue(&hive,&forged,&inlineValue,sizeof(inlineValue),&bytesRead)==Rc95LdrCorruptData);
   forged=value;forged.Type=1;bytesRead=99;
   assert(Rc95LdrHiveReadValue(&hive,&forged,&inlineValue,sizeof(inlineValue),&bytesRead)==Rc95LdrCorruptData);
   assert(bytesRead==0);
   /* A too-small destination must not report the requested value length as
      bytes actually read; callers may use BytesRead as an initialized span. */
   bytesRead=99;
   assert(Rc95LdrHiveReadValue(&hive,&value,&inlineValue,sizeof(inlineValue)-1,&bytesRead)==Rc95LdrBufferTooSmall);
   assert(bytesRead==0);
   bytesRead=99;
   assert(Rc95LdrHiveReadValue(&hive,&value,NULL,sizeof(inlineValue),&bytesRead)==Rc95LdrInvalidParameter);
   assert(bytesRead==0);
   forged=value;forged.DataCell^=4u;
   assert(Rc95LdrHiveReadValue(&hive,&forged,&inlineValue,sizeof(inlineValue),&bytesRead)==Rc95LdrCorruptData);
   forged=value;strcpy(forged.Name,"Forged");
   assert(Rc95LdrHiveReadValue(&hive,&forged,&inlineValue,sizeof(inlineValue),&bytesRead)==Rc95LdrCorruptData);
   forged=value;forged.NameLength++;
   assert(Rc95LdrHiveReadValue(&hive,&forged,&inlineValue,sizeof(inlineValue),&bytesRead)==Rc95LdrCorruptData);
   /* A caller-modified key descriptor cannot redirect the value-list walk. */
   {RC95LDR_HIVE_KEY badKey=key;badKey.ValueCount=0xffffffffu;badKey.ValueList=0xfffffffcu;
    assert(Rc95LdrHiveGetValue(&hive,&badKey,"Current",&value)==Rc95LdrSuccess);}
   /* Inline values cannot claim more than the four bytes in the VK record. */
   Put32(h+0x1120,0x80000005u);
   assert(Rc95LdrHiveGetValue(&hive,&key,"Current",&value)==Rc95LdrCorruptData);
   assert(Rc95LdrHiveGetValue(&hive,&key,"Absent",&value)==Rc95LdrCorruptData);
   Put32(h+0x1120,0x80000004u);
   /* Even an unrequested VK must not carry an out-of-range external data
      cell. Full-list validation prevents malformed trailing values hiding. */
   Put32(h+0x1120,1u);Put32(h+0x1128,0xffcu);
   assert(Rc95LdrHiveGetValue(&hive,&key,"Absent",&value)==Rc95LdrCorruptData);
   Put32(h+0x1120,0x80000004u);Put32(h+0x1128,4u);
  }
  assert(Rc95LdrHiveResolveCurrentControlSet(&hive,&control)==Rc95LdrSuccess);
  assert(strcmp(control.Name,"ControlSet001")==0);
 }
}

static unsigned RosLoadCalls;
static RC95LDR_STATUS LaunchRosLoad(void *context,const RC95LDR_NATIVE_PREPARE_REQUEST *request)
{
 (void)context;assert(request->RosLoadPath!=0);RosLoadCalls++;return Rc95LdrIoError;
}
static void TestReactOsDelegatesToRosLoad(void)
{
 RC95LDR_OS_IDENTITY id;RC95LDR_OS_PROFILE profile;
 RC95LDR_NATIVE_PREPARE_REQUEST request;RC95LDR_NATIVE_PREPARE_RESULT result;
 uint16_t path[]={'r','o','s','l','o','a','d','.','e','x','e',0};
 memset(&id,0,sizeof(id));id.StructureSize=sizeof(id);id.Version=RC95LDR_OS_PROFILE_VERSION;
 id.OsFamily=Rc95LdrOsReactOS;id.Firmware=Rc95LdrFirmwareBios;id.BootEntry=Rc95LdrEntryReactOs;
 assert(Rc95LdrSelectOsProfile(&id,&profile)==Rc95LdrSuccess);
 memset(&request,0,sizeof(request));request.StructureSize=sizeof(request);
 request.Version=RC95LDR_NATIVE_PREPARE_VERSION;request.Profile=&profile;
 request.RosLoadPath=path;request.LaunchRosLoad=LaunchRosLoad;
 memset(&result,0,sizeof(result));result.StructureSize=sizeof(result);
 RosLoadCalls=0;
 assert(Rc95LdrPrepareNativeLoaderBlock(&request,&result)==Rc95LdrIoError);
 assert(RosLoadCalls==1);
}

static void TestHandoffPreflightIsFailClosed(void)
{
 RC95LDR_KERNEL_HANDOFF_CONTEXT h;memset(&h,0,sizeof(h));
 h.Signature=RC95LDR_HANDOFF_SIGNATURE;h.StructureSize=sizeof(h);
 h.Version=RC95LDR_HANDOFF_VERSION;h.OsFamily=Rc95LdrOsWindowsModern;
 h.Firmware=Rc95LdrFirmwareBios;
#if UINTPTR_MAX > UINT32_MAX
 h.Architecture=Rc95LdrNtArchAmd64;
#else
 h.Architecture=Rc95LdrNtArchI386;
#endif
 h.KernelBase=0x100000;h.KernelSize=0x20000;h.KernelEntry=0x101000;
 h.HalBase=0x200000;h.LoaderBlockAddress=0x300000;h.LoaderBlockSize=0x1000;h.MemoryRangeCount=1;
 h.RequiredRequirements=RC95LDR_HANDOFF_REQUIRED;
 h.CompletedRequirements=RC95LDR_HANDOFF_KERNEL_MAPPED|RC95LDR_HANDOFF_HAL_MAPPED|
     RC95LDR_HANDOFF_SYSTEM_HIVE|RC95LDR_HANDOFF_BOOT_DRIVERS|
     RC95LDR_HANDOFF_MEMORY_MAP|RC95LDR_HANDOFF_LOADER_BLOCK|
     RC95LDR_HANDOFF_FIRMWARE_STATE;
 assert(Rc95LdrValidateKernelHandoff(&h)==Rc95LdrUnsupported);
 h.KernelEntry=0x500000;
 assert(Rc95LdrValidateKernelHandoff(&h)==Rc95LdrInvalidImage);
}

static void TestMetadataBuilder(void)
{
    union { uint64_t Align; uint8_t Bytes[4096]; } storage;
    const uint8_t ansi[]={1,2,3},oem[]={4,5},unicode[]={6,7,8,9};
    RC95LDR_ARC_DISK_INPUT disks[2];RC95LDR_METADATA_INPUT in;RC95LDR_METADATA_RESULT out;
    RC95LDR_NT_LIST_ENTRY *head,*first,*second;
    memset(disks,0,sizeof(disks));disks[0].Signature=0x12345678;disks[0].ArcName="multi(0)disk(0)rdisk(0)";
    disks[1].IsGpt=1;disks[1].GptSignature[0]=0xa5;disks[1].ArcName="multi(0)disk(1)rdisk(0)";
    memset(&in,0,sizeof(in));in.StructureSize=sizeof(in);in.Version=RC95LDR_METADATA_VERSION;
    in.Nls.AnsiData=ansi;in.Nls.AnsiSize=sizeof(ansi);in.Nls.OemData=oem;in.Nls.OemSize=sizeof(oem);
    in.Nls.UnicodeData=unicode;in.Nls.UnicodeSize=sizeof(unicode);
    in.Disks=disks;in.DiskCount=2;in.Buffer=storage.Bytes;in.BufferSize=sizeof(storage.Bytes);
    assert(Rc95LdrBuildLoaderMetadata(&in,&out)==Rc95LdrSuccess);
    assert(out.NlsData&&out.ArcDiskInformation&&out.BytesUsed<=sizeof(storage.Bytes));
    {struct {uintptr_t A,O,U;} *n=(void*)(uintptr_t)out.NlsData;
     assert(*(const uint8_t*)(uintptr_t)n->A==1&&*(const uint8_t*)(uintptr_t)n->O==4&&*(const uint8_t*)(uintptr_t)n->U==6);}
    head=(RC95LDR_NT_LIST_ENTRY*)(uintptr_t)out.ArcDiskInformation;
    first=(RC95LDR_NT_LIST_ENTRY*)(uintptr_t)head->Flink;second=(RC95LDR_NT_LIST_ENTRY*)(uintptr_t)first->Flink;
    assert(first!=head&&second!=head&&second->Flink==(uintptr_t)head);
}
static uint8_t g_AcpiRootTable[64],g_AcpiDeviceTable[64];
static const void *MapTestPhysical(void *ctx,uint64_t address,size_t length)
{
    (void)ctx;
    if(address==0x1000&&length<=sizeof(g_AcpiRootTable))return g_AcpiRootTable;
    if(address==0x2000&&length<=sizeof(g_AcpiDeviceTable))return g_AcpiDeviceTable;
    return 0;
}
static RC95LDR_STATUS ReadTestPci(void *ctx,uint16_t segment,uint8_t bus,uint8_t dev,uint8_t fn,uint16_t offset,uint32_t *value)
{
    (void)ctx;(void)segment;
    if(bus==0&&dev==1&&fn==0){
        if(offset==0)*value=0x56781234u;
        else if(offset==8)*value=0x03000000u;
        else if(offset==0x0c)*value=0;
        else *value=0;
        return Rc95LdrSuccess;
    }
    *value=0xffffffffu;return Rc95LdrSuccess;
}
static void TestAutomaticPlatformInventory(void)
{
    typedef struct {char Signature[4];uint32_t Length;uint8_t Revision,Checksum;char OemId[6],OemTableId[8];uint32_t OemRevision,CreatorId,CreatorRevision;} ACPI_TEST_HEADER;
    RC95LDR_PLATFORM_INVENTORY_INPUT in;RC95LDR_PLATFORM_INVENTORY_RESULT out;
    uint32_t length=(uint32_t)sizeof(ACPI_TEST_HEADER)+4;uint8_t sum=0;size_t j;
    memset(g_AcpiRootTable,0,sizeof(g_AcpiRootTable));memset(g_AcpiDeviceTable,0,sizeof(g_AcpiDeviceTable));
    memcpy(g_AcpiRootTable,"RSDT",4);memcpy(g_AcpiRootTable+4,&length,4);g_AcpiRootTable[8]=1;
    {uint32_t address=0x2000;memcpy(g_AcpiRootTable+sizeof(ACPI_TEST_HEADER),&address,4);}
    /* MADT has a 44-byte fixed header even when it has no entries. */
    memcpy(g_AcpiDeviceTable,"APIC",4);{uint32_t n=44;memcpy(g_AcpiDeviceTable+4,&n,4);}
    for(j=0;j<44u;j++)sum=(uint8_t)(sum+g_AcpiDeviceTable[j]);g_AcpiDeviceTable[9]=(uint8_t)(0-sum);
    sum=0;for(j=0;j<length;j++)sum=(uint8_t)(sum+g_AcpiRootTable[j]);g_AcpiRootTable[9]=(uint8_t)(0-sum);
    memset(&in,0,sizeof(in));in.StructureSize=sizeof(in);in.Version=RC95LDR_PLATFORM_INVENTORY_VERSION;
    in.AcpiRoot.Revision=0;in.AcpiRoot.RsdtAddress=0x1000;in.MapPhysical=MapTestPhysical;in.ReadPciConfig=ReadTestPci;
    assert(Rc95LdrBuildPlatformInventory(&in,&out)==Rc95LdrSuccess);
    assert(out.ComponentCount==3&&out.PciFunctionCount==1&&out.AcpiTableCount==1);
    assert(out.Components[0].ParentIndex==-1&&out.Components[1].ParentIndex==0&&out.Components[2].ParentIndex==0);
    assert(out.PciFunctions[0].VendorId==0x1234&&out.PciFunctions[0].DeviceId==0x5678);
}

/* Compact registry-hive builder used to exercise the public boot-driver
   enumerator rather than testing only the dependency graph primitive. */
#define TEST_HIVE_SIZE 0x6000u
#define TEST_HBIN_SIZE 0x5000u
typedef struct _TEST_HIVE_BUILDER {
    uint8_t Bytes[TEST_HIVE_SIZE];
    uint32_t NextCell;
} TEST_HIVE_BUILDER;

static uint32_t TestHiveAlloc(TEST_HIVE_BUILDER *b, uint32_t payloadBytes)
{
    uint32_t total = (payloadBytes + 4u + 7u) & ~7u;
    uint32_t cell = b->NextCell;
    assert(total >= 8u && cell <= TEST_HBIN_SIZE - total);
    Put32(b->Bytes + 0x1000u + cell, (uint32_t)(0u - total));
    memset(b->Bytes + 0x1004u + cell, 0, total - 4u);
    b->NextCell += total;
    return cell;
}

static uint8_t *TestHiveCell(TEST_HIVE_BUILDER *b, uint32_t cell)
{
    return b->Bytes + 0x1004u + cell;
}

static uint32_t TestHiveKey(TEST_HIVE_BUILDER *b, const char *name)
{
    size_t n = strlen(name);
    uint32_t cell = TestHiveAlloc(b, (uint32_t)(0x4cu + n));
    uint8_t *p = TestHiveCell(b, cell);
    Put16(p, 0x6b6eu); Put16(p + 2, 0x20u);
    Put16(p + 0x48, (uint16_t)n);
    memcpy(p + 0x4c, name, n);
    return cell;
}

static void TestHiveChildren(TEST_HIVE_BUILDER *b, uint32_t parent,
                             const uint32_t *children, uint32_t count)
{
    uint32_t index, i;
    uint8_t *p = TestHiveCell(b, parent);
    if (!count) return;
    index = TestHiveAlloc(b, 4u + count * 4u);
    p = TestHiveCell(b, parent);
    Put32(p + 0x14, count); Put32(p + 0x1c, index);
    p = TestHiveCell(b, index);
    Put16(p, 0x696cu); Put16(p + 2, (uint16_t)count);
    for (i = 0; i < count; ++i) Put32(p + 4u + i * 4u, children[i]);
}

static uint32_t TestHiveDword(TEST_HIVE_BUILDER *b, const char *name,
                              uint32_t value)
{
    size_t n = strlen(name);
    uint32_t cell = TestHiveAlloc(b, (uint32_t)(0x14u + n));
    uint8_t *p = TestHiveCell(b, cell);
    Put16(p, 0x6b76u); Put16(p + 2, (uint16_t)n);
    Put32(p + 4, 0x80000004u); Put32(p + 8, value);
    Put32(p + 0x0c, 4u); Put16(p + 0x10, 1u);
    memcpy(p + 0x14, name, n);
    return cell;
}

static uint32_t TestHiveMultiSz(TEST_HIVE_BUILDER *b, const char *name,
                                const uint8_t *data, uint32_t length)
{
    size_t n = strlen(name);
    uint32_t dataCell = TestHiveAlloc(b, length);
    uint32_t cell;
    uint8_t *p = TestHiveCell(b, dataCell);
    memcpy(p, data, length);
    cell = TestHiveAlloc(b, (uint32_t)(0x14u + n));
    p = TestHiveCell(b, cell);
    Put16(p, 0x6b76u); Put16(p + 2, (uint16_t)n);
    Put32(p + 4, length); Put32(p + 8, dataCell);
    Put32(p + 0x0c, 7u); Put16(p + 0x10, 1u);
    memcpy(p + 0x14, name, n);
    return cell;
}

static uint32_t TestHiveBinary(TEST_HIVE_BUILDER *b, const char *name,
                               const uint8_t *data, uint32_t length)
{
    size_t n = strlen(name);
    uint32_t dataCell = TestHiveAlloc(b, length);
    uint32_t cell;
    uint8_t *p = TestHiveCell(b, dataCell);
    memcpy(p, data, length);
    cell = TestHiveAlloc(b, (uint32_t)(0x14u + n));
    p = TestHiveCell(b, cell);
    Put16(p, 0x6b76u); Put16(p + 2, (uint16_t)n);
    Put32(p + 4, length); Put32(p + 8, dataCell);
    Put32(p + 0x0c, 3u); Put16(p + 0x10, 1u);
    memcpy(p + 0x14, name, n);
    return cell;
}

static uint32_t TestHiveString(TEST_HIVE_BUILDER *b, const char *name,
                               const char *value)
{
    size_t nameLength = strlen(name), valueLength = strlen(value), i;
    uint32_t dataLength = (uint32_t)((valueLength + 1u) * 2u);
    uint32_t dataCell = TestHiveAlloc(b, dataLength);
    uint32_t cell = TestHiveAlloc(b, (uint32_t)(0x14u + nameLength));
    uint8_t *data = TestHiveCell(b, dataCell);
    uint8_t *p = TestHiveCell(b, cell);
    for (i = 0; i < valueLength; ++i) Put16(data + i * 2u, (uint8_t)value[i]);
    Put16(data + valueLength * 2u, 0);
    Put16(p, 0x6b76u); Put16(p + 2, (uint16_t)nameLength);
    Put32(p + 4, dataLength); Put32(p + 8, dataCell);
    Put32(p + 0x0c, 1u); Put16(p + 0x10, 1u);
    memcpy(p + 0x14, name, nameLength);
    return cell;
}

static void TestHiveValues(TEST_HIVE_BUILDER *b, uint32_t key,
                           const uint32_t *values, uint32_t count)
{
    uint32_t list, i;
    uint8_t *p;
    if (!count) return;
    list = TestHiveAlloc(b, count * 4u);
    p = TestHiveCell(b, key);
    Put32(p + 0x24, count); Put32(p + 0x28, list);
    p = TestHiveCell(b, list);
    for (i = 0; i < count; ++i) Put32(p + i * 4u, values[i]);
}

enum TEST_DEPENDENCY_FIXTURE {
    TestDependencyChain,
    TestDependencyMissing,
    TestDependencyNonBootTarget,
    TestDependencySelf,
    TestDependencyCycle,
    TestDependencyGroup,
    TestDependencyMalformed,
    TestDependencyGroupOrder,
    TestGroupOrderOnly,
    TestTagOrderOnly,
    TestMalformedGroupOrderList,
    TestMalformedTagOrderList,
    TestMalformedGroupValue,
    TestMalformedTagValue
};

static void TestBuildDependencyHive(TEST_HIVE_BUILDER *b,
                                    enum TEST_DEPENDENCY_FIXTURE scenario)
{
    static const uint8_t depB[] = { 'B',0, 0,0, 0,0 };
    static const uint8_t depC[] = { 'C',0, 0,0, 0,0 };
    static const uint8_t depA[] = { 'A',0, 0,0, 0,0 };
    static const uint8_t self[] = { 'A',0, 0,0, 0,0 };
    static const uint8_t missing[] = { 'X',0, 0,0, 0,0 };
    static const uint8_t group[] = { 'G',0, 0,0, 0,0 };
    static const uint8_t malformed[] = { 'B',0, 0,0 };
    static const uint8_t wrongGroupType[] = { 'E',0 };
    /* REG_MULTI_SZ: UTF-16LE "Early\\0Late\\0\\0". */
    static const uint8_t groupOrder[] = {
        'E',0,'a',0,'r',0,'l',0,'y',0,0,0,
        'L',0,'a',0,'t',0,'e',0,0,0,0,0
    };
    static const uint8_t duplicateGroupOrder[] = {
        'E',0,'a',0,'r',0,'l',0,'y',0,0,0,
        'E',0,'a',0,'r',0,'l',0,'y',0,0,0,0,0
    };
    uint32_t root, select, controlSet, services, a, bb, c;
    uint32_t control, serviceGroupOrder, groupOrderList;
    uint32_t children[3], values[4], count;
    memset(b, 0, sizeof(*b));
    b->NextCell = 0x20u;
    memcpy(b->Bytes, "regf", 4);
    Put32(b->Bytes + 4, 1u); Put32(b->Bytes + 8, 1u);
    Put32(b->Bytes + 0x14, 1u); Put32(b->Bytes + 0x18, 5u);
    Put32(b->Bytes + 0x1c, 0u); Put32(b->Bytes + 0x20, 1u);
    Put32(b->Bytes + 0x24, 0x20u); Put32(b->Bytes + 0x28, TEST_HBIN_SIZE);
    memcpy(b->Bytes + 0x1000, "hbin", 4);
    Put32(b->Bytes + 0x1008, TEST_HBIN_SIZE);

    root = TestHiveKey(b, "");
    select = TestHiveKey(b, "Select");
    controlSet = TestHiveKey(b, "ControlSet001");
    services = TestHiveKey(b, "Services");
    control = TestHiveKey(b, "Control");
    serviceGroupOrder = TestHiveKey(b, "ServiceGroupOrder");
    a = TestHiveKey(b, "A");
    bb = TestHiveKey(b, "B");
    c = TestHiveKey(b, "C");

    children[0] = select; children[1] = controlSet;
    TestHiveChildren(b, root, children, 2u);
    children[0] = services;
    count = 1u;
    if (scenario == TestDependencyGroupOrder ||
        scenario == TestGroupOrderOnly || scenario == TestTagOrderOnly ||
        scenario == TestMalformedGroupOrderList ||
        scenario == TestMalformedTagOrderList)
        children[count++] = control;
    TestHiveChildren(b, controlSet, children, count);
    if (scenario == TestDependencyGroupOrder ||
        scenario == TestGroupOrderOnly ||
        scenario == TestMalformedGroupOrderList) {
        uint32_t listValue;
        children[0] = serviceGroupOrder;
        TestHiveChildren(b, control, children, 1u);
        if (scenario == TestMalformedGroupOrderList)
            listValue = TestHiveMultiSz(b, "List", duplicateGroupOrder,
                                        sizeof(duplicateGroupOrder));
        else
            listValue = TestHiveMultiSz(b, "List", groupOrder, sizeof(groupOrder));
        TestHiveValues(b, serviceGroupOrder, &listValue, 1u);
    } else if (scenario == TestTagOrderOnly ||
               scenario == TestMalformedTagOrderList) {
        static const uint8_t tagOrder[] = { 2,0,0,0, 1,0,0,0, 2,0,0,0 };
        static const uint8_t malformedTagOrder[] = { 2,0,0,0, 1,0,0,0 };
        uint32_t tagValue;
        groupOrderList = TestHiveKey(b, "GroupOrderList");
        children[0] = groupOrderList;
        TestHiveChildren(b, control, children, 1u);
        if (scenario == TestMalformedTagOrderList)
            tagValue = TestHiveBinary(b, "Early", malformedTagOrder,
                                      sizeof(malformedTagOrder));
        else
            tagValue = TestHiveBinary(b, "Early", tagOrder, sizeof(tagOrder));
        TestHiveValues(b, groupOrderList, &tagValue, 1u);
    }

    count = 0;
    values[count++] = TestHiveDword(b, "Current", 1u);
    TestHiveValues(b, select, values, count);

    children[0] = a; children[1] = bb;
    count = 2u;
    if (scenario == TestDependencyNonBootTarget) children[count++] = c;
    TestHiveChildren(b, services, children, count);

    count = 0;
    values[count++] = TestHiveDword(b, "Start", 0u);
    values[count++] = TestHiveDword(b, "Type", 1u);
    if (scenario == TestDependencyChain || scenario == TestDependencyCycle ||
        scenario == TestDependencySelf || scenario == TestDependencyMissing ||
        scenario == TestDependencyNonBootTarget ||
        scenario == TestDependencyMalformed ||
        scenario == TestDependencyGroupOrder) {
        const uint8_t *dep = depB;
        uint32_t depLength = sizeof(depB);
        if (scenario == TestDependencySelf) dep = self;
        if (scenario == TestDependencyMissing) dep = missing;
        if (scenario == TestDependencyNonBootTarget) dep = depC;
        if (scenario == TestDependencyMalformed) {
            dep = malformed; depLength = sizeof(malformed);
        }
        values[count++] = TestHiveMultiSz(b, "DependOnService", dep, depLength);
    } else if (scenario == TestDependencyGroup) {
        values[count++] = TestHiveMultiSz(b, "DependOnGroup", group, sizeof(group));
    }
    if (scenario == TestDependencyGroupOrder || scenario == TestTagOrderOnly ||
        scenario == TestMalformedGroupOrderList ||
        scenario == TestMalformedTagOrderList)
        values[count++] = TestHiveString(b, "Group", "Early");
    else if (scenario == TestGroupOrderOnly)
        values[count++] = TestHiveString(b, "Group", "Late");
    if (scenario == TestTagOrderOnly || scenario == TestMalformedTagOrderList)
        values[count++] = TestHiveDword(b, "Tag", 2u);
    else if (scenario == TestMalformedGroupValue)
        values[count++] = TestHiveBinary(b, "Group", wrongGroupType, sizeof(wrongGroupType));
    else if (scenario == TestMalformedTagValue) {
        static const uint8_t wrongTagType[] = { 2,0,0,0 };
        values[count++] = TestHiveString(b, "Group", "Early");
        values[count++] = TestHiveMultiSz(b, "Tag", wrongTagType,
                                          sizeof(wrongTagType));
    }
    TestHiveValues(b, a, values, count);

    count = 0;
    values[count++] = TestHiveDword(b, "Start", 0u);
    values[count++] = TestHiveDword(b, "Type", 1u);
    if (scenario == TestDependencyCycle)
        values[count++] = TestHiveMultiSz(b, "DependOnService", depA, sizeof(depA));
    if (scenario == TestDependencyGroupOrder)
        values[count++] = TestHiveString(b, "Group", "Late");
    else if (scenario == TestGroupOrderOnly)
        values[count++] = TestHiveString(b, "Group", "Early");
    else if (scenario == TestTagOrderOnly ||
             scenario == TestMalformedGroupOrderList ||
             scenario == TestMalformedTagOrderList) {
        values[count++] = TestHiveString(b, "Group", "Early");
        if (scenario != TestMalformedGroupOrderList)
            values[count++] = TestHiveDword(b, "Tag", 1u);
    }
    if (scenario == TestMalformedTagValue)
        values[count++] = TestHiveString(b, "Group", "Early");
    TestHiveValues(b, bb, values, count);

    if (scenario == TestDependencyNonBootTarget) {
        count = 0;
        values[count++] = TestHiveDword(b, "Start", 3u);
        values[count++] = TestHiveDword(b, "Type", 1u);
        TestHiveValues(b, c, values, count);
    }

    /* Root is the first allocated cell. */
    Put32(b->Bytes + 0x24, root);
    Put32(b->Bytes + 0x1fc, HeaderXor(b->Bytes));
}

typedef struct _TEST_DRIVER_CAPTURE {
    char Names[4][RC95LDR_HIVE_MAX_NAME_CHARS + 1u];
    uint32_t Count;
} TEST_DRIVER_CAPTURE;

static RC95LDR_STATUS CaptureBootDriver(void *context,
    const RC95LDR_HIVE_BOOT_DRIVER *driver)
{
    TEST_DRIVER_CAPTURE *capture = (TEST_DRIVER_CAPTURE *)context;
    assert(capture->Count < 4u);
    strcpy(capture->Names[capture->Count++], driver->ServiceName);
    return Rc95LdrSuccess;
}

static void TestSystemHiveDependencyIntegration(void)
{
    enum TEST_DEPENDENCY_FIXTURE scenario;
    for (scenario = TestDependencyChain;
         scenario <= TestMalformedTagValue; ++scenario) {
        TEST_HIVE_BUILDER builder;
        RC95LDR_SYSTEM_HIVE hive;
        RC95LDR_HIVE_BOOT_DRIVER_WORKSPACE workspace;
        TEST_DRIVER_CAPTURE capture;
        RC95LDR_STATUS status;
        memset(&capture, 0, sizeof(capture));
        TestBuildDependencyHive(&builder, scenario);
        assert(Rc95LdrOpenSystemHive(builder.Bytes, sizeof(builder.Bytes),
                                     &hive) == Rc95LdrSuccess);
        status = Rc95LdrHiveEnumerateBootDriversWithWorkspace(
            &hive, CaptureBootDriver, &capture, &workspace);
        if (scenario == TestDependencyChain ||
            scenario == TestDependencyGroupOrder ||
            scenario == TestGroupOrderOnly ||
            scenario == TestTagOrderOnly) {
            assert(status == Rc95LdrSuccess);
            assert(capture.Count == 2u);
            if (scenario == TestTagOrderOnly) {
                assert(strcmp(capture.Names[0], "B") == 0);
                assert(strcmp(capture.Names[1], "A") == 0);
            } else {
                assert(strcmp(capture.Names[0], "B") == 0);
                assert(strcmp(capture.Names[1], "A") == 0);
            }
        } else {
            assert(status == (scenario == TestDependencyNonBootTarget ||
                              scenario == TestDependencyGroup
                              ? Rc95LdrUnsupported : Rc95LdrCorruptData));
            assert(capture.Count == 0u);
        }
    }
}

int main(void){TestSystemHiveDependencyIntegration();TestDependencyMultiSzValidation();TestMetadataBuilder();TestAutomaticPlatformInventory();TestNativeLayouts();TestRc95FirmwareValidation();TestHiveRootBounds();TestReactOsDelegatesToRosLoad();TestHandoffPreflightIsFailClosed();puts("RC95LDR native loader ABI and hive tests passed");return 0;}
