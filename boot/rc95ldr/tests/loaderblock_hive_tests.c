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
 RC95LDR_LOADED_MODULE m[2]={{0x100000,0x20000,0x101000,RC95LDR_MODULE_KERNEL,"\\SystemRoot\\system32\\ntoskrnl.exe",0},{0x200000,0x10000,0x201000,RC95LDR_MODULE_HAL,"\\SystemRoot\\system32\\hal.dll",0}};
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
 in.FirmwareMapAddress=(uint64_t)(uintptr_t)hive;in.FirmwareMapSize=256;in.FirmwareMapDescriptorSize=48;
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
static void Put16(uint8_t*p,uint16_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void Put32(uint8_t*p,uint32_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);}
static uint32_t HeaderXor(const uint8_t*p){uint32_t x=0;size_t i;for(i=0;i<0x1fc;i+=4)x^=(uint32_t)p[i]|((uint32_t)p[i+1]<<8)|((uint32_t)p[i+2]<<16)|((uint32_t)p[i+3]<<24);return x==0xffffffffu?0xfffffffeu:(x?x:1u);}
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
 assert(Rc95LdrHiveOpenPath(&hive,"Select",&key)==Rc95LdrSuccess);
 {uint32_t current=0;RC95LDR_HIVE_KEY control;
  assert(Rc95LdrHiveGetDword(&hive,&key,"Current",&current)==Rc95LdrSuccess);
  assert(current==1);
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

static void TestLoaderStateBuilders(void)
{
    union { uint64_t Align; uint8_t Bytes[4096]; } storage;
    RC95LDR_CONFIG_COMPONENT_INPUT components[2];
    RC95LDR_LOADERSTATE_INPUT input;
    RC95LDR_LOADERSTATE_RESULT result;
    RC95LDR_NT_CONFIGURATION_COMPONENT_DATA *root,*child;
    uint32_t cpuConfig=0x12345678u;
    memset(components,0,sizeof(components));
    components[0].Class=0;components[0].Type=0;components[0].Identifier="RC95 Platform";
    components[0].ParentIndex=-1;
    components[1].Class=1;components[1].Type=1;components[1].Identifier="CPU";
    components[1].ConfigurationData=&cpuConfig;components[1].ConfigurationDataLength=sizeof(cpuConfig);
    components[1].ParentIndex=0;
    memset(&input,0,sizeof(input));input.StructureSize=sizeof(input);
    input.Version=RC95LDR_LOADERSTATE_VERSION;input.NtVersion=Rc95LdrNtVersion61;
    input.OsMajorVersion=6;input.OsMinorVersion=1;
    input.Components=components;input.ComponentCount=2;
    input.Buffer=storage.Bytes;input.BufferSize=sizeof(storage.Bytes);
    memset(&result,0,sizeof(result));
    assert(Rc95LdrBuildNtLoaderState(&input,&result)==Rc95LdrSuccess);
    assert(result.ConfigurationRoot!=0&&result.LoaderExtension!=0);
    root=(void*)(uintptr_t)result.ConfigurationRoot;
    child=(void*)(uintptr_t)root->Child;
    assert(root->Parent==0&&child!=0);
    assert(child->Parent==result.ConfigurationRoot);
    assert((child->ConfigurationData&3u)==0);
    assert(*(const uint32_t*)(uintptr_t)child->ConfigurationData==cpuConfig);
    assert(strcmp((const char*)(uintptr_t)root->ComponentEntry.Identifier,"RC95 Platform")==0);
    assert(((RC95LDR_NT_LOADER_EXTENSION_PREFIX_V61*)(uintptr_t)result.LoaderExtension)->Size==
        sizeof(RC95LDR_NT_LOADER_EXTENSION_PREFIX_V61));
    input.NtVersion=Rc95LdrNtVersion5;input.OsMajorVersion=5;input.OsMinorVersion=2;
    assert(Rc95LdrBuildNtLoaderState(&input,&result)==Rc95LdrSuccess);
    assert(((RC95LDR_NT_LOADER_EXTENSION_PREFIX*)(uintptr_t)result.LoaderExtension)->MajorVersion==5);
    assert(((RC95LDR_NT_LOADER_EXTENSION_PREFIX*)(uintptr_t)result.LoaderExtension)->MinorVersion==2);
    input.NtVersion=Rc95LdrNtVersion4;
    assert(Rc95LdrBuildNtLoaderState(&input,&result)==Rc95LdrSuccess);
    assert(result.ConfigurationRoot!=0&&result.LoaderExtension==0);
}
static void TestArchitectureHandoffPreparation(void)
{
    RC95LDR_KERNEL_HANDOFF_CONTEXT context;
    RC95LDR_ARCH_HANDOFF_INPUT input;
    union { uint64_t Align[4]; uint8_t Bytes[256]; } frame;
    uintptr_t frameAligned;
    memset(&context,0,sizeof(context));memset(&input,0,sizeof(input));memset(&frame,0,sizeof(frame));
#if UINTPTR_MAX > UINT32_MAX
    input.Architecture=Rc95LdrNtArchAmd64;
#else
    input.Architecture=Rc95LdrNtArchI386;
#endif
    context.Architecture=input.Architecture;
    input.StructureSize=sizeof(input);input.Version=RC95LDR_ARCH_HANDOFF_INPUT_VERSION;
    input.StackBase=0x100000;input.StackSize=0x4000;input.StackPointer=0x104000;input.PageTableRoot=0x200000;
    input.ArchitectureState=0x210000;input.CpuState=0x220000;
    input.PcrAddress=0x230000;input.PcrSize=0x1000;input.PrcbAddress=0x232000;input.PrcbSize=0x1000;
    input.KernelEntry=0x101000;input.LoaderBlockArgument=0x300000;
    frameAligned=((uintptr_t)frame.Bytes+15u)&~(uintptr_t)15u;
    input.RegisterFrameBuffer=(void*)frameAligned;
    input.RegisterFrameCapacity=sizeof(frame.Bytes)-(size_t)(frameAligned-(uintptr_t)frame.Bytes);
    assert(Rc95LdrPrepareArchitectureHandoff(&input,&context)==Rc95LdrSuccess);
    assert((context.CompletedRequirements&RC95LDR_HANDOFF_STACK)!=0);
    assert((context.CompletedRequirements&RC95LDR_HANDOFF_PAGE_TABLES)!=0);
    assert((context.CompletedRequirements&RC95LDR_HANDOFF_ARCH_STATE)!=0);
    assert((context.CompletedRequirements&RC95LDR_HANDOFF_CPU_STATE)!=0);
    assert((context.CompletedRequirements&RC95LDR_HANDOFF_PCR)!=0);
    assert((context.CompletedRequirements&RC95LDR_HANDOFF_PRCB)!=0);
    assert((context.CompletedRequirements&RC95LDR_HANDOFF_REGISTER_FRAME)!=0);
#if UINTPTR_MAX > UINT32_MAX
    assert(((RC95LDR_AMD64_REGISTER_FRAME*)input.RegisterFrameBuffer)->Rcx==input.LoaderBlockArgument);
    assert(((RC95LDR_AMD64_REGISTER_FRAME*)input.RegisterFrameBuffer)->Rip==input.KernelEntry);
#else
    assert(((RC95LDR_I386_REGISTER_FRAME*)input.RegisterFrameBuffer)->Eax==input.LoaderBlockArgument);
    assert(((RC95LDR_I386_REGISTER_FRAME*)input.RegisterFrameBuffer)->Eip==input.KernelEntry);
#endif
    input.StackBase++;
    assert(Rc95LdrPrepareArchitectureHandoff(&input,&context)==Rc95LdrInvalidParameter);
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
    memcpy(g_AcpiDeviceTable,"APIC",4);{uint32_t n=sizeof(ACPI_TEST_HEADER);memcpy(g_AcpiDeviceTable+4,&n,4);}
    for(j=0;j<sizeof(ACPI_TEST_HEADER);j++)sum=(uint8_t)(sum+g_AcpiDeviceTable[j]);g_AcpiDeviceTable[9]=(uint8_t)(0-sum);
    sum=0;for(j=0;j<length;j++)sum=(uint8_t)(sum+g_AcpiRootTable[j]);g_AcpiRootTable[9]=(uint8_t)(0-sum);
    memset(&in,0,sizeof(in));in.StructureSize=sizeof(in);in.Version=RC95LDR_PLATFORM_INVENTORY_VERSION;
    in.AcpiRoot.Revision=0;in.AcpiRoot.RsdtAddress=0x1000;in.MapPhysical=MapTestPhysical;in.ReadPciConfig=ReadTestPci;
    assert(Rc95LdrBuildPlatformInventory(&in,&out)==Rc95LdrSuccess);
    assert(out.ComponentCount==3&&out.PciFunctionCount==1&&out.AcpiTableCount==1);
    assert(out.Components[0].ParentIndex==-1&&out.Components[1].ParentIndex==0&&out.Components[2].ParentIndex==0);
    assert(out.PciFunctions[0].VendorId==0x1234&&out.PciFunctions[0].DeviceId==0x5678);
}

int main(void){TestMetadataBuilder();TestAutomaticPlatformInventory();TestLoaderStateBuilders();TestArchitectureHandoffPreparation();TestNativeLayouts();TestHiveRootBounds();TestReactOsDelegatesToRosLoad();TestHandoffPreflightIsFailClosed();puts("RC95LDR native loader ABI and hive tests passed");return 0;}
