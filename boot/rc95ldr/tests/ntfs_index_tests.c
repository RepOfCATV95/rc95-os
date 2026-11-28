#include "rc95ldr_ntfs_index.h"
#include "rc95ldr_ntfs_record.h"
#include <stdio.h>
#include <string.h>
static void P16(uint8_t *P,uint16_t V){P[0]=(uint8_t)V;P[1]=(uint8_t)(V>>8);}
static void P32(uint8_t *P,uint32_t V){P[0]=(uint8_t)V;P[1]=(uint8_t)(V>>8);P[2]=(uint8_t)(V>>16);P[3]=(uint8_t)(V>>24);}
static void P64(uint8_t *P,uint64_t V){unsigned I;for(I=0;I<8;I++)P[I]=(uint8_t)(V>>(I*8));}
static unsigned Seen;
static RC95LDR_STATUS Visit(void *C,const RC95LDR_NTFS_INDEX_ENTRY *E)
{(void)C;if(E->Flags&RC95LDR_NTFS_INDEX_ENTRY_END)return Rc95LdrSuccess;if(E->FileReference!=0x000700000000002au||E->KeyLength!=70||!(E->Flags&RC95LDR_NTFS_INDEX_ENTRY_NODE)||E->ChildVcn!=9)return Rc95LdrCorruptData;Seen++;return Rc95LdrSuccess;}
int main(void)
{
 uint8_t V[160];size_t Count;RC95LDR_NTFS_FILE_NAME_KEY Name;
 memset(V,0,sizeof(V));P32(V,0x30);P32(V+4,1);P32(V+8,4096);V[12]=1;
 P32(V+16,16);P32(V+20,128);P32(V+24,128);V[28]=1;
 P64(V+32,0x000700000000002aULL);P16(V+40,96);P16(V+42,70);P16(V+44,1);
 P64(V+48,0x0001000000000001ULL);P32(V+64,0);P32(V+68,0);
 /* FILE_NAME key: parent reference, fixed metadata, length and namespace. */
 P64(V+48,0x0001000000000001ULL);V[32+16+64]=2;V[32+16+65]=1;
 P16(V+32+16+66,'A');P16(V+32+16+68,'B');
 P64(V+120,9);
 /* End entry occupies the remaining 24 bytes. */
 P16(V+136,16);P16(V+138,0);P16(V+140,RC95LDR_NTFS_INDEX_ENTRY_END);
 Seen=0;
 if(Rc95LdrNtfsEnumerateIndexRoot(V,sizeof(V),4096,Visit,NULL,&Count)!=Rc95LdrSuccess||Count!=1||Seen!=1)return 1;
 if(Rc95LdrNtfsDecodeFileNameKey(V+48,70,&Name)!=Rc95LdrSuccess||
    Name.NameLength!=2||Name.Name[0]!='A'||Name.Name[1]!='B'||Name.Namespace!=1)return 2;
 V[32+16+64]=0;
 if(Rc95LdrNtfsDecodeFileNameKey(V+48,68,&Name)!=Rc95LdrCorruptData)return 3;
 V[32+16+64]=2;P16(V+40,80);
 if(Rc95LdrNtfsEnumerateIndexRoot(V,sizeof(V),4096,Visit,NULL,&Count)!=Rc95LdrCorruptData)return 4;
 P16(V+40,96);P16(V+136,8); /* truncated end entry */
 if(Rc95LdrNtfsEnumerateIndexRoot(V,sizeof(V),4096,Visit,NULL,&Count)!=Rc95LdrCorruptData)return 5;
 P16(V+136,16); /* Restore the valid terminal entry for directory integration. */

 {
  uint8_t Record[1024];size_t DirectoryCount=0;
  memset(Record,0,sizeof(Record));P32(Record,RC95LDR_NTFS_FILE_MAGIC);
  P16(Record+20,56);P32(Record+24,252);P32(Record+28,1024);
  P32(Record+56,0x90);P32(Record+60,192);Record[65]=4;
  P16(Record+66,24);P32(Record+72,160);P16(Record+76,32);
  P16(Record+80,0x24);
  P16(Record+82,'I');P16(Record+84,'3');P16(Record+86,'0');
  memcpy(Record+88,V,160);P32(Record+248,RC95LDR_NTFS_ATTRIBUTE_END);
  if(Rc95LdrNtfsEnumerateDirectoryIndexRoot(Record,sizeof(Record),512,
       4096,Visit,NULL,&DirectoryCount)!=Rc95LdrSuccess||DirectoryCount!=1)
      return 6;
  P32(Record+56,0x80);
  if(Rc95LdrNtfsEnumerateDirectoryIndexRoot(Record,sizeof(Record),512,
       4096,Visit,NULL,&DirectoryCount)!=Rc95LdrNotFound)return 7;
 }

 {
  uint8_t Buffer[4096];size_t BufferCount=0;
  memset(Buffer,0,sizeof(Buffer));P32(Buffer,RC95LDR_NTFS_INDEX_BUFFER_MAGIC);
  P16(Buffer+510,0xa55a);P16(Buffer+1022,0xa55a);
  P16(Buffer+1534,0xa55a);P16(Buffer+2046,0xa55a);
  P16(Buffer+2558,0xa55a);P16(Buffer+3070,0xa55a);
  P16(Buffer+3582,0xa55a);P16(Buffer+4094,0xa55a);
  P16(Buffer+40,0xa55a);
  P16(Buffer+42,0x1111);P16(Buffer+44,0x2222);
  P16(Buffer+46,0x3333);P16(Buffer+48,0x4444);
  P16(Buffer+50,0x5555);P16(Buffer+52,0x6666);
  P16(Buffer+54,0x7777);P16(Buffer+56,0x8888);
  P16(Buffer+4,40);P16(Buffer+6,9);P64(Buffer+16,7);
  P32(Buffer+24,40);P32(Buffer+28,152);P32(Buffer+32,4072);Buffer[36]=1;
  memcpy(Buffer+64,V+32,96);memcpy(Buffer+160,V+128,16);
  P16(Buffer+510,0xdead);
  if(Rc95LdrNtfsApplyIndexBufferFixups(Buffer,sizeof(Buffer),512)!=Rc95LdrCorruptData)return 8;
  P16(Buffer+510,0xa55a);
  if(Rc95LdrNtfsApplyIndexBufferFixups(Buffer,sizeof(Buffer),512)!=Rc95LdrSuccess||
     Rc95LdrReadU16(Buffer+510)!=0x1111||Rc95LdrReadU16(Buffer+4094)!=0x8888)
      return 8;
  if(Rc95LdrNtfsEnumerateIndexBuffer(Buffer,sizeof(Buffer),512,7,
       Visit,NULL,&BufferCount)!=Rc95LdrSuccess||BufferCount!=1)return 8;
  if(Rc95LdrNtfsEnumerateIndexBuffer(Buffer,sizeof(Buffer),512,8,
       Visit,NULL,&BufferCount)!=Rc95LdrCorruptData)return 9;
  P16(Buffer+4,500);
  if(Rc95LdrNtfsEnumerateIndexBuffer(Buffer,sizeof(Buffer),512,7,
       Visit,NULL,&BufferCount)!=Rc95LdrCorruptData)return 10;
  P16(Buffer+4,40);
  P32(Buffer+24,151);
  if(Rc95LdrNtfsEnumerateIndexBuffer(Buffer,sizeof(Buffer),512,7,
       Visit,NULL,&BufferCount)!=Rc95LdrCorruptData)return 10;
 }
 puts("RC95LDR NTFS index tests passed");return 0;
}
