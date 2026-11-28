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
    path[0]=4;path[1]=1;p16(path+2,42);p32(path+4,1);p64(path+8,40);p64(path+16,61);
    path[24]=1;path[40]=2;path[41]=2;path[42]=0x7f;path[43]=0xff;p16(path+44,4);
    if(Rc95LdrParseUefiDevicePath(path,sizeof(PartitionPath),&pn,&start,&count,guid)!=Rc95LdrSuccess)return 0;
    return pn==1&&start==40&&count==61&&guid[0]==1;
}
static int test_mbr_enumeration(void)
{
    RC95LDR_UEFI_FIRMWARE_OPS ops;
    RC95LDR_PARTITION_INFO parts[8];
    RC95LDR_UEFI_BLOCK_IO partitionIo;
    RC95LDR_PARTITION_IDENTITY identity;
    size_t resolvedIndex = (size_t)-1;
    size_t count = 0, scratchSize = 512 + 128 * 128;
    uint8_t *Scratch;
    uint8_t *Entry = Disk + 446;
    uint32_t Signature = 0x78563412u;
    RC95LDR_STATUS Status;

    memset(Disk, 0, sizeof(Disk));
    p32(Disk + 440, Signature);
    Entry[4] = 0x07; /* NTFS/HPFS/exFAT partition type. */
    p32(Entry + 8, 40);
    p32(Entry + 12, 61);
    Disk[510] = 0x55;
    Disk[511] = 0xaa;
    Disk[40 * 512 + 3] = 'N';
    Disk[40 * 512 + 4] = 'T';
    Disk[40 * 512 + 5] = 'F';
    Disk[40 * 512 + 6] = 'S';
    Disk[40 * 512 + 7] = ' ';
    Disk[40 * 512 + 510] = 0x55;
    Disk[40 * 512 + 511] = 0xaa;

    memset(PartitionPath, 0, sizeof(PartitionPath));
    PartitionPath[0] = 4;
    PartitionPath[1] = 1;
    p16(PartitionPath + 2, 42);
    p32(PartitionPath + 4, 1);
    p64(PartitionPath + 8, 40);
    p64(PartitionPath + 16, 61);
    p32(PartitionPath + 24, Signature);
    PartitionPath[40] = 1; /* MBR partition format. */
    PartitionPath[41] = 1; /* MBR signature type. */
    PartitionPath[42] = 0x7f;
    PartitionPath[43] = 0xff;
    p16(PartitionPath + 44, 4);

    memset(&ops, 0, sizeof(ops));
    ops.GetHandleCount = count_handles;
    ops.GetHandleAt = get_handle;
    ops.OpenBlockIo = open_io;
    ops.GetDevicePath = get_path;
    Scratch = malloc(scratchSize);
    if (!Scratch)
        return 0;

    Status = Rc95LdrUefiEnumeratePartitions(&ops, parts, 8, &count,
        Scratch, scratchSize);
    free(Scratch);
    if (Status != Rc95LdrSuccess || count != 1 ||
        parts[0].Style != Rc95LdrPartitionMbr ||
        parts[0].MbrDiskSignature != Signature ||
        parts[0].StartLba != 40 || parts[0].BlockCount != 61 ||
        parts[0].FileSystem != Rc95LdrFsNtfs ||
        parts[0].FirmwareHandleIndex != 1 ||
        parts[0].ParentDiskHandleIndex != 0 ||
        parts[0].FirmwarePartitionNumber != 1)
        return 0;

    if (open_io(NULL, &PartitionHandle, &partitionIo) != Rc95LdrSuccess ||
        Rc95LdrUefiValidatePartitionHandle(&parts[0], &partitionIo,
            PartitionPath, sizeof(PartitionPath)) != Rc95LdrSuccess)
        return 0;

    memset(&identity, 0, sizeof(identity));
    identity.Kind = Rc95LdrPartitionIdMbr;
    identity.MbrDiskSignature = Signature;
    identity.MbrPartitionOffsetBytes = 40u * 512u;
    if (Rc95LdrResolvePartition(parts, count, &identity, Rc95LdrFsNtfs,
            &resolvedIndex) != Rc95LdrSuccess || resolvedIndex != 0)
        return 0;
    identity.MbrPartitionOffsetBytes++;
    if (Rc95LdrResolvePartition(parts, count, &identity, Rc95LdrFsUnknown,
            &resolvedIndex) != Rc95LdrNotFound)
        return 0;

    /* The exact byte offset is the UEFI device-path identity, not a rounded LBA. */
    p64(PartitionPath + 8, 41);
    if (Rc95LdrUefiValidatePartitionHandle(&parts[0], &partitionIo,
            PartitionPath, sizeof(PartitionPath)) != Rc95LdrNotFound)
        return 0;
    p64(PartitionPath + 8, 40);
    p32(PartitionPath + 24, Signature ^ 1u);
    if (Rc95LdrUefiValidatePartitionHandle(&parts[0], &partitionIo,
            PartitionPath, sizeof(PartitionPath)) != Rc95LdrNotFound)
        return 0;
    return 1;
}

int main(void)
{
    uint8_t *H=Disk+512,*E=Disk+1024,*Scratch;
    RC95LDR_UEFI_FIRMWARE_OPS ops;RC95LDR_PARTITION_INFO parts[8];RC95LDR_UEFI_BLOCK_IO partitionIo;size_t count=0;
    size_t scratchSize=512+128*128;RC95LDR_STATUS s;
    memset(Disk,0,sizeof(Disk));
    if (!test_device_path())
        return 1;
    if (!test_mbr_enumeration())
    {
        puts("RC95LDR UEFI MBR/device-path tests failed");
        return 1;
    }
    memset(Disk, 0, sizeof(Disk));
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
    if(s!=Rc95LdrSuccess||count!=1||parts[0].Style!=Rc95LdrPartitionGpt||
       parts[0].StartLba!=40||parts[0].BlockCount!=61||parts[0].DiskGuid[0]!=0x55||
       parts[0].FileSystem!=Rc95LdrFsUnknown ||
       parts[0].FirmwareHandleIndex!=1 ||
       parts[0].ParentDiskHandleIndex!=0 ||
       parts[0].FirmwarePartitionNumber!=1)
    {
        free(Scratch);
        return 1;
    }

    /* A GPT header with a zero disk GUID must not enter discovery. */
    memset(H+56,0,16);
    p32(H+16,0);p32(H+16,crc32(H,92));
    s=Rc95LdrUefiEnumerateGptPartitions(&ops,parts,8,&count,Scratch,scratchSize);
    if(s!=Rc95LdrCorruptData){free(Scratch);return 1;}

    /* A non-empty GPT entry with a zero unique GUID is also malformed. */
    H[56]=0x55;
    memset(E+16,0,16);
    p32(H+88,crc32(E,128*128));
    p32(H+16,0);p32(H+16,crc32(H,92));
    s=Rc95LdrUefiEnumerateGptPartitions(&ops,parts,8,&count,Scratch,scratchSize);
    if(s!=Rc95LdrCorruptData){free(Scratch);return 1;}

    /* Restore the valid fixture for the device-path association checks. */
    E[16]=1;
    p32(H+88,crc32(E,128*128));
    p32(H+16,0);p32(H+16,crc32(H,92));
    free(Scratch);
    if(open_io(NULL,&PartitionHandle,&partitionIo)!=Rc95LdrSuccess ||
       Rc95LdrUefiValidatePartitionHandle(&parts[0],&partitionIo,
           PartitionPath,sizeof(PartitionPath))!=Rc95LdrSuccess)
        return 1;
    /* A stale extent, GUID, or non-partition handle must never resolve. */
    p64(PartitionPath+8,41);
    if(Rc95LdrUefiValidatePartitionHandle(&parts[0],&partitionIo,
           PartitionPath,sizeof(PartitionPath))!=Rc95LdrNotFound)
        return 1;
    p64(PartitionPath+8,40);
    p32(PartitionPath+4,4);
    if(Rc95LdrUefiValidatePartitionHandle(&parts[0],&partitionIo,
           PartitionPath,sizeof(PartitionPath))!=Rc95LdrNotFound)
        return 1;
    p32(PartitionPath+4,1);
    p64(PartitionPath+16,60);
    if(Rc95LdrUefiValidatePartitionHandle(&parts[0],&partitionIo,
           PartitionPath,sizeof(PartitionPath))!=Rc95LdrNotFound)
        return 1;
    p64(PartitionPath+16,61);
    PartitionPath[24]=2;
    if(Rc95LdrUefiValidatePartitionHandle(&parts[0],&partitionIo,
           PartitionPath,sizeof(PartitionPath))!=Rc95LdrNotFound)
        return 1;
    PartitionPath[24]=1;

    /* A correct path cannot legitimize media with a different geometry. */
    partitionIo.Media.BlockSize=4096;
    if(Rc95LdrUefiValidatePartitionHandle(&parts[0],&partitionIo,
           PartitionPath,sizeof(PartitionPath))!=Rc95LdrUnsupported)
        return 1;
    partitionIo.Media.BlockSize=512;
    partitionIo.Media.LastBlock=59;
    if(Rc95LdrUefiValidatePartitionHandle(&parts[0],&partitionIo,
           PartitionPath,sizeof(PartitionPath))!=Rc95LdrUnsupported)
        return 1;
    partitionIo.Media.LastBlock=60;
    partitionIo.ReadBlocks=NULL;
    if(Rc95LdrUefiValidatePartitionHandle(&parts[0],&partitionIo,
           PartitionPath,sizeof(PartitionPath))!=Rc95LdrUnsupported)
        return 1;
    partitionIo.ReadBlocks=read_blocks;
    partitionIo.Media.LogicalPartition=0;
    if(Rc95LdrUefiValidatePartitionHandle(&parts[0],&partitionIo,
           PartitionPath,sizeof(PartitionPath))!=Rc95LdrUnsupported)
        return 1;
    puts("RC95LDR UEFI GPT/device-path tests passed");
    return 0;
}
