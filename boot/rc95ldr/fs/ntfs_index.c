#include "rc95ldr_ntfs_index.h"
#include "rc95ldr_ntfs_record.h"
#include <string.h>
static int NtfsIndexRange(size_t Total,size_t Offset,size_t Length)
{return Offset<=Total&&Length<=Total-Offset;}
RC95LDR_STATUS
Rc95LdrNtfsEnumerateIndexRoot(const void *Value,size_t ValueLength,
    uint32_t ExpectedIndexBufferBytes,RC95LDR_NTFS_INDEX_CALLBACK Callback,
    void *Context,size_t *EntryCount)
{
    const uint8_t *V=(const uint8_t*)Value;
    uint32_t IndexedType,Collation,BufferBytes,EntriesOffset,IndexLength,Allocated;
    uint8_t HeaderFlags;size_t Offset,End,Count=0;int Terminated=0;
    if(!V||!EntryCount||ValueLength<32u)return Rc95LdrInvalidParameter;
    *EntryCount=0;
    IndexedType=Rc95LdrReadU32(V);Collation=Rc95LdrReadU32(V+4);
    BufferBytes=Rc95LdrReadU32(V+8);
    if(IndexedType!=RC95LDR_NTFS_INDEX_FILE_NAME||Collation!=1u||
       !BufferBytes||(ExpectedIndexBufferBytes&&BufferBytes!=ExpectedIndexBufferBytes)||
       !V[12]||V[13]||V[14]||V[15])
        return Rc95LdrCorruptData;
    EntriesOffset=Rc95LdrReadU32(V+16);
    IndexLength=Rc95LdrReadU32(V+20);
    Allocated=Rc95LdrReadU32(V+24);
    HeaderFlags=V[28];
    if((HeaderFlags&~1u)||V[29]||V[30]||V[31]||
       EntriesOffset<16u||(EntriesOffset&7u)||
       IndexLength<EntriesOffset||Allocated<IndexLength||
       !NtfsIndexRange(ValueLength,16u,Allocated))
        return Rc95LdrCorruptData;
    Offset=16u+(size_t)EntriesOffset;
    End=16u+(size_t)IndexLength;
    if(Offset>End||End>ValueLength)return Rc95LdrCorruptData;
    while(Offset<End){
        RC95LDR_NTFS_INDEX_ENTRY Entry;
        uint16_t Length,KeyLength,Flags;
        size_t ChildBytes,Minimum;
        if(!NtfsIndexRange(End,Offset,16u))return Rc95LdrCorruptData;
        Length=Rc95LdrReadU16(V+Offset+8);
        KeyLength=Rc95LdrReadU16(V+Offset+10);
        Flags=Rc95LdrReadU16(V+Offset+12);
        if((Flags&~(RC95LDR_NTFS_INDEX_ENTRY_NODE|
                    RC95LDR_NTFS_INDEX_ENTRY_END))||
           Rc95LdrReadU16(V+Offset+14)||Length<16u||(Length&7u)||
           !NtfsIndexRange(End,Offset,Length))
            return Rc95LdrCorruptData;
        ChildBytes=(Flags&RC95LDR_NTFS_INDEX_ENTRY_NODE)?8u:0u;
        if((Flags&RC95LDR_NTFS_INDEX_ENTRY_NODE)&&!(HeaderFlags&1u))
            return Rc95LdrCorruptData;
        if((size_t)KeyLength>SIZE_MAX-16u-ChildBytes)
            return Rc95LdrIntegerOverflow;
        Minimum=16u+(size_t)KeyLength+ChildBytes;
        if(Minimum>Length)return Rc95LdrCorruptData;
        if(Flags&RC95LDR_NTFS_INDEX_ENTRY_END){
            if(KeyLength||Offset+Length!=End)return Rc95LdrCorruptData;
            Entry.FileReference=0;Entry.Key=NULL;Entry.KeyLength=0;
            Entry.Flags=Flags;
            Entry.ChildVcn=ChildBytes?Rc95LdrReadU64(V+Offset+Length-8u):0;
            if(Callback){
                RC95LDR_STATUS Status=Callback(Context,&Entry);
                if(Status!=Rc95LdrSuccess)return Status;
            }
            Terminated=1;Offset+=Length;break;
        }
        if(!KeyLength)return Rc95LdrCorruptData;
        Entry.FileReference=Rc95LdrReadU64(V+Offset);
        Entry.Key=V+Offset+16u;Entry.KeyLength=KeyLength;Entry.Flags=Flags;
        Entry.ChildVcn=ChildBytes?
            Rc95LdrReadU64(V+Offset+Length-8u):0;
        if(Callback){
            RC95LDR_STATUS Status=Callback(Context,&Entry);
            if(Status!=Rc95LdrSuccess)return Status;
        }
        if(Count==SIZE_MAX)return Rc95LdrIntegerOverflow;
        ++Count;Offset+=Length;
    }
    if(!Terminated||Offset!=End)return Rc95LdrCorruptData;
    *EntryCount=Count;
    return Rc95LdrSuccess;
}
RC95LDR_STATUS
Rc95LdrNtfsDecodeFileNameKey(const uint8_t *Key,size_t Length,
                             RC95LDR_NTFS_FILE_NAME_KEY *Name)
{
    uint8_t Characters,Namespace;size_t NameBytes,Required,I;
    if(!Key||!Name||Length<66u)return Rc95LdrInvalidParameter;
    Characters=Key[64];Namespace=Key[65];
    if(!Characters||Namespace>3u||
       !Rc95LdrMultiplySize((size_t)Characters,2u,&NameBytes)||
       !Rc95LdrAddSize(66u,NameBytes,&Required))
        return Rc95LdrCorruptData;
    if(Required!=Length)return Rc95LdrCorruptData;
    for(I=0;I<Characters;I++){
        uint16_t Ch=Rc95LdrReadU16(Key+66u+I*2u);
        if(!Ch||Ch=='/'||Ch=='\\')return Rc95LdrCorruptData;
    }
    Name->ParentFileReference=Rc95LdrReadU64(Key);
    Name->Name=(const uint16_t*)(const void*)(Key+66u);
    Name->NameLength=Characters;Name->Namespace=Namespace;
    return Rc95LdrSuccess;
}

RC95LDR_STATUS
Rc95LdrNtfsEnumerateDirectoryIndexRoot(const uint8_t *Record,
    size_t RecordLength,uint32_t BytesPerSector,uint32_t ExpectedIndexBufferBytes,
    RC95LDR_NTFS_INDEX_CALLBACK Callback,void *Context,size_t *EntryCount)
{
    static const uint16_t IndexName[4]={'$','I','3','0'};
    RC95LDR_NTFS_ATTRIBUTE Attribute;
    RC95LDR_STATUS Status;
    if(!Record||!EntryCount)return Rc95LdrInvalidParameter;
    Status=Rc95LdrNtfsFindNamedAttribute(Record,RecordLength,BytesPerSector,
        0x90u,IndexName,4u,&Attribute);
    if(Status!=Rc95LdrSuccess)return Status;
    if(Attribute.NonResident||Attribute.Flags||!Attribute.ValueLength)
        return Rc95LdrCorruptData;
    return Rc95LdrNtfsEnumerateIndexRoot(Attribute.Value,Attribute.ValueLength,
        ExpectedIndexBufferBytes,Callback,Context,EntryCount);
}

RC95LDR_STATUS
Rc95LdrNtfsApplyIndexBufferFixups(uint8_t *B,size_t Length,uint32_t SectorSize)
{
    uint16_t UsaOffset,UsaCount,Usn;
    size_t Expected,UsaBytes,I;
    if(!B||SectorSize<512u||SectorSize>4096u||
       (SectorSize&(SectorSize-1u))||Length<64u||Length%SectorSize)
        return Rc95LdrInvalidParameter;
    if(Rc95LdrReadU32(B)!=RC95LDR_NTFS_INDEX_BUFFER_MAGIC)
        return Rc95LdrInvalidImage;
    UsaOffset=Rc95LdrReadU16(B+4);UsaCount=Rc95LdrReadU16(B+6);
    Expected=Length/SectorSize+1u;
    if(Expected>UINT16_MAX||UsaCount!=Expected||(UsaOffset&1u)||
       UsaOffset<40u||UsaCount>SIZE_MAX/2u)
        return Rc95LdrCorruptData;
    UsaBytes=(size_t)UsaCount*2u;
    if(!NtfsIndexRange(Length,UsaOffset,UsaBytes)||
       UsaOffset>SectorSize-2u||UsaBytes>SectorSize-2u-UsaOffset)
        return Rc95LdrCorruptData;
    Usn=Rc95LdrReadU16(B+UsaOffset);
    if(!Usn)return Rc95LdrCorruptData;
    for(I=1;I<UsaCount;I++){
        size_t Trailer=I*(size_t)SectorSize-2u;
        if(!NtfsIndexRange(Length,Trailer,2u)||
           Rc95LdrReadU16(B+Trailer)!=Usn)
            return Rc95LdrCorruptData;
    }
    /* All trailers are checked before the first in-place restoration. */
    for(I=1;I<UsaCount;I++){
        size_t Trailer=I*(size_t)SectorSize-2u;
        B[Trailer]=B[UsaOffset+I*2u];
        B[Trailer+1u]=B[UsaOffset+I*2u+1u];
    }
    return Rc95LdrSuccess;
}

RC95LDR_STATUS
Rc95LdrNtfsEnumerateIndexBuffer(const void *Buffer,size_t BufferLength,
    uint32_t BytesPerSector,uint64_t ExpectedVcn,
    RC95LDR_NTFS_INDEX_CALLBACK Callback,void *Context,size_t *EntryCount)
{
    const uint8_t *B=(const uint8_t*)Buffer;
    uint16_t UsaOffset,UsaCount;
    uint32_t EntriesOffset,IndexLength,Allocated;
    uint8_t HeaderFlags;
    size_t Offset,End,Count=0,ExpectedUsa;
    int Terminated=0;
    if(!B||!EntryCount||BytesPerSector<512u||BytesPerSector>4096u||
       (BytesPerSector&(BytesPerSector-1u))||BufferLength<64u||
       BufferLength%BytesPerSector)
        return Rc95LdrInvalidParameter;
    *EntryCount=0;
    if(Rc95LdrReadU32(B)!=RC95LDR_NTFS_INDEX_BUFFER_MAGIC||
       Rc95LdrReadU64(B+16)!=ExpectedVcn)
        return Rc95LdrCorruptData;
    UsaOffset=Rc95LdrReadU16(B+4);UsaCount=Rc95LdrReadU16(B+6);
    ExpectedUsa=BufferLength/BytesPerSector+1u;
    if(ExpectedUsa>UINT16_MAX||UsaCount!=ExpectedUsa||
       (UsaOffset&1u)||UsaOffset<40u||
       !NtfsIndexRange(BufferLength,UsaOffset,(size_t)UsaCount*2u)||
       UsaOffset>BytesPerSector-2u||
       (size_t)UsaCount*2u>BytesPerSector-2u-UsaOffset)
        return Rc95LdrCorruptData;
    EntriesOffset=Rc95LdrReadU32(B+24);
    IndexLength=Rc95LdrReadU32(B+28);
    Allocated=Rc95LdrReadU32(B+32);
    HeaderFlags=B[36];
    if((HeaderFlags&~1u)||B[37]||B[38]||B[39]||
       EntriesOffset<40u||(EntriesOffset&7u)||
       IndexLength<EntriesOffset||Allocated<IndexLength||
       !NtfsIndexRange(BufferLength,24u,Allocated))
        return Rc95LdrCorruptData;
    if((size_t)EntriesOffset<(size_t)UsaOffset+(size_t)UsaCount*2u-24u)
        return Rc95LdrCorruptData;
    Offset=24u+(size_t)EntriesOffset;
    End=24u+(size_t)IndexLength;
    if(Offset>End||End>BufferLength)return Rc95LdrCorruptData;
    while(Offset<End){
        RC95LDR_NTFS_INDEX_ENTRY Entry;
        uint16_t Length,KeyLength,Flags;
        size_t ChildBytes,Minimum;
        if(!NtfsIndexRange(End,Offset,16u))return Rc95LdrCorruptData;
        Length=Rc95LdrReadU16(B+Offset+8);
        KeyLength=Rc95LdrReadU16(B+Offset+10);
        Flags=Rc95LdrReadU16(B+Offset+12);
        if((Flags&~(RC95LDR_NTFS_INDEX_ENTRY_NODE|
                    RC95LDR_NTFS_INDEX_ENTRY_END))||
           Rc95LdrReadU16(B+Offset+14)||Length<16u||(Length&7u)||
           !NtfsIndexRange(End,Offset,Length))
            return Rc95LdrCorruptData;
        ChildBytes=(Flags&RC95LDR_NTFS_INDEX_ENTRY_NODE)?8u:0u;
        if((Flags&RC95LDR_NTFS_INDEX_ENTRY_NODE)&&!(HeaderFlags&1u))
            return Rc95LdrCorruptData;
        if((size_t)KeyLength>SIZE_MAX-16u-ChildBytes)
            return Rc95LdrIntegerOverflow;
        Minimum=16u+(size_t)KeyLength+ChildBytes;
        if(Minimum>Length)return Rc95LdrCorruptData;
        if(Flags&RC95LDR_NTFS_INDEX_ENTRY_END){
            if(KeyLength||Offset+Length!=End)return Rc95LdrCorruptData;
            Entry.FileReference=0;Entry.Key=NULL;Entry.KeyLength=0;
            Entry.Flags=Flags;
            Entry.ChildVcn=ChildBytes?Rc95LdrReadU64(B+Offset+Length-8u):0;
            if(Callback){
                RC95LDR_STATUS Status=Callback(Context,&Entry);
                if(Status!=Rc95LdrSuccess)return Status;
            }
            Terminated=1;Offset+=Length;break;
        }
        if(!KeyLength)return Rc95LdrCorruptData;
        Entry.FileReference=Rc95LdrReadU64(B+Offset);
        Entry.Key=B+Offset+16u;Entry.KeyLength=KeyLength;Entry.Flags=Flags;
        Entry.ChildVcn=ChildBytes?Rc95LdrReadU64(B+Offset+Length-8u):0;
        if(Callback){
            RC95LDR_STATUS Status=Callback(Context,&Entry);
            if(Status!=Rc95LdrSuccess)return Status;
        }
        if(Count==SIZE_MAX)return Rc95LdrIntegerOverflow;
        ++Count;Offset+=Length;
    }
    if(!Terminated||Offset!=End)return Rc95LdrCorruptData;
    *EntryCount=Count;
    return Rc95LdrSuccess;
}
