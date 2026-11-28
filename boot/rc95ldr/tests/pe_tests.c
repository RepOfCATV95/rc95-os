#include "rc95ldrpe.h"
#include "rc95ldrwinload.h"
#include "rc95ldrprofile.h"
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define FILE_BYTES 0x400u
#define NT_OFF 0x80u
#define SEC_OFF32 0x178u
#define SEC_OFF64 0x188u
static void W16(uint8_t *p,uint16_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void W32(uint8_t *p,uint32_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);}
static void W64(uint8_t *p,uint64_t v){W32(p,(uint32_t)v);W32(p+4,(uint32_t)(v>>32));}
static void MakePe(uint8_t *f,int plus)
{
    uint32_t opt=plus?240:224, sec=plus?SEC_OFF64:SEC_OFF32;
    memset(f,0,FILE_BYTES);
    W16(f,0x5a4d); W32(f+0x3c,NT_OFF); W32(f+NT_OFF,0x4550);
    W16(f+NT_OFF+4,plus?RC95LDR_PE_MACHINE_AMD64:RC95LDR_PE_MACHINE_I386);
    W16(f+NT_OFF+6,1); W16(f+NT_OFF+20,(uint16_t)opt);
    W16(f+NT_OFF+22,0x0002);
    W16(f+NT_OFF+24,plus?0x20b:0x10b);
    W32(f+NT_OFF+24+16,0x1000);
    if(plus) W64(f+NT_OFF+24+24,0x400000);
    else W32(f+NT_OFF+24+28,0x400000);
    W32(f+NT_OFF+24+32,0x1000); W32(f+NT_OFF+24+36,0x200);
    W32(f+NT_OFF+24+56,0x2000); W32(f+NT_OFF+24+60,0x200);
    W32(f+NT_OFF+24+(plus?108:92),16);
    memcpy(f+sec,".text",5); W32(f+sec+8,0x200); W32(f+sec+12,0x1000);
    W32(f+sec+16,0x200); W32(f+sec+20,0x200); W32(f+sec+36,0x60000020);
    f[0x200]=0xc3;
}
typedef struct _FAKE_PLATFORM {
    uint8_t File[FILE_BYTES];
    void *Raw[4];
    uint64_t Address[4];
    uint64_t AllocationBytes[4];
    size_t AllocationCount;
    size_t OpenCount;
} FAKE_PLATFORM;
static RC95LDR_STATUS FakeOpen(void *ctx,const uint16_t *path,RC95LDR_WINLOAD_FILE *file)
{
    FAKE_PLATFORM *p=(FAKE_PLATFORM *)ctx;
    (void)path; ++p->OpenCount; file->FileContext=p; file->FileSize=sizeof(p->File);
    return Rc95LdrSuccess;
}
static RC95LDR_STATUS FakeRead(void *ctx,const RC95LDR_WINLOAD_FILE *file,
    uint64_t offset,void *buffer,size_t length)
{
    FAKE_PLATFORM *p=(FAKE_PLATFORM *)ctx;
    (void)file;
    if(offset>sizeof(p->File) || length>sizeof(p->File)-(size_t)offset)
        return Rc95LdrOutOfBounds;
    memcpy(buffer,p->File+(size_t)offset,length);
    return Rc95LdrSuccess;
}
static void FakeClose(void *ctx,RC95LDR_WINLOAD_FILE *file)
{ (void)ctx; (void)file; }
static RC95LDR_STATUS FakeAllocate(void *ctx,uint64_t bytes,uint64_t alignment,uint64_t *address)
{
    FAKE_PLATFORM *p=(FAKE_PLATFORM *)ctx;
    uintptr_t raw,aligned;
    void *allocation;
    if(p->AllocationCount>=4 || !alignment || (alignment&(alignment-1)) ||
       bytes>SIZE_MAX || alignment>SIZE_MAX-bytes)
        return Rc95LdrOutOfMemory;
    allocation=malloc((size_t)(bytes+alignment-1));
    if(!allocation) return Rc95LdrOutOfMemory;
    raw=(uintptr_t)allocation;
    aligned=(raw+(uintptr_t)alignment-1)&~((uintptr_t)alignment-1);
    p->Raw[p->AllocationCount]=allocation;
    p->Address[p->AllocationCount]=(uint64_t)aligned;
    p->AllocationBytes[p->AllocationCount]=bytes;
    ++p->AllocationCount;
    *address=(uint64_t)aligned;
    return Rc95LdrSuccess;
}
static void FakeFree(void *ctx,uint64_t address,uint64_t bytes)
{
    FAKE_PLATFORM *p=(FAKE_PLATFORM *)ctx;
    size_t i;
    (void)bytes;
    for(i=0;i<p->AllocationCount;i++) if(p->Address[i]==address) {
        free(p->Raw[i]);
        p->Raw[i]=p->Raw[p->AllocationCount-1];
        p->Address[i]=p->Address[p->AllocationCount-1];
        p->AllocationBytes[i]=p->AllocationBytes[p->AllocationCount-1];
        --p->AllocationCount;
        return;
    }
}
static void MakePeWithRelocations(uint8_t *file,int plus)
{
    uint32_t directory=plus?112:96;
    MakePe(file,plus);
    W32(file+NT_OFF+24+directory+5*8,0x1100);
    W32(file+NT_OFF+24+directory+5*8+4,12);
    W32(file+0x300,0x1000); W32(file+0x304,12);
    W16(file+0x308,(uint16_t)(((plus?10:3)<<12)|0x20));
    W16(file+0x30a,0);
    if(plus) W64(file+0x220,0x401234);
    else W32(file+0x220,0x401234);
}
static RC95LDR_STATUS FakeValidateAllocationExtent(void *ctx,uint64_t virtualBase,
    uint64_t physicalBase,uint64_t imageSize,uint64_t allocationSize)
{
    FAKE_PLATFORM *p=(FAKE_PLATFORM *)ctx;
    size_t i;
    if(!virtualBase || virtualBase!=physicalBase || !imageSize || allocationSize<imageSize)
        return Rc95LdrInvalidImage;
    for(i=0;i<p->AllocationCount;i++)
        if(p->Address[i]==physicalBase && p->AllocationBytes[i]==allocationSize)
            return Rc95LdrSuccess;
    return Rc95LdrInvalidImage;
}
static RC95LDR_STATUS FakeGetMemoryMap(void *ctx,RC95LDR_WINLOAD_MEMORY_RANGE *ranges,size_t capacity,size_t *count)
{ (void)ctx;(void)ranges;(void)capacity;(void)count;return Rc95LdrUnsupported; }
static RC95LDR_STATUS FakeExitFirmwareAndHandoff(void *ctx,uint64_t entry,uint64_t block,uint64_t size)
{ (void)ctx;(void)entry;(void)block;(void)size;return Rc95LdrUnsupported; }
static RC95LDR_STATUS FakeBuildNativePreparation(void *ctx,const RC95LDR_WINLOAD_REQUEST *request,
    uint64_t virtualBase,uint64_t physicalBase,uint64_t imageSize,uint64_t allocationSize,
    uint64_t entry,struct _RC95LDR_NATIVE_PREPARE_REQUEST *prepareRequest)
{ (void)ctx;(void)virtualBase;(void)physicalBase;(void)imageSize;(void)allocationSize;(void)entry;(void)prepareRequest;
  assert(request && request->KernelPeSections);
  assert(request->KernelPeSectionCount==1u);
  assert(request->KernelPeSizeOfHeaders==0x200u);
  assert(request->KernelPeSections[0].VirtualAddress==0x1000u);
  assert((request->KernelPeSections[0].Characteristics &
      (RC95LDR_PE_SCN_MEM_READ|RC95LDR_PE_SCN_MEM_EXECUTE)) ==
      (RC95LDR_PE_SCN_MEM_READ|RC95LDR_PE_SCN_MEM_EXECUTE));
  return Rc95LdrUnsupported; }
static void FakeReleaseNativePreparation(void *ctx,struct _RC95LDR_NATIVE_PREPARE_REQUEST *prepareRequest)
{ (void)ctx;(void)prepareRequest; }

static void TestKernelLoadIntegration(void)
{
    FAKE_PLATFORM fake;
    RC95LDR_WINLOAD_PLATFORM platform;
    RC95LDR_WINLOAD_REQUEST request;
    RC95LDR_WINLOAD_RESULT result;
    RC95LDR_BOOT_CONTEXT boot;
    const uint16_t kernelPath[]={'\\','k','e','r','n','e','l',0};
#if defined(_M_AMD64) || defined(_M_X64) || defined(_AMD64_) || defined(__x86_64__) || defined(__amd64__)
    const int plus=1;
#else
    const int plus=0;
#endif
    RC95LDR_STATUS status;
    memset(&fake,0,sizeof(fake));
    MakePeWithRelocations(fake.File,plus);
    memset(&platform,0,sizeof(platform));
    platform.StructureSize=sizeof(platform);
    platform.Version=RC95LDR_WINLOAD_ABI_VERSION;
    platform.Context=&fake;
    platform.OpenFile=FakeOpen; platform.ReadFileAt=FakeRead;
    platform.CloseFile=FakeClose; platform.AllocatePages=FakeAllocate;
    platform.FreePages=FakeFree;
    platform.ValidateAllocationExtent=FakeValidateAllocationExtent;
    platform.GetMemoryMap=FakeGetMemoryMap;
    platform.ExitFirmwareAndHandoff=FakeExitFirmwareAndHandoff;
    platform.BuildNativePreparation=FakeBuildNativePreparation;
    platform.ReleaseNativePreparation=FakeReleaseNativePreparation;
    memset(&request,0,sizeof(request));
    request.StructureSize=sizeof(request);
    request.Version=RC95LDR_WINLOAD_ABI_VERSION;
    request.OsFamily=Rc95LdrOsRc95;
    request.Firmware=Rc95LdrFirmwareBios;
    request.BootEntryKind=Rc95LdrEntryRc95Winload;
    request.ResumePolicy=Rc95LdrResumeDisabled;
    request.KernelPath=kernelPath;
    /* A mismatched Windows loader entry must fail before opening any image. */
    request.BootEntryKind=Rc95LdrEntryWindowsWinload;
    memset(&result,0,sizeof(result));
    result.StructureSize=sizeof(result);
    result.Version=RC95LDR_WINLOAD_ABI_VERSION;
    Rc95LdrInitializeContext(&boot,&fake);
    status=Rc95LdrLoadAndBootWindows(&boot,&platform,&request,&result);
    assert(status==Rc95LdrInvalidParameter);
    assert(fake.OpenCount==0 && fake.AllocationCount==0);
    request.BootEntryKind=Rc95LdrEntryRc95Winload;
    memset(&result,0,sizeof(result));
    result.StructureSize=sizeof(result);
    result.Version=RC95LDR_WINLOAD_ABI_VERSION;
    Rc95LdrInitializeContext(&boot,&fake);
    status=Rc95LdrLoadAndBootWindows(&boot,&platform,&request,&result);
    if(status!=Rc95LdrUnsupported) fprintf(stderr,"kernel integration status=%d stage=%d detail=%u opens=%lu allocations=%lu\\n",(int)status,(int)boot.CurrentStage,boot.LastError.Detail,(unsigned long)fake.OpenCount,(unsigned long)fake.AllocationCount);
    assert(status==Rc95LdrUnsupported);
    assert(boot.CurrentStage==Rc95LdrStageOsPrepare);
    /* The PE mapped successfully, but native preparation is deliberately\n       unsupported by this host adapter. Failure must release the kernel and\n       publish no stale image or handoff addresses to the caller. */
    assert(fake.OpenCount==1);
    assert(result.ImageCount==0 && result.KernelImageSize==0);
    assert(result.KernelImageBase==0 && result.KernelPhysicalBase==0);
    assert(result.KernelAllocationSize==0 && result.KernelEntryPoint==0);
    assert(result.LoaderBlockAddress==0 && result.LoaderBlockSize==0);
    assert(fake.AllocationCount==0);
}

static void TestValidAndMap(int plus)
{
    uint8_t file[FILE_BYTES], image[0x2000];
    RC95LDR_PE_INFO info;
    MakePe(file,plus);
    assert(Rc95LdrPeValidate(file,sizeof(file),0,&info)==Rc95LdrSuccess);
    assert(info.Format==(plus?Rc95LdrPe32Plus:Rc95LdrPe32));
    assert(info.EntryPointRva==0x1000);
    assert(info.SectionCount==1u);
    assert(info.Sections[0].VirtualAddress==0x1000u);
    assert(info.Sections[0].VirtualSize==0x200u);
    assert(info.Sections[0].MappedSize==0x200u);
    assert((info.Sections[0].Characteristics &
        (RC95LDR_PE_SCN_MEM_READ|RC95LDR_PE_SCN_MEM_EXECUTE)) ==
        (RC95LDR_PE_SCN_MEM_READ|RC95LDR_PE_SCN_MEM_EXECUTE));
    assert(Rc95LdrPeMap(file,sizeof(file),&info,image,sizeof(image),
        info.PreferredImageBase)==Rc95LdrSuccess);
    assert(image[0x1000]==0xc3);
}
static void TestRelocations(int plus)
{
    uint8_t file[FILE_BYTES], image[0x2000];
    RC95LDR_PE_INFO info;
    uint32_t dir=plus?112:96;
    MakePe(file,plus);
    W32(file+NT_OFF+24+dir+5*8,0x1100);
    W32(file+NT_OFF+24+dir+5*8+4,12);
    W32(file+0x300,0x1000); W32(file+0x304,12);
    W16(file+0x308,(uint16_t)(((plus?10:3)<<12)|0x20));
    W16(file+0x30a,0);
    if(plus) W64(file+0x220,0x401234);
    else W32(file+0x220,0x401234);
    assert(Rc95LdrPeValidate(file,sizeof(file),0,&info)==Rc95LdrSuccess);
    assert(Rc95LdrPeMap(file,sizeof(file),&info,image,sizeof(image),
        0x500000)==Rc95LdrSuccess);
    if(plus) assert(Rc95LdrReadU64(image+0x1020)==0x501234);
    else assert(Rc95LdrReadU32(image+0x1020)==0x501234);
}
static void TestResourceOnlyImage(int plus)
{
    uint8_t file[FILE_BYTES], image[0x2000];
    RC95LDR_PE_INFO info;
    uint32_t section=plus?SEC_OFF64:SEC_OFF32;
    uint32_t optional=NT_OFF+24;
    uint32_t directory=plus?112u:96u;

    MakePe(file,plus);
    W32(file+optional+16,0);             /* resource-only DLL has no entry */
    W16(file+NT_OFF+22,0x2002u);         /* executable image + DLL */
    W32(file+optional+directory+2u*8u,0x1000u);
    W32(file+optional+directory+2u*8u+4u,0x200u);
    memcpy(file+section,".rsrc",5);
    W32(file+section+36,0x40000040u);    /* initialized, readable data */
    memset(file+0x200,0,0x200);

    assert(Rc95LdrPeValidate(file,sizeof(file),0,&info)==Rc95LdrInvalidImage);
    assert(Rc95LdrPeValidateResource(file,sizeof(file),
        plus?RC95LDR_PE_MACHINE_AMD64:RC95LDR_PE_MACHINE_I386,&info)==Rc95LdrSuccess);
    assert(info.EntryPointRva==0 && info.ImportRva==0 && info.ImportSize==0);
    /* Resource RVAs remain valid without a relocation directory. */
    assert(Rc95LdrPeMapResource(file,sizeof(file),&info,image,sizeof(image),
        0x500000u)==Rc95LdrSuccess);
    assert(Rc95LdrPeMap(file,sizeof(file),&info,image,sizeof(image),
        0x500000u)==Rc95LdrInvalidImage);

    /* A resource-only DLL with an import directory is never accepted. */
    W32(file+optional+directory+8,0x1100);
    W32(file+optional+directory+12,20);
    assert(Rc95LdrPeValidateResource(file,sizeof(file),info.Machine,&info)==
        Rc95LdrInvalidImage);
    /* Nor is an ordinary EXE accepted under the resource-only contract. */
    MakePe(file,plus);
    assert(Rc95LdrPeValidateResource(file,sizeof(file),info.Machine,&info)==
        Rc95LdrInvalidImage);
}

static void TestMalformed(void)
{
    uint8_t f[FILE_BYTES]; RC95LDR_PE_INFO info;
    MakePe(f,0); f[0]=0;
    assert(Rc95LdrPeValidate(f,sizeof(f),0,&info)==Rc95LdrInvalidImage);
    MakePe(f,0); W32(f+0x3c,0xfffffff0u);
    assert(Rc95LdrPeValidate(f,sizeof(f),0,&info)==Rc95LdrInvalidImage);
    MakePe(f,0); W32(f+SEC_OFF32+20,0x3f0);
    assert(Rc95LdrPeValidate(f,sizeof(f),0,&info)==Rc95LdrInvalidImage);
    MakePe(f,0); W32(f+NT_OFF+24+16,0x1fff);
    assert(Rc95LdrPeValidate(f,sizeof(f),0,&info)==Rc95LdrInvalidImage);
    MakePe(f,0); W32(f+NT_OFF+24+32,3);
    assert(Rc95LdrPeValidate(f,sizeof(f),0,&info)==Rc95LdrInvalidImage);
    MakePe(f,0); W32(f+NT_OFF+20,95);
    assert(Rc95LdrPeValidate(f,sizeof(f),0,&info)==Rc95LdrInvalidImage);
    MakePe(f,0); W32(f+NT_OFF+24+92,17);
    assert(Rc95LdrPeValidate(f,sizeof(f),0,&info)==Rc95LdrInvalidImage);
    MakePe(f,0); W16(f+NT_OFF+4,0xaa64);
    assert(Rc95LdrPeValidate(f,sizeof(f),0,&info)==Rc95LdrUnsupported);
    MakePe(f,0); W16(f+NT_OFF+6,2);
    memcpy(f+SEC_OFF32+40,f+SEC_OFF32,40);
    assert(Rc95LdrPeValidate(f,sizeof(f),0,&info)==Rc95LdrInvalidImage);
    /* Two sections may not alias the same bytes in the file image. */
    MakePe(f,0); W16(f+NT_OFF+6,2);
    memcpy(f+SEC_OFF32+40,f+SEC_OFF32,40);
    W32(f+SEC_OFF32+40+8,0x100);
    W32(f+SEC_OFF32+40+12,0x2000);
    W32(f+SEC_OFF32+40+16,0x100);
    W32(f+SEC_OFF32+40+20,0x300);
    assert(Rc95LdrPeValidate(f,sizeof(f),0,&info)==Rc95LdrInvalidImage);
    /* Byte-disjoint sections that share one hardware page are invalid:
       their independent protections cannot be represented by one PTE. */
    MakePe(f,0); W16(f+NT_OFF+6,2);
    memcpy(f+SEC_OFF32+40,f+SEC_OFF32,40);
    W32(f+NT_OFF+24+32,0x200); /* permit sub-page section alignment */
    W32(f+SEC_OFF32+40+8,0x100);
    W32(f+SEC_OFF32+40+12,0x1200);
    W32(f+SEC_OFF32+40+16,0x100);
    W32(f+SEC_OFF32+40+20,0x300);
    assert(Rc95LdrPeValidate(f,sizeof(f),0,&info)==Rc95LdrInvalidImage);
    MakePe(f,0); W32(f+NT_OFF+24+96+5*8,0x1fff);
    W32(f+NT_OFF+24+96+5*8+4,0x100);
    assert(Rc95LdrPeValidate(f,sizeof(f),0,&info)==Rc95LdrInvalidImage);
}

typedef struct _IMPORT_TEST_CONTEXT {
    unsigned Calls;
    uint64_t Address;
} IMPORT_TEST_CONTEXT;
static RC95LDR_STATUS TestImportResolver(void *context,const char *module,
    const char *symbol,uint16_t ordinal,int byOrdinal,uint64_t *address)
{
    IMPORT_TEST_CONTEXT *test=(IMPORT_TEST_CONTEXT *)context;
    (void)ordinal;
    ++test->Calls;
    if(strcmp(module,"hal.dll")!=0 || byOrdinal || !symbol ||
       strcmp(symbol,"HalQuerySystemInformation")!=0)
        return Rc95LdrNotFound;
    *address=test->Address;
    return Rc95LdrSuccess;
}
static void MakePeWithImport(uint8_t *f,int plus)
{
    uint32_t dir=plus?112u:96u;
    MakePe(f,plus);
    W32(f+NT_OFF+24+dir+8,0x1100);
    W32(f+NT_OFF+24+dir+12,40);
    /* IMAGE_IMPORT_DESCRIPTOR and its all-zero terminator. */
    W32(f+0x300,0x1160);
    W32(f+0x30c,0x1140);
    W32(f+0x310,0x1180);
    memcpy(f+0x340,"hal.dll",8);
    /* The lookup table must include an explicit zero thunk terminator;
       MakePe's nonzero padding otherwise looks like a second import. */
    memset(f+0x360,0,16);
    if(plus) W64(f+0x360,0x11a0);
    else W32(f+0x360,0x11a0);
    /* IMAGE_IMPORT_BY_NAME: hint followed by the exact export name. */
    W16(f+0x3a0,0);
    memcpy(f+0x3a2,"HalQuerySystemInformation",26);
}
static void TestImportBinding(int plus)
{
    uint8_t f[FILE_BYTES], image[0x2000];
    RC95LDR_PE_INFO info;
    IMPORT_TEST_CONTEXT test;
    uint64_t expected=plus?0x1234567887654321ull:0x87654321u;
    memset(&test,0,sizeof(test));
    test.Address=expected;
    MakePeWithImport(f,plus);
    assert(Rc95LdrPeValidate(f,sizeof(f),0,&info)==Rc95LdrSuccess);
    {
        RC95LDR_STATUS status=Rc95LdrPeMapEx(f,sizeof(f),&info,image,sizeof(image),
            info.PreferredImageBase,TestImportResolver,&test);
        if(status!=Rc95LdrSuccess)
            fprintf(stderr,"PE import map failed: format=%u status=%u calls=%u import=%x/%x\n",
                (unsigned)info.Format,(unsigned)status,test.Calls,
                info.ImportRva,info.ImportSize);
        assert(status==Rc95LdrSuccess);
    }
    assert(test.Calls==1);
    assert((plus?Rc95LdrReadU64(image+0x1180):Rc95LdrReadU32(image+0x1180))==expected);
}
static RC95LDR_STATUS OrdinalImportResolver(void *context,const char *module,
    const char *symbol,uint16_t ordinal,int byOrdinal,uint64_t *address)
{
    unsigned *calls=(unsigned *)context;
    ++*calls;
    if(strcmp(module,"hal.dll")!=0 || symbol || !byOrdinal || ordinal!=7)
        return Rc95LdrNotFound;
    *address=0x12345678u;
    return Rc95LdrSuccess;
}
static void TestOrdinalImport(void)
{
    uint8_t f[FILE_BYTES], image[0x2000];
    RC95LDR_PE_INFO info;
    unsigned calls=0;
    MakePeWithImport(f,0);
    W32(f+0x360,0x80000007u);
    assert(Rc95LdrPeValidate(f,sizeof(f),0,&info)==Rc95LdrSuccess);
    assert(Rc95LdrPeMapEx(f,sizeof(f),&info,image,sizeof(image),
        info.PreferredImageBase,OrdinalImportResolver,&calls)==Rc95LdrSuccess);
    assert(calls==1 && Rc95LdrReadU32(image+0x1180)==0x12345678u);
}

static RC95LDR_STATUS RejectImportResolver(void *context,const char *module,
    const char *symbol,uint16_t ordinal,int byOrdinal,uint64_t *address)
{
    (void)context;(void)module;(void)symbol;(void)ordinal;(void)byOrdinal;(void)address;
    return Rc95LdrNotFound;
}
static void TestImportFailures(void)
{
    uint8_t f[FILE_BYTES], image[0x2000];
    RC95LDR_PE_INFO info;
    MakePeWithImport(f,0);
    assert(Rc95LdrPeValidate(f,sizeof(f),0,&info)==Rc95LdrSuccess);
    assert(Rc95LdrPeMap(f,sizeof(f),&info,image,sizeof(image),
        info.PreferredImageBase)==Rc95LdrUnsupported);
    assert(Rc95LdrPeMapEx(f,sizeof(f),&info,image,sizeof(image),
        info.PreferredImageBase,RejectImportResolver,NULL)==Rc95LdrNotFound);
    /* The import descriptor's module name must be terminated in-image. */
    W32(f+0x30c,0x2000);
    assert(Rc95LdrPeMapEx(f,sizeof(f),&info,image,sizeof(image),
        info.PreferredImageBase,RejectImportResolver,NULL)==Rc95LdrInvalidImage);
}

static void TestUnsupportedImports(void)
{
    uint8_t f[FILE_BYTES], image[0x2000]; RC95LDR_PE_INFO info;
    MakePe(f,0);
    W32(f+NT_OFF+24+96+8,0x1100);
    W32(f+NT_OFF+24+96+12,20);
    assert(Rc95LdrPeValidate(f,sizeof(f),0,&info)==Rc95LdrSuccess);
    assert(Rc95LdrPeMap(f,sizeof(f),&info,image,sizeof(image),
        info.PreferredImageBase)==Rc95LdrUnsupported);
}
int main(void)
{
    TestValidAndMap(0); TestValidAndMap(1);
    TestRelocations(0); TestRelocations(1);
    TestMalformed(); TestUnsupportedImports();
    TestResourceOnlyImage(0); TestResourceOnlyImage(1);
    TestImportBinding(0); TestImportBinding(1); TestOrdinalImport();
    TestImportFailures();
    TestKernelLoadIntegration();
    puts("RC95LDR PE tests passed");
    return 0;
}
