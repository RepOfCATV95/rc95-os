#include "rc95ldr_ntfs_stream.h"
#include <string.h>

typedef struct _NTFS_STREAM_COLLECT_CONTEXT {
    const RC95LDR_NTFS_VOLUME *Volume;
    uint8_t *ExtensionRecord;
    size_t ExtensionCapacity;
    uint32_t Type;
    const uint16_t *Name;
    size_t NameLength;
    uint64_t BaseReference;
    uint16_t BaseAttributeId;
    uint64_t NextVcn;
    RC95LDR_NTFS_STREAM Candidate;
    unsigned BaseSeen;
} NTFS_STREAM_COLLECT_CONTEXT;

static RC95LDR_STATUS NtfsStreamAppendAttribute(
    NTFS_STREAM_COLLECT_CONTEXT *C,const RC95LDR_NTFS_ATTRIBUTE *A,
    uint64_t ExpectedVcn)
{
    RC95LDR_NTFS_RUN Extent[RC95LDR_NTFS_MAX_STREAM_RUNS];
    size_t Count=0;
    uint64_t Next;
    RC95LDR_STATUS S;
    if(!A->NonResident||A->Flags!=C->Candidate.Flags||
       A->LowestVcn!=ExpectedVcn||A->HighestVcn==UINT64_MAX)
        return Rc95LdrCorruptData;
    S=Rc95LdrNtfsDecodeRunlist(A->Value,A->ValueLength,A->LowestVcn,
        C->Volume->Geometry.ClusterCount,0,Extent,
        RC95LDR_ARRAY_COUNT(Extent),&Count,&Next);
    if(S!=Rc95LdrSuccess)return S;
    if(!Count||Next!=A->HighestVcn+1u)return Rc95LdrCorruptData;
    S=Rc95LdrNtfsAppendRunlistExtent(Extent,Count,ExpectedVcn,
        C->Volume->Geometry.ClusterCount,C->Candidate.Runs,
        RC95LDR_ARRAY_COUNT(C->Candidate.Runs),&C->Candidate.RunCount,
        &C->NextVcn);
    if(S!=Rc95LdrSuccess)return S;
    C->Candidate.NextVcn=C->NextVcn;
    return Rc95LdrSuccess;
}

static RC95LDR_STATUS NtfsStreamListEntry(void *Opaque,
    const RC95LDR_NTFS_ATTRIBUTE_LIST_ENTRY *Entry)
{
    NTFS_STREAM_COLLECT_CONTEXT *C=(NTFS_STREAM_COLLECT_CONTEXT*)Opaque;
    RC95LDR_NTFS_ATTRIBUTE A;
    RC95LDR_NTFS_FILE_RECORD_INFO Info;
    RC95LDR_STATUS S;
    if(Entry->Type!=C->Type||Entry->NameLength!=C->NameLength)
        return Rc95LdrSuccess;
    {
        size_t I;
        for(I=0;I<C->NameLength;I++)
            if(Rc95LdrReadU16((const uint8_t*)Entry->Name+I*2u)!=C->Name[I])
                return Rc95LdrSuccess;
    }
    if(Entry->LowestVcn==0){
        if(C->BaseSeen||Entry->FileReference!=C->BaseReference||
           Entry->AttributeId!=C->BaseAttributeId)
            return Rc95LdrCorruptData;
        C->BaseSeen=1;
        return Rc95LdrSuccess;
    }
    if(!C->BaseSeen||Entry->LowestVcn!=C->NextVcn)
        return Rc95LdrCorruptData;
    if(!C->ExtensionRecord)
        return Rc95LdrInvalidParameter;
    if(C->ExtensionCapacity<C->Volume->Geometry.FileRecordBytes)
        return Rc95LdrBufferTooSmall;
    S=Rc95LdrNtfsReadMftRecordByReference(C->Volume,Entry->FileReference,
        C->ExtensionRecord,C->ExtensionCapacity);
    if(S!=Rc95LdrSuccess)return S;
    S=Rc95LdrNtfsGetFileRecordInfo(C->ExtensionRecord,
        C->Volume->Geometry.FileRecordBytes,&Info);
    if(S!=Rc95LdrSuccess)return S;
    if(Info.BaseFileReference!=C->BaseReference)return Rc95LdrCorruptData;
    S=Rc95LdrNtfsFindNamedAttribute(C->ExtensionRecord,
        C->Volume->Geometry.FileRecordBytes,C->Volume->Geometry.BytesPerSector,
        C->Type,C->Name,C->NameLength,&A);
    if(S!=Rc95LdrSuccess)return S;
    if(A.Instance!=Entry->AttributeId||A.LowestVcn!=Entry->LowestVcn)
        return Rc95LdrCorruptData;
    return NtfsStreamAppendAttribute(C,&A,Entry->LowestVcn);
}

RC95LDR_STATUS Rc95LdrNtfsCollectStream(
    const RC95LDR_NTFS_VOLUME *V,const uint8_t *BaseRecord,
    size_t BaseRecordLength,uint64_t BaseFileReference,uint32_t Type,const uint16_t *Name,
    size_t NameLength,void *ExtensionRecord,size_t ExtensionRecordLength,
    RC95LDR_NTFS_STREAM *Stream)
{
    static const uint16_t EmptyName=0;
    NTFS_STREAM_COLLECT_CONTEXT C;
    RC95LDR_NTFS_ATTRIBUTE Base,List;
    RC95LDR_NTFS_FILE_RECORD_INFO Info;
    RC95LDR_STATUS S;
    size_t Entries=0;
    uint64_t ClusterBytes;
    if(!V||!V->Mounted||!BaseRecord||!Stream||(!Name&&NameLength))
        return Rc95LdrInvalidParameter;
    if(!Name)Name=&EmptyName;
    if(BaseRecordLength!=V->Geometry.FileRecordBytes)
        return Rc95LdrInvalidParameter;
    S=Rc95LdrNtfsGetFileRecordInfo(BaseRecord,BaseRecordLength,&Info);
    if(S!=Rc95LdrSuccess)return S;
    if(!(Info.Flags&0x0001u))return Rc95LdrCorruptData;
    S=Rc95LdrNtfsFindNamedAttribute(BaseRecord,BaseRecordLength,
        V->Geometry.BytesPerSector,Type,Name,NameLength,&Base);
    if(S!=Rc95LdrSuccess)return S;
    if(!Base.NonResident||Base.LowestVcn!=0||Base.Flags&0xc001u)
        return Rc95LdrUnsupported;
    memset(&C,0,sizeof(C));
    C.Volume=V;
    C.ExtensionRecord=(uint8_t*)ExtensionRecord;
    C.ExtensionCapacity=ExtensionRecordLength;C.Type=Type;
    C.Name=Name;C.NameLength=NameLength;
    C.BaseReference=BaseFileReference;
    if(!BaseFileReference||((uint16_t)(BaseFileReference>>48))!=Info.SequenceNumber||
       (Info.RecordNumber&&Info.RecordNumber!=(uint32_t)
        (BaseFileReference&UINT64_C(0x0000ffffffffffff))))
        return Rc95LdrCorruptData;
    C.BaseAttributeId=Base.Instance;C.Candidate.Flags=Base.Flags;
    C.Candidate.AllocatedSize=Base.AllocatedSize;
    C.Candidate.DataSize=Base.DataSize;
    C.Candidate.InitializedSize=Base.InitializedSize;
    C.NextVcn=0;
    S=NtfsStreamAppendAttribute(&C,&Base,0);
    if(S!=Rc95LdrSuccess)return S;
    S=Rc95LdrNtfsFindAttribute(BaseRecord,BaseRecordLength,
        V->Geometry.BytesPerSector,RC95LDR_NTFS_ATTRIBUTE_LIST,&List);
    if(S==Rc95LdrSuccess){
        if(List.NonResident||List.Flags||!List.ValueLength)
            return Rc95LdrUnsupported;
        S=Rc95LdrNtfsEnumerateAttributeList(List.Value,List.ValueLength,
            NtfsStreamListEntry,&C,&Entries);
        if(S!=Rc95LdrSuccess)return S;
        if(!Entries||!C.BaseSeen)return Rc95LdrCorruptData;
    }else if(S!=Rc95LdrNotFound)return S;
    ClusterBytes=V->Geometry.BytesPerCluster;
    if(!ClusterBytes||C.Candidate.AllocatedSize%ClusterBytes||
       C.NextVcn!=C.Candidate.AllocatedSize/ClusterBytes||
       C.Candidate.InitializedSize>C.Candidate.DataSize||
       C.Candidate.DataSize>C.Candidate.AllocatedSize)
        return Rc95LdrCorruptData;
    C.Candidate.NextVcn=C.NextVcn;
    *Stream=C.Candidate;
    return Rc95LdrSuccess;
}

RC95LDR_STATUS Rc95LdrNtfsReadStreamRange(
    const RC95LDR_NTFS_VOLUME *V,const RC95LDR_NTFS_STREAM *Stream,
    uint64_t Offset,void *Buffer,size_t Length,void *Bounce,size_t BounceLength)
{
    uint64_t End,ClusterBytes;
    size_t I;
    uint8_t *Out=(uint8_t*)Buffer;
    uint64_t InitializedRounded;
    uint32_t Sector;
    if(!V||!V->Mounted||!V->DeviceStorage.Read||!Stream||(!Buffer&&Length)||
       !V->Geometry.BytesPerSector||!V->Geometry.BytesPerCluster||
       !V->Geometry.SectorsPerCluster||
       Stream->RunCount>RC95LDR_NTFS_MAX_STREAM_RUNS||
       Stream->InitializedSize>Stream->DataSize||
       Stream->DataSize>Stream->AllocatedSize||
       Offset%V->Geometry.BytesPerSector||
       Length%V->Geometry.BytesPerSector||
       Offset>Stream->AllocatedSize||(uint64_t)Length>Stream->AllocatedSize-Offset)
        return Rc95LdrInvalidParameter;
    if(!Length)return Rc95LdrSuccess;
    if((uint64_t)Length>UINT64_MAX-Offset)return Rc95LdrIntegerOverflow;
    Sector=V->Geometry.BytesPerSector;
    if(Stream->InitializedSize>UINT64_MAX-(Sector-1u))
        return Rc95LdrIntegerOverflow;
    InitializedRounded=(Stream->InitializedSize+Sector-1u)&~((uint64_t)Sector-1u);
    if(Offset<InitializedRounded&&
       (!Bounce||BounceLength<Sector))
        return Rc95LdrBufferTooSmall;
    End=Offset+Length;ClusterBytes=V->Geometry.BytesPerCluster;
    {
        uint64_t ExpectedVcn=0;
        for(I=0;I<Stream->RunCount;I++){
            const RC95LDR_NTFS_RUN *R=&Stream->Runs[I];
            if(!R->ClusterCount||R->Sparse>1u||R->Vcn!=ExpectedVcn||
               R->ClusterCount>UINT64_MAX-ExpectedVcn||
               ExpectedVcn+R->ClusterCount>V->Geometry.ClusterCount||
               (!R->Sparse&&(R->Lcn>=V->Geometry.ClusterCount||
                R->ClusterCount>V->Geometry.ClusterCount-R->Lcn)))
                return Rc95LdrCorruptData;
            ExpectedVcn+=R->ClusterCount;
        }
        if(ExpectedVcn!=Stream->NextVcn||
           ExpectedVcn>UINT64_MAX/ClusterBytes||
           ExpectedVcn*ClusterBytes!=Stream->AllocatedSize)
            return Rc95LdrCorruptData;
    }
    for(I=0;I<Stream->RunCount&&Offset<End;I++){
        const RC95LDR_NTFS_RUN *R=&Stream->Runs[I];
        uint64_t Start,Bytes,RunEnd,Position,Take,PhysicalTake;
        uint64_t Cluster,InCluster,Lba,Blocks;
        RC95LDR_STATUS S;
        if(R->Vcn>UINT64_MAX/ClusterBytes||
           R->ClusterCount>UINT64_MAX/ClusterBytes)return Rc95LdrIntegerOverflow;
        Start=R->Vcn*ClusterBytes;Bytes=R->ClusterCount*ClusterBytes;
        if(Start>UINT64_MAX-Bytes)return Rc95LdrIntegerOverflow;
        RunEnd=Start+Bytes;
        if(End<=Start||Offset>=RunEnd)continue;
        if(!R->ClusterCount||R->Sparse>1u)return Rc95LdrCorruptData;
        Position=Offset>Start?Offset:Start;
        if(Position!=Offset)return Rc95LdrCorruptData;
        Take=End<RunEnd?End-Position:RunEnd-Position;
        if(Take%Sector)return Rc95LdrCorruptData;
        PhysicalTake=0;
        if(Position<InitializedRounded){
            PhysicalTake=Take;
            if(PhysicalTake>InitializedRounded-Position)
                PhysicalTake=InitializedRounded-Position;
        }
        if(PhysicalTake){
            uint64_t ValidBytes=0;
            if(R->Sparse)return Rc95LdrUnsupported;
            InCluster=Position-Start;
            if(R->Lcn>UINT64_MAX-InCluster/ClusterBytes)return Rc95LdrIntegerOverflow;
            Cluster=R->Lcn+InCluster/ClusterBytes;
            if(Cluster>UINT64_MAX/V->Geometry.SectorsPerCluster)
                return Rc95LdrIntegerOverflow;
            Lba=Cluster*V->Geometry.SectorsPerCluster;
            if(Lba>UINT64_MAX-(InCluster%ClusterBytes)/Sector)
                return Rc95LdrIntegerOverflow;
            Lba+=(InCluster%ClusterBytes)/Sector;
            Blocks=PhysicalTake/Sector;
            if(Lba>V->PartitionBlockCount||Blocks>V->PartitionBlockCount-Lba||
               V->PartitionStartLba>UINT64_MAX-Lba||Blocks>UINT32_MAX)
                return Rc95LdrOutOfBounds;
            S=Rc95LdrReadBlocks(&V->DeviceStorage,V->PartitionStartLba+Lba,
                (uint32_t)Blocks,Out,(size_t)PhysicalTake);
            if(S!=Rc95LdrSuccess)return S;
            if(Position<Stream->InitializedSize){
                ValidBytes=Stream->InitializedSize-Position;
                if(ValidBytes>PhysicalTake)ValidBytes=PhysicalTake;
            }
            if(ValidBytes<PhysicalTake)
                memset(Out+(size_t)ValidBytes,0,(size_t)(PhysicalTake-ValidBytes));
        }
        if(PhysicalTake<Take)
            memset(Out+(size_t)PhysicalTake,0,(size_t)(Take-PhysicalTake));
        Out+=(size_t)Take;Offset+=Take;
    }
    return Offset==End?Rc95LdrSuccess:Rc95LdrCorruptData;
}
