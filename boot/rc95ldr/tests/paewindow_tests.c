#include "rc95ldrrc95nativeprovider.h"
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
static void InitCpu(RC95LDR_RC95_CPU_INFO *c){memset(c,0,sizeof(*c));c->PagingMode=RC95LDR_CPU_PAGING_I386_PAE;c->FeatureFlags=RC95LDR_CPU_FEATURE_PAE|RC95LDR_CPU_FEATURE_CX8;c->PhysicalAddressBits=36;}
int main(void){
 RC95LDR_RC95_CPU_INFO cpu;RC95LDR_PAE_VIRTUAL_WINDOW w={0xC0000000ULL,0x20000000ULL};
 RC95LDR_PAE_PHYSICAL_MAPPING m[RC95LDR_PAE_WINDOW_MAPPING_MAX];
 RC95LDR_FIRMWARE_MEMORY_RANGE r;RC95LDR_LOADED_MODULE mod;RC95LDR_NATIVE_ALLOCATION a;size_t n=0;
 InitCpu(&cpu);memset(&r,0,sizeof(r));r.Base=0x100000000ULL;r.Length=0x20000000ULL;r.Kind=Rc95LdrFirmwareMemoryAvailable;
 assert(Rc95LdrBuildI386PaeWindowMappings(&cpu,&w,NULL,0,NULL,0,&r,1,m,RC95LDR_PAE_WINDOW_MAPPING_MAX,&n)==Rc95LdrSuccess);
 assert(n==1&&m[0].PhysicalBase==0x100000000ULL&&m[0].VirtualBase==w.VirtualBase&&m[0].Length==w.Length);
 memset(&mod,0,sizeof(mod));mod.ImageBase=0xC1000000ULL;mod.ImageSize=0x10000;
 assert(Rc95LdrBuildI386PaeWindowMappings(&cpu,&w,&mod,1,NULL,0,&r,1,m,RC95LDR_PAE_WINDOW_MAPPING_MAX,&n)==Rc95LdrInvalidImage);
 memset(&mod,0,sizeof(mod));memset(&a,0,sizeof(a));a.PhysicalBase=0xC1000000ULL;a.Length=0x10000;
 assert(Rc95LdrBuildI386PaeWindowMappings(&cpu,&w,NULL,0,&a,1,&r,1,m,RC95LDR_PAE_WINDOW_MAPPING_MAX,&n)==Rc95LdrInvalidImage);
 w.Length=0x10000000ULL;
 assert(Rc95LdrBuildI386PaeWindowMappings(&cpu,&w,NULL,0,NULL,0,&r,1,m,RC95LDR_PAE_WINDOW_MAPPING_MAX,&n)==Rc95LdrBufferTooSmall);
 w.VirtualBase=0x100000000ULL;w.Length=0x1000;
 assert(Rc95LdrBuildI386PaeWindowMappings(&cpu,&w,NULL,0,NULL,0,&r,1,m,RC95LDR_PAE_WINDOW_MAPPING_MAX,&n)==Rc95LdrInvalidParameter);
 TestInstallsMappingsIntoPaeTables();
 return 0;
}