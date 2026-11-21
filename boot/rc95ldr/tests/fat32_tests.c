#include "rc95ldr_fat32.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define SECTORS 67000u
#define FAT_START 32u
#define FAT_SIZE 520u
#define DATA_START (FAT_START+2u*FAT_SIZE)
static uint8_t *Image;
static void p16(uint8_t*p,uint16_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void p32(uint8_t*p,uint32_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);}
static RC95LDR_STATUS Read(void*c,uint64_t lba,uint32_t n,void*b,size_t cap)
{size_t bytes=(size_t)n*512;(void)c;if(lba>SECTORS||n>SECTORS-lba||bytes>cap)return Rc95LdrOutOfBounds;memcpy(b,Image+(size_t)lba*512,bytes);return Rc95LdrSuccess;}
static void fat(uint32_t cluster,uint32_t next){p32(Image+(size_t)FAT_START*512+cluster*4,next);}
static uint8_t checksum(const uint8_t n[11]){uint8_t s=0;unsigned i;for(i=0;i<11;i++)s=(uint8_t)(((s&1)?0x80:0)+(s>>1)+n[i]);return s;}
static void entry(uint8_t*e,const char name[11],uint8_t attr,uint32_t cl,uint32_t size)
{memset(e,0,32);memcpy(e,name,11);e[11]=attr;p16(e+20,(uint16_t)(cl>>16));p16(e+26,(uint16_t)cl);p32(e+28,size);}
int main(void)
{
    RC95LDR_BLOCK_DEVICE dev;RC95LDR_FAT32 fat32;RC95LDR_VOLUME volume;
    const RC95LDR_FILESYSTEM *fs;void *h=NULL;uint64_t size=0;char data[4];
    uint8_t *b,*root,*efi,*ms,*boot,*bcd,*lfn;uint8_t shortMs[11]={'M','I','C','R','O','S','~','1',' ',' ',' '};
    static const uint16_t name[26]={'M','i','c','r','o','s','o','f','t','S','y','s','t','e','m',0,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff};
    unsigned i;
    Image=calloc(SECTORS,512);if(!Image)return 2;
    b=Image;p16(b+11,512);b[13]=1;p16(b+14,32);b[16]=2;p16(b+17,0);p16(b+19,0);p16(b+22,0);p32(b+32,SECTORS);p32(b+36,FAT_SIZE);p32(b+44,2);p16(b+510,0xaa55);
    fat(0,0x0ffffff8);fat(1,0x0fffffff);fat(2,0x0fffffff);fat(3,0x0fffffff);fat(4,0x0fffffff);fat(5,0x0fffffff);fat(6,0x0fffffff);
    root=Image+(size_t)(DATA_START+0)*512;efi=Image+(size_t)(DATA_START+1)*512;
    ms=Image+(size_t)(DATA_START+2)*512;boot=Image+(size_t)(DATA_START+3)*512;
    bcd=Image+(size_t)(DATA_START+4)*512;
    entry(root,"EFI        ",0x10,3,0);
    lfn=efi;memset(lfn,0xff,64);lfn[0]=0x42;lfn[11]=0x0f;lfn[12]=0;lfn[13]=checksum(shortMs);p16(lfn+26,0);
    {const unsigned off[13]={1,3,5,7,9,14,16,18,20,22,24,28,30};for(i=0;i<13;i++)p16(lfn+off[i],name[13+i]);}
    lfn+=32;lfn[0]=0x01;lfn[11]=0x0f;lfn[12]=0;lfn[13]=checksum(shortMs);p16(lfn+26,0);
    {const unsigned off[13]={1,3,5,7,9,14,16,18,20,22,24,28,30};for(i=0;i<13;i++)p16(lfn+off[i],name[i]);}
    entry(efi+64,(const char*)shortMs,0x10,4,0);
    entry(ms,"BOOT       ",0x10,5,0);
    entry(boot,"BCD        ",0x20,6,4);
    memcpy(bcd,"HIVE",4);
    memset(&dev,0,sizeof(dev));dev.BlockSize=512;dev.BlockCount=SECTORS;dev.Read=Read;
    if(Rc95LdrFat32Mount(&fat32,&dev,0)!=Rc95LdrSuccess)return 1;
    if(Rc95LdrFat32BindVolume(&fat32,&volume)!=Rc95LdrSuccess)return 1;
    fs=volume.FileSystem;
    if(fs->OpenRead(fs->FileSystemContext,"\\EFI\\MicrosoftSystem\\Boot\\BCD",&h,&size)!=Rc95LdrSuccess||size!=4)return 1;
    if(fs->ReadAt(fs->FileSystemContext,h,0,data,4)!=Rc95LdrSuccess||memcmp(data,"HIVE",4))return 1;
    fs->Close(fs->FileSystemContext,h);Rc95LdrFat32Unmount(&fat32);free(Image);
    puts("RC95LDR FAT32 long-name read tests passed");return 0;
}
