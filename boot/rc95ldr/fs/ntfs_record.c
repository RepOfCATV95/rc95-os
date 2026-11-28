#include "rc95ldr_ntfs_record.h"
#include <string.h>
static int NtfsListRange(size_t Total,size_t Offset,size_t Length)
{return Offset<=Total&&Length<=Total-Offset;}
RC95LDR_STATUS
Rc95LdrNtfsEnumerateAttributeList(const void *Value,size_t ValueLength,
    RC95LDR_NTFS_ATTRIBUTE_LIST_CALLBACK Callback,void *Context,
    size_t *EntryCount)
{
    const uint8_t *Bytes=(const uint8_t*)Value;
    size_t Offset=0,Count=0;
    if(!Bytes||!EntryCount)return Rc95LdrInvalidParameter;
    *EntryCount=0;
    while(Offset<ValueLength){
        RC95LDR_NTFS_ATTRIBUTE_LIST_ENTRY Entry;
        uint16_t Length;
        uint8_t NameLength,NameOffset;
        size_t NameBytes,Minimum,I;
        if(!NtfsListRange(ValueLength,Offset,26u))
            return Rc95LdrCorruptData;
        Length=Rc95LdrReadU16(Bytes+Offset+4u);
        NameLength=Bytes[Offset+6u];NameOffset=Bytes[Offset+7u];
        if(Length<26u||(Length&7u)||
           !NtfsListRange(ValueLength,Offset,Length))
            return Rc95LdrCorruptData;
        NameBytes=(size_t)NameLength*2u;
        if(NameLength){
            if(NameOffset<26u||(NameOffset&1u)||
               !NtfsListRange(Length,NameOffset,NameBytes))
                return Rc95LdrCorruptData;
            if((size_t)NameOffset<(size_t)26u)
                return Rc95LdrCorruptData;
        }else if(NameOffset!=0u){
            return Rc95LdrCorruptData;
        }
        Minimum=NameLength?(size_t)NameOffset+NameBytes:26u;
        if(Minimum>Length)return Rc95LdrCorruptData;
        Entry.Type=Rc95LdrReadU32(Bytes+Offset);
        Entry.LowestVcn=Rc95LdrReadU64(Bytes+Offset+8u);
        Entry.FileReference=Rc95LdrReadU64(Bytes+Offset+16u);
        Entry.AttributeId=Rc95LdrReadU16(Bytes+Offset+24u);
        Entry.NameLength=NameLength;
        Entry.Name=NameLength?(const uint16_t*)(const void*)
            (Bytes+Offset+NameOffset):NULL;
        if(Entry.Type==RC95LDR_NTFS_ATTRIBUTE_END||
           Entry.Type==0u||!Entry.FileReference)
            return Rc95LdrCorruptData;
        for(I=0;I<NameLength;I++){
            uint16_t Ch=Rc95LdrReadU16(Bytes+Offset+NameOffset+I*2u);
            if(!Ch)return Rc95LdrCorruptData;
        }
        if(Callback){
            RC95LDR_STATUS Status=Callback(Context,&Entry);
            if(Status!=Rc95LdrSuccess)return Status;
        }
        if(Count==SIZE_MAX)return Rc95LdrIntegerOverflow;
        ++Count;Offset+=Length;
    }
    if(!Count)return Rc95LdrCorruptData;
    *EntryCount=Count;
    return Rc95LdrSuccess;
}
static int NtfsRange(size_t Total,size_t Offset,size_t Length)
{return Offset<=Total&&Length<=Total-Offset;}
static int NtfsPowerOfTwo(uint32_t V){return V&&!(V&(V-1u));}

RC95LDR_STATUS
Rc95LdrNtfsApplyFileRecordFixups(uint8_t *R,size_t Length,uint32_t SectorSize)
{
    uint16_t UsaOffset,UsaCount,Usn;
    uint32_t Used,Allocated;
    size_t Expected,I,UsaBytes;
    if(!R||!NtfsPowerOfTwo(SectorSize)||SectorSize<512u||SectorSize>4096u||
       Length<48u||Length%SectorSize)return Rc95LdrInvalidParameter;
    if(Rc95LdrReadU32(R)!=RC95LDR_NTFS_FILE_MAGIC)return Rc95LdrInvalidImage;
    UsaOffset=Rc95LdrReadU16(R+4);UsaCount=Rc95LdrReadU16(R+6);
    Used=Rc95LdrReadU32(R+24);Allocated=Rc95LdrReadU32(R+28);
    Expected=Length/SectorSize+1u;
    if(Expected>UINT16_MAX||UsaCount!=Expected||(UsaOffset&1u)||
       UsaOffset<48u||(size_t)UsaCount>SIZE_MAX/2u)return Rc95LdrCorruptData;
    UsaBytes=(size_t)UsaCount*2u;
    if(!NtfsRange(Length,UsaOffset,UsaBytes)||Used<48u||Used>Allocated||
       Allocated!=Length)return Rc95LdrCorruptData;
    {
        uint16_t AttributeOffset=Rc95LdrReadU16(R+20);
        if(AttributeOffset<48u||(AttributeOffset&7u)||
           (size_t)AttributeOffset<(size_t)UsaOffset+UsaBytes||
           Used<((size_t)AttributeOffset+4u))
            return Rc95LdrCorruptData;
    }
    Usn=Rc95LdrReadU16(R+UsaOffset);
    if(!Usn)return Rc95LdrCorruptData;
    for(I=1;I<UsaCount;I++){
        size_t T=I*SectorSize-2u;
        if(!NtfsRange(Length,T,2u)||Rc95LdrReadU16(R+T)!=Usn)
            return Rc95LdrCorruptData;
    }
    for(I=1;I<UsaCount;I++){
        size_t T=I*SectorSize-2u;
        R[T]=R[UsaOffset+I*2u];R[T+1u]=R[UsaOffset+I*2u+1u];
    }
    return Rc95LdrSuccess;
}

static RC95LDR_STATUS
NtfsDecodeAttribute(const uint8_t *R,size_t Total,size_t Offset,
                    RC95LDR_NTFS_ATTRIBUTE *Out,size_t *Next)
{
    const uint8_t *A;uint32_t Type,Length;uint8_t NonResident,NameLength;
    uint16_t NameOffset,Instance;size_t Header,NameBytes;
    RC95LDR_NTFS_ATTRIBUTE X;
    if(!NtfsRange(Total,Offset,4u))return Rc95LdrCorruptData;
    A=R+Offset;Type=Rc95LdrReadU32(A);
    if(Type==RC95LDR_NTFS_ATTRIBUTE_END){*Next=Offset+4u;return Rc95LdrNotFound;}
    if(!NtfsRange(Total,Offset,8u))return Rc95LdrCorruptData;
    Length=Rc95LdrReadU32(A+4);
    if(Length<16u||(Length&7u)||!NtfsRange(Total,Offset,Length))
        return Rc95LdrCorruptData;
    NonResident=A[8];NameLength=A[9];NameOffset=Rc95LdrReadU16(A+10);
    Instance=Rc95LdrReadU16(A+14);
    if(NonResident>1u)return Rc95LdrCorruptData;
    Header=NonResident?64u:24u;NameBytes=(size_t)NameLength*2u;
    if(Length<Header)return Rc95LdrCorruptData;
    if(NameLength&&((NameOffset&1u)||NameOffset<Header||
       !NtfsRange(Length,NameOffset,NameBytes)))return Rc95LdrCorruptData;
    memset(&X,0,sizeof(X));X.Type=Type;X.Length=Length;X.Instance=Instance;
    X.NonResident=NonResident;X.NameLength=NameLength;X.Flags=Rc95LdrReadU16(A+12);
    X.Name=NameLength?(const uint16_t *)(const void *)(A+NameOffset):NULL;
    X.Record=A;
    if(!NonResident){
        uint32_t ValueLength=Rc95LdrReadU32(A+16);
        uint16_t ValueOffset=Rc95LdrReadU16(A+20);
        if(ValueOffset<Header||!NtfsRange(Length,ValueOffset,ValueLength))
            return Rc95LdrCorruptData;
        if(NameLength&&ValueLength&&(size_t)ValueOffset<(size_t)NameOffset+NameBytes&&
           (size_t)NameOffset<(size_t)ValueOffset+ValueLength)
            return Rc95LdrCorruptData;
        if(X.Flags)return Rc95LdrCorruptData;
        X.Value=A+ValueOffset;X.ValueLength=ValueLength;X.DataSize=ValueLength;
    }else{
        uint16_t MappingOffset=Rc95LdrReadU16(A+32);
        X.LowestVcn=Rc95LdrReadU64(A+16);X.HighestVcn=Rc95LdrReadU64(A+24);
        X.MappingPairsOffset=MappingOffset;X.AllocatedSize=Rc95LdrReadU64(A+40);
        X.DataSize=Rc95LdrReadU64(A+48);X.InitializedSize=Rc95LdrReadU64(A+56);
        if(MappingOffset<Header||MappingOffset>=Length||
           X.HighestVcn<X.LowestVcn||X.InitializedSize>X.DataSize||
           (!(X.Flags&(0x0001u|0x8000u))&&X.DataSize>X.AllocatedSize))return Rc95LdrCorruptData;
        if(NameLength&&(size_t)MappingOffset<(size_t)NameOffset+NameBytes)
            return Rc95LdrCorruptData;
        X.Value=A+MappingOffset;X.ValueLength=Length-MappingOffset;
    }
    *Out=X;*Next=Offset+Length;return Rc95LdrSuccess;
}

RC95LDR_STATUS
Rc95LdrNtfsEnumerateAttributes(const uint8_t *R,size_t Length,
    uint32_t SectorSize,uint32_t Type,RC95LDR_NTFS_ATTRIBUTE_CALLBACK Callback,
    void *Context,size_t *EntryCount)
{
    uint16_t AttributeOffset;
    uint32_t Used,Allocated;
    size_t Offset,Count=0;
    if(!R||!EntryCount||!NtfsPowerOfTwo(SectorSize)||
       Length<48u||Length%SectorSize||
       Rc95LdrReadU32(R)!=RC95LDR_NTFS_FILE_MAGIC)
        return Rc95LdrInvalidParameter;
    *EntryCount=0;
    AttributeOffset=Rc95LdrReadU16(R+20);
    Used=Rc95LdrReadU32(R+24);
    Allocated=Rc95LdrReadU32(R+28);
    if(Allocated!=Length||Used>Length||AttributeOffset<48u||
       (AttributeOffset&7u)||Used<AttributeOffset+4u)
        return Rc95LdrCorruptData;
    Offset=AttributeOffset;
    while(Offset<Used){
        RC95LDR_NTFS_ATTRIBUTE Current;
        size_t Next;
        RC95LDR_STATUS Status=NtfsDecodeAttribute(R,Used,Offset,&Current,&Next);
        if(Status==Rc95LdrNotFound){
            if(Next!=Used)return Rc95LdrCorruptData;
            *EntryCount=Count;
            return Rc95LdrSuccess;
        }
        if(Status!=Rc95LdrSuccess)return Status;
        if(Current.Type==Type){
            if(Callback){
                Status=Callback(Context,&Current);
                if(Status!=Rc95LdrSuccess)return Status;
            }
            if(Count==SIZE_MAX)return Rc95LdrIntegerOverflow;
            ++Count;
        }
        Offset=Next;
    }
    return Rc95LdrCorruptData;
}

RC95LDR_STATUS
Rc95LdrNtfsFindNamedAttribute(const uint8_t *R,size_t Length,
    uint32_t SectorSize,uint32_t Type,const uint16_t *Name,size_t NameLength,
    RC95LDR_NTFS_ATTRIBUTE *Out)
{
    uint16_t AttributeOffset;uint32_t Used,Allocated;size_t Offset;
    int Found=0;RC95LDR_NTFS_ATTRIBUTE Match;
    if(!R||!Out||(!Name&&NameLength)||!NtfsPowerOfTwo(SectorSize)||
       Length<48u||Length%SectorSize||
       Rc95LdrReadU32(R)!=RC95LDR_NTFS_FILE_MAGIC)
        return Rc95LdrInvalidParameter;
    AttributeOffset=Rc95LdrReadU16(R+20);Used=Rc95LdrReadU32(R+24);
    Allocated=Rc95LdrReadU32(R+28);
    if(Allocated!=Length||Used>Length||AttributeOffset<48u||
       (AttributeOffset&7u)||Used<AttributeOffset+4u)
        return Rc95LdrCorruptData;
    Offset=AttributeOffset;
    while(Offset<Used){
        RC95LDR_NTFS_ATTRIBUTE Current;size_t Next,I;
        RC95LDR_STATUS S=NtfsDecodeAttribute(R,Used,Offset,&Current,&Next);
        if(S==Rc95LdrNotFound){
            if(Next!=Used)return Rc95LdrCorruptData;
            if(Found)*Out=Match;
            return Found?Rc95LdrSuccess:Rc95LdrNotFound;
        }
        if(S!=Rc95LdrSuccess)return S;
        if(Current.Type==Type&&Current.NameLength==NameLength){
            int Equal=1;
            for(I=0;I<NameLength;I++){
                if(Rc95LdrReadU16((const uint8_t*)Current.Name+I*2u)!=Name[I]){
                    Equal=0;break;
                }
            }
            if(Equal){
                if(Found)return Rc95LdrCorruptData;
                Match=Current;Found=1;
            }
        }
        Offset=Next;
    }
    return Rc95LdrCorruptData;
}
RC95LDR_STATUS
Rc95LdrNtfsFindAttribute(const uint8_t *R,size_t Length,uint32_t SectorSize,
                         uint32_t Type,RC95LDR_NTFS_ATTRIBUTE *Out)
{
    return Rc95LdrNtfsFindNamedAttribute(R,Length,SectorSize,Type,NULL,0,Out);
}

RC95LDR_STATUS
Rc95LdrNtfsGetFileRecordInfo(const uint8_t *R,size_t Length,
                             RC95LDR_NTFS_FILE_RECORD_INFO *Info)
{
    RC95LDR_NTFS_FILE_RECORD_INFO X;
    uint16_t AttributeOffset;
    if(!R||!Info||Length<48u||Rc95LdrReadU32(R)!=RC95LDR_NTFS_FILE_MAGIC)
        return Rc95LdrInvalidParameter;
    memset(&X,0,sizeof(X));
    X.SequenceNumber=Rc95LdrReadU16(R+16);
    X.HardLinkCount=Rc95LdrReadU16(R+18);
    AttributeOffset=Rc95LdrReadU16(R+20);
    X.Flags=Rc95LdrReadU16(R+22);
    X.BytesInUse=Rc95LdrReadU32(R+24);
    X.BytesAllocated=Rc95LdrReadU32(R+28);
    X.BaseFileReference=Rc95LdrReadU64(R+32);
    X.NextAttributeId=Rc95LdrReadU16(R+40);
    X.RecordNumber=Rc95LdrReadU32(R+44);
    if(X.BytesAllocated!=Length||X.BytesInUse>Length||
       AttributeOffset<48u||(AttributeOffset&7u)||
       X.BytesInUse<(uint32_t)AttributeOffset+4u||
       X.BytesInUse<48u)
        return Rc95LdrCorruptData;
    *Info=X;
    return Rc95LdrSuccess;
}

RC95LDR_STATUS
Rc95LdrNtfsValidateFileReference(const uint8_t *Record,size_t RecordLength,
                                 uint64_t FileReference)
{
    RC95LDR_NTFS_FILE_RECORD_INFO Info;
    uint64_t RecordNumber=FileReference&0x0000ffffffffffffULL;
    uint16_t Sequence=(uint16_t)(FileReference>>48);
    RC95LDR_STATUS Status;
    if(!Sequence)return Rc95LdrNotFound;
    Status=Rc95LdrNtfsGetFileRecordInfo(Record,RecordLength,&Info);
    if(Status!=Rc95LdrSuccess)return Status;
    if(!(Info.Flags&0x0001u)||Info.SequenceNumber!=Sequence)
        return Rc95LdrNotFound;
    /* NTFS 3.x stores the low 32 bits of the MFT record number in FILE+0x2c.
       Older records leave this field zero, so zero remains a compatible
       "not supplied" value while nonzero identities are checked strictly. */
    if(Info.RecordNumber&&Info.RecordNumber!=(uint32_t)RecordNumber)
        return Rc95LdrCorruptData;
    return Rc95LdrSuccess;
}
