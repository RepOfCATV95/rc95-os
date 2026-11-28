#include "rc95ldruedisk.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define DISK_BLOCKS 67000u
typedef struct _FAKE_FW { uint8_t *Image; uint8_t Path[46]; uint32_t Reads; uint32_t Writes; uint32_t IoAlign; uint8_t ReadOnly; } FAKE_FW;
static void Put16(uint8_t *P,uint16_t V){P[0]=(uint8_t)V;P[1]=(uint8_t)(V>>8);}
static void Put32(uint8_t *P,uint32_t V){P[0]=(uint8_t)V;P[1]=(uint8_t)(V>>8);P[2]=(uint8_t)(V>>16);P[3]=(uint8_t)(V>>24);}
static RC95LDR_STATUS ReadBlocks(void *P,uint32_t Id,uint64_t Lba,size_t Bytes,void *Buffer)
{
    FAKE_FW *F=(FAKE_FW*)P;size_t Offset;(void)Id;
    if(Lba>DISK_BLOCKS||Lba>SIZE_MAX/512)return Rc95LdrOutOfBounds;
    Offset=(size_t)Lba*512;
    if(Offset>DISK_BLOCKS*512u||Bytes>DISK_BLOCKS*512u-Offset)return Rc95LdrOutOfBounds;
    memcpy(Buffer,F->Image+Offset,Bytes);F->Reads++;return Rc95LdrSuccess;
}
static RC95LDR_STATUS WriteBlocks(void *P,uint32_t Id,uint64_t Lba,size_t Bytes,const void *Buffer)
{
    FAKE_FW *F=(FAKE_FW*)P;size_t Offset;(void)Id;
    if(F->ReadOnly)return Rc95LdrIoError;
    if(Lba>DISK_BLOCKS||Lba>SIZE_MAX/512)return Rc95LdrOutOfBounds;
    Offset=(size_t)Lba*512;
    if(Offset>DISK_BLOCKS*512u||Bytes>DISK_BLOCKS*512u-Offset)return Rc95LdrOutOfBounds;
    memcpy(F->Image+Offset,Buffer,Bytes);F->Writes++;return Rc95LdrSuccess;
}
static RC95LDR_STATUS GetHandle(void *C,size_t I,void **H)
{if(I)return Rc95LdrNotFound;*H=C;return Rc95LdrSuccess;}
static RC95LDR_STATUS OpenBlockIo(void *C,void *H,RC95LDR_UEFI_BLOCK_IO *Io)
{
    FAKE_FW *F=(FAKE_FW*)C;(void)H;memset(Io,0,sizeof(*Io));
    Io->Protocol=F;Io->Media.MediaId=7;Io->Media.BlockSize=512;
    Io->Media.IoAlign=F->IoAlign;Io->Media.LastBlock=DISK_BLOCKS-1;
    Io->Media.MediaPresent=1;Io->Media.LogicalPartition=1;
    Io->Media.ReadOnly=F->ReadOnly;
    Io->ReadBlocks=ReadBlocks;Io->WriteBlocks=WriteBlocks;
    return Rc95LdrSuccess;
}
static RC95LDR_STATUS GetPath(void *C,void *H,const uint8_t **P,size_t *N)
{FAKE_FW *F=(FAKE_FW*)C;(void)H;*P=F->Path;*N=sizeof(F->Path);return Rc95LdrSuccess;}
int main(void)
{
    FAKE_FW F;RC95LDR_UEFI_FIRMWARE_OPS Ops;RC95LDR_PARTITION_INFO P;
    RC95LDR_UEFI_FAT32_VOLUME Native;RC95LDR_VOLUME Volume;
    RC95LDR_STATUS Status;
    memset(&F,0,sizeof(F));memset(&Ops,0,sizeof(Ops));memset(&P,0,sizeof(P));
    F.Image=(uint8_t*)calloc(DISK_BLOCKS,512);if(!F.Image)return 1;
    /* Valid FAT32 BPB, partition-relative LBA zero. */
    Put16(F.Image+11,512);F.Image[13]=1;Put16(F.Image+14,32);
    F.Image[16]=2;Put32(F.Image+32,DISK_BLOCKS);Put32(F.Image+36,520);
    Put32(F.Image+44,2);Put16(F.Image+510,0xaa55);
    /* Exact GPT hard-drive node plus end-entire node. */
    F.Path[0]=4;F.Path[1]=1;F.Path[2]=42;F.Path[3]=0;
    Put32(F.Path+4,1);Put32(F.Path+8,2048);Put32(F.Path+16,DISK_BLOCKS);
    F.Path[24]=0x5a;F.Path[41]=2;
    F.Path[42]=0x7f;F.Path[43]=0xff;F.Path[44]=4;
    P.Style=Rc95LdrPartitionGpt;P.FileSystem=Rc95LdrFsFat32;
    P.BlockSize=512;P.BlockCount=DISK_BLOCKS;P.StartLba=2048;
    P.FirmwareHandleIndex=0;P.FirmwarePartitionNumber=1;P.UniqueGuid[0]=0x5a;
    Ops.Context=&F;Ops.GetHandleAt=GetHandle;Ops.OpenBlockIo=OpenBlockIo;
    Ops.GetDevicePath=GetPath;
    Status=Rc95LdrUefiMountFat32Volume(&Ops,&P,&Native,&Volume);
    if(Status!=Rc95LdrSuccess||!Native.Mounted||!Volume.FileSystem||
       F.Reads==0||!Native.Device.Write)return 2;
    {
        uint8_t Sector[512];memset(Sector,0xA5,sizeof(Sector));
        if(Native.Device.Write(Native.Device.DeviceContext,100,1,Sector,
             sizeof(Sector))!=Rc95LdrSuccess||F.Writes!=1||
             F.Image[100u*512u]!=0xA5)return 7;
    }
    Rc95LdrUefiUnmountFat32Volume(&Native);
    if(Native.Mounted)return 3;

    /* Never mount a different partition or a buffer alignment contract that
       this minimal adapter cannot satisfy. */
    P.UniqueGuid[0]=0x6b;
    if(Rc95LdrUefiMountFat32Volume(&Ops,&P,&Native,&Volume)!=Rc95LdrNotFound)return 4;
    P.UniqueGuid[0]=0x5a;F.IoAlign=8;
    if(Rc95LdrUefiMountFat32Volume(&Ops,&P,&Native,&Volume)!=Rc95LdrCorruptData)return 5;
    F.IoAlign=0;F.ReadOnly=1;
    if(Rc95LdrUefiMountFat32Volume(&Ops,&P,&Native,&Volume)!=Rc95LdrSuccess||
       Native.Device.Write!=NULL)return 8;
    Rc95LdrUefiUnmountFat32Volume(&Native);
    F.ReadOnly=0;P.FirmwareHandleIndex=UINT32_MAX;
    if(Rc95LdrUefiMountFat32Volume(&Ops,&P,&Native,&Volume)!=Rc95LdrInvalidParameter)return 6;
    free(F.Image);puts("RC95LDR UEFI native volume tests passed");return 0;
}
