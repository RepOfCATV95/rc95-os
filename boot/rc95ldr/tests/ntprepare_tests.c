#include "rc95ldrntprepare.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static void Put32(uint8_t*p,uint32_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);}
static uint32_t HeaderXor(const uint8_t*p){uint32_t x=0;size_t i;for(i=0;i<0x1fc;i+=4)x^=(uint32_t)p[i]|((uint32_t)p[i+1]<<8)|((uint32_t)p[i+2]<<16)|((uint32_t)p[i+3]<<24);return x==0xffffffffu?0xfffffffeu:(x?x:1u);}
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
int main(void){TestDriverOrder();TestHiveHeader();puts("RC95LDR NT preparation tests passed");return 0;}
