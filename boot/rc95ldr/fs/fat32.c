#include "rc95ldr_fat32.h"
#include <string.h>

#define FAT32_EOC 0x0ffffff8u
#define FAT32_BAD 0x0ffffff7u
#define FAT_ATTR_DIRECTORY 0x10u
#define FAT_ATTR_VOLUME 0x08u

static RC95LDR_STATUS
FatPartitionRead(void *Context,uint64_t Lba,uint32_t BlockCount,
                 void *Buffer,size_t BufferLength)
{
    RC95LDR_FAT32 *F=Context;
    if(!F||!F->Mounted||Lba>F->PartitionBlockCount||
       (uint64_t)BlockCount>F->PartitionBlockCount-Lba ||
       F->PartitionStartLba>UINT64_MAX-Lba)
        return Rc95LdrOutOfBounds;
    return Rc95LdrReadBlocks(F->Device,F->PartitionStartLba+Lba,
                             BlockCount,Buffer,BufferLength);
}

static RC95LDR_STATUS
FatReadSector(RC95LDR_FAT32 *F,uint64_t RelativeSector,void *Buffer)
{
    uint64_t Lba;
    if(RelativeSector>=F->PartitionBlockCount ||
       F->PartitionStartLba>UINT64_MAX-RelativeSector)
        return Rc95LdrOutOfBounds;
    Lba=F->PartitionStartLba+RelativeSector;
    return Rc95LdrReadBlocks(F->Device,Lba,1,Buffer,F->BytesPerSector);
}
static uint64_t
FatClusterLba(const RC95LDR_FAT32 *F,uint32_t Cluster)
{
    return (uint64_t)F->DataStartSector+
           (uint64_t)(Cluster-2u)*F->SectorsPerCluster;
}
static RC95LDR_STATUS
FatNextCluster(RC95LDR_FAT32 *F,uint32_t Cluster,uint32_t *Next)
{
    uint8_t Sector[RC95LDR_FAT32_MAX_SECTOR_SIZE];
    uint32_t Offset, FatSector, InSector;
    RC95LDR_STATUS S;
    if(Cluster<2 || Cluster>=F->ClusterCount+2 || Cluster>0x0ffffff5u)
        return Rc95LdrCorruptData;
    Offset=Cluster*4u; FatSector=Offset/F->BytesPerSector;
    InSector=Offset%F->BytesPerSector;
    if(FatSector>=F->FatSectors) return Rc95LdrCorruptData;
    S=FatReadSector(F,F->FatStartSector+FatSector,Sector);
    if(S!=Rc95LdrSuccess) return S;
    *Next=Rc95LdrReadU32(Sector+InSector)&0x0fffffffu;
    if(*Next==FAT32_BAD||(*Next>=2&&*Next<FAT32_EOC&&*Next>=F->ClusterCount+2))
        return Rc95LdrCorruptData;
    return Rc95LdrSuccess;
}
static void
FatMakeShortName(const char *Name,uint8_t Out[11])
{
    unsigned i=0,j=0;
    memset(Out,' ',11);
    while(Name[i]&&Name[i]!='.'&&j<8){char c=Name[i++];Out[j++]=(uint8_t)(c>='a'&&c<='z'?c-32:c);}
    if(Name[i]=='.'){i++;j=8;while(Name[i]&&j<11){char c=Name[i++];Out[j++]=(uint8_t)(c>='a'&&c<='z'?c-32:c);}}
}
static uint8_t
FatLfnChecksum(const uint8_t ShortName[11])
{
    uint8_t Sum=0; unsigned i;
    for(i=0;i<11;i++) Sum=(uint8_t)(((Sum&1u)?0x80u:0u)+(Sum>>1)+ShortName[i]);
    return Sum;
}
static int
FatLfnEquals(const uint16_t *LongName,const char *Name)
{
    size_t i=0;
    while(Name[i] && LongName[i] && LongName[i]!=0xffffu){
        uint16_t A=LongName[i]; unsigned char B=(unsigned char)Name[i];
        if(A>0x7f) return 0;
        if(A>='A'&&A<='Z') A=(uint16_t)(A+32);
        if(B>='A'&&B<='Z') B=(unsigned char)(B+32);
        if(A!=B) return 0;
        ++i;
    }
    return Name[i]==0 && (LongName[i]==0 || LongName[i]==0xffffu);
}
static void
FatCopyLfnPart(uint16_t *Name,const uint8_t *Entry,uint8_t Sequence)
{
    static const uint8_t Offsets[13]={1,3,5,7,9,14,16,18,20,22,24,28,30};
    unsigned i; size_t Base=(size_t)(Sequence-1u)*13u;
    for(i=0;i<13;i++) Name[Base+i]=Rc95LdrReadU16(Entry+Offsets[i]);
}
static RC95LDR_STATUS
FatFindInDirectory(RC95LDR_FAT32 *F,uint32_t Directory,const char *Name,
                   uint32_t *Cluster,uint32_t *Size,uint8_t *Attributes)
{
    uint8_t Sector[RC95LDR_FAT32_MAX_SECTOR_SIZE],Wanted[11];
    uint16_t LongName[260];
    uint8_t LfnChecksum=0,ExpectedSequence=0;
    int HaveLfn=0;
    uint32_t Current=Directory,Guard=0;
    FatMakeShortName(Name,Wanted);
    while(Current>=2&&Current<F->ClusterCount+2&&Guard++<F->ClusterCount){
        uint32_t S;
        for(S=0;S<F->SectorsPerCluster;S++){
            uint32_t Pos;
            RC95LDR_STATUS Status=FatReadSector(F,FatClusterLba(F,Current)+S,Sector);
            if(Status!=Rc95LdrSuccess) return Status;
            for(Pos=0;Pos+32<=F->BytesPerSector;Pos+=32){
                const uint8_t *E=Sector+Pos;
                uint8_t Attr=E[11];
                if(E[0]==0) return Rc95LdrNotFound;
                if(E[0]==0xe5){ExpectedSequence=0;HaveLfn=0;continue;}
                if(Attr==0x0f){
                    uint8_t Sequence=(uint8_t)(E[0]&0x1fu);
                    if(E[0]&0x40u){
                        unsigned L;
                        for(L=0;L<260;L++) LongName[L]=0xffffu;
                        ExpectedSequence=Sequence;
                        LfnChecksum=E[13];
                        HaveLfn=1;
                    }
                    if(!Sequence||Sequence!=ExpectedSequence||
                       E[13]!=LfnChecksum||Sequence>20){
                        ExpectedSequence=0; HaveLfn=0; continue;
                    }
                    FatCopyLfnPart(LongName,E,Sequence);
                    --ExpectedSequence;
                    continue;
                }
                if(Attr&FAT_ATTR_VOLUME){ExpectedSequence=0;HaveLfn=0;continue;}
                {
                    int NameMatches=(HaveLfn && ExpectedSequence==0 &&
                                     FatLfnChecksum(E)==LfnChecksum &&
                                     FatLfnEquals(LongName,Name)) ||
                                    memcmp(E,Wanted,11)==0;
                    ExpectedSequence=0; HaveLfn=0;
                    if(NameMatches){
                    *Cluster=((uint32_t)Rc95LdrReadU16(E+20)<<16)|Rc95LdrReadU16(E+26);
                    *Size=Rc95LdrReadU32(E+28); *Attributes=Attr;
                    return Rc95LdrSuccess;
                    }
                }
            }
        }
        {
            uint32_t Next; RC95LDR_STATUS Status=FatNextCluster(F,Current,&Next);
            if(Status!=Rc95LdrSuccess) return Status;
            if(Next>=FAT32_EOC) return Rc95LdrNotFound;
            if(Next<2) return Rc95LdrCorruptData;
            Current=Next;
        }
    }
    return Rc95LdrCorruptData;
}
static RC95LDR_STATUS
FatOpenRead(void *Context,const char *Path,void **Handle,uint64_t *FileSize)
{
    RC95LDR_FAT32 *F=Context; char Component[256]; uint32_t Directory,Cluster,Size;
    uint8_t Attr; unsigned Slot,i; const char *P=Path;
    if(!F||!F->Mounted||!Path||!Handle||!FileSize) return Rc95LdrInvalidParameter;
    while(*P=='/'||*P=='\\') P++;
    Directory=F->RootCluster;
    for(;;){
        i=0; while(P[i]&&P[i]!='/'&&P[i]!='\\'&&i<255){Component[i]=P[i];i++;}
        if(i==0||i>=255) return Rc95LdrUnsupported;
        Component[i]=0;
        while(P[i]=='/'||P[i]=='\\') i++;
        {
            RC95LDR_STATUS S=FatFindInDirectory(F,Directory,Component,&Cluster,&Size,&Attr);
            if(S!=Rc95LdrSuccess) return S;
        }
        P+=i;
        if(*P){
            if(!(Attr&FAT_ATTR_DIRECTORY)||Cluster<2) return Rc95LdrNotFound;
            Directory=Cluster;
        } else {
            if(Attr&FAT_ATTR_DIRECTORY) return Rc95LdrInvalidParameter;
            for(Slot=0;Slot<RC95LDR_FAT32_MAX_OPEN_FILES;Slot++)
                if(!F->Files[Slot].InUse) break;
            if(Slot==RC95LDR_FAT32_MAX_OPEN_FILES) return Rc95LdrOutOfMemory;
            F->Files[Slot].FirstCluster=Cluster; F->Files[Slot].FileSize=Size;
            F->Files[Slot].InUse=1; *Handle=&F->Files[Slot]; *FileSize=Size;
            return Rc95LdrSuccess;
        }
    }
}
static RC95LDR_STATUS
FatReadAt(void *Context,void *Handle,uint64_t Offset,void *Buffer,size_t Length)
{
    RC95LDR_FAT32 *F=Context; RC95LDR_FAT32_FILE *File=Handle;
    uint8_t Sector[RC95LDR_FAT32_MAX_SECTOR_SIZE];
    uint8_t *Out=Buffer; uint64_t ClusterBytes; uint32_t Cluster,Skip,Guard=0;
    if(!F||!File||!File->InUse||(!Buffer&&Length)) return Rc95LdrInvalidParameter;
    if(Offset>File->FileSize||Length>File->FileSize-Offset) return Rc95LdrOutOfBounds;
    ClusterBytes=(uint64_t)F->BytesPerSector*F->SectorsPerCluster;
    if(!Length) return Rc95LdrSuccess;
    if(File->FirstCluster<2 || File->FirstCluster>=F->ClusterCount+2)
        return Rc95LdrCorruptData;
    Cluster=File->FirstCluster; Skip=(uint32_t)(Offset/ClusterBytes);
    while(Skip--){
        uint32_t Next; RC95LDR_STATUS S=FatNextCluster(F,Cluster,&Next);
        if(S!=Rc95LdrSuccess||Next>=FAT32_EOC||Next<2||
           Next>=F->ClusterCount+2) return Rc95LdrCorruptData;
        Cluster=Next;
    }
    Offset%=ClusterBytes;
    while(Length){
        uint32_t SectorInCluster=(uint32_t)(Offset/F->BytesPerSector);
        uint32_t ByteInSector=(uint32_t)(Offset%F->BytesPerSector);
        size_t Chunk=F->BytesPerSector-ByteInSector;
        RC95LDR_STATUS S;
        if(Chunk>Length) Chunk=Length;
        S=FatReadSector(F,FatClusterLba(F,Cluster)+SectorInCluster,Sector);
        if(S!=Rc95LdrSuccess) return S;
        memcpy(Out,Sector+ByteInSector,Chunk);
        Out+=Chunk; Length-=Chunk; Offset+=Chunk;
        if(Offset>=ClusterBytes&&Length){
            uint32_t Next; S=FatNextCluster(F,Cluster,&Next);
            if(S!=Rc95LdrSuccess||Next>=FAT32_EOC||Next<2||
               ++Guard>F->ClusterCount) return Rc95LdrCorruptData;
            Cluster=Next; Offset=0;
        }
    }
    return Rc95LdrSuccess;
}
static void FatClose(void *Context,void *Handle)
{ RC95LDR_FAT32_FILE *F=Handle; (void)Context; if(F) memset(F,0,sizeof(*F)); }

RC95LDR_STATUS
Rc95LdrFat32MountPartition(RC95LDR_FAT32 *F,const RC95LDR_BLOCK_DEVICE *Device,
                           uint64_t StartLba,uint64_t PartitionBlockCount)
{
    uint8_t B[RC95LDR_FAT32_MAX_SECTOR_SIZE];
    uint32_t Total,FatSize,Root,DataStart,ActiveFat;
    uint16_t ExtendedFlags;
    uint64_t FatBytes;
    RC95LDR_STATUS S;
    if(!F||!Device||!Device->Read||Device->BlockSize>RC95LDR_FAT32_MAX_SECTOR_SIZE||
       Device->BlockSize<512||!PartitionBlockCount||StartLba>=Device->BlockCount||
       PartitionBlockCount>Device->BlockCount-StartLba)
        return Rc95LdrInvalidParameter;
    memset(F,0,sizeof(*F)); F->Device=Device; F->PartitionStartLba=StartLba;
    F->PartitionBlockCount=PartitionBlockCount;
    S=Rc95LdrReadBlocks(Device,StartLba,1,B,sizeof(B)); if(S!=Rc95LdrSuccess)return S;
    if(B[510]!=0x55||B[511]!=0xaa) return Rc95LdrInvalidImage;
    if(StartLba>=Device->BlockCount) return Rc95LdrOutOfBounds;
    F->BytesPerSector=Rc95LdrReadU16(B+11); F->SectorsPerCluster=B[13];
    F->ReservedSectors=Rc95LdrReadU16(B+14); F->FatCount=B[16];
    FatSize=Rc95LdrReadU32(B+36); Root=Rc95LdrReadU32(B+44);
    ExtendedFlags=Rc95LdrReadU16(B+40);
    Total=Rc95LdrReadU32(B+32); if(!Total) Total=Rc95LdrReadU16(B+19);
    if((F->BytesPerSector!=512 && F->BytesPerSector!=1024 &&
        F->BytesPerSector!=2048 && F->BytesPerSector!=4096) ||
       F->BytesPerSector!=Device->BlockSize||!F->SectorsPerCluster||
       (F->SectorsPerCluster&(F->SectorsPerCluster-1))||
       !F->ReservedSectors||!F->FatCount||F->FatCount>2||!FatSize||Root<2||
       Rc95LdrReadU16(B+17)!=0||Rc95LdrReadU16(B+22)!=0||
       Rc95LdrReadU16(B+42)!=0||
       !Total||Total>PartitionBlockCount) return Rc95LdrCorruptData;
    {
        uint64_t Start=(uint64_t)F->ReservedSectors+(uint64_t)F->FatCount*FatSize;
        if(Start>UINT32_MAX) return Rc95LdrIntegerOverflow;
        DataStart=(uint32_t)Start;
    }
    F->FatSectors=FatSize; F->RootCluster=Root;
    ActiveFat=((ExtendedFlags&0x0080u)!=0)?(ExtendedFlags&0x000fu):0;
    if(ActiveFat>=F->FatCount) return Rc95LdrCorruptData;
    F->FatStartSector=F->ReservedSectors+ActiveFat*FatSize;
    F->DataStartSector=DataStart;
    if(F->DataStartSector>=Total) return Rc95LdrCorruptData;
    F->ClusterCount=(Total-F->DataStartSector)/F->SectorsPerCluster;
    if(F->ClusterCount<65525u) return Rc95LdrUnsupported; /* FAT32 only */
    if(F->ClusterCount>0x0ffffff5u || Root>=F->ClusterCount+2u)
        return Rc95LdrCorruptData;
    FatBytes=(uint64_t)FatSize*F->BytesPerSector;
    if(FatBytes<(uint64_t)(F->ClusterCount+2u)*4u)
        return Rc95LdrCorruptData;
    F->Mounted=1;
    F->PartitionDevice.DeviceContext=F;
    F->PartitionDevice.BlockSize=Device->BlockSize;
    F->PartitionDevice.BlockCount=PartitionBlockCount;
    F->PartitionDevice.Read=FatPartitionRead;
    F->Interface.FileSystemContext=F; F->Interface.OpenRead=FatOpenRead;
    F->Interface.ReadAt=FatReadAt; F->Interface.Close=FatClose;
    return Rc95LdrSuccess;
}
RC95LDR_STATUS
Rc95LdrFat32Mount(RC95LDR_FAT32 *F,const RC95LDR_BLOCK_DEVICE *Device,uint64_t StartLba)
{
    if(!Device || StartLba>=Device->BlockCount)
        return Rc95LdrInvalidParameter;
    return Rc95LdrFat32MountPartition(F,Device,StartLba,
        Device->BlockCount-StartLba);
}

void Rc95LdrFat32Unmount(RC95LDR_FAT32 *F)
{ if(F) memset(F,0,sizeof(*F)); }
const RC95LDR_FILESYSTEM *Rc95LdrFat32GetInterface(RC95LDR_FAT32 *F)
{ return F&&F->Mounted?&F->Interface:NULL; }

RC95LDR_STATUS
Rc95LdrFat32BindVolume(RC95LDR_FAT32 *F,RC95LDR_VOLUME *Volume)
{
    if(!F||!Volume||!F->Mounted) return Rc95LdrInvalidParameter;
    Volume->BlockDevice=&F->PartitionDevice;
    Volume->FileSystem=&F->Interface;
    return Rc95LdrSuccess;
}
