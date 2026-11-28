#include "rc95ldr_ntfs_stream.h"
#include <stdio.h>
#include <string.h>
#define BLOCKS 128u
static uint8_t Disk[BLOCKS*512u];
static void P16(uint8_t *P,uint16_t V){P[0]=(uint8_t)V;P[1]=(uint8_t)(V>>8);}
static void P32(uint8_t *P,uint32_t V){P[0]=(uint8_t)V;P[1]=(uint8_t)(V>>8);P[2]=(uint8_t)(V>>16);P[3]=(uint8_t)(V>>24);}
static void P64(uint8_t *P,uint64_t V){unsigned I;for(I=0;I<8;I++)P[I]=(uint8_t)(V>>(I*8));}
static RC95LDR_STATUS Read(void *C,uint64_t L,uint32_t N,void *B,size_t Bytes)
{(void)C;if(L>BLOCKS||N>BLOCKS-L||Bytes!=(size_t)N*512u)return Rc95LdrOutOfBounds;memcpy(B,Disk+L*512u,Bytes);return Rc95LdrSuccess;}
static void MakeRecord(uint8_t *R,uint16_t Sequence,uint32_t Number,uint16_t Flags,uint64_t Base)
{
 memset(R,0,1024);P32(R,RC95LDR_NTFS_FILE_MAGIC);P16(R+4,48);P16(R+6,3);
 P16(R+16,Sequence);P16(R+20,56);P16(R+22,Flags);P32(R+24,60);
 P32(R+28,1024);P64(R+32,Base);P32(R+44,Number);
 P16(R+48,0xaaaa);P16(R+50,0x1111);P16(R+52,0x2222);
 P16(R+510,0xaaaa);P16(R+1022,0xaaaa);
}
static void MakeIndexExtent(uint8_t *A,uint64_t Lowest,uint64_t Highest,
                            uint16_t Id,uint8_t Lcn)
{
 P32(A,0xa0);P32(A+4,80);A[8]=1;A[9]=4;P16(A+10,64);
 P16(A+14,Id);P64(A+16,Lowest);P64(A+24,Highest);
 P16(A+32,72);P64(A+40,2048);P64(A+48,2048);P64(A+56,2048);
 P16(A+64,0x24);P16(A+66,'I');P16(A+68,'3');P16(A+70,'0');
 A[72]=0x11;A[73]=2;A[74]=Lcn;A[75]=0;
}
int main(void)
{
 RC95LDR_BLOCK_DEVICE Device={NULL,BLOCKS,512,Read};
 RC95LDR_NTFS_VOLUME V;
 RC95LDR_NTFS_STREAM Stream;
 uint8_t *Base=Disk+30u*512u,*Extension=Disk+32u*512u;
 uint8_t Scratch[1024],Output[2048];
 uint16_t IndexName[4]={0x24,'I','3','0'};
 uint64_t BaseRef=(UINT64_C(1)<<48)|5u;
 size_t I;
 memset(Disk,0,sizeof(Disk));memset(&V,0,sizeof(V));
 V.DeviceStorage=Device;V.PartitionBlockCount=BLOCKS;V.Mounted=1;
 V.Geometry.BytesPerSector=512;V.Geometry.SectorsPerCluster=1;
 V.Geometry.BytesPerCluster=512;V.Geometry.ClusterCount=BLOCKS;
 V.Geometry.FileRecordBytes=1024;
 V.MftRuns[0].Vcn=0;V.MftRuns[0].Lcn=4;V.MftRuns[0].ClusterCount=10;
 V.MftRuns[1].Vcn=10;V.MftRuns[1].Lcn=30;V.MftRuns[1].ClusterCount=20;
 V.MftRunCount=2;V.MftDataSize=30u*512u;
 MakeRecord(Base,1,5,3,0);MakeIndexExtent(Base+56,0,1,1,50);
 /* Resident $ATTRIBUTE_LIST: base extent and extension extent. */
 P32(Base+136,RC95LDR_NTFS_ATTRIBUTE_LIST);P32(Base+140,104);
 P32(Base+152,80);P16(Base+156,24);
 P32(Base+160,0xa0);P16(Base+164,40);Base[166]=4;Base[167]=28;
 P64(Base+168,0);P64(Base+176,BaseRef);P16(Base+184,1);
 P16(Base+188,0x24);P16(Base+190,'I');P16(Base+192,'3');P16(Base+194,'0');
 P32(Base+200,0xa0);P16(Base+204,40);Base[206]=4;Base[207]=28;
 P64(Base+208,2);P64(Base+216,(UINT64_C(2)<<48)|6u);P16(Base+224,2);
 P16(Base+228,0x24);P16(Base+230,'I');P16(Base+232,'3');P16(Base+234,'0');
 P32(Base+240,RC95LDR_NTFS_ATTRIBUTE_END);P32(Base+24,244);
 MakeRecord(Extension,2,6,1,BaseRef);MakeIndexExtent(Extension+56,2,3,2,60);
 P32(Extension+136,RC95LDR_NTFS_ATTRIBUTE_END);P32(Extension+24,140);
 memset(Disk+50u*512u,'A',1024);memset(Disk+60u*512u,'B',1024);
 /* Attribute-list extension records require a real destination buffer. */
 if(Rc95LdrNtfsCollectStream(&V,Base,1024,BaseRef,0xa0,IndexName,4,
      NULL,sizeof(Scratch),&Stream)!=Rc95LdrInvalidParameter)return 16;
 if(Rc95LdrNtfsCollectStream(&V,Base,1024,BaseRef,0xa0,IndexName,4,
      Scratch,sizeof(Scratch),&Stream)!=Rc95LdrSuccess)return 1;
 if(Stream.RunCount!=2||Stream.Runs[0].Vcn!=0||Stream.Runs[0].Lcn!=50||
    Stream.Runs[0].ClusterCount!=2||Stream.Runs[1].Vcn!=2||
    Stream.Runs[1].Lcn!=60||Stream.Runs[1].ClusterCount!=2||
    Stream.DataSize!=2048)return 2;
 if(Rc95LdrNtfsReadStreamRange(&V,&Stream,0,Output,sizeof(Output),
      Scratch,512)!=Rc95LdrSuccess)return 3;
 for(I=0;I<1024;I++)if(Output[I]!='A')return 4;
 for(I=1024;I<2048;I++)if(Output[I]!='B')return 5;
 /* A forged physical run pointer must be rejected before the block callback. */
 {
  uint64_t SavedLcn=Stream.Runs[1].Lcn;
  Stream.Runs[1].Lcn=BLOCKS;
  if(Rc95LdrNtfsReadStreamRange(&V,&Stream,0,Output,sizeof(Output),
       Scratch,512)!=Rc95LdrCorruptData)return 15;
  Stream.Runs[1].Lcn=SavedLcn;
 }
 /* Bytes beyond InitializedSize must be zero, not stale disk contents. */
 Stream.InitializedSize=600;
 memset(Output,0xcc,sizeof(Output));
 if(Rc95LdrNtfsReadStreamRange(&V,&Stream,0,Output,sizeof(Output),
      Scratch,512)!=Rc95LdrSuccess)return 10;
 for(I=0;I<600;I++)if(Output[I]!='A')return 11;
 for(I=600;I<sizeof(Output);I++)if(Output[I]!=0)return 12;
 memset(Output,0xcc,512);
 if(Rc95LdrNtfsReadStreamRange(&V,&Stream,1024,Output,512,
      Scratch,512)!=Rc95LdrSuccess)return 13;
 for(I=0;I<512;I++)if(Output[I]!=0)return 14;
 Stream.InitializedSize=2048;
 /* A stale extension sequence must not be accepted. */
 P16(Extension+16,3);
 if(Rc95LdrNtfsCollectStream(&V,Base,1024,BaseRef,0xa0,IndexName,4,
      Scratch,sizeof(Scratch),&Stream)!=Rc95LdrNotFound)return 6;
 P16(Extension+16,2);
 /* An extension must point back to the exact base FILE reference. */
 P64(Extension+32,0);
 if(Rc95LdrNtfsCollectStream(&V,Base,1024,BaseRef,0xa0,IndexName,4,
      Scratch,sizeof(Scratch),&Stream)!=Rc95LdrCorruptData)return 7;
 P64(Extension+32,BaseRef);
 /* Physical clusters may not alias an earlier extent. */
 Extension[56+74]=50;
 if(Rc95LdrNtfsCollectStream(&V,Base,1024,BaseRef,0xa0,IndexName,4,
      Scratch,sizeof(Scratch),&Stream)!=Rc95LdrCorruptData)return 8;
 Extension[56+74]=60;
 /* Attribute-list VCNs must be contiguous. */
 P64(Base+208,3);
 if(Rc95LdrNtfsCollectStream(&V,Base,1024,BaseRef,0xa0,IndexName,4,
      Scratch,sizeof(Scratch),&Stream)!=Rc95LdrCorruptData)return 9;
 puts("RC95LDR NTFS stream tests passed");return 0;
}
