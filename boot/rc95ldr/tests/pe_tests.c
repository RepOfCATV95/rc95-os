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
    status=LoadAndBootWindows(&boot,&platform,&request,&result);
    assert(status==Rc95LdrInvalidParameter);
    assert(fake.OpenCount==0 && fake.AllocationCount==0);
    request.BootEntryKind=Rc95LdrEntryRc95Winload;
    memset(&result,0,sizeof(result));
    result.StructureSize=sizeof(result);
    result.Version=RC95LDR_WINLOAD_ABI_VERSION;
    Rc95LdrInitializeContext(&boot,&fake);
    status=LoadAndBootWindows(&boot,&platform,&request,&result);
    assert(status==Rc95LdrUnsupported);
    assert(boot.CurrentStage==Rc95LdrStageOsPrepare);
    assert(result.ImageCount==1 && result.KernelImageSize==0x2000);
    assert(result.KernelImageBase!=0);
    assert(result.KernelEntryPoint==result.KernelImageBase+0x1000);
    while(fake.AllocationCount)
        FakeFree(&fake,fake.Address[0],0);
}

static void TestValidAndMap(int plus)
{
    uint8_t file[FILE_BYTES], image[0x2000];
    RC95LDR_PE_INFO info;
    MakePe(file,plus);
    assert(Rc95LdrPeValidate(file,sizeof(file),0,&info)==Rc95LdrSuccess);
    assert(info.Format==(plus?Rc95LdrPe32Plus:Rc95LdrPe32));
    assert(info.EntryPointRva==0x1000);
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
    MakePe(f,0); W32(f+NT_OFF+24+96+5*8,0x1fff);
    W32(f+NT_OFF+24+96+5*8+4,0x100);
    assert(Rc95LdrPeValidate(f,sizeof(f),0,&info)==Rc95LdrInvalidImage);
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
    TestMalformed(); TestUnsupportedImports(); TestKernelLoadIntegration();
    puts("RC95LDR PE tests passed");
    return 0;
}
