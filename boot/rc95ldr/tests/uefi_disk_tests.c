#include "rc95ldruedisk.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static uint8_t Disk[128*512];
static int PartitionHandle;
static uint32_t crc32(const uint8_t *p,size_t n)
{
    uint32_t c=0xffffffffu; size_t i; unsigned b;
    for(i=0;i<n;i++){c^=p[i];for(b=0;b<8;b++)c=(c>>1)^((c&1)?0xedb88320u:0);}
    return ~c;
}
static void p16(uint8_t*p,uint16_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void p32(uint8_t*p,uint32_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);}
static void p64(uint8_t*p,uint64_t v){p32(p,(uint32_t)v);p32(p+4,(uint32_t)(v>>32));}
static RC95LDR_STATUS read_blocks(void *p,uint32_t id,uint64_t lba,size_t bytes,void *out)
{(void)p;(void)id;if(lba>128||bytes>sizeof(Disk)-(size_t)lba*512)return Rc95LdrOutOfBounds;memcpy(out,Disk+(size_t)lba*512,bytes);return Rc95LdrSuccess;}
static RC95LDR_STATUS count_handles(void *c,size_t *n){(void)c;*n=2;return Rc95LdrSuccess;}
static RC95LDR_STATUS get_handle(void *c,size_t i,void **h){(void)c;if(i==0)*h=Disk;else if(i==1)*h=&PartitionHandle;else return Rc95LdrNotFound;return Rc95LdrSuccess;}
static RC95LDR_STATUS open_io(void *c,void *h,RC95LDR_UEFI_BLOCK_IO *io)
{(void)c;if(h!=Disk&&h!=&PartitionHandle)return Rc95LdrNotFound;memset(io,0,sizeof(*io));io->Protocol=Disk;io->Media.MediaPresent=1;io->Media.BlockSize=512;io->Media.LastBlock=(h==Disk)?127:60;io->Media.LogicalPartition=(h==&PartitionHandle);io->ReadBlocks=read_blocks;return Rc95LdrSuccess;}
static uint8_t PartitionPath[46];
static RC95LDR_STATUS get_path(void *c,void *h,const uint8_t **p,size_t *n)
{(void)c;if(h!=&PartitionHandle)return Rc95LdrNotFound;*p=PartitionPath;*n=sizeof(PartitionPath);return Rc95LdrSuccess;}

static int test_device_path(void)
{
    uint8_t *path=PartitionPath,guid[16];uint32_t pn;uint64_t start,count;
    memset(path,0,sizeof(PartitionPath));
    path[0]=4;path[1]=1;p16(path+2,42);p32(path+4,3);p64(path+8,40);p64(path+16,61);
    path[24]=1;path[40]=2;path[42]=0x7f;path[43]=0xff;p16(path+44,4);
    if(Rc95LdrParseUefiDevicePath(path,sizeof(PartitionPath),&pn,&start,&count,guid)!=Rc95LdrSuccess)return 0;
    return pn==3&&start==40&&count==61&&guid[0]==1;
}
int main(void)
{
    uint8_t *H=Disk+512,*E=Disk+1024,*Scratch;
    RC95LDR_UEFI_FIRMWARE_OPS ops;RC95LDR_PARTITION_INFO parts[8];size_t count=0;
    size_t scratchSize=512+128*128;RC95LDR_STATUS s;
    memset(Disk,0,sizeof(Disk));
    p64(H,0x5452415020494645ULL);p32(H+8,0x00010000);p32(H+12,92);
    p64(H+24,1);p64(H+32,127);p64(H+40,34);p64(H+48,120);H[56]=0x55;
    p64(H+72,2);p32(H+80,128);p32(H+84,128);
    /* EFI System Partition type GUID, followed by a nonzero unique GUID. */
    {const uint8_t esp[16]={0x28,0x73,0x2a,0xc1,0xf8,0xf1,0xd2,0x11,0xba,0x4b,0,0xa0,0xc9,0x3e,0xc9,0x3b};memcpy(E,esp,16);}
    E[16]=1;E[32]=40;E[40]=100;
    p32(H+88,crc32(E,128*128));p32(H+16,crc32(H,92));
    memset(&ops,0,sizeof(ops));ops.GetHandleCount=count_handles;ops.GetHandleAt=get_handle;
    ops.OpenBlockIo=open_io;ops.GetDevicePath=get_path;
    Scratch=malloc(scratchSize);if(!Scratch)return 2;
    s=Rc95LdrUefiEnumerateGptPartitions(&ops,parts,8,&count,Scratch,scratchSize);
    free(Scratch);
    if(s!=Rc95LdrSuccess||count!=1||parts[0].Style!=Rc95LdrPartitionGpt||
       parts[0].StartLba!=40||parts[0].BlockCount!=61||parts[0].DiskGuid[0]!=0x55||
       parts[0].FileSystem!=Rc95LdrFsUnknown ||
       parts[0].FirmwareHandleIndex!=1 ||
       parts[0].ParentDiskHandleIndex!=0 ||
       parts[0].FirmwarePartitionNumber!=3)
        return 1;
    if(!test_device_path())return 1;
    puts("RC95LDR UEFI GPT/device-path tests passed");
    return 0;
}
