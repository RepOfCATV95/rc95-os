#include "rc95ldr_ntfs_volume.h"
#include <string.h>

#define NTFS_ATTRIBUTE_DATA 0x80u
#define NTFS_MAX_SECTOR_SIZE 4096u

static RC95LDR_STATUS
NtfsReadPartitionBlocks(const RC95LDR_NTFS_VOLUME *V,uint64_t Lba,
                        uint32_t Blocks,void *Buffer,size_t Length)
{
    if(!V||!V->DeviceStorage.Read||Lba>V->PartitionBlockCount||
       (uint64_t)Blocks>V->PartitionBlockCount-Lba||
       V->PartitionStartLba>UINT64_MAX-Lba)
        return Rc95LdrOutOfBounds;
    return Rc95LdrReadBlocks(&V->DeviceStorage,V->PartitionStartLba+Lba,
                             Blocks,Buffer,Length);
}

static RC95LDR_STATUS
NtfsReadMftBytes(const RC95LDR_NTFS_VOLUME *V,uint64_t Offset,
                 void *Buffer,size_t Length)
{
    uint8_t *Out=Buffer;
    uint64_t ClusterBytes,End;
    size_t I;
    if(!V||!V->Mounted||!V->DeviceStorage.Read||
       !V->Geometry.BytesPerSector||!V->Geometry.BytesPerCluster||
       !V->Geometry.SectorsPerCluster||
       V->MftRunCount>RC95LDR_NTFS_MAX_MFT_RUNS||(!Buffer&&Length))
        return Rc95LdrInvalidParameter;
    if(Offset>V->MftDataSize||
       (uint64_t)Length>V->MftDataSize-Offset)
        return Rc95LdrOutOfBounds;
    if(!Length)return Rc95LdrSuccess;
    if(Offset%V->Geometry.BytesPerSector||
       Length%V->Geometry.BytesPerSector)
        return Rc95LdrInvalidParameter;
    ClusterBytes=V->Geometry.BytesPerCluster;
    if((uint64_t)Length>UINT64_MAX-Offset)return Rc95LdrIntegerOverflow;
    End=Offset+(uint64_t)Length;
    {
        uint64_t ExpectedVcn=0;
        for(I=0;I<V->MftRunCount;I++){
            const RC95LDR_NTFS_RUN *Run=&V->MftRuns[I];
            if(!Run->ClusterCount||Run->Sparse>1u||Run->Sparse||
               Run->Vcn!=ExpectedVcn||
               Run->ClusterCount>UINT64_MAX-ExpectedVcn||
               ExpectedVcn+Run->ClusterCount>V->Geometry.ClusterCount||
               Run->Lcn>=V->Geometry.ClusterCount||
               Run->ClusterCount>V->Geometry.ClusterCount-Run->Lcn)
                return Rc95LdrCorruptData;
            ExpectedVcn+=Run->ClusterCount;
        }
    }
    for(I=0;I<V->MftRunCount&&Offset<End;I++){
        const RC95LDR_NTFS_RUN *Run=&V->MftRuns[I];
        uint64_t RunStart,RunBytes,RunEnd;
        if(Run->Vcn>UINT64_MAX/ClusterBytes||
           Run->ClusterCount>UINT64_MAX/ClusterBytes)
            return Rc95LdrIntegerOverflow;
        RunStart=Run->Vcn*ClusterBytes;
        RunBytes=Run->ClusterCount*ClusterBytes;
        if(RunStart>UINT64_MAX-RunBytes)
            return Rc95LdrIntegerOverflow;
        RunEnd=RunStart+RunBytes;
        uint64_t Position,Take;
        if(End<=RunStart||Offset>=RunEnd)continue;
        if(Run->Sparse)return Rc95LdrCorruptData;
        Position=Offset>RunStart?Offset:RunStart;
        if(Position!=Offset)return Rc95LdrCorruptData; /* MFT map has a gap */
        Take=End<RunEnd?End-Position:RunEnd-Position;
        {
            uint64_t InRun=Position-RunStart;
            uint64_t Cluster=Run->Lcn+InRun/ClusterBytes;
            uint64_t SectorInCluster=(InRun%ClusterBytes)/V->Geometry.BytesPerSector;
            uint64_t Lba,Blocks=Take/V->Geometry.BytesPerSector;
            if(Cluster>UINT64_MAX/V->Geometry.SectorsPerCluster)
                return Rc95LdrIntegerOverflow;
            Lba=Cluster*V->Geometry.SectorsPerCluster;
            if(Lba>UINT64_MAX-SectorInCluster)
                return Rc95LdrIntegerOverflow;
            Lba+=SectorInCluster;
            if(Blocks>UINT32_MAX||Lba>V->PartitionBlockCount||
               Blocks>V->PartitionBlockCount-Lba)
                return Rc95LdrOutOfBounds;
            {
                RC95LDR_STATUS Status=NtfsReadPartitionBlocks(
                    V,Lba,(uint32_t)Blocks,Out,(size_t)Take);
                if(Status!=Rc95LdrSuccess)return Status;
            }
        }
        Out+=(size_t)Take;Offset+=Take;
    }
    return Offset==End?Rc95LdrSuccess:Rc95LdrCorruptData;
}


typedef struct _NTFS_MFT_ATTRIBUTE_LIST_CONTEXT {
    RC95LDR_NTFS_VOLUME *Volume;
    uint8_t *ExtensionRecord;
    size_t ExtensionCapacity;
    uint64_t BaseReference;
    uint16_t BaseAttributeId;
    uint64_t NextVcn;
    size_t RunCount;
    int BaseEntrySeen;
} NTFS_MFT_ATTRIBUTE_LIST_CONTEXT;

static RC95LDR_STATUS
NtfsMftAttributeListEntry(void *Opaque,
    const RC95LDR_NTFS_ATTRIBUTE_LIST_ENTRY *Entry)
{
    NTFS_MFT_ATTRIBUTE_LIST_CONTEXT *C=(NTFS_MFT_ATTRIBUTE_LIST_CONTEXT*)Opaque;
    RC95LDR_NTFS_ATTRIBUTE Attribute;
    RC95LDR_NTFS_FILE_RECORD_INFO Info;
    RC95LDR_NTFS_RUN Extent[RC95LDR_NTFS_MAX_MFT_RUNS];
    size_t ExtentCount=0;
    uint64_t Next;
    RC95LDR_STATUS Status;
    if(Entry->Type!=NTFS_ATTRIBUTE_DATA||Entry->NameLength!=0u)
        return Rc95LdrSuccess;
    if(Entry->LowestVcn==0u){
        if(C->BaseEntrySeen||Entry->FileReference!=C->BaseReference||
           Entry->AttributeId!=C->BaseAttributeId)
            return Rc95LdrCorruptData;
        C->BaseEntrySeen=1;
        return Rc95LdrSuccess;
    }
    if(!C->BaseEntrySeen||Entry->LowestVcn!=C->NextVcn)
        return Rc95LdrCorruptData;
    if(C->ExtensionCapacity<C->Volume->Geometry.FileRecordBytes)
        return Rc95LdrBufferTooSmall;
    Status=Rc95LdrNtfsReadMftRecordByReference(C->Volume,
        Entry->FileReference,C->ExtensionRecord,C->ExtensionCapacity);
    if(Status!=Rc95LdrSuccess)return Status;
    Status=Rc95LdrNtfsGetFileRecordInfo(C->ExtensionRecord,
        C->Volume->Geometry.FileRecordBytes,&Info);
    if(Status!=Rc95LdrSuccess)return Status;
    if(Info.BaseFileReference!=C->BaseReference)
        return Rc95LdrCorruptData;
    Status=Rc95LdrNtfsFindNamedAttribute(C->ExtensionRecord,
        C->Volume->Geometry.FileRecordBytes,
        C->Volume->Geometry.BytesPerSector,NTFS_ATTRIBUTE_DATA,NULL,0,&Attribute);
    if(Status!=Rc95LdrSuccess)return Status;
    if(!Attribute.NonResident||Attribute.Flags!=0||
       Attribute.NameLength!=0u||Attribute.Instance!=Entry->AttributeId||
       Attribute.LowestVcn!=Entry->LowestVcn)
        return Rc95LdrCorruptData;
    Status=Rc95LdrNtfsDecodeRunlist(Attribute.Value,Attribute.ValueLength,
        Attribute.LowestVcn,C->Volume->Geometry.ClusterCount,0,
        Extent,RC95LDR_ARRAY_COUNT(Extent),&ExtentCount,&Next);
    if(Status!=Rc95LdrSuccess)return Status;
    if(!ExtentCount||Attribute.HighestVcn==UINT64_MAX||
       Next!=Attribute.HighestVcn+1u)
        return Rc95LdrCorruptData;
    Status=Rc95LdrNtfsAppendRunlistExtent(Extent,ExtentCount,
        C->NextVcn,C->Volume->Geometry.ClusterCount,C->Volume->MftRuns,
        RC95LDR_NTFS_MAX_MFT_RUNS,&C->RunCount,&C->NextVcn);
    return Status;
}

RC95LDR_STATUS
Rc95LdrNtfsMountPartition(RC95LDR_NTFS_VOLUME *V,
    const RC95LDR_BLOCK_DEVICE *Device,uint64_t Start,uint64_t Count,
    void *Scratch,size_t ScratchLength)
{
    uint8_t Boot[NTFS_MAX_SECTOR_SIZE];
    RC95LDR_NTFS_VOLUME Candidate;
    RC95LDR_NTFS_ATTRIBUTE Data,AttributeList;
    RC95LDR_STATUS Status;
    uint64_t MftLba,RecordBlocks;
    size_t RunCount;
    RC95LDR_NTFS_FILE_RECORD_INFO MftInfo;
    if(!V)return Rc95LdrInvalidParameter;
    /* A failed remount must not leave a previously mounted volume usable. */
    memset(V,0,sizeof(*V));
    if(!Device||!Device->Read||!Scratch||!Count||
       Device->BlockSize>NTFS_MAX_SECTOR_SIZE||Device->BlockSize<512u||
       Start>=Device->BlockCount||Count>Device->BlockCount-Start)
        return Rc95LdrInvalidParameter;
    memset(&Candidate,0,sizeof(Candidate));
    Candidate.DeviceStorage=*Device;Candidate.PartitionStartLba=Start;
    Candidate.PartitionBlockCount=Count;
    Status=Rc95LdrReadBlocks(Device,Start,1,Boot,sizeof(Boot));
    if(Status!=Rc95LdrSuccess)return Status;
    Status=Rc95LdrNtfsParseGeometry(Boot,Device->BlockSize,Device->BlockSize,
                                    Count,&Candidate.Geometry);
    if(Status!=Rc95LdrSuccess)return Status;
    if(ScratchLength<Candidate.Geometry.FileRecordBytes)
        return Rc95LdrBufferTooSmall;
    if(Candidate.Geometry.FileRecordBytes%Candidate.Geometry.BytesPerSector)
        return Rc95LdrCorruptData;
    if(Candidate.Geometry.MftLcn>UINT64_MAX/
       Candidate.Geometry.SectorsPerCluster)
        return Rc95LdrIntegerOverflow;
    MftLba=Candidate.Geometry.MftLcn*Candidate.Geometry.SectorsPerCluster;
    RecordBlocks=Candidate.Geometry.FileRecordBytes/
                 Candidate.Geometry.BytesPerSector;
    if(RecordBlocks>UINT32_MAX||MftLba>Candidate.Geometry.TotalSectors||
       RecordBlocks>Candidate.Geometry.TotalSectors-MftLba)
        return Rc95LdrCorruptData;
    Status=NtfsReadPartitionBlocks(&Candidate,MftLba,(uint32_t)RecordBlocks,
                                   Scratch,Candidate.Geometry.FileRecordBytes);
    if(Status!=Rc95LdrSuccess)return Status;
    Status=Rc95LdrNtfsApplyFileRecordFixups(
        (uint8_t*)Scratch,Candidate.Geometry.FileRecordBytes,
        Candidate.Geometry.BytesPerSector);
    if(Status!=Rc95LdrSuccess)return Status;
    Status=Rc95LdrNtfsGetFileRecordInfo((const uint8_t*)Scratch,
        Candidate.Geometry.FileRecordBytes,&MftInfo);
    if(Status!=Rc95LdrSuccess||!(MftInfo.Flags&0x0001u)||
       !MftInfo.SequenceNumber||MftInfo.RecordNumber!=0u||
       MftInfo.BaseFileReference!=0u)
        return Rc95LdrCorruptData;
    Status=Rc95LdrNtfsFindNamedAttribute((const uint8_t*)Scratch,
        Candidate.Geometry.FileRecordBytes,Candidate.Geometry.BytesPerSector,
        NTFS_ATTRIBUTE_DATA,NULL,0,&Data);
    if(Status!=Rc95LdrSuccess)return Status;
    if(!Data.NonResident||Data.Flags!=0||Data.LowestVcn!=0||Data.ValueLength==0||
       Data.DataSize<Candidate.Geometry.FileRecordBytes)
        return Rc95LdrCorruptData;
    Status=Rc95LdrNtfsDecodeRunlist(Data.Value,Data.ValueLength,
        Data.LowestVcn,Candidate.Geometry.ClusterCount,0,
        Candidate.MftRuns,RC95LDR_NTFS_MAX_MFT_RUNS,&RunCount,
        &Candidate.MftNextVcn);
    if(Status!=Rc95LdrSuccess)return Status;
    if(!RunCount||Candidate.MftRuns[0].Lcn!=Candidate.Geometry.MftLcn||
       Data.HighestVcn==UINT64_MAX||
       Candidate.MftNextVcn!=Data.HighestVcn+1u||
       Candidate.MftNextVcn<
           ((uint64_t)Candidate.Geometry.FileRecordBytes+
            Candidate.Geometry.BytesPerCluster-1u)/
            Candidate.Geometry.BytesPerCluster)
        return Rc95LdrCorruptData;
    Candidate.MftRunCount=RunCount;
    Candidate.MftDataSize=Data.DataSize;
    Candidate.Mounted=1;
    Status=Rc95LdrNtfsFindAttribute((const uint8_t*)Scratch,
        Candidate.Geometry.FileRecordBytes,Candidate.Geometry.BytesPerSector,
        RC95LDR_NTFS_ATTRIBUTE_LIST,&AttributeList);
    if(Status==Rc95LdrSuccess){
        NTFS_MFT_ATTRIBUTE_LIST_CONTEXT ListContext;
        RC95LDR_NTFS_FILE_RECORD_INFO BaseInfo;
        size_t EntryCount=0;
        uint64_t BaseReference;
        if(AttributeList.NonResident||AttributeList.Flags||
           AttributeList.ValueLength==0)
            return Rc95LdrUnsupported;
        Status=Rc95LdrNtfsGetFileRecordInfo((const uint8_t*)Scratch,
            Candidate.Geometry.FileRecordBytes,&BaseInfo);
        if(Status!=Rc95LdrSuccess)return Status;
        BaseReference=((uint64_t)BaseInfo.SequenceNumber<<48)|
                      BaseInfo.RecordNumber;
        memset(&ListContext,0,sizeof(ListContext));
        ListContext.Volume=&Candidate;
        ListContext.ExtensionRecord=(uint8_t*)Scratch+
                                    Candidate.Geometry.FileRecordBytes;
        ListContext.ExtensionCapacity=ScratchLength-
                                     Candidate.Geometry.FileRecordBytes;
        ListContext.BaseReference=BaseReference;
        ListContext.BaseAttributeId=Data.Instance;
        ListContext.NextVcn=Candidate.MftNextVcn;
        ListContext.RunCount=Candidate.MftRunCount;
        Status=Rc95LdrNtfsEnumerateAttributeList(AttributeList.Value,
            AttributeList.ValueLength,NtfsMftAttributeListEntry,
            &ListContext,&EntryCount);
        if(Status!=Rc95LdrSuccess)return Status;
        if(!EntryCount||!ListContext.BaseEntrySeen)
            return Rc95LdrCorruptData;
        Candidate.MftRunCount=ListContext.RunCount;
        Candidate.MftNextVcn=ListContext.NextVcn;
    }else if(Status!=Rc95LdrNotFound){
        return Status;
    }
    *V=Candidate;
    return Rc95LdrSuccess;
}

void Rc95LdrNtfsUnmount(RC95LDR_NTFS_VOLUME *V)
{if(V)memset(V,0,sizeof(*V));}

RC95LDR_STATUS
Rc95LdrNtfsReadMftRecord(const RC95LDR_NTFS_VOLUME *V,uint64_t Number,
                         void *Buffer,size_t Capacity)
{
    uint64_t Offset;
    RC95LDR_STATUS Status;
    if (!V || !V->Mounted || !Buffer ||
        !V->Geometry.FileRecordBytes || !V->Geometry.BytesPerSector)
        return Rc95LdrInvalidParameter;
    if (Capacity < V->Geometry.FileRecordBytes)
        return Rc95LdrBufferTooSmall;
    if (Number > UINT64_MAX / V->Geometry.FileRecordBytes)
        return Rc95LdrIntegerOverflow;
    Offset=Number*V->Geometry.FileRecordBytes;
    Status=NtfsReadMftBytes(V,Offset,Buffer,V->Geometry.FileRecordBytes);
    if(Status!=Rc95LdrSuccess)return Status;
    return Rc95LdrNtfsApplyFileRecordFixups((uint8_t*)Buffer,
        V->Geometry.FileRecordBytes,V->Geometry.BytesPerSector);
}

RC95LDR_STATUS
Rc95LdrNtfsReadMftRecordByReference(const RC95LDR_NTFS_VOLUME *V,
    uint64_t FileReference,void *Buffer,size_t Capacity)
{
    uint64_t RecordNumber=FileReference&0x0000ffffffffffffULL;
    RC95LDR_STATUS Status;
    if(!V||!V->Mounted||!Buffer)return Rc95LdrInvalidParameter;
    Status=Rc95LdrNtfsReadMftRecord(V,RecordNumber,Buffer,Capacity);
    if(Status!=Rc95LdrSuccess)return Status;
    return Rc95LdrNtfsValidateFileReference((const uint8_t*)Buffer,
        V->Geometry.FileRecordBytes,FileReference);
}
