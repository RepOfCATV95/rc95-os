#include "rc95ldr_ntfs_record.h"
#include <stdio.h>
#include <string.h>
static void P16(uint8_t *P,uint16_t V){P[0]=(uint8_t)V;P[1]=(uint8_t)(V>>8);}
static void P32(uint8_t *P,uint32_t V){P[0]=(uint8_t)V;P[1]=(uint8_t)(V>>8);P[2]=(uint8_t)(V>>16);P[3]=(uint8_t)(V>>24);}
static void P64(uint8_t *P,uint64_t V){P32(P,(uint32_t)V);P32(P+4,(uint32_t)(V>>32));}
static void Make(uint8_t *R){
 memset(R,0,1024);P32(R,RC95LDR_NTFS_FILE_MAGIC);P16(R+4,48);P16(R+6,3);
 P16(R+48,0xaaaa);P16(R+50,0x1111);P16(R+52,0x2222);
 P16(R+20,56);P32(R+24,100);P32(R+28,1024);
 P32(R+56,0x80);P32(R+60,40);P16(R+70,1);P32(R+72,4);P16(R+76,24);
 memcpy(R+80,"DATA",4);P32(R+96,RC95LDR_NTFS_ATTRIBUTE_END);
 P16(R+510,0xaaaa);P16(R+1022,0xaaaa);
}
static RC95LDR_STATUS CountEntry(void *Context,const RC95LDR_NTFS_ATTRIBUTE_LIST_ENTRY *Entry){
 size_t *Count=(size_t*)Context;
 if(Entry->Type!=0x80u||Entry->LowestVcn!=0u||
    (Entry->FileReference&0xffffu)!=42u||Entry->AttributeId!=7u||
    Entry->NameLength!=4u)return Rc95LdrCorruptData;
 ++*Count;return Rc95LdrSuccess;
}
typedef struct _ATTRIBUTE_COUNT_CONTEXT { size_t Count; } ATTRIBUTE_COUNT_CONTEXT;
static RC95LDR_STATUS CountAttribute(void *Opaque,
    const RC95LDR_NTFS_ATTRIBUTE *Attribute)
{
    ATTRIBUTE_COUNT_CONTEXT *Context=(ATTRIBUTE_COUNT_CONTEXT*)Opaque;
    if(Attribute->Type!=0x30u||Attribute->ValueLength!=70u)
        return Rc95LdrCorruptData;
    ++Context->Count;
    return Rc95LdrSuccess;
}
int main(void){
 uint8_t R[1024];RC95LDR_NTFS_ATTRIBUTE A;RC95LDR_NTFS_FILE_RECORD_INFO I;Make(R);
 if(Rc95LdrNtfsApplyFileRecordFixups(R,sizeof(R),512)!=Rc95LdrSuccess)return 1;
 if(Rc95LdrReadU16(R+510)!=0x1111||Rc95LdrReadU16(R+1022)!=0x2222)return 2;
 if(Rc95LdrNtfsGetFileRecordInfo(R,sizeof(R),&I)!=Rc95LdrSuccess||I.SequenceNumber!=0||I.BytesInUse!=100)return 3;
 if(Rc95LdrNtfsFindAttribute(R,sizeof(R),512,0x80,&A)!=Rc95LdrSuccess||
    A.NonResident||A.ValueLength!=4||memcmp(A.Value,"DATA",4)||A.Instance!=1)return 4;
 if(Rc95LdrNtfsFindAttribute(R,sizeof(R),512,0x90,&A)!=Rc95LdrNotFound)return 5;
 Make(R);P16(R+510,0xffff);
 if(Rc95LdrNtfsApplyFileRecordFixups(R,sizeof(R),512)!=Rc95LdrCorruptData)return 6;
 Make(R);P32(R+60,8);
 if(Rc95LdrNtfsFindAttribute(R,sizeof(R),512,0x80,&A)!=Rc95LdrCorruptData)return 7;
 {
  uint16_t AttributeName[4]={0x24,'I','3','0'};
  Make(R);P32(R+56,0x90);P32(R+60,40);
  R[65]=4;P16(R+66,24);P32(R+72,4);P16(R+76,32);
  P16(R+80,0x24);P16(R+82,'I');P16(R+84,'3');P16(R+86,'0');
  memcpy(R+88,"ROOT",4);P32(R+96,RC95LDR_NTFS_ATTRIBUTE_END);
  if(Rc95LdrNtfsFindNamedAttribute(R,sizeof(R),512,0x90,AttributeName,4,&A)!=Rc95LdrSuccess||
     A.NameLength!=4||memcmp(A.Value,"ROOT",4))return 8;
  if(Rc95LdrNtfsFindNamedAttribute(R,sizeof(R),512,0x90,NULL,0,&A)!=Rc95LdrNotFound)return 9;
  AttributeName[1]='i';
  if(Rc95LdrNtfsFindNamedAttribute(R,sizeof(R),512,0x90,AttributeName,4,&A)!=Rc95LdrNotFound)return 10;
 }
 {
  uint64_t Reference=((uint64_t)5<<48)|42u;
  Make(R);P16(R+16,5);P16(R+22,1);P32(R+44,42);
  if(Rc95LdrNtfsValidateFileReference(R,sizeof(R),Reference)!=Rc95LdrSuccess)return 11;
  if(Rc95LdrNtfsValidateFileReference(R,sizeof(R),((uint64_t)4<<48)|42u)!=Rc95LdrNotFound)return 12;
  P16(R+22,0);
  if(Rc95LdrNtfsValidateFileReference(R,sizeof(R),Reference)!=Rc95LdrNotFound)return 13;
  P16(R+22,1);P32(R+44,43);
  if(Rc95LdrNtfsValidateFileReference(R,sizeof(R),Reference)!=Rc95LdrCorruptData)return 14;
 }
 {
  uint8_t List[40];size_t Seen=0,Entries=0;
  memset(List,0,sizeof(List));P32(List,0x80);P16(List+4,40);
  List[6]=4;List[7]=26;P64(List+8,0);P64(List+16,((uint64_t)3<<48)|42u);
  P16(List+24,7);P16(List+26,'$');P16(List+28,'D');
  P16(List+30,'A');P16(List+32,'T'); 
  if(Rc95LdrNtfsEnumerateAttributeList(List,sizeof(List),CountEntry,&Seen,&Entries)!=Rc95LdrSuccess||
     Entries!=1||Seen!=1)return 15;
  P16(List+4,24);
  if(Rc95LdrNtfsEnumerateAttributeList(List,sizeof(List),NULL,NULL,&Entries)!=Rc95LdrCorruptData)return 16;
  P16(List+4,40);List[7]=25;
  if(Rc95LdrNtfsEnumerateAttributeList(List,sizeof(List),NULL,NULL,&Entries)!=Rc95LdrCorruptData)return 17;
 }
 {
  ATTRIBUTE_COUNT_CONTEXT Context={0};
  size_t AttributeCount=0;
  Make(R);
  /* Append a second FILE_NAME attribute (a hard-link name). */
  P32(R+96,0x30);P32(R+100,96);P32(R+112,70);
  P16(R+116,24);P16(R+118,2);
  P64(R+120,((uint64_t)5<<48)|5u);
  R[184]=2;R[185]=1;P16(R+186,'O');P16(R+188,'K');
  P32(R+192,RC95LDR_NTFS_ATTRIBUTE_END);P32(R+24,196);
  if(Rc95LdrNtfsEnumerateAttributes(R,sizeof(R),512,0x30,
       CountAttribute,&Context,&AttributeCount)!=Rc95LdrSuccess||
     AttributeCount!=1||Context.Count!=1)return 18;
  if(Rc95LdrNtfsEnumerateAttributes(R,sizeof(R),512,0x80,
       NULL,NULL,&AttributeCount)!=Rc95LdrSuccess||AttributeCount!=1)return 19;
 }
 puts("RC95LDR NTFS record tests passed");return 0;
}
