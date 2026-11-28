#include "rc95ldrntprepare.h"
#include "rc95ldrnativeprepare.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static void Put32(uint8_t*p,uint32_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);}
static uint32_t HeaderXor(const uint8_t*p){uint32_t x=0;size_t i;for(i=0;i<0x1fc;i+=4)x^=(uint32_t)p[i]|((uint32_t)p[i+1]<<8)|((uint32_t)p[i+2]<<16)|((uint32_t)p[i+3]<<24);return x==0xffffffffu?0xfffffffeu:(x?x:1u);}
typedef struct _RESOURCE_VALIDATION_CONTEXT {
    uint32_t Calls;
    RC95LDR_STATUS Result;
} RESOURCE_VALIDATION_CONTEXT;
static RC95LDR_STATUS ValidateResourceAllocation(void *opaque,
    uint64_t virtualBase,uint64_t physicalBase,uint64_t imageSize,
    uint64_t allocationSize)
{
    RESOURCE_VALIDATION_CONTEXT *context=(RESOURCE_VALIDATION_CONTEXT *)opaque;
    ++context->Calls;
    if (virtualBase!=0x400000u || physicalBase!=0x400000u ||
        imageSize!=0x18000u || allocationSize!=0x18000u)
        return Rc95LdrInvalidImage;
    return context->Result;
}
static void TestBootResourceModuleValidation(void)
{
    RC95LDR_LOADED_MODULE module;
    RESOURCE_VALIDATION_CONTEXT context;
    memset(&module,0,sizeof(module));
    memset(&context,0,sizeof(context));
    context.Result=Rc95LdrSuccess;
    module.ImageBase=0x400000u;
    module.ImageSize=0x18000u;
    module.EntryPoint=0;
    module.PhysicalBase=0x400000u;
    module.PhysicalSize=0x18000u;
    module.Flags=RC95LDR_MODULE_RESOURCE;
    module.Path="\\SystemRoot\\System32\\bootres.dll";
    assert(Rc95LdrValidateBootResourceModule(&module,
        ValidateResourceAllocation,&context)==Rc95LdrSuccess);
    assert(context.Calls==1);

    module.EntryPoint=0x1000u;
    assert(Rc95LdrValidateBootResourceModule(&module,
        ValidateResourceAllocation,&context)==Rc95LdrInvalidImage);
    module.EntryPoint=0;
    module.Flags=RC95LDR_MODULE_BOOT_DRIVER;
    assert(Rc95LdrValidateBootResourceModule(&module,
        ValidateResourceAllocation,&context)==Rc95LdrInvalidImage);
    module.Flags=RC95LDR_MODULE_RESOURCE;
    module.Path="\\SystemRoot\\System32\\notbootres.dll";
    assert(Rc95LdrValidateBootResourceModule(&module,
        ValidateResourceAllocation,&context)==Rc95LdrInvalidImage);
    module.Path="\\SystemRoot\\System32\\bootres.dll";
    module.PhysicalBase+=1;
    assert(Rc95LdrValidateBootResourceModule(&module,
        ValidateResourceAllocation,&context)==Rc95LdrInvalidImage);
    module.PhysicalBase=0x400000u;
    module.PhysicalSize=0x10000u;
    assert(Rc95LdrValidateBootResourceModule(&module,
        ValidateResourceAllocation,&context)==Rc95LdrInvalidImage);
    module.PhysicalSize=0x18000u;
    assert(Rc95LdrValidateBootResourceModule(&module,NULL,&context)==
        Rc95LdrUnsupported);
    context.Result=Rc95LdrOutOfBounds;
    assert(Rc95LdrValidateBootResourceModule(&module,
        ValidateResourceAllocation,&context)==Rc95LdrInvalidImage);
}
static void TestDriverOrder(void)
{
 RC95LDR_BOOT_DRIVER_NODE n[4]={{0,10},{1,11},{2,12},{1,13}};uint32_t o[4];
 assert(Rc95LdrOrderBootDrivers(n,4,o,4)==Rc95LdrSuccess);
 assert(o[0]==10&&o[1]==11&&o[2]==12&&o[3]==13);
 n[0].Dependencies=4;assert(Rc95LdrOrderBootDrivers(n,4,o,4)==Rc95LdrCorruptData);
 n[0].Dependencies=16;assert(Rc95LdrOrderBootDrivers(n,4,o,4)==Rc95LdrCorruptData);
}
static void TestHiveHeader(void)
{
 uint8_t h[0x2000];uint32_t n=0;memset(h,0,sizeof(h));
 Put32(h,0x66676572);Put32(h+4,1);Put32(h+8,1);Put32(h+0x14,1);
 Put32(h+0x18,5);Put32(h+0x1c,0);Put32(h+0x20,1);Put32(h+0x28,0x1000);
 Put32(h+0x1fc,HeaderXor(h));
 assert(Rc95LdrValidateSystemHive(h,sizeof(h),&n)==Rc95LdrSuccess&&n==0x1000);
 h[0x30]^=1;assert(Rc95LdrValidateSystemHive(h,sizeof(h),&n)==Rc95LdrCorruptData);
}
int main(void){TestDriverOrder();TestHiveHeader();TestBootResourceModuleValidation();puts("RC95LDR NT preparation tests passed");return 0;}
