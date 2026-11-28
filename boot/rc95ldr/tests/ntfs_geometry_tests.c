#include "rc95ldr_ntfs_geometry.h"
#include <stdio.h>
#include <string.h>

static uint8_t Sector[4096];
static void p16(uint8_t *p,uint16_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void p64(uint8_t *p,uint64_t v)
{unsigned i;for(i=0;i<8;i++)p[i]=(uint8_t)(v>>(i*8));}
static void reset(void)
{
    memset(Sector,0,sizeof(Sector));
    memcpy(Sector+3,"NTFS    ",8);
    p16(Sector+11,512);Sector[13]=8;
    p64(Sector+40,100000);p64(Sector+48,4);p64(Sector+56,8);
    Sector[64]=(uint8_t)-10;Sector[68]=(uint8_t)-12;
    p64(Sector+72,0x123456789abcdef0ULL);
    Sector[510]=0x55;Sector[511]=0xaa;
}
static int rejected(uint32_t BlockSize,uint64_t PartitionBlocks)
{
    RC95LDR_NTFS_GEOMETRY G;
    return Rc95LdrNtfsParseGeometry(Sector,512,BlockSize,PartitionBlocks,&G)
           ==Rc95LdrCorruptData;
}
int main(void)
{
    RC95LDR_NTFS_GEOMETRY G;
    reset();
    if(Rc95LdrNtfsParseGeometry(Sector,sizeof(Sector),512,100000,&G)!=Rc95LdrSuccess)return 1;
    if(G.BytesPerSector!=512||G.SectorsPerCluster!=8||
       G.BytesPerCluster!=4096||G.ClusterCount!=12500||
       G.MftLcn!=4||G.MftMirrorLcn!=8||
       G.FileRecordBytes!=1024||G.IndexBufferBytes!=4096||
       G.VolumeSerial!=0x123456789abcdef0ULL)return 1;
    /* Positive encodings express a count of clusters per record. */
    Sector[64]=1;Sector[68]=2;
    if(Rc95LdrNtfsParseGeometry(Sector,sizeof(Sector),512,100000,&G)!=Rc95LdrSuccess||
       G.FileRecordBytes!=4096||G.IndexBufferBytes!=8192)return 1;
    reset();
    if(!rejected(4096,100000))return 1; /* device/volume sector mismatch */
    p16(Sector+11,4096);
    if(Rc95LdrNtfsParseGeometry(Sector,512,4096,100000,&G)!=Rc95LdrBufferTooSmall)return 1;
    reset();
    p64(Sector+40,UINT64_MAX);
    if(Rc95LdrNtfsParseGeometry(Sector,sizeof(Sector),512,UINT64_MAX,&G)!=
       Rc95LdrIntegerOverflow)return 1; /* sector-to-byte volume size overflows */
    reset();
    if(!rejected(512,99999))return 1; /* volume exceeds partition */
    Sector[13]=3;if(!rejected(512,100000))return 1; /* invalid cluster size */
    reset();p64(Sector+48,12500);if(!rejected(512,100000))return 1; /* MFT LCN */
    reset();p16(Sector+11,4096);Sector[13]=32;
    if(!rejected(4096,100000))return 1; /* cluster > 64 KiB */
    reset();Sector[13]=1;Sector[64]=(uint8_t)-8;
    if(!rejected(512,100000))return 1; /* record smaller than logical sector */
    reset();Sector[68]=(uint8_t)-8;
    if(!rejected(512,100000))return 1; /* index buffer smaller than sector */
    reset();Sector[64]=(uint8_t)-9;Sector[68]=(uint8_t)-10;
    p16(Sector+11,1024);
    if(Rc95LdrNtfsParseGeometry(Sector,sizeof(Sector),1024,100000,&G)!=
       Rc95LdrCorruptData)return 1; /* metadata records not sector-aligned */
    reset();
    if(Rc95LdrNtfsParseGeometry(Sector,sizeof(Sector),0,100000,&G)!=
       Rc95LdrCorruptData)return 1; /* invalid device logical block size */
    reset();Sector[64]=0;if(!rejected(512,100000))return 1; /* record encoding */
    reset();Sector[64]=0xef;if(!rejected(512,100000))return 1; /* record exceeds 64 KiB */
    reset();Sector[68]=0x80;if(!rejected(512,100000))return 1; /* exponent overflow */
    reset();Sector[510]=0;if(Rc95LdrNtfsParseGeometry(Sector,sizeof(Sector),512,100000,&G)!=Rc95LdrInvalidImage)return 1;
    reset();memcpy(Sector+3,"FAT32   ",8);if(Rc95LdrNtfsParseGeometry(Sector,sizeof(Sector),512,100000,&G)!=Rc95LdrInvalidImage)return 1;
    puts("RC95LDR NTFS geometry tests passed");return 0;
}
