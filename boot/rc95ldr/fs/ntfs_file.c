#include "rc95ldr_ntfs_volume.h"
#include "rc95ldr_ntfs_index.h"
#include "rc95ldr_ntfs_stream.h"
#include <string.h>

#define NTFS_ATTR_DATA 0x80u
#define NTFS_FILE_DIRECTORY 0x0002u
#define NTFS_FILE_IN_USE 0x0001u
#define NTFS_MAX_FILE_RUNS 128u
#define NTFS_MAX_PATH 512u

typedef struct _NTFS_INDEX_LOOKUP_CONTEXT {
    uint64_t ParentReference;
    const uint16_t *Name;
    size_t NameLength;
    uint64_t FoundReference;
    unsigned Found;
} NTFS_INDEX_LOOKUP_CONTEXT;


#define NTFS_ATTR_INDEX_ALLOCATION 0xa0u
#define NTFS_ATTR_BITMAP 0xb0u

static RC95LDR_STATUS
NtfsReadBitmapByte(const RC95LDR_NTFS_VOLUME *V,
    const RC95LDR_NTFS_ATTRIBUTE *Bitmap,
    const RC95LDR_NTFS_STREAM *BitmapStream,uint64_t ByteOffset,
    uint8_t *Value,void *Bounce,size_t BounceLength)
{
    uint32_t Sector=V->Geometry.BytesPerSector;
    uint64_t Aligned=ByteOffset&~((uint64_t)Sector-1u);
    uint8_t *B=(uint8_t*)Bounce;
    RC95LDR_STATUS S;
    if(ByteOffset>=Bitmap->DataSize)return Rc95LdrCorruptData;
    if(!Bitmap->NonResident){
        if(ByteOffset>=Bitmap->ValueLength)return Rc95LdrCorruptData;
        *Value=Bitmap->Value[(size_t)ByteOffset];
        return Rc95LdrSuccess;
    }
    if(!BitmapStream)return Rc95LdrInvalidParameter;
    if(BounceLength<Sector)return Rc95LdrBufferTooSmall;
    if(Aligned>BitmapStream->AllocatedSize||
       Sector>BitmapStream->AllocatedSize-Aligned)
        return Rc95LdrCorruptData;
    S=Rc95LdrNtfsReadStreamRange(V,BitmapStream,Aligned,B,Sector,
        Bounce,BounceLength);
    if(S!=Rc95LdrSuccess)return S;
    *Value=B[(size_t)(ByteOffset-Aligned)];
    return Rc95LdrSuccess;
}

typedef struct _NTFS_INDEX_ROUTE_CONTEXT {
    NTFS_INDEX_LOOKUP_CONTEXT Match;
    const uint16_t *Target;
    size_t TargetLength;
    uint64_t ChildVcn;
    unsigned HaveChild;
    unsigned BranchChosen;
} NTFS_INDEX_ROUTE_CONTEXT;

static int NtfsCompareIndexName(const uint16_t *A,size_t ALen,
                                const uint16_t *B,size_t BLen)
{
    size_t I,Limit=ALen<BLen?ALen:BLen;
    for(I=0;I<Limit;I++){
        uint16_t X=A[I],Y=B[I];
        if(X>='A'&&X<='Z')X=(uint16_t)(X+('a'-'A'));
        if(Y>='A'&&Y<='Z')Y=(uint16_t)(Y+('a'-'A'));
        if(X<Y)return -1;
        if(X>Y)return 1;
    }
    return ALen<BLen?-1:(ALen>BLen?1:0);
}

static RC95LDR_STATUS NtfsRouteIndexEntry(void *Opaque,
    const RC95LDR_NTFS_INDEX_ENTRY *Entry)
{
    NTFS_INDEX_ROUTE_CONTEXT *C=(NTFS_INDEX_ROUTE_CONTEXT*)Opaque;
    RC95LDR_NTFS_FILE_NAME_KEY Key;
    int Compare;
    RC95LDR_STATUS S;
    if(Entry->Flags&RC95LDR_NTFS_INDEX_ENTRY_END){
        if(!C->BranchChosen&&(Entry->Flags&RC95LDR_NTFS_INDEX_ENTRY_NODE)){
            C->ChildVcn=Entry->ChildVcn;C->HaveChild=1;
        }
        return Rc95LdrSuccess;
    }
    S=Rc95LdrNtfsDecodeFileNameKey(Entry->Key,Entry->KeyLength,&Key);
    if(S!=Rc95LdrSuccess)return S;
    if(Key.ParentFileReference!=C->Match.ParentReference)
        return Rc95LdrCorruptData;
    Compare=NtfsCompareIndexName(C->Target,C->TargetLength,Key.Name,
                                 Key.NameLength);
    if(Compare==0&&(Key.Namespace==1u||Key.Namespace==3u)){
        if(C->Match.Found&&C->Match.FoundReference!=Entry->FileReference)
            return Rc95LdrCorruptData;
        C->Match.FoundReference=Entry->FileReference;
        C->Match.Found=1;
        return Rc95LdrSuccess;
    }
    if(!C->BranchChosen&&Compare<0){
        if(!(Entry->Flags&RC95LDR_NTFS_INDEX_ENTRY_NODE))
            return Rc95LdrCorruptData;
        C->ChildVcn=Entry->ChildVcn;C->HaveChild=1;
        C->BranchChosen=1;
    }else if(!C->BranchChosen&&
             (Entry->Flags&RC95LDR_NTFS_INDEX_ENTRY_NODE)){
        C->ChildVcn=Entry->ChildVcn;C->HaveChild=1;
    }
    return Rc95LdrSuccess;
}

RC95LDR_STATUS
Rc95LdrNtfsFindChildByName(const RC95LDR_NTFS_VOLUME *V,uint64_t Parent,
    const uint16_t *Name,size_t NameLength,void *Record,size_t RecordCapacity,
    uint64_t *Reference,uint16_t *Flags)
{
    static const uint16_t IndexName[4]={0x24u,0x49u,0x33u,0x30u};
    NTFS_INDEX_ROUTE_CONTEXT Route;
    RC95LDR_NTFS_FILE_RECORD_INFO Info;
    RC95LDR_NTFS_ATTRIBUTE Allocation,Bitmap;
    RC95LDR_NTFS_STREAM AllocationStream,BitmapStream;
    uint8_t *ExtensionRecord;
    uint8_t *IndexBuffer,*Bounce;
    size_t RecordBytes,IndexBytes,SectorBytes,EntryCount=0;
    uint64_t BufferCount,VcnUnit,CurrentVcn,Visited[256];
    size_t VisitedCount=0;
    RC95LDR_STATUS S;
    if(!V||!Name||!NameLength||!Record||!Reference||!Flags)
        return Rc95LdrInvalidParameter;
    RecordBytes=V->Geometry.FileRecordBytes;
    IndexBytes=V->Geometry.IndexBufferBytes;
    SectorBytes=V->Geometry.BytesPerSector;
    if(!IndexBytes||IndexBytes<SectorBytes||IndexBytes%SectorBytes)
        return Rc95LdrCorruptData;
    if(RecordCapacity<RecordBytes)return Rc95LdrBufferTooSmall;
    S=Rc95LdrNtfsReadMftRecordByReference(V,Parent,Record,RecordCapacity);
    if(S!=Rc95LdrSuccess)return S;
    S=Rc95LdrNtfsGetFileRecordInfo((const uint8_t*)Record,RecordBytes,&Info);
    if(S!=Rc95LdrSuccess||!(Info.Flags&NTFS_FILE_DIRECTORY))
        return Rc95LdrCorruptData;
    memset(&Route,0,sizeof(Route));
    Route.Match.ParentReference=Parent;Route.Match.Name=Name;
    Route.Match.NameLength=NameLength;Route.Target=Name;
    Route.TargetLength=NameLength;
    S=Rc95LdrNtfsEnumerateDirectoryIndexRoot((const uint8_t*)Record,
        RecordBytes,SectorBytes,IndexBytes,NtfsRouteIndexEntry,&Route,&EntryCount);
    if(S!=Rc95LdrSuccess&&S!=Rc95LdrNotFound)return S;
    if(Route.Match.Found)goto ResolveChild;
    if(!Route.HaveChild)return Rc95LdrNotFound;
    if(IndexBytes>SIZE_MAX-RecordBytes||
       SectorBytes>SIZE_MAX-RecordBytes-IndexBytes||
       RecordBytes>SIZE_MAX-RecordBytes-IndexBytes-SectorBytes||
       RecordCapacity<RecordBytes+IndexBytes+SectorBytes+RecordBytes)
        return Rc95LdrBufferTooSmall;
    IndexBuffer=(uint8_t*)Record+RecordBytes;
    Bounce=IndexBuffer+IndexBytes;
    ExtensionRecord=Bounce+SectorBytes;
    S=Rc95LdrNtfsFindNamedAttribute((const uint8_t*)Record,RecordBytes,
        SectorBytes,NTFS_ATTR_INDEX_ALLOCATION,IndexName,4,&Allocation);
    if(S!=Rc95LdrSuccess)return S;
    S=Rc95LdrNtfsFindNamedAttribute((const uint8_t*)Record,RecordBytes,
        SectorBytes,NTFS_ATTR_BITMAP,IndexName,4,&Bitmap);
    if(S!=Rc95LdrSuccess)return S;
    if(!Allocation.NonResident||Allocation.Flags||Allocation.LowestVcn||
       !Allocation.DataSize||Allocation.DataSize%IndexBytes||Bitmap.Flags)
        return Rc95LdrCorruptData;
    S=Rc95LdrNtfsCollectStream(V,(const uint8_t*)Record,RecordBytes,
        Parent,NTFS_ATTR_INDEX_ALLOCATION,IndexName,4,
        ExtensionRecord,RecordBytes,&AllocationStream);
    if(S!=Rc95LdrSuccess)return S;
    if(Bitmap.NonResident){
        S=Rc95LdrNtfsCollectStream(V,(const uint8_t*)Record,RecordBytes,
            Parent,NTFS_ATTR_BITMAP,IndexName,4,
            ExtensionRecord,RecordBytes,&BitmapStream);
        if(S!=Rc95LdrSuccess)return S;
    }else memset(&BitmapStream,0,sizeof(BitmapStream));
    if(AllocationStream.DataSize%IndexBytes||
       AllocationStream.DataSize>V->Geometry.ClusterCount*
                                  (uint64_t)V->Geometry.BytesPerCluster)
        return Rc95LdrCorruptData;
    BufferCount=AllocationStream.DataSize/IndexBytes;
    if(!BufferCount||BufferCount>UINT64_MAX-7u||
       Bitmap.DataSize<(BufferCount+7u)/8u)
        return Rc95LdrCorruptData;
    VcnUnit=V->Geometry.BytesPerCluster<=IndexBytes?
        V->Geometry.BytesPerCluster:SectorBytes;
    if(!VcnUnit||IndexBytes%VcnUnit)return Rc95LdrCorruptData;
    CurrentVcn=Route.ChildVcn;
    for(;;){
        uint64_t StreamOffset,Ordinal,ByteOffset;
        uint8_t Bits;
        size_t I,BufferEntries=0;
        int Seen=0;
        if(CurrentVcn>UINT64_MAX/VcnUnit)return Rc95LdrCorruptData;
        StreamOffset=CurrentVcn*VcnUnit;
        if(StreamOffset%IndexBytes)return Rc95LdrCorruptData;
        Ordinal=StreamOffset/IndexBytes;
        if(Ordinal>=BufferCount)return Rc95LdrCorruptData;
        for(I=0;I<VisitedCount;I++)if(Visited[I]==CurrentVcn){Seen=1;break;}
        if(Seen)return Rc95LdrCorruptData;
        if(VisitedCount==sizeof(Visited)/sizeof(Visited[0]))
            return Rc95LdrUnsupported;
        Visited[VisitedCount++]=CurrentVcn;
        ByteOffset=Ordinal/8u;
        S=NtfsReadBitmapByte(V,&Bitmap,Bitmap.NonResident?&BitmapStream:NULL,
            ByteOffset,&Bits,Bounce,SectorBytes);
        if(S!=Rc95LdrSuccess)return S;
        if(!(Bits&(uint8_t)(1u<<(Ordinal&7u))))return Rc95LdrCorruptData;
        S=Rc95LdrNtfsReadStreamRange(V,&AllocationStream,StreamOffset,IndexBuffer,
            IndexBytes,Bounce,SectorBytes);
        if(S!=Rc95LdrSuccess)return S;
        S=Rc95LdrNtfsApplyIndexBufferFixups(IndexBuffer,IndexBytes,SectorBytes);
        if(S!=Rc95LdrSuccess)return S;
        memset(&Route,0,sizeof(Route));
        Route.Match.ParentReference=Parent;Route.Match.Name=Name;
        Route.Match.NameLength=NameLength;Route.Target=Name;
        Route.TargetLength=NameLength;
        S=Rc95LdrNtfsEnumerateIndexBuffer(IndexBuffer,IndexBytes,SectorBytes,
            CurrentVcn,NtfsRouteIndexEntry,&Route,&BufferEntries);
        if(S!=Rc95LdrSuccess)return S;
        if(Route.Match.Found)break;
        if(!Route.HaveChild)return Rc95LdrNotFound;
        CurrentVcn=Route.ChildVcn;
    }
ResolveChild:
    S=Rc95LdrNtfsReadMftRecordByReference(V,Route.Match.FoundReference,
        Record,RecordCapacity);
    if(S!=Rc95LdrSuccess)return S;
    S=Rc95LdrNtfsGetFileRecordInfo((const uint8_t*)Record,RecordBytes,&Info);
    if(S!=Rc95LdrSuccess||!(Info.Flags&NTFS_FILE_IN_USE))
        return Rc95LdrCorruptData;
    *Reference=Route.Match.FoundReference;*Flags=Info.Flags;
    return Rc95LdrSuccess;
}

static RC95LDR_STATUS NtfsFindChild(const RC95LDR_NTFS_VOLUME *V,
    uint64_t Parent,const uint16_t *Name,size_t NameLength,
    void *Record,size_t RecordCapacity,uint64_t *Reference,uint16_t *Flags)
{
    return Rc95LdrNtfsFindChildByName(V,Parent,Name,NameLength,
        Record,RecordCapacity,Reference,Flags);
}

static RC95LDR_STATUS NtfsReadFilePrefixByReference(
    const RC95LDR_NTFS_VOLUME *V, uint64_t Reference,
    void *Record, size_t RecordCapacity, void *Output,
    size_t OutputCapacity, size_t *BytesRead, uint64_t *FileSize)
{
    RC95LDR_NTFS_ATTRIBUTE Data;
    RC95LDR_STATUS S;
    size_t RecordBytes, ToRead;
    if (!V || !Record || !Output || !BytesRead || !FileSize)
        return Rc95LdrInvalidParameter;
    *BytesRead = 0; *FileSize = 0;
    RecordBytes = V->Geometry.FileRecordBytes;
    S = Rc95LdrNtfsReadMftRecordByReference(V, Reference, Record, RecordCapacity);
    if (S != Rc95LdrSuccess) return S;
    S = Rc95LdrNtfsFindAttribute((const uint8_t *)Record, RecordBytes,
        V->Geometry.BytesPerSector, NTFS_ATTR_DATA, &Data);
    if (S != Rc95LdrSuccess) return S;
    if (Data.Flags || Data.NameLength) return Rc95LdrUnsupported;
    *FileSize = Data.DataSize;
    ToRead = OutputCapacity;
    if ((uint64_t)ToRead > Data.DataSize) ToRead = (size_t)Data.DataSize;
    if (!ToRead) return Rc95LdrSuccess;
    if (!Data.NonResident) {
        if (ToRead > Data.ValueLength) return Rc95LdrCorruptData;
        memcpy(Output, Data.Value, ToRead);
        *BytesRead = ToRead;
        return Rc95LdrSuccess;
    }
    {
        RC95LDR_NTFS_RUN Runs[NTFS_MAX_FILE_RUNS];
        size_t RunCount = 0, I, InitializedBytes;
        uint64_t NextVcn, ClusterBytes = V->Geometry.BytesPerCluster;
        uint64_t Rounded;
        uint8_t *Out = (uint8_t *)Output;
        if (Data.LowestVcn != 0 || !ClusterBytes ||
            Data.HighestVcn == UINT64_MAX ||
            Data.InitializedSize > Data.DataSize)
            return Rc95LdrUnsupported;
        InitializedBytes = ToRead;
        if ((uint64_t)InitializedBytes > Data.InitializedSize)
            InitializedBytes = (size_t)Data.InitializedSize;
        if (!InitializedBytes) {
            memset(Output, 0, ToRead);
            *BytesRead = ToRead;
            return Rc95LdrSuccess;
        }
        S = Rc95LdrNtfsDecodeRunlist(Data.Value, Data.ValueLength, 0,
            V->Geometry.ClusterCount, 0, Runs, NTFS_MAX_FILE_RUNS,
            &RunCount, &NextVcn);
        if (S != Rc95LdrSuccess) return S;
        if (!RunCount || NextVcn != Data.HighestVcn + 1u)
            return Rc95LdrCorruptData;
        if (InitializedBytes > SIZE_MAX - (V->Geometry.BytesPerSector - 1u))
            return Rc95LdrIntegerOverflow;
        Rounded = ((uint64_t)InitializedBytes + V->Geometry.BytesPerSector - 1u) /
                  V->Geometry.BytesPerSector * V->Geometry.BytesPerSector;
        if (Rounded > OutputCapacity) return Rc95LdrBufferTooSmall;
        for (I = 0; I < RunCount && *BytesRead < Rounded; ++I) {
            uint64_t RunStart, RunBytes;
            uint64_t Take, Lba, Blocks;
            if (Runs[I].Vcn > UINT64_MAX / ClusterBytes ||
                Runs[I].ClusterCount > UINT64_MAX / ClusterBytes)
                return Rc95LdrIntegerOverflow;
            RunStart = Runs[I].Vcn * ClusterBytes;
            RunBytes = Runs[I].ClusterCount * ClusterBytes;
            if (RunStart > UINT64_MAX - RunBytes)
                return Rc95LdrIntegerOverflow;
            if (Runs[I].Sparse) return Rc95LdrUnsupported;
            if (*BytesRead < RunStart) return Rc95LdrCorruptData;
            if (*BytesRead >= RunStart + RunBytes) continue;
            Take = RunStart + RunBytes - *BytesRead;
            if (Take > Rounded - *BytesRead) Take = Rounded - *BytesRead;
            if ((Take % V->Geometry.BytesPerSector) != 0 ||
                Runs[I].Lcn > UINT64_MAX / V->Geometry.SectorsPerCluster)
                return Rc95LdrCorruptData;
            Lba = Runs[I].Lcn * V->Geometry.SectorsPerCluster;
            if (Lba > UINT64_MAX -
                ((*BytesRead - RunStart) / V->Geometry.BytesPerSector))
                return Rc95LdrIntegerOverflow;
            Lba += (*BytesRead - RunStart) / V->Geometry.BytesPerSector;
            Blocks = Take / V->Geometry.BytesPerSector;
            if (Blocks > UINT32_MAX || Lba > V->PartitionBlockCount ||
                Blocks > V->PartitionBlockCount - Lba ||
                V->PartitionStartLba > UINT64_MAX - Lba)
                return Rc95LdrOutOfBounds;
            S = Rc95LdrReadBlocks(&V->DeviceStorage, V->PartitionStartLba + Lba,
                (uint32_t)Blocks, Out + (size_t)*BytesRead, (size_t)Take);
            if (S != Rc95LdrSuccess) return S;
            *BytesRead += (size_t)Take;
        }
        if (*BytesRead < Rounded) return Rc95LdrCorruptData;
        if (InitializedBytes < ToRead)
            memset(Out + InitializedBytes, 0, ToRead - InitializedBytes);
        *BytesRead = ToRead;
    }
    return Rc95LdrSuccess;
}

RC95LDR_STATUS Rc95LdrNtfsReadFilePrefix(const RC95LDR_NTFS_VOLUME *V,
    const uint16_t *Path, size_t PathCharacters, void *Scratch,
    size_t ScratchLength, void *Output, size_t OutputCapacity,
    size_t *BytesRead, uint64_t *FileSize)
{
    uint64_t Parent, Child;
    uint16_t Flags;
    size_t Offset = 0, SegmentStart, SegmentLength;
    RC95LDR_NTFS_FILE_RECORD_INFO Root;
    RC95LDR_STATUS S;
    if (!V || !V->Mounted || !Path || !PathCharacters || PathCharacters > NTFS_MAX_PATH ||
        !Scratch || !Output || !BytesRead || !FileSize)
        return Rc95LdrInvalidParameter;
    if (ScratchLength < V->Geometry.FileRecordBytes) return Rc95LdrBufferTooSmall;
    S = Rc95LdrNtfsReadMftRecord(V, 5, Scratch, ScratchLength);
    if (S != Rc95LdrSuccess) return S;
    S = Rc95LdrNtfsGetFileRecordInfo((const uint8_t *)Scratch,
        V->Geometry.FileRecordBytes, &Root);
    if (S != Rc95LdrSuccess ||
        !(Root.Flags & NTFS_FILE_IN_USE) ||
        !(Root.Flags & NTFS_FILE_DIRECTORY))
        return Rc95LdrCorruptData;
    Parent = ((uint64_t)Root.SequenceNumber << 48) | 5u;
    while (Offset < PathCharacters) {
        while (Offset < PathCharacters && (Path[Offset] == '\\' || Path[Offset] == '/')) ++Offset;
        if (Offset == PathCharacters) break;
        SegmentStart = Offset;
        while (Offset < PathCharacters && Path[Offset] != '\\' && Path[Offset] != '/') ++Offset;
        SegmentLength = Offset - SegmentStart;
        if (!SegmentLength || SegmentLength > 255u) return Rc95LdrInvalidImage;
        S = NtfsFindChild(V, Parent, Path + SegmentStart, SegmentLength,
            Scratch, ScratchLength, &Child, &Flags);
        if (S != Rc95LdrSuccess) return S;
        if (Offset < PathCharacters) {
            if (!(Flags & NTFS_FILE_DIRECTORY)) return Rc95LdrNotFound;
            Parent = Child;
        } else {
            if (Flags & NTFS_FILE_DIRECTORY) return Rc95LdrInvalidImage;
            return NtfsReadFilePrefixByReference(V, Child, Scratch,
                ScratchLength, Output, OutputCapacity, BytesRead, FileSize);
        }
    }
    return Rc95LdrInvalidImage;
}
