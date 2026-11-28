
#include "rc95ldrmodule.h"
#include <assert.h>
#include <stddef.h>
#include <string.h>

static RC95LDR_STATUS RC95LDR_MODULE_ABI Open(void *c,const uint16_t*p,RC95LDR_MODULE_FILE*f)
{(void)c;(void)p;(void)f;return Rc95LdrSuccess;}
static RC95LDR_STATUS RC95LDR_MODULE_ABI ReadFile(void*c,const RC95LDR_MODULE_FILE*f,uint64_t o,void*b,size_t n)
{(void)c;(void)f;(void)o;(void)b;(void)n;return Rc95LdrSuccess;}
static void RC95LDR_MODULE_ABI Close(void*c,RC95LDR_MODULE_FILE*f){(void)c;(void)f;}
static RC95LDR_STATUS RC95LDR_MODULE_ABI Alloc(void*c,uint64_t n,uint64_t a,uint64_t*p)
{(void)c;(void)n;(void)a;(void)p;return Rc95LdrSuccess;}
static void RC95LDR_MODULE_ABI Free(void*c,uint64_t p,uint64_t n){(void)c;(void)p;(void)n;}
static RC95LDR_STATUS RC95LDR_MODULE_ABI Extent(void*c,uint64_t p,uint64_t v,uint64_t n,uint64_t a)
{(void)c;(void)p;(void)v;(void)n;(void)a;return Rc95LdrSuccess;}
static RC95LDR_STATUS RC95LDR_MODULE_ABI Quiesce(void*c){(void)c;return Rc95LdrSuccess;}
static RC95LDR_STATUS RC95LDR_MODULE_ABI DeviceCount(void*c,uint32_t*n){(void)c;*n=0;return Rc95LdrSuccess;}
static RC95LDR_STATUS RC95LDR_MODULE_ABI DeviceInfo(void*c,uint32_t i,RC95LDR_MODULE_BLOCK_DEVICE_INFO*n)
{(void)c;(void)i;(void)n;return Rc95LdrNotFound;}
static RC95LDR_STATUS RC95LDR_MODULE_ABI PartitionCount(void*c,uint32_t i,uint32_t*n)
{(void)c;(void)i;*n=0;return Rc95LdrSuccess;}
static RC95LDR_STATUS RC95LDR_MODULE_ABI PartitionInfo(void*c,uint32_t i,uint32_t p,RC95LDR_MODULE_PARTITION_INFO*n)
{(void)c;(void)i;(void)p;(void)n;return Rc95LdrNotFound;}
static RC95LDR_STATUS RC95LDR_MODULE_ABI ReadBlocks(void*c,uint32_t i,uint64_t l,uint32_t n,void*b,size_t z)
{(void)c;(void)i;(void)l;(void)n;(void)b;(void)z;return Rc95LdrSuccess;}

static void InitV1(RC95LDR_MODULE_SERVICES *s)
{
    memset(s,0,sizeof(*s)); s->Signature=RC95LDR_MODULE_SERVICES_SIGNATURE;
    s->StructureSize=sizeof(*s); s->Version=RC95LDR_MODULE_SERVICES_VERSION;
    s->Firmware=Rc95LdrModuleBios; s->Architecture=Rc95LdrModuleI386;
    s->OpenFile=Open;s->ReadFileAt=ReadFile;s->CloseFile=Close;
    s->AllocatePages=Alloc;s->FreePages=Free;s->ValidateAllocationExtent=Extent;s->Quiesce=Quiesce;
}
int main(void)
{
    RC95LDR_MODULE_SERVICES_V2 v2;
    RC95LDR_MODULE_BLOCK_DEVICE_INFO d;
    RC95LDR_MODULE_PARTITION_INFO p;
    RC95LDR_MODULE_SERVICES v1;
    InitV1(&v1);
    assert(sizeof(RC95LDR_MODULE_SERVICES)==(sizeof(void*)==8?88u:56u));
    assert(Rc95LdrValidateModuleServices(&v1,sizeof(v1),Rc95LdrModuleBios,Rc95LdrModuleI386)==Rc95LdrSuccess);
    assert(Rc95LdrValidateModuleServices(&v1,sizeof(v1)-1,Rc95LdrModuleBios,Rc95LdrModuleI386)==Rc95LdrInvalidParameter);
    memset(&v2,0,sizeof(v2)); v2.V1=v1;
    v2.V1.StructureSize=sizeof(v2); v2.V1.Version=RC95LDR_MODULE_SERVICES_V2_VERSION;
    v2.GetBlockDeviceCount=DeviceCount;v2.GetBlockDeviceInfo=DeviceInfo;
    v2.GetPartitionCount=PartitionCount;v2.GetPartitionInfo=PartitionInfo;v2.ReadBlocks=ReadBlocks;
    assert(Rc95LdrValidateModuleServicesV2(&v2,sizeof(v2),Rc95LdrModuleBios,Rc95LdrModuleI386)==Rc95LdrSuccess);
    assert(Rc95LdrValidateModuleServices(&v2.V1,sizeof(v2),Rc95LdrModuleBios,Rc95LdrModuleI386)==Rc95LdrInvalidParameter);
    assert(Rc95LdrValidateModuleServicesV2(&v2,sizeof(v2)-1,Rc95LdrModuleBios,Rc95LdrModuleI386)==Rc95LdrInvalidParameter);
    v2.ReadBlocks=NULL;
    assert(Rc95LdrValidateModuleServicesV2(&v2,sizeof(v2),Rc95LdrModuleBios,Rc95LdrModuleI386)==Rc95LdrInvalidParameter);
    v2.ReadBlocks=ReadBlocks; v2.V1.Version=1;
    assert(Rc95LdrValidateModuleServicesV2(&v2,sizeof(v2),Rc95LdrModuleBios,Rc95LdrModuleI386)==Rc95LdrInvalidParameter);

    memset(&d,0,sizeof(d)); d.StructureSize=sizeof(d);d.Version=1;d.BlockCount=100000;d.BlockSize=512;d.Flags=RC95LDR_MODULE_BLOCK_DEVICE_READ_ONLY;
    assert(Rc95LdrValidateModuleBlockDeviceInfo(&d,sizeof(d))==Rc95LdrSuccess);
    d.BlockSize=513; assert(Rc95LdrValidateModuleBlockDeviceInfo(&d,sizeof(d))==Rc95LdrInvalidParameter);
    d.BlockSize=512;d.Reserved=1;assert(Rc95LdrValidateModuleBlockDeviceInfo(&d,sizeof(d))==Rc95LdrInvalidParameter);
    d.Reserved=0;
    memset(&p,0,sizeof(p));p.StructureSize=sizeof(p);p.Version=1;p.Scheme=RC95LDR_MODULE_PARTITION_GPT;p.StartLba=2048;p.BlockCount=4096;p.TypeGuid[0]=1;p.UniqueGuid[0]=1;
    assert(Rc95LdrValidateModulePartitionInfo(&p,sizeof(p),&d)==Rc95LdrSuccess);
    p.StartLba=99999;p.BlockCount=2;assert(Rc95LdrValidateModulePartitionInfo(&p,sizeof(p),&d)==Rc95LdrOutOfBounds);
    p.Scheme=RC95LDR_MODULE_PARTITION_MBR;p.StartLba=2048;p.BlockCount=4096;p.MbrType=0x07;memset(p.TypeGuid,0,sizeof(p.TypeGuid));
    assert(Rc95LdrValidateModulePartitionInfo(&p,sizeof(p),&d)==Rc95LdrSuccess);
    p.Reserved[0]=1;assert(Rc95LdrValidateModulePartitionInfo(&p,sizeof(p),&d)==Rc95LdrInvalidParameter);
    return 0;
}
