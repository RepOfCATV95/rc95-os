#include "rc95ldr_ntfs_volume.h"
#include "rc95ldr_ntfs_provider.h"
#include "rc95ldr_ntfs_index.h"
#include "rc95ldruefinativemounts.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
static int TestFailure(int Code,const char *Name)
{fprintf(stderr,"%s failed at check %d\\n",Name,Code);return Code;}
#define DISK_BLOCKS 256u
static uint8_t Disk[DISK_BLOCKS*512u];
static uint8_t ProviderWorkspace[65536u];
static void P16(uint8_t *P,uint16_t V){P[0]=(uint8_t)V;P[1]=(uint8_t)(V>>8);}
static void P32(uint8_t *P,uint32_t V){P[0]=(uint8_t)V;P[1]=(uint8_t)(V>>8);P[2]=(uint8_t)(V>>16);P[3]=(uint8_t)(V>>24);}
static void P64(uint8_t *P,uint64_t V){unsigned I;for(I=0;I<8;I++)P[I]=(uint8_t)(V>>(I*8));}
static uint8_t UefiDevicePath[46];
static int UefiFailGetHandle, UefiFailOpenBlock, UefiFailDevicePath;
static unsigned UefiAllocationCalls, UefiFailAllocationAt, UefiFreeCalls;
static RC95LDR_STATUS UefiReadBlocks(void*,uint32_t,uint64_t,size_t,void*);
static RC95LDR_STATUS UefiGetHandle(void *C,size_t I,void **H)
{(void)C;if(UefiFailGetHandle||I)return Rc95LdrNotFound;*H=(void*)1;return Rc95LdrSuccess;}
static RC95LDR_STATUS UefiOpenBlockIo(void *C,void *H,RC95LDR_UEFI_BLOCK_IO *Io)
{
 (void)C;
 if(UefiFailOpenBlock)return Rc95LdrIoError;
 if(H!=(void*)1||!Io)return Rc95LdrInvalidParameter;
 memset(Io,0,sizeof(*Io));Io->Protocol=Disk;
 Io->Media.MediaId=7;Io->Media.BlockSize=512;Io->Media.IoAlign=1;
 Io->Media.LastBlock=DISK_BLOCKS-1;Io->Media.MediaPresent=1;
 Io->Media.LogicalPartition=1;
 Io->ReadBlocks=UefiReadBlocks;return Rc95LdrSuccess;
}
static RC95LDR_STATUS UefiReadBlocks(void *P,uint32_t Id,uint64_t L,size_t Bytes,void *B)
{
 (void)P;if(Id!=7||L>DISK_BLOCKS||Bytes>(size_t)(DISK_BLOCKS-L)*512u||
    (Bytes%512u))return Rc95LdrOutOfBounds;
 memcpy(B,Disk+(size_t)L*512u,Bytes);return Rc95LdrSuccess;
}
static RC95LDR_STATUS UefiGetDevicePath(void *C,void *H,const uint8_t **P,size_t *N)
{(void)C;if(UefiFailDevicePath||H!=(void*)1)return Rc95LdrNotFound;*P=UefiDevicePath;*N=sizeof(UefiDevicePath);return Rc95LdrSuccess;}
static RC95LDR_STATUS UefiAllocate(void *C,size_t N,void **P)
{(void)C;++UefiAllocationCalls;*P=NULL;
 if(UefiFailAllocationAt==UefiAllocationCalls)return Rc95LdrOutOfMemory;
 *P=malloc(N);return *P?Rc95LdrSuccess:Rc95LdrOutOfMemory;}
static void UefiFree(void *C,void *P){(void)C;if(P){++UefiFreeCalls;free(P);}}
static void BuildUefiDevicePath(const uint8_t Guid[16])
{
 memset(UefiDevicePath,0,sizeof(UefiDevicePath));
 UefiDevicePath[0]=4;UefiDevicePath[1]=1;P16(UefiDevicePath+2,42);
 P32(UefiDevicePath+4,1);P64(UefiDevicePath+8,1);
 P64(UefiDevicePath+16,DISK_BLOCKS);memcpy(UefiDevicePath+24,Guid,16);
 UefiDevicePath[40]=2;UefiDevicePath[41]=2;
 UefiDevicePath[42]=0x7f;UefiDevicePath[43]=0xff;
 P16(UefiDevicePath+44,4);
}
static int TestUefiNativeMount(void)
{
 RC95LDR_UEFI_NATIVE_MOUNT_CONTEXT Context;
 RC95LDR_UEFI_FIRMWARE_OPS Firmware;
 RC95LDR_PARTITION_INFO Partition;
 RC95LDR_VOLUME Mounted;
 uint8_t Guid[16]={1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
 void *Handle=NULL;uint64_t Size=0;uint8_t Data[4];
 memset(&Firmware,0,sizeof(Firmware));
 Firmware.GetHandleAt=UefiGetHandle;Firmware.OpenBlockIo=UefiOpenBlockIo;
 Firmware.GetDevicePath=UefiGetDevicePath;
 memset(&Partition,0,sizeof(Partition));
 Partition.Style=Rc95LdrPartitionGpt;Partition.FileSystem=Rc95LdrFsNtfs;
 memcpy(Partition.UniqueGuid,Guid,16);Partition.StartLba=1;
 Partition.BlockCount=DISK_BLOCKS;Partition.BlockSize=512;
 Partition.FirmwareHandleIndex=0;Partition.FirmwarePartitionNumber=1;
 BuildUefiDevicePath(Guid);
 if(Rc95LdrUefiInitializeNativeMounts(&Context,&Firmware,
      UefiAllocate,UefiFree,NULL)!=Rc95LdrSuccess)
   return TestFailure(30,"uefi_ntfs_mount");

 /* Bad partition references and each firmware adapter failure must unwind
    without leaving an NTFS mount or pool allocation behind. */
 {
  RC95LDR_PARTITION_INFO BadPartition=Partition;
  BadPartition.FirmwareHandleIndex=UINT32_MAX;
  if(Rc95LdrUefiMountNativeVolume(&Context,&BadPartition,Rc95LdrFsNtfs,
       &Mounted)!=Rc95LdrInvalidParameter)
    return TestFailure(35,"uefi_ntfs_mount");
  UefiFailGetHandle=1;
  if(Rc95LdrUefiMountNativeVolume(&Context,&Partition,Rc95LdrFsNtfs,
       &Mounted)!=Rc95LdrNotFound)
    return TestFailure(36,"uefi_ntfs_mount");
  UefiFailGetHandle=0;
  UefiFailOpenBlock=1;
  if(Rc95LdrUefiMountNativeVolume(&Context,&Partition,Rc95LdrFsNtfs,
       &Mounted)!=Rc95LdrIoError)
    return TestFailure(37,"uefi_ntfs_mount");
  UefiFailOpenBlock=0;
  UefiFailDevicePath=1;
  if(Rc95LdrUefiMountNativeVolume(&Context,&Partition,Rc95LdrFsNtfs,
       &Mounted)!=Rc95LdrNotFound)
    return TestFailure(38,"uefi_ntfs_mount");
  UefiFailDevicePath=0;

  UefiAllocationCalls=UefiFreeCalls=0;UefiFailAllocationAt=1;
  if(Rc95LdrUefiMountNativeVolume(&Context,&Partition,Rc95LdrFsNtfs,
       &Mounted)!=Rc95LdrOutOfMemory||UefiFreeCalls!=0)
    return TestFailure(39,"uefi_ntfs_mount");
  UefiAllocationCalls=UefiFreeCalls=0;UefiFailAllocationAt=2;
  if(Rc95LdrUefiMountNativeVolume(&Context,&Partition,Rc95LdrFsNtfs,
       &Mounted)!=Rc95LdrOutOfMemory||UefiFreeCalls!=1)
    return TestFailure(40,"uefi_ntfs_mount");
  UefiAllocationCalls=UefiFreeCalls=UefiFailAllocationAt=0;
 }
 if(Rc95LdrUefiMountNativeVolume(&Context,&Partition,Rc95LdrFsNtfs,
      &Mounted)!=Rc95LdrSuccess)
   return TestFailure(31,"uefi_ntfs_mount");
 if(!Mounted.FileSystem||!Mounted.BlockDevice||
    Mounted.FileSystem->OpenRead(Mounted.FileSystem->FileSystemContext,
      "\\W",&Handle,&Size)!=Rc95LdrSuccess||!Handle||Size!=1024)
   return TestFailure(32,"uefi_ntfs_mount");
 if(Mounted.FileSystem->ReadAt(Mounted.FileSystem->FileSystemContext,
      Handle,510,Data,sizeof(Data))!=Rc95LdrSuccess||
      Data[0]!='A'||Data[1]!='A'||Data[2]!='B'||Data[3]!='B')
   return TestFailure(33,"uefi_ntfs_mount");
 Mounted.FileSystem->Close(Mounted.FileSystem->FileSystemContext,Handle);
 Rc95LdrUefiUnmountNativeVolume(&Context,&Mounted);
 if(Mounted.FileSystem||Context.Ntfs.Mounted||
    Context.Ntfs.ProviderWorkspace||Context.Ntfs.MountScratch)
   return TestFailure(34,"uefi_ntfs_mount");
 Rc95LdrUefiDestroyNativeMounts(&Context);
 return 0;
}
static RC95LDR_STATUS Read(void *C,uint64_t L,uint32_t N,void *B,size_t Bytes)
{(void)C;if(L>DISK_BLOCKS||N>DISK_BLOCKS-L||Bytes!=(size_t)N*512u)return Rc95LdrOutOfBounds;memcpy(B,Disk+L*512u,Bytes);return Rc95LdrSuccess;}
static void MakeFileRecord(uint8_t *R,uint32_t RecordNumber)
{
 memset(R,0,1024);P32(R,RC95LDR_NTFS_FILE_MAGIC);P16(R+4,48);P16(R+6,3);
 P16(R+48,0xaaaa);P16(R+50,0x1111);P16(R+52,0x2222);
 P16(R+20,56);P32(R+24,60);P32(R+28,1024);P32(R+44,RecordNumber);
 P16(R+16,1);P16(R+22,1);P32(R+56,RC95LDR_NTFS_ATTRIBUTE_END);
 P16(R+510,0xaaaa);P16(R+1022,0xaaaa);
}
int main(void)
{
 RC95LDR_BLOCK_DEVICE D={NULL,DISK_BLOCKS,512,Read};
 RC95LDR_NTFS_VOLUME V;uint8_t Scratch[8192],Out[1024];uint8_t *B=Disk;
 memset(Disk,0,sizeof(Disk));
 {
  uint16_t Path[]={'\\\\','W'};
  size_t ReadBytes=0;uint64_t FileSize=0;
  if(Rc95LdrNtfsReadFilePrefix(NULL,Path,2,Scratch,sizeof(Scratch),
       Out,sizeof(Out),&ReadBytes,&FileSize)!=Rc95LdrInvalidParameter)
      return TestFailure(11,"ntfs_volume");
 }
 memset(&V, 0, sizeof(V));
 V.Mounted = 1;
 if (Rc95LdrNtfsReadMftRecord(&V, 0, Out, sizeof(Out)) !=
     Rc95LdrInvalidParameter)
     return TestFailure(19, "ntfs_volume");
 memset(&V, 0, sizeof(V));
 memcpy(B+3,"NTFS    ",8);P16(B+11,512);B[13]=1;
 P64(B+40,200);P64(B+48,4);P64(B+56,8);B[64]=(uint8_t)-10;B[68]=(uint8_t)-12;
 B[510]=0x55;B[511]=0xaa;
 MakeFileRecord(Disk+4u*512u,0);
 /* Replace the end marker with the nonresident $MFT::$DATA attribute. */
 P32(Disk+4u*512u+56,0x80);P32(Disk+4u*512u+60,72);
 Disk[4u*512u+64]=1;P16(Disk+4u*512u+88,64);
 P64(Disk+4u*512u+72,0);P64(Disk+4u*512u+80,9);
 P64(Disk+4u*512u+96,20480);P64(Disk+4u*512u+104,20480);
 P64(Disk+4u*512u+112,20480);
 /* Base $MFT::$DATA extent: VCN 0-9 at LCN 4. */
 P16(Disk+4u*512u+56+14,1);
 Disk[4u*512u+56+64]=0x11;Disk[4u*512u+56+65]=10;
 Disk[4u*512u+56+66]=4;Disk[4u*512u+56+67]=0;
 /* Resident $ATTRIBUTE_LIST names the base and extension DATA attributes. */
 P32(Disk+4u*512u+128,RC95LDR_NTFS_ATTRIBUTE_LIST);
 P32(Disk+4u*512u+132,88);P32(Disk+4u*512u+144,64);
 P16(Disk+4u*512u+148,24);
 P32(Disk+4u*512u+152,0x80);P16(Disk+4u*512u+156,32);
 P64(Disk+4u*512u+160,0);P64(Disk+4u*512u+168,((uint64_t)1<<48));
 P16(Disk+4u*512u+176,1);
 P32(Disk+4u*512u+184,0x80);P16(Disk+4u*512u+188,32);
 P64(Disk+4u*512u+192,10);P64(Disk+4u*512u+200,((uint64_t)1<<48)|1u);
 P16(Disk+4u*512u+208,1);
 P32(Disk+4u*512u+216,RC95LDR_NTFS_ATTRIBUTE_END);
 P32(Disk+4u*512u+24,220);
 MakeFileRecord(Disk+44u*512u,10);
 MakeFileRecord(Disk+6u*512u,1);
 /* Extension record 1 owns VCN 10-39 at LCN 34. */
 P64(Disk+6u*512u+32,((uint64_t)1<<48));
 P32(Disk+6u*512u+56,0x80);P32(Disk+6u*512u+60,72);
 Disk[6u*512u+64]=1;P16(Disk+6u*512u+70,1);
 P16(Disk+6u*512u+88,64);
 P64(Disk+6u*512u+72,10);P64(Disk+6u*512u+80,39);
 P64(Disk+6u*512u+96,20480);P64(Disk+6u*512u+104,20480);
 P64(Disk+6u*512u+112,20480);
 Disk[6u*512u+120]=0x11;Disk[6u*512u+121]=30;
 Disk[6u*512u+122]=34;Disk[6u*512u+123]=0;
 P32(Disk+6u*512u+128,RC95LDR_NTFS_ATTRIBUTE_END);
 P32(Disk+6u*512u+24,132);
 if(Rc95LdrNtfsMountPartition(&V,&D,0,DISK_BLOCKS,Scratch,512)!=Rc95LdrBufferTooSmall)return TestFailure(1,"ntfs_volume");
 if(Rc95LdrNtfsMountPartition(&V,&D,0,199,Scratch,sizeof(Scratch))!=Rc95LdrCorruptData)return TestFailure(2,"ntfs_volume");
 Disk[4u*512u+56+66]=5;
 if(Rc95LdrNtfsMountPartition(&V,&D,0,DISK_BLOCKS,Scratch,sizeof(Scratch))!=Rc95LdrCorruptData)return TestFailure(3,"ntfs_volume");
 Disk[4u*512u+56+66]=4;
 if(Rc95LdrNtfsMountPartition(&V,&D,0,DISK_BLOCKS,Scratch,1024)!=Rc95LdrBufferTooSmall)return TestFailure(4,"ntfs_volume");
 P64(Disk+4u*512u+200,((uint64_t)2<<48)|1u);
 if(Rc95LdrNtfsMountPartition(&V,&D,0,DISK_BLOCKS,Scratch,sizeof(Scratch))!=Rc95LdrNotFound)return TestFailure(4,"ntfs_volume");
 P64(Disk+4u*512u+200,((uint64_t)1<<48)|1u);
 P64(Disk+6u*512u+32,((uint64_t)1<<48)|2u);
 if(Rc95LdrNtfsMountPartition(&V,&D,0,DISK_BLOCKS,Scratch,sizeof(Scratch))!=Rc95LdrCorruptData)return TestFailure(4,"ntfs_volume");
 P64(Disk+6u*512u+32,((uint64_t)1<<48));
 Disk[6u*512u+122]=5;
 if(Rc95LdrNtfsMountPartition(&V,&D,0,DISK_BLOCKS,Scratch,sizeof(Scratch))!=Rc95LdrCorruptData)return TestFailure(4,"ntfs_volume");
 Disk[6u*512u+122]=34;
 if(Rc95LdrNtfsMountPartition(&V,&D,0,DISK_BLOCKS,Scratch,sizeof(Scratch))!=Rc95LdrSuccess)return TestFailure(4,"ntfs_volume");
 if(V.MftRunCount!=2||V.MftRuns[0].Lcn!=4||
    V.MftRuns[0].ClusterCount!=10||V.MftRuns[1].Vcn!=10||
    V.MftRuns[1].Lcn!=34||V.MftDataSize!=20480)return TestFailure(5,"ntfs_volume");
 if(Rc95LdrNtfsReadMftRecord(&V,1,Out,sizeof(Out))!=Rc95LdrSuccess||
    Rc95LdrReadU32(Out)!=RC95LDR_NTFS_FILE_MAGIC||
    Rc95LdrReadU32(Out+44)!=1)return TestFailure(6,"ntfs_volume");
 /* The mounted volume owns a descriptor snapshot; caller stack descriptor
    changes after mount must not invalidate the volume. */
 {
  RC95LDR_BLOCK_READ SavedRead=D.Read;
  D.Read=NULL;
  if(Rc95LdrNtfsReadMftRecord(&V,1,Out,sizeof(Out))!=Rc95LdrSuccess)
      return TestFailure(18,"ntfs_volume");
  D.Read=SavedRead;
 }
 if(Rc95LdrNtfsReadMftRecord(&V,20,Out,sizeof(Out))!=Rc95LdrOutOfBounds)return TestFailure(7,"ntfs_volume");
 if(Rc95LdrNtfsReadMftRecordByReference(&V,((uint64_t)1<<48)|1u,
       Out,sizeof(Out))!=Rc95LdrSuccess)return TestFailure(8,"ntfs_volume");
 if(Rc95LdrNtfsReadMftRecordByReference(&V,((uint64_t)2<<48)|1u,
       Out,sizeof(Out))!=Rc95LdrNotFound)return TestFailure(9,"ntfs_volume");
 /* Record 10 is in the second physical run, not adjacent to the first. */
 if(Rc95LdrNtfsReadMftRecord(&V,10,Out,sizeof(Out))!=Rc95LdrSuccess||
    Rc95LdrReadU32(Out+44)!=10)return TestFailure(10,"ntfs_volume");

 /* A directory with a resident $I30 root must resolve a child by its
    index entry rather than requiring a whole-MFT scan. */
 {
  uint8_t *Directory=Disk+34u*512u;
  uint8_t *Child=Disk+36u*512u;
  uint8_t IndexRoot[144];
  uint16_t Path[]={'\\\\','W'};
  size_t ReadBytes=0;uint64_t FileSize=0;
  memset(IndexRoot,0,sizeof(IndexRoot));
  P32(IndexRoot,0x30);P32(IndexRoot+4,1);P32(IndexRoot+8,4096);
  IndexRoot[12]=1;P32(IndexRoot+16,16);P32(IndexRoot+20,128);
  P32(IndexRoot+24,128);
  P64(IndexRoot+32,((uint64_t)1<<48)|6u);
  P16(IndexRoot+40,96);P16(IndexRoot+42,68);
  P64(IndexRoot+48,((uint64_t)1<<48)|5u);
  IndexRoot[112]=1;IndexRoot[113]=1;P16(IndexRoot+114,'W');
  P16(IndexRoot+136,16);P16(IndexRoot+140,RC95LDR_NTFS_INDEX_ENTRY_END);
  MakeFileRecord(Directory,5);P16(Directory+22,3);
  P32(Directory+56,0x90);P32(Directory+60,176);
  Directory[56+9]=4;P16(Directory+56+10,24);
  P32(Directory+56+16,144);P16(Directory+56+20,32);
  P16(Directory+56+24,0x24);P16(Directory+56+26,'I');
  P16(Directory+56+28,'3');P16(Directory+56+30,'0');
  memcpy(Directory+56+32,IndexRoot,sizeof(IndexRoot));
  P32(Directory+56+176,RC95LDR_NTFS_ATTRIBUTE_END);
  P32(Directory+24,56+176+4);
  P32(Directory+28,1024);
  P16(Directory+510,0xaaaa);P16(Directory+1022,0xaaaa);
  MakeFileRecord(Child,6);
  P32(Child+56,0x80);P32(Child+60,32);
  P32(Child+56+16,4);P16(Child+56+20,24);
  memcpy(Child+56+24,"MZ!!",4);
  P32(Child+56+32,RC95LDR_NTFS_ATTRIBUTE_END);
  P32(Child+24,56+32+4);

  { RC95LDR_STATUS TestStatus=Rc95LdrNtfsReadFilePrefix(&V,Path,2,Scratch,sizeof(Scratch),
       Out,sizeof(Out),&ReadBytes,&FileSize);
    if(TestStatus!=Rc95LdrSuccess||ReadBytes!=4||FileSize!=4||
       memcmp(Out,"MZ!!",4)!=0){
      fprintf(stderr,"resident root lookup status=%d bytes=%lu size=%llu\\n",
          (int)TestStatus,(unsigned long)ReadBytes,(unsigned long long)FileSize);
      return TestFailure(11,"ntfs_volume");
    }
  }

  /* A miss in the resident root must walk allocated INDX buffers using the
     directory bitmap, not fall back to a whole-MFT filename scan. */
  {
   uint8_t *Index=Disk+70u*512u;
   uint8_t *A=Directory+56;
   uint8_t *Alloc=Directory+232;
   uint8_t *Map=Directory+312;
   unsigned I;
   memset(Directory,0,1024);
   MakeFileRecord(Directory,5);P16(Directory+22,3);
   /* Resident $I30 root: key "A" and a child pointer to INDX VCN zero. */
   P32(A,0x90);P32(A+4,176);A[9]=4;P16(A+10,24);
   P32(A+16,144);P16(A+20,32);P16(A+24,0x24);
   P16(A+26,'I');P16(A+28,'3');P16(A+30,'0');
   P32(A+32,0x30);P32(A+36,1);P32(A+40,4096);
   A[44]=1;P32(A+48,16);P32(A+52,128);P32(A+56,128);A[60]=1;
   P64(A+64,((uint64_t)1<<48)|6u);P16(A+72,96);P16(A+74,68);
   P16(A+76,RC95LDR_NTFS_INDEX_ENTRY_NODE);
   P64(A+80,((uint64_t)1<<48)|5u);
   A[144]=1;A[145]=1;P16(A+146,'A');P64(A+152,0);
   P16(A+168,16);P16(A+172,RC95LDR_NTFS_INDEX_ENTRY_END);
   /* Nonresident $INDEX_ALLOCATION::$I30 at LCN 70, eight clusters. */
   P32(Alloc,0xa0);P32(Alloc+4,80);Alloc[8]=1;Alloc[9]=4;
   P16(Alloc+10,64);P16(Alloc+32,72);P16(Alloc+64,'I');
   P16(Alloc+66,'3');P16(Alloc+68,'0');P16(Alloc+70,0);
   P64(Alloc+16,0);P64(Alloc+24,7);P64(Alloc+40,4096);
   P64(Alloc+48,4096);P64(Alloc+56,4096);
   Alloc[72]=0x11;Alloc[73]=8;Alloc[74]=70;Alloc[75]=0;
   /* Resident $BITMAP::$I30, first index buffer allocated. */
   P32(Map,0xb0);P32(Map+4,40);Map[9]=4;P16(Map+10,24);
   P16(Map+20,32);P16(Map+24,'I');P16(Map+26,'3');P16(Map+28,'0');
   P32(Map+16,1);Map[32]=1;
   P32(Directory+312+40,RC95LDR_NTFS_ATTRIBUTE_END);
   P32(Directory+24,356);
   /* INDX buffer with a single matching child entry, plus valid USA. */
   memset(Index,0,4096);P32(Index,RC95LDR_NTFS_INDEX_BUFFER_MAGIC);
   P16(Index+4,40);P16(Index+6,9);P64(Index+16,0);
   P32(Index+24,40);P32(Index+28,152);P32(Index+32,4072);
   for(I=0;I<9;I++)P16(Index+40+I*2, I? (uint16_t)(0x1110u+I):0xaaaa);
   for(I=1;I<9;I++)P16(Index+I*512-2,0xaaaa);
   P64(Index+64,((uint64_t)1<<48)|6u);
   P16(Index+72,96);P16(Index+74,68);
   P64(Index+80,((uint64_t)1<<48)|5u);
   Index[144]=1;Index[145]=1;P16(Index+146,'W');
   P16(Index+168,16);P16(Index+172,RC95LDR_NTFS_INDEX_ENTRY_END);
   P16(Index+510,0xaaaa);P16(Index+1022,0xaaaa);
   P16(Index+1534,0xaaaa);P16(Index+2046,0xaaaa);
   P16(Index+2558,0xaaaa);P16(Index+3070,0xaaaa);P16(Index+3582,0xaaaa);
   P16(Index+42,0x1111);P16(Index+44,0x2222);P16(Index+46,0x3333);
   P16(Index+48,0x4444);P16(Index+50,0x5555);P16(Index+52,0x6666);
   P16(Index+54,0x7777);P16(Index+56,0x8888);
   if(Rc95LdrNtfsReadFilePrefix(&V,Path,2,Scratch,sizeof(Scratch),
       Out,sizeof(Out),&ReadBytes,&FileSize)!=Rc95LdrSuccess||
       ReadBytes!=4||FileSize!=4||memcmp(Out,"MZ!!",4)!=0)return TestFailure(12,"ntfs_volume");
   if(Rc95LdrNtfsReadFilePrefix(&V,Path,2,Scratch,2048,
       Out,sizeof(Out),&ReadBytes,&FileSize)!=Rc95LdrBufferTooSmall)return TestFailure(14,"ntfs_volume");

   /* A set bitmap bit cannot make a buffer with the wrong VCN acceptable. */
   P64(Index+16,1);
   if(Rc95LdrNtfsReadFilePrefix(&V,Path,2,Scratch,sizeof(Scratch),
       Out,sizeof(Out),&ReadBytes,&FileSize)!=Rc95LdrCorruptData)return TestFailure(15,"ntfs_volume");
   P64(Index+16,0);

   /* Clearing the allocation bit makes the same on-disk INDX block invisible. */
   Map[32]=0;
   /* Keep a matching $FILE_NAME in the MFT record but hide its INDX buffer.
      Discovery must fail: the MFT is no longer a directory-name fallback. */
   P32(Child+88,0x30);P32(Child+92,96);
   P32(Child+104,68);P16(Child+108,24);
   P64(Child+112,((uint64_t)1<<48)|5u);
   Child[176]=1;Child[177]=1;P16(Child+178,'W');
   P32(Child+184,RC95LDR_NTFS_ATTRIBUTE_END);
   P32(Child+24,188);
   if(Rc95LdrNtfsReadFilePrefix(&V,Path,2,Scratch,sizeof(Scratch),
       Out,sizeof(Out),&ReadBytes,&FileSize)!=Rc95LdrNotFound)return TestFailure(13,"ntfs_volume");
   Map[32]=1;

   /* Exercise the native filesystem adapter with a fragmented nonresident
      stream and an unaligned read crossing the run/sector boundary. */
   {
    RC95LDR_NTFS_PROVIDER_CONTEXT ProviderContext;
    RC95LDR_VOLUME MountedVolume;
    void *FileHandle=NULL;
    uint64_t OpenedFileSize=0;
    size_t WorkspaceBytes=0;
    uint8_t Readback[4];
    unsigned I;
    uint8_t *DataAttribute=Child+56;
    memmove(Child+128,Child+88,96); /* Preserve the indexed FILE_NAME attribute. */
    memset(DataAttribute,0,72);
    P32(DataAttribute,0x80);P32(DataAttribute+4,72);
    DataAttribute[8]=1;P64(DataAttribute+16,0);P64(DataAttribute+24,1);
    P16(DataAttribute+32,64);
    P64(DataAttribute+40,1024);P64(DataAttribute+48,1024);
    P64(DataAttribute+56,1024);
    DataAttribute[64]=0x11;DataAttribute[65]=1;
    DataAttribute[66]=100;DataAttribute[67]=0x11;
    DataAttribute[68]=1;DataAttribute[69]=20;DataAttribute[70]=0;
    P32(Child+224,RC95LDR_NTFS_ATTRIBUTE_END);
    P32(Child+24,228);
    for(I=0;I<512u;I++)Disk[100u*512u+I]='A';
    for(I=0;I<512u;I++)Disk[120u*512u+I]='B';
    if(Rc95LdrNtfsFileSystemWorkspaceSize(&V,&WorkspaceBytes)!=
       Rc95LdrSuccess||WorkspaceBytes>sizeof(ProviderWorkspace))
       return TestFailure(20,"ntfs_provider");
    if(Rc95LdrNtfsBindVolume(&V,&ProviderContext,ProviderWorkspace,
       sizeof(ProviderWorkspace),&MountedVolume)!=Rc95LdrSuccess)
       return TestFailure(21,"ntfs_provider");
    if(!MountedVolume.FileSystem||!MountedVolume.FileSystem->OpenRead||
       !MountedVolume.FileSystem->ReadAt||!MountedVolume.FileSystem->Close)
       return TestFailure(22,"ntfs_provider");
    if(MountedVolume.FileSystem->OpenRead(
       MountedVolume.FileSystem->FileSystemContext,"\\\\W",
       &FileHandle,&OpenedFileSize)!=Rc95LdrSuccess||
       !FileHandle||OpenedFileSize!=1024)
       return TestFailure(23,"ntfs_provider");
    if(MountedVolume.FileSystem->ReadAt(
       MountedVolume.FileSystem->FileSystemContext,FileHandle,510,
       Readback,sizeof(Readback))!=Rc95LdrSuccess||
       Readback[0]!='A'||Readback[1]!='A'||
       Readback[2]!='B'||Readback[3]!='B')
       return TestFailure(24,"ntfs_provider");
    MountedVolume.FileSystem->Close(
       MountedVolume.FileSystem->FileSystemContext,FileHandle);
    Rc95LdrNtfsDestroyFileSystem(&ProviderContext);
   }
   { int UefiTest=TestUefiNativeMount(); if(UefiTest)return UefiTest; }
  }
 }
 /* The bootstrap FILE record must remain inside BPB TotalSectors, even if
    the enclosing partition has additional trailing blocks. A failed remount
    must also clear the previously mounted volume state. */
 P64(B+48,199);
 if(Rc95LdrNtfsMountPartition(&V,&D,0,DISK_BLOCKS,Scratch,sizeof(Scratch))!=
       Rc95LdrCorruptData||V.Mounted)return TestFailure(16,"ntfs_volume");
 P64(B+48,4);
 /* MFT record zero cannot claim a different record identity. */
 P32(Disk+4u*512u+44,1);
 if(Rc95LdrNtfsMountPartition(&V,&D,0,DISK_BLOCKS,Scratch,sizeof(Scratch))!=
       Rc95LdrCorruptData||V.Mounted)return TestFailure(17,"ntfs_volume");
 P32(Disk+4u*512u+44,0);
 Rc95LdrNtfsUnmount(&V);
 puts("RC95LDR NTFS volume tests passed");return 0;
}
