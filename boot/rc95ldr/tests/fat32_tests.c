#include "rc95ldr_fat32.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define SECTORS 67000u
#define FAT_START 32u
#define FAT_SIZE 520u
#define DATA_START (FAT_START+2u*FAT_SIZE)
static uint8_t *Image;
static int FailWrite;
static unsigned WriteCount,FailWriteAt;
static void p16(uint8_t*p,uint16_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void p32(uint8_t*p,uint32_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);}
static RC95LDR_STATUS Read(void*c,uint64_t lba,uint32_t n,void*b,size_t cap)
{size_t bytes=(size_t)n*512;(void)c;if(lba>SECTORS||n>SECTORS-lba||bytes>cap)return Rc95LdrOutOfBounds;memcpy(b,Image+(size_t)lba*512,bytes);return Rc95LdrSuccess;}
static RC95LDR_STATUS ReadHuge(void*c,uint64_t lba,uint32_t n,void*b,size_t cap)
{(void)c;if(lba!=0||n!=1||cap<512)return Rc95LdrOutOfBounds;memcpy(b,Image,512);return Rc95LdrSuccess;}
static RC95LDR_STATUS Write(void*c,uint64_t lba,uint32_t n,const void*b,size_t bytes)
{(void)c;WriteCount++;if(FailWrite||(FailWriteAt&&WriteCount==FailWriteAt))return Rc95LdrIoError;if(lba>SECTORS||n>SECTORS-lba||bytes!=(size_t)n*512)return Rc95LdrOutOfBounds;memcpy(Image+(size_t)lba*512,b,bytes);return Rc95LdrSuccess;}
static uint32_t get32(const uint8_t*p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static void fat(uint32_t cluster,uint32_t next){p32(Image+(size_t)FAT_START*512+cluster*4,next);}
static uint8_t checksum(const uint8_t n[11]){uint8_t s=0;unsigned i;for(i=0;i<11;i++)s=(uint8_t)(((s&1)?0x80:0)+(s>>1)+n[i]);return s;}
static void entry(uint8_t*e,const char name[11],uint8_t attr,uint32_t cl,uint32_t size)
{memset(e,0,32);memcpy(e,name,11);e[11]=attr;p16(e+20,(uint16_t)(cl>>16));p16(e+26,(uint16_t)cl);p32(e+28,size);}
static int reject_bpb16(RC95LDR_BLOCK_DEVICE *dev,RC95LDR_FAT32 *volume,
                        unsigned offset,uint16_t value)
{
    uint16_t saved=(uint16_t)(Image[offset]|((uint16_t)Image[offset+1]<<8));
    RC95LDR_STATUS status;
    p16(Image+offset,value);
    status=Rc95LdrFat32Mount(volume,dev,0);
    p16(Image+offset,saved);
    return status==Rc95LdrCorruptData&&!volume->Mounted;
}
static int reject_bpb32(RC95LDR_BLOCK_DEVICE *dev,RC95LDR_FAT32 *volume,
                        unsigned offset,uint32_t value)
{
    uint32_t saved=(uint32_t)Image[offset]|((uint32_t)Image[offset+1]<<8)|
                   ((uint32_t)Image[offset+2]<<16)|((uint32_t)Image[offset+3]<<24);
    RC95LDR_STATUS status;
    p32(Image+offset,value);
    status=Rc95LdrFat32Mount(volume,dev,0);
    p32(Image+offset,saved);
    return status==Rc95LdrCorruptData&&!volume->Mounted;
}
static int reject_bpb8(RC95LDR_BLOCK_DEVICE *dev,RC95LDR_FAT32 *volume,
                       unsigned offset,uint8_t value)
{
    uint8_t saved=Image[offset];
    RC95LDR_STATUS status;
    Image[offset]=value;
    status=Rc95LdrFat32Mount(volume,dev,0);
    Image[offset]=saved;
    return status==Rc95LdrCorruptData&&!volume->Mounted;
}
int main(void)
{
    RC95LDR_BLOCK_DEVICE dev;RC95LDR_FAT32 fat32;RC95LDR_VOLUME volume;
    const RC95LDR_FILESYSTEM *fs;void *h=NULL;uint64_t size=0;char data[4];
    uint8_t *b,*root,*efi,*ms,*boot,*bcd,*lfn;uint8_t shortMs[11]={'M','I','C','R','O','S','~','1',' ',' ',' '};
    static const uint16_t name[26]={'M','i','c','r','o','s','o','f','t','S','y','s','t','e','m',0,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff};
    unsigned i;
    Image=calloc(SECTORS,512);if(!Image)return 2;
    b=Image;p16(b+11,512);b[13]=1;p16(b+14,32);b[16]=2;p16(b+17,0);p16(b+19,0);p16(b+22,0);p32(b+32,SECTORS);p32(b+36,FAT_SIZE);p32(b+44,2);p16(b+48,1);p16(b+50,6);p16(b+510,0xaa55);
    /* Valid primary and backup FSInfo sectors with deliberately exact hints. */
    p32(Image+512,0x41615252u);p32(Image+512+484,0x61417272u);
    p32(Image+512+488,1234u);p32(Image+512+492,9u);p32(Image+512+508,0xaa550000u);
    p32(Image+7u*512u,0x41615252u);p32(Image+7u*512u+484,0x61417272u);
    p32(Image+7u*512u+488,1234u);p32(Image+7u*512u+492,9u);p32(Image+7u*512u+508,0xaa550000u);
    fat(0,0x0ffffff8);fat(1,0x0fffffff);fat(2,0x0fffffff);fat(3,0x0fffffff);fat(4,0x0fffffff);fat(5,0x0fffffff);fat(6,7);fat(7,0x0fffffff);
    p32(Image+(size_t)(FAT_START+FAT_SIZE)*512,0x0ffffff8);
    p32(Image+(size_t)(FAT_START+FAT_SIZE)*512+4,0x0fffffff);
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
    entry(boot,"BCD        ",0x20,6,513);
    memcpy(bcd,"HIVE",4);
    entry(root+32,"HIBERFILSYS",0x20,9,513);
    fat(9,10);fat(10,0x0fffffff);
    p32(Image+(size_t)(FAT_START+FAT_SIZE)*512+9*4,10);
    p32(Image+(size_t)(FAT_START+FAT_SIZE)*512+10*4,0x0fffffff);
    memset(&dev,0,sizeof(dev));dev.BlockSize=512;dev.BlockCount=SECTORS;dev.Read=Read;dev.Write=Write;
    if(Rc95LdrFat32Mount(&fat32,&dev,0)!=Rc95LdrSuccess)return 1;
    if(Rc95LdrFat32BindVolume(&fat32,&volume)!=Rc95LdrSuccess)return 1;
    fs=volume.FileSystem;
    if(fs->DeleteFile(fs->FileSystemContext,"\\\\..\\\\hiberfil.sys")!=Rc95LdrInvalidParameter||
       fs->DeleteFile(fs->FileSystemContext,"\\\\hiberfil.sys\\\\")!=Rc95LdrInvalidParameter||
       fs->DeleteFile(fs->FileSystemContext,"\\\\hiber*.sys")!=Rc95LdrInvalidParameter||
       WriteCount!=0)return 1;
    root[32+11]=0x21; /* read-only + archive */
    if(fs->DeleteFile(fs->FileSystemContext,"\\\\hiberfil.sys")!=Rc95LdrUnsupported||
       WriteCount!=0)return 1;
    root[32+11]=0x20;
    if(fs->OpenRead(fs->FileSystemContext,"\\EFI\\MicrosoftSystem\\Boot\\BCD",&h,&size)!=Rc95LdrSuccess||size!=513)return 1;
    /* A forged handle and a handle from another volume must be rejected
       before dereferencing caller-controlled memory. */
    if(fs->ReadAt(fs->FileSystemContext,(void*)(uintptr_t)1,0,data,1)!=
       Rc95LdrInvalidParameter)return 1;
    fs->Close(fs->FileSystemContext,(void*)(uintptr_t)1);
    {
        RC95LDR_FAT32 Other;
        if(Rc95LdrFat32Mount(&Other,&dev,0)!=Rc95LdrSuccess)return 1;
        if(fs->ReadAt(fs->FileSystemContext,&Other.Files[0],0,data,1)!=
           Rc95LdrInvalidParameter)return 1;
        Rc95LdrFat32Unmount(&Other);
    }
    if(fs->ReadAt(fs->FileSystemContext,h,0,data,4)!=Rc95LdrSuccess||memcmp(data,"HIVE",4))return 1;
    /* The descriptor is snapshotted at mount; callback table edits by the
       caller do not invalidate an already-mounted filesystem. */
    {
        RC95LDR_BLOCK_READ SavedRead=dev.Read;
        dev.Read=NULL;
        if(fs->ReadAt(fs->FileSystemContext,h,0,data,4)!=Rc95LdrSuccess||
           memcmp(data,"HIVE",4))return 1;
        dev.Read=SavedRead;
    }
    /* A reserved FAT32 marker must not be followed as a data cluster. */
    fat(6,0x0ffffff0);
    if(fs->ReadAt(fs->FileSystemContext,h,512,data,1)!=Rc95LdrCorruptData)return 1;
    fat(6,7);
    Image[(size_t)(DATA_START+5)*512]=0x5a;
    if(fs->ReadAt(fs->FileSystemContext,h,512,data,1)!=Rc95LdrSuccess||data[0]!=0x5a)return 1;
    if(fs->ReadAt(fs->FileSystemContext,h,513,data,1)!=Rc95LdrOutOfBounds)return 1;
    fs->Close(fs->FileSystemContext,h);
    /* Delete a fragmented-chain candidate from the root, mirror both FATs,
       and remove the directory entry only after validating the whole chain. */
    if(!fs->DeleteFile||fs->DeleteFile(fs->FileSystemContext,"\\\\hiberfil.sys")!=Rc95LdrSuccess)return 1;
    if(Image[(size_t)(DATA_START+0)*512+32]!=0xe5||
       (get32(Image+(size_t)FAT_START*512+9*4)&0x0fffffffu)!=0||
       (get32(Image+(size_t)FAT_START*512+10*4)&0x0fffffffu)!=0||
       (get32(Image+(size_t)(FAT_START+FAT_SIZE)*512+9*4)&0x0fffffffu)!=0||
       (get32(Image+(size_t)(FAT_START+FAT_SIZE)*512+10*4)&0x0fffffffu)!=0||
       get32(Image+512+488)!=0xffffffffu||get32(Image+512+492)!=0xffffffffu||
       get32(Image+7u*512u+488)!=0xffffffffu||
       get32(Image+7u*512u+492)!=0xffffffffu)return 1;
    if(fs->OpenRead(fs->FileSystemContext,"\\\\hiberfil.sys",&h,&size)!=Rc95LdrNotFound)return 1;
    /* An injected first-write failure must be propagated, not reported as
       successful deletion. */
    entry(root+64,"HIBERFILSYS",0x20,9,513);fat(9,10);fat(10,0x0fffffff);
    p32(Image+(size_t)(FAT_START+FAT_SIZE)*512+9*4,10);
    p32(Image+(size_t)(FAT_START+FAT_SIZE)*512+10*4,0x0fffffff);
    FailWrite=1;
    if(fs->DeleteFile(fs->FileSystemContext,"\\\\hiberfil.sys")!=Rc95LdrIoError)return 1;
    FailWrite=0;
    if(root[64]!= 'H')return 1;
    /* A failure after directory removal but before FAT reclamation leaves
       the volume dirty and does not free a cluster still in the chain. */
    entry(root+96,"HIBERFILSYS",0x20,9,513);
    WriteCount=0;FailWriteAt=4;
    if(fs->DeleteFile(fs->FileSystemContext,"\\\\hiberfil.sys")!=Rc95LdrIoError||
       root[64]!=0xe5||root[96]!= 'H'||
       (get32(Image+(size_t)FAT_START*512+9*4)&0x0fffffffu)!=10u)return 1;
    FailWriteAt=0;
    /* Read-only media and a volume already marked dirty are never mutated. */
    dev.Write=NULL;
    if(fs->DeleteFile(fs->FileSystemContext,"\\\\hiberfil.sys")!=Rc95LdrUnsupported||
       root[96]!='H')return 1;
    dev.Write=Write;
    if(fs->DeleteFile(fs->FileSystemContext,"\\\\hiberfil.sys")!=Rc95LdrUnsupported||
       root[96]!='H')return 1;
    Rc95LdrFat32Unmount(&fat32);
    /* Mount must reject contradictory legacy geometry and reserved flags. */
    if(!reject_bpb16(&dev,&fat32,19,1))return 1; /* FAT16 total sectors */
    if(!reject_bpb16(&dev,&fat32,22,1))return 1; /* FAT16 FAT size */
    if(!reject_bpb16(&dev,&fat32,40,0x0100))return 1; /* reserved ExtFlags */
    if(!reject_bpb16(&dev,&fat32,40,0x0082))return 1; /* active FAT out of range */
    if(!reject_bpb16(&dev,&fat32,42,1))return 1; /* unsupported FAT version */
    if(!reject_bpb8(&dev,&fat32,13,3))return 1; /* non-power-of-two cluster */
    if(!reject_bpb8(&dev,&fat32,13,0))return 1; /* zero cluster size */
    if(!reject_bpb32(&dev,&fat32,32,SECTORS+1))return 1; /* beyond partition */

    /* Directory entries may continue in a non-contiguous cluster. */
    memset(ms,0xe5,512); /* no end-of-directory marker before the next cluster */
    entry(Image+(size_t)(DATA_START+6)*512,"BOOT       ",0x10,5,0); /* cluster 8 */
    fat(4,8);fat(8,0x0fffffff);
    if(Rc95LdrFat32Mount(&fat32,&dev,0)!=Rc95LdrSuccess)return 1;
    if(Rc95LdrFat32BindVolume(&fat32,&volume)!=Rc95LdrSuccess)return 1;
    fs=volume.FileSystem;h=NULL;size=0;
    if(fs->OpenRead(fs->FileSystemContext,"\\EFI\\MicrosoftSystem\\Boot\\BCD",&h,&size)!=Rc95LdrSuccess||size!=513)return 1;
    fs->Close(fs->FileSystemContext,h);Rc95LdrFat32Unmount(&fat32);

    /* A free-cluster link in a directory chain is corruption, not EOF. */
    fat(4,0);
    if(Rc95LdrFat32Mount(&fat32,&dev,0)!=Rc95LdrSuccess)return 1;
    if(Rc95LdrFat32BindVolume(&fat32,&volume)!=Rc95LdrSuccess)return 1;
    fs=volume.FileSystem;h=NULL;size=0;
    if(fs->OpenRead(fs->FileSystemContext,"\\EFI\\MicrosoftSystem\\Missing",&h,&size)!=Rc95LdrCorruptData)return 1;
    Rc95LdrFat32Unmount(&fat32);
    /* A BPB whose cluster numbers enter the reserved FAT32 marker range is
       not a valid FAT32 volume, even when the partition is very large. */
    {
        RC95LDR_BLOCK_DEVICE huge={NULL,UINT64_C(0x10400014),512,ReadHuge,NULL};
        p32(b+32,0x10400014u);p32(b+36,0x00200001u);
        p32(b+44,0x0ffffff0u);
        if(Rc95LdrFat32MountPartition(&fat32,&huge,0,huge.BlockCount)!=
           Rc95LdrCorruptData)return 1;
        p32(b+32,SECTORS);p32(b+36,FAT_SIZE);p32(b+44,2);
    }
    free(Image);
    puts("RC95LDR FAT32 directory-chain, long-name, and BPB tests passed");return 0;
}
