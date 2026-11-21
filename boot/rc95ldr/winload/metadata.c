#include "rc95ldrmetadata.h"
#include <string.h>
#include <stdint.h>
#include <limits.h>
typedef struct _RC95LDR_NT_NLS_DATA_BLOCK {
    RC95LDR_NT_PTR AnsiCodePageData,OemCodePageData,UnicodeCodePageData;
} RC95LDR_NT_NLS_DATA_BLOCK;
typedef struct _RC95LDR_NT_ARC_DISK_SIGNATURE {
    RC95LDR_NT_LIST_ENTRY ListEntry;
    uint32_t Signature;
    RC95LDR_NT_PTR ArcName;
    uint32_t Checksum;
    uint8_t ValidPartitionTable,ExtendedInt13,IsGpt,Reserved;
    uint8_t GptSignature[16];
} RC95LDR_NT_ARC_DISK_SIGNATURE;
typedef struct _RC95LDR_NT_ARC_DISK_INFORMATION {
    RC95LDR_NT_LIST_ENTRY DiskSignatureListHead;
} RC95LDR_NT_ARC_DISK_INFORMATION;
static int Add(size_t*n,size_t x){if(*n>SIZE_MAX-x)return 0;*n+=x;return 1;}
static size_t Align(size_t n,size_t a){return n>SIZE_MAX-(a-1)?SIZE_MAX:(n+a-1)&~(a-1);}
static int Overlap(const void*a,size_t an,const void*b,size_t bn){uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;if(!a||!b||an>UINTPTR_MAX-x||bn>UINTPTR_MAX-y)return 1;return x<y+bn&&y<x+an;}
static void InitList(RC95LDR_NT_LIST_ENTRY*h){h->Flink=(uintptr_t)h;h->Blink=(uintptr_t)h;}
static void InsertTail(RC95LDR_NT_LIST_ENTRY*h,RC95LDR_NT_LIST_ENTRY*e)
{RC95LDR_NT_LIST_ENTRY*t=(void*)(uintptr_t)h->Blink;e->Flink=(uintptr_t)h;e->Blink=(uintptr_t)t;t->Flink=(uintptr_t)e;h->Blink=(uintptr_t)e;}
RC95LDR_STATUS Rc95LdrBuildLoaderMetadata(const RC95LDR_METADATA_INPUT*i,RC95LDR_METADATA_RESULT*r)
{
    uint8_t*b;size_t need=0,arcAt,diskAt,namesAt,at;uint32_t j;
    RC95LDR_NT_NLS_DATA_BLOCK*nls;RC95LDR_NT_ARC_DISK_INFORMATION*arc;
    if(!i||!r||i->StructureSize<sizeof(*i)||i->Version!=RC95LDR_METADATA_VERSION||
       !i->Buffer||!i->BufferSize||((uintptr_t)i->Buffer&(sizeof(void*)-1))||
       i->BufferSize>UINTPTR_MAX-(uintptr_t)i->Buffer||i->DiskCount>RC95LDR_ARC_DISK_MAX||
       (i->DiskCount&&!i->Disks))return Rc95LdrInvalidParameter;
    if(!i->Nls.AnsiData||!i->Nls.AnsiSize||!i->Nls.OemData||!i->Nls.OemSize||
       !i->Nls.UnicodeData||!i->Nls.UnicodeSize)return Rc95LdrInvalidParameter;
    if(Overlap(i->Buffer,i->BufferSize,i->Nls.AnsiData,i->Nls.AnsiSize)||
       Overlap(i->Buffer,i->BufferSize,i->Nls.OemData,i->Nls.OemSize)||
       Overlap(i->Buffer,i->BufferSize,i->Nls.UnicodeData,i->Nls.UnicodeSize))return Rc95LdrInvalidParameter;
    if(!Add(&need,sizeof(*nls)))return Rc95LdrIntegerOverflow;
    need=Align(need,4);if(!Add(&need,i->Nls.AnsiSize)||!Add(&need,i->Nls.OemSize)||!Add(&need,i->Nls.UnicodeSize))return Rc95LdrIntegerOverflow;
    need=Align(need,sizeof(void*));if(!Add(&need,sizeof(*arc)))return Rc95LdrIntegerOverflow;
    if(i->DiskCount>(SIZE_MAX-need)/sizeof(RC95LDR_NT_ARC_DISK_SIGNATURE))return Rc95LdrIntegerOverflow;
    need+=i->DiskCount*sizeof(RC95LDR_NT_ARC_DISK_SIGNATURE);
    for(j=0;j<i->DiskCount;j++){size_t n;if(!i->Disks[j].ArcName)return Rc95LdrInvalidParameter;n=strlen(i->Disks[j].ArcName)+1;if(n>32767u||Overlap(i->Buffer,i->BufferSize,i->Disks[j].ArcName,n)||!Add(&need,n))return Rc95LdrInvalidParameter;}
    if(need>i->BufferSize)return Rc95LdrBufferTooSmall;
    b=(uint8_t*)i->Buffer;memset(b,0,need);
    nls=(void*)b;at=Align(sizeof(*nls),4);
    nls->AnsiCodePageData=(uintptr_t)(b+at);memcpy(b+at,i->Nls.AnsiData,i->Nls.AnsiSize);at+=i->Nls.AnsiSize;
    nls->OemCodePageData=(uintptr_t)(b+at);memcpy(b+at,i->Nls.OemData,i->Nls.OemSize);at+=i->Nls.OemSize;
    nls->UnicodeCodePageData=(uintptr_t)(b+at);memcpy(b+at,i->Nls.UnicodeData,i->Nls.UnicodeSize);at+=i->Nls.UnicodeSize;
    arcAt=Align(at,sizeof(void*));arc=(void*)(b+arcAt);InitList(&arc->DiskSignatureListHead);
    diskAt=arcAt+sizeof(*arc);namesAt=diskAt+(size_t)i->DiskCount*sizeof(RC95LDR_NT_ARC_DISK_SIGNATURE);
    for(j=0;j<i->DiskCount;j++){
        RC95LDR_NT_ARC_DISK_SIGNATURE*d=(void*)(b+diskAt+(size_t)j*sizeof(*d));
        size_t n=strlen(i->Disks[j].ArcName)+1;
        d->Signature=i->Disks[j].Signature;d->Checksum=i->Disks[j].Checksum;
        d->ValidPartitionTable=i->Disks[j].ValidPartitionTable;d->ExtendedInt13=i->Disks[j].ExtendedInt13;
        d->IsGpt=i->Disks[j].IsGpt;d->Reserved=i->Disks[j].Reserved;
        memcpy(d->GptSignature,i->Disks[j].GptSignature,16);
        d->ArcName=(uintptr_t)(b+namesAt);memcpy(b+namesAt,i->Disks[j].ArcName,n);namesAt+=n;
        InsertTail(&arc->DiskSignatureListHead,&d->ListEntry);
    }
    r->NlsData=(uintptr_t)nls;r->ArcDiskInformation=(uintptr_t)arc;r->BytesUsed=need;
    return Rc95LdrSuccess;
}
