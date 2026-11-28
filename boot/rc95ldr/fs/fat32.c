#include "rc95ldr_fat32.h"
#include <string.h>

#define FAT32_EOC 0x0ffffff8u
#define FAT32_BAD 0x0ffffff7u
/* FAT32 reserves cluster numbers 0x0ffffff0 through 0x0ffffff7. */
#define FAT32_MAX_DATA_CLUSTER 0x0fffffefu
#define FAT32_MAX_CLUSTER_COUNT (FAT32_MAX_DATA_CLUSTER - 1u)

/* ReactOS headers may already provide the standard FAT attribute flags. */
#ifndef FAT_ATTR_READ_ONLY
#define FAT_ATTR_READ_ONLY 0x01u
#endif
#ifndef FAT_ATTR_DIRECTORY
#define FAT_ATTR_DIRECTORY 0x10u
#endif
#ifndef FAT_ATTR_VOLUME
#define FAT_ATTR_VOLUME 0x08u
#endif

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
FatPartitionWrite(void *Context,uint64_t Lba,uint32_t Blocks,
                  const void *Buffer,size_t BufferLength)
{
    RC95LDR_FAT32 *F=(RC95LDR_FAT32*)Context;
    size_t Expected;
    if(!F||!F->Mounted||!F->Device||!F->Device->Write)
        return Rc95LdrUnsupported;
    if(Lba>F->PartitionBlockCount||
       (uint64_t)Blocks>F->PartitionBlockCount-Lba||
       F->PartitionStartLba>UINT64_MAX-Lba)
        return Rc95LdrOutOfBounds;
    if(!Rc95LdrMultiplySize(Blocks,F->BytesPerSector,&Expected))
        return Rc95LdrIntegerOverflow;
    if(Expected!=BufferLength)return Rc95LdrInvalidParameter;
    return F->Device->Write(F->Device->DeviceContext,
        F->PartitionStartLba+Lba,Blocks,Buffer,BufferLength);
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
static RC95LDR_STATUS
FatWriteSector(RC95LDR_FAT32 *F,uint64_t RelativeSector,const void *Buffer)
{
    uint64_t Lba;
    if(!F||!F->Device||!F->Device->Write)return Rc95LdrUnsupported;
    if(RelativeSector>=F->PartitionBlockCount ||
       F->PartitionStartLba>UINT64_MAX-RelativeSector)
        return Rc95LdrOutOfBounds;
    Lba=F->PartitionStartLba+RelativeSector;
    return F->Device->Write(F->Device->DeviceContext,Lba,1,Buffer,F->BytesPerSector);
}
typedef struct _FAT32_ENTRY_LOCATION {
    uint64_t Sector[21];
    uint16_t Offset[21];
    uint8_t Count;
} FAT32_ENTRY_LOCATION;
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
    if(Cluster<2 || Cluster>=F->ClusterCount+2 ||
       Cluster>FAT32_MAX_DATA_CLUSTER)
        return Rc95LdrCorruptData;
    Offset=Cluster*4u; FatSector=Offset/F->BytesPerSector;
    InSector=Offset%F->BytesPerSector;
    if(FatSector>=F->FatSectors) return Rc95LdrCorruptData;
    S=FatReadSector(F,F->FatStartSector+FatSector,Sector);
    if(S!=Rc95LdrSuccess) return S;
    *Next=Rc95LdrReadU32(Sector+InSector)&0x0fffffffu;
    /* Only data-cluster links and the EOC range are legal here. Free,
     * reserved, bad, and out-of-volume cluster markers are corruption. */
    if(*Next<FAT32_EOC &&
       (*Next<2 || *Next==FAT32_BAD ||
        (*Next>=0x0ffffff0u && *Next<=0x0ffffff6u) ||
        *Next>=F->ClusterCount+2u))
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
                   uint32_t *Cluster,uint32_t *Size,uint8_t *Attributes,
                   FAT32_ENTRY_LOCATION *Location)
{
    uint8_t Sector[RC95LDR_FAT32_MAX_SECTOR_SIZE],Wanted[11];
    uint16_t LongName[260];
    uint8_t LfnChecksum=0,ExpectedSequence=0;
    int HaveLfn=0;
    FAT32_ENTRY_LOCATION LfnLocation;
    memset(&LfnLocation,0,sizeof(LfnLocation));
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
                if(E[0]==0xe5){ExpectedSequence=0;HaveLfn=0;LfnLocation.Count=0;continue;}
                if(Attr==0x0f){
                    uint8_t Sequence=(uint8_t)(E[0]&0x1fu);
                    if(E[0]&0x40u){
                        unsigned L;
                        for(L=0;L<260;L++) LongName[L]=0xffffu;
                        ExpectedSequence=Sequence;
                        LfnChecksum=E[13];
                        HaveLfn=1;
                        LfnLocation.Count=0;
                    }
                    if(!Sequence||Sequence!=ExpectedSequence||
                       E[13]!=LfnChecksum||Sequence>20){
                        ExpectedSequence=0; HaveLfn=0; continue;
                    }
                    FatCopyLfnPart(LongName,E,Sequence);
                    if(LfnLocation.Count<20u){
                        LfnLocation.Sector[LfnLocation.Count]=
                            FatClusterLba(F,Current)+S;
                        LfnLocation.Offset[LfnLocation.Count]=(uint16_t)Pos;
                        ++LfnLocation.Count;
                    }else {HaveLfn=0;LfnLocation.Count=0;}
                    --ExpectedSequence;
                    continue;
                }
                if(Attr&FAT_ATTR_VOLUME){ExpectedSequence=0;HaveLfn=0;LfnLocation.Count=0;continue;}
                {
                    int NameMatches=(HaveLfn && ExpectedSequence==0 &&
                                     FatLfnChecksum(E)==LfnChecksum &&
                                     FatLfnEquals(LongName,Name)) ||
                                    memcmp(E,Wanted,11)==0;
                    ExpectedSequence=0; HaveLfn=0;
                    if(NameMatches){
                    *Cluster=((uint32_t)Rc95LdrReadU16(E+20)<<16)|Rc95LdrReadU16(E+26);
                    *Size=Rc95LdrReadU32(E+28); *Attributes=Attr;
                    if(Location){
                        unsigned L;
                        memset(Location,0,sizeof(*Location));
                        for(L=0;L<LfnLocation.Count;L++){
                            Location->Sector[Location->Count]=LfnLocation.Sector[L];
                            Location->Offset[Location->Count]=LfnLocation.Offset[L];
                            ++Location->Count;
                        }
                        Location->Sector[Location->Count]=FatClusterLba(F,Current)+S;
                        Location->Offset[Location->Count]=(uint16_t)Pos;
                        ++Location->Count;
                    }
                    return Rc95LdrSuccess;
                    }
                    LfnLocation.Count=0;
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
    if(Handle)*Handle=NULL;
    if(FileSize)*FileSize=0;
    if(!F||!F->Mounted||!Path||!Handle||!FileSize) return Rc95LdrInvalidParameter;
    while(*P=='/'||*P=='\\') P++;
    Directory=F->RootCluster;
    for(;;){
        i=0; while(P[i]&&P[i]!='/'&&P[i]!='\\'&&i<255){Component[i]=P[i];i++;}
        if(i==0||i>=255) return Rc95LdrUnsupported;
        Component[i]=0;
        while(P[i]=='/'||P[i]=='\\') i++;
        {
            RC95LDR_STATUS S=FatFindInDirectory(F,Directory,Component,&Cluster,&Size,&Attr,NULL);
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
static int
FatOwnsHandle(const RC95LDR_FAT32 *F,const void *Handle)
{
    uintptr_t Base,Address,End;
    if(!F||!Handle)return 0;
    Base=(uintptr_t)&F->Files[0];
    Address=(uintptr_t)Handle;
    End=Base+sizeof(F->Files);
    return End>=Base&&Address>=Base&&Address<End&&
           ((Address-Base)%sizeof(F->Files[0]))==0;
}

static RC95LDR_STATUS
FatReadAt(void *Context,void *Handle,uint64_t Offset,void *Buffer,size_t Length)
{
    RC95LDR_FAT32 *F=Context; RC95LDR_FAT32_FILE *File=Handle;
    uint8_t Sector[RC95LDR_FAT32_MAX_SECTOR_SIZE];
    uint8_t *Out=Buffer; uint64_t ClusterBytes; uint32_t Cluster,Skip,Guard=0;
    if(!F||!F->Mounted||!FatOwnsHandle(F,Handle)||!File->InUse||(!Buffer&&Length)) return Rc95LdrInvalidParameter;
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
{
    RC95LDR_FAT32 *Volume=(RC95LDR_FAT32*)Context;
    RC95LDR_FAT32_FILE *File=(RC95LDR_FAT32_FILE*)Handle;
    if(Volume&&Volume->Mounted&&FatOwnsHandle(Volume,Handle)&&File->InUse)
        memset(File,0,sizeof(*File));
}

static RC95LDR_STATUS
FatReadEntryValue(RC95LDR_FAT32 *F,uint32_t Cluster,uint32_t *Value)
{
    uint8_t Sector[RC95LDR_FAT32_MAX_SECTOR_SIZE];
    uint64_t Relative;
    uint32_t ByteOffset=Cluster*4u;
    RC95LDR_STATUS S;
    if(Cluster<2u||Cluster>=F->ClusterCount+2u)return Rc95LdrCorruptData;
    Relative=F->FatStartSector+ByteOffset/F->BytesPerSector;
    S=FatReadSector(F,Relative,Sector);
    if(S!=Rc95LdrSuccess)return S;
    *Value=Rc95LdrReadU32(Sector+(ByteOffset%F->BytesPerSector))&0x0fffffffu;
    return Rc95LdrSuccess;
}
static RC95LDR_STATUS
FatWriteEntryValue(RC95LDR_FAT32 *F,uint32_t Cluster,uint32_t Value)
{
    uint8_t Sector[RC95LDR_FAT32_MAX_SECTOR_SIZE];
    uint32_t First,Last,Copy,ByteOffset=Cluster*4u;
    RC95LDR_STATUS S;
    if(!F->Device->Write)return Rc95LdrUnsupported;
    if(Cluster<2u||Cluster>=F->ClusterCount+2u)return Rc95LdrCorruptData;
    First=F->FatMirroringEnabled?0u:F->ActiveFat;
    Last=F->FatMirroringEnabled?F->FatCount:First+1u;
    for(Copy=First;Copy<Last;Copy++){
        uint64_t Relative=(uint64_t)F->ReservedSectors+
            (uint64_t)Copy*F->FatSectors+ByteOffset/F->BytesPerSector;
        uint32_t Offset=ByteOffset%F->BytesPerSector;
        S=FatReadSector(F,Relative,Sector);
        if(S!=Rc95LdrSuccess)return S;
        {
            uint32_t Old=Rc95LdrReadU32(Sector+Offset);
            uint32_t New=(Old&0xf0000000u)|(Value&0x0fffffffu);
            Sector[Offset]=(uint8_t)New;Sector[Offset+1]=(uint8_t)(New>>8);
            Sector[Offset+2]=(uint8_t)(New>>16);Sector[Offset+3]=(uint8_t)(New>>24);
        }
        S=FatWriteSector(F,Relative,Sector);
        if(S!=Rc95LdrSuccess)return S;
    }
    return Rc95LdrSuccess;
}
static RC95LDR_STATUS
FatCheckCleanVolume(RC95LDR_FAT32 *F)
{
    uint8_t Sector[RC95LDR_FAT32_MAX_SECTOR_SIZE];
    uint32_t First=F->FatMirroringEnabled?0u:F->ActiveFat;
    uint32_t Last=F->FatMirroringEnabled?F->FatCount:First+1u,Copy;
    for(Copy=First;Copy<Last;Copy++){
        uint64_t Lba=(uint64_t)F->ReservedSectors+
            (uint64_t)Copy*F->FatSectors;
        uint32_t Entry;
        RC95LDR_STATUS S=FatReadSector(F,Lba,Sector);
        if(S!=Rc95LdrSuccess)return S;
        Entry=Rc95LdrReadU32(Sector+4u)&0x0fffffffu;
        if((Entry&0x0c000000u)!=0x0c000000u)
            return Rc95LdrUnsupported; /* dirty or previous hard I/O error */
    }
    return Rc95LdrSuccess;
}
static RC95LDR_STATUS
FatSetVolumeCleanState(RC95LDR_FAT32 *F,int Clean)
{
    uint8_t Sector[RC95LDR_FAT32_MAX_SECTOR_SIZE];
    uint32_t First=F->FatMirroringEnabled?0u:F->ActiveFat;
    uint32_t Last=F->FatMirroringEnabled?F->FatCount:First+1u;
    uint32_t Pass;
    /* When committing clean, publish the active FAT last. If any mirror
       write fails, the FAT used for reads remains marked dirty. When marking
       dirty, publish the active FAT first so an interrupted setup is safe. */
    for(Pass=0;Pass<Last-First;Pass++){
        uint32_t Copy;
        if(Clean&&F->FatMirroringEnabled)
            Copy=(Pass+1u)%F->FatCount;
        else Copy=First+Pass;
        {
            uint64_t Lba=(uint64_t)F->ReservedSectors+
                (uint64_t)Copy*F->FatSectors;
            uint32_t Raw,Entry;
            RC95LDR_STATUS S=FatReadSector(F,Lba,Sector);
            if(S!=Rc95LdrSuccess)return S;
            Raw=Rc95LdrReadU32(Sector+4u);
            Entry=Raw&0x0fffffffu;
            if(Clean)Entry|=0x0c000000u;
            else Entry&=~0x08000000u;
            Entry|=Raw&0xf0000000u;
            Sector[4]=(uint8_t)Entry;Sector[5]=(uint8_t)(Entry>>8);
            Sector[6]=(uint8_t)(Entry>>16);Sector[7]=(uint8_t)(Entry>>24);
            S=FatWriteSector(F,Lba,Sector);
            if(S!=Rc95LdrSuccess)return S;
        }
    }
    return Rc95LdrSuccess;
}
static RC95LDR_STATUS
FatInvalidateFsInfoSector(RC95LDR_FAT32 *F,uint32_t SectorNumber)
{
    uint8_t Sector[RC95LDR_FAT32_MAX_SECTOR_SIZE];
    RC95LDR_STATUS S;
    if(!SectorNumber||SectorNumber==0xffffu||
       SectorNumber>=F->ReservedSectors)return Rc95LdrSuccess;
    S=FatReadSector(F,SectorNumber,Sector);
    if(S!=Rc95LdrSuccess)return S;
    if(Rc95LdrReadU32(Sector)!=0x41615252u||
       Rc95LdrReadU32(Sector+484)!=0x61417272u||
       Rc95LdrReadU32(Sector+508)!=0xaa550000u)
        return Rc95LdrSuccess; /* invalid FSInfo is advisory; leave it alone */
    /* Free count and allocation hint become unknown. Windows will rescan;
       stale exact counts must not survive cluster reclamation. */
    Sector[488]=0xff;Sector[489]=0xff;Sector[490]=0xff;Sector[491]=0xff;
    Sector[492]=0xff;Sector[493]=0xff;Sector[494]=0xff;Sector[495]=0xff;
    return FatWriteSector(F,SectorNumber,Sector);
}
static RC95LDR_STATUS
FatInvalidateFsInfo(RC95LDR_FAT32 *F)
{
    RC95LDR_STATUS S;
    uint64_t Backup;
    S=FatInvalidateFsInfoSector(F,F->FsInfoSector);
    if(S!=Rc95LdrSuccess)return S;
    if(!F->BackupBootSector||F->BackupBootSector==0xffffu||
       !F->FsInfoSector||F->FsInfoSector==0xffffu)
        return Rc95LdrSuccess;
    Backup=(uint64_t)F->BackupBootSector+F->FsInfoSector;
    if(Backup>=F->ReservedSectors)return Rc95LdrSuccess;
    return FatInvalidateFsInfoSector(F,(uint32_t)Backup);
}
static RC95LDR_STATUS
FatResolveParent(RC95LDR_FAT32 *F,const char *Path,uint32_t *Parent,
                 char Leaf[256])
{
    char Component[256];const char *P=Path;uint32_t Directory=F->RootCluster;
    size_t PathLength;
    if(!Path||!Parent||!Leaf)return Rc95LdrInvalidParameter;
    PathLength=strlen(Path);
    if(!PathLength||Path[PathLength-1u]=='/'||Path[PathLength-1u]=='\\')
        return Rc95LdrInvalidParameter;
    while(*P=='/'||*P=='\\')P++;
    for(;;){
        size_t N=0;uint32_t Cluster,Size;uint8_t Attr;
        while(P[N]&&P[N]!='/'&&P[N]!='\\'&&N<255u){Component[N]=P[N];N++;}
        if(!N||N>=255u)return Rc95LdrInvalidParameter;
        Component[N]=0;
        if(strcmp(Component,".")==0||strcmp(Component,"..")==0||
           strchr(Component,':')||strchr(Component,'*')||strchr(Component,'?'))
            return Rc95LdrInvalidParameter;
        while(P[N]=='/'||P[N]=='\\')N++;
        P+=N;
        if(!*P){size_t K=0;while(Component[K])K++;memcpy(Leaf,Component,K+1u);*Parent=Directory;return Rc95LdrSuccess;}
        {
            RC95LDR_STATUS S=FatFindInDirectory(F,Directory,Component,
                &Cluster,&Size,&Attr,NULL);
            if(S!=Rc95LdrSuccess)return S;
        }
        if(!(Attr&FAT_ATTR_DIRECTORY)||Cluster<2u)return Rc95LdrNotFound;
        Directory=Cluster;
    }
}
RC95LDR_STATUS
Rc95LdrFat32DeleteFile(RC95LDR_FAT32 *F,const char *Path)
{
    uint8_t Sector[RC95LDR_FAT32_MAX_SECTOR_SIZE];
    FAT32_ENTRY_LOCATION Location;
    char Leaf[256];uint32_t Parent,FirstCluster,Size,Current,Guard=0;
    uint8_t Attributes;RC95LDR_STATUS S;unsigned I;
    if(!F||!F->Mounted||!Path||!F->Device||!F->Device->Write)
        return Rc95LdrUnsupported;
    S=FatResolveParent(F,Path,&Parent,Leaf);
    if(S!=Rc95LdrSuccess)return S;
    S=FatFindInDirectory(F,Parent,Leaf,&FirstCluster,&Size,&Attributes,&Location);
    if(S!=Rc95LdrSuccess)return S;
    if((Attributes&(FAT_ATTR_READ_ONLY|FAT_ATTR_DIRECTORY|FAT_ATTR_VOLUME))!=0)
        return Rc95LdrUnsupported;
    /* Refuse to invalidate an open handle owned by this volume. */
    for(I=0;I<RC95LDR_FAT32_MAX_OPEN_FILES;I++)
        if(F->Files[I].InUse&&F->Files[I].FirstCluster==FirstCluster&&
           F->Files[I].FileSize==Size)return Rc95LdrUnsupported;
    /* Validate the entire chain before the first on-disk mutation. */
    Current=FirstCluster;
    if(Current==0u){if(Size!=0u)return Rc95LdrCorruptData;}
    else {
        uint64_t ClusterBytes=(uint64_t)F->BytesPerSector*F->SectorsPerCluster;
        uint64_t Required=(Size+ClusterBytes-1u)/ClusterBytes;
        uint64_t Seen=0;int ReachedEoc=0;
        if(Required>F->ClusterCount)return Rc95LdrCorruptData;
        while(Current>=2u&&Current<F->ClusterCount+2u&&Guard++<F->ClusterCount){
            uint32_t Next;Seen++;
            S=FatNextCluster(F,Current,&Next);
            if(S!=Rc95LdrSuccess)return S;
            if(Next>=FAT32_EOC){ReachedEoc=1;break;}
            Current=Next;
        }
        if(!ReachedEoc||Seen<Required||Guard>F->ClusterCount)
            return Rc95LdrCorruptData;
    }
    /* Refuse volumes already marked dirty. Mark this transaction dirty before
       touching directory/FAT metadata; any later I/O failure leaves that flag
       set so the next OS boot knows it must check the filesystem. */
    S=FatCheckCleanVolume(F);
    if(S!=Rc95LdrSuccess)return S;
    S=FatSetVolumeCleanState(F,0);
    if(S!=Rc95LdrSuccess)return S;
    /* Remove the LFN slots and short entry first. A later FAT write failure
       can leak clusters, but cannot leave a visible hiberfile pointing at
       partially freed storage. */
    for(I=0;I<Location.Count;I++){
        S=FatReadSector(F,Location.Sector[I],Sector);
        if(S!=Rc95LdrSuccess)return S;
        if(Location.Offset[I]+32u>F->BytesPerSector)return Rc95LdrCorruptData;
        if(Sector[Location.Offset[I]]==0u||Sector[Location.Offset[I]]==0xe5u)
            return Rc95LdrCorruptData;
        Sector[Location.Offset[I]]=0xe5u;
        S=FatWriteSector(F,Location.Sector[I],Sector);
        if(S!=Rc95LdrSuccess)return S;
    }
    Current=FirstCluster;Guard=0;
    while(Current>=2u&&Current<F->ClusterCount+2u&&Guard++<F->ClusterCount){
        uint32_t Next;S=FatReadEntryValue(F,Current,&Next);
        if(S!=Rc95LdrSuccess)return S;
        S=FatWriteEntryValue(F,Current,0u);
        if(S!=Rc95LdrSuccess)return S;
        if(Next>=FAT32_EOC)break;
        if(Next<2u||Next>=F->ClusterCount+2u)return Rc95LdrCorruptData;
        Current=Next;
    }
    if(Guard>F->ClusterCount)return Rc95LdrCorruptData;
    S=FatInvalidateFsInfo(F);
    if(S!=Rc95LdrSuccess)return S;
    return FatSetVolumeCleanState(F,1);
}

static RC95LDR_STATUS
FatDeleteFile(void *Context,const char *Path)
{
    return Rc95LdrFat32DeleteFile((RC95LDR_FAT32*)Context,Path);
}

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
    memset(F,0,sizeof(*F));
    F->DeviceStorage=*Device; F->Device=&F->DeviceStorage;
    F->PartitionStartLba=StartLba;
    F->PartitionBlockCount=PartitionBlockCount;
    S=Rc95LdrReadBlocks(F->Device,StartLba,1,B,sizeof(B)); if(S!=Rc95LdrSuccess)return S;
    if(B[510]!=0x55||B[511]!=0xaa) return Rc95LdrInvalidImage;
    if(StartLba>=Device->BlockCount) return Rc95LdrOutOfBounds;
    F->BytesPerSector=Rc95LdrReadU16(B+11); F->SectorsPerCluster=B[13];
    F->ReservedSectors=Rc95LdrReadU16(B+14); F->FatCount=B[16];
    F->FsInfoSector=Rc95LdrReadU16(B+48);
    F->BackupBootSector=Rc95LdrReadU16(B+50);
    FatSize=Rc95LdrReadU32(B+36); Root=Rc95LdrReadU32(B+44);
    ExtendedFlags=Rc95LdrReadU16(B+40);
    Total=Rc95LdrReadU32(B+32);
    /* FAT32 uses the 32-bit total-sector and FAT-size fields. Reject
     * contradictory FAT12/16 geometry instead of guessing which wins. */
    if((F->BytesPerSector!=512 && F->BytesPerSector!=1024 &&
        F->BytesPerSector!=2048 && F->BytesPerSector!=4096) ||
       F->BytesPerSector!=Device->BlockSize||!F->SectorsPerCluster||
       F->SectorsPerCluster>128u||
       (F->SectorsPerCluster&(F->SectorsPerCluster-1))||
       !F->ReservedSectors||!F->FatCount||F->FatCount>2||!FatSize||Root<2||
       Rc95LdrReadU16(B+17)!=0||Rc95LdrReadU16(B+19)!=0||
       Rc95LdrReadU16(B+22)!=0||Rc95LdrReadU16(B+42)!=0||
       (ExtendedFlags&0xff70u)!=0||
       (!(ExtendedFlags&0x0080u)&&(ExtendedFlags&0x000fu)!=0)||
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
    F->ActiveFat=ActiveFat;
    F->FatMirroringEnabled=(uint8_t)((ExtendedFlags&0x0080u)==0);
    F->DataStartSector=DataStart;
    if(F->DataStartSector>=Total) return Rc95LdrCorruptData;
    F->ClusterCount=(Total-F->DataStartSector)/F->SectorsPerCluster;
    if(F->ClusterCount<65525u) return Rc95LdrUnsupported; /* FAT32 only */
    if(F->ClusterCount>FAT32_MAX_CLUSTER_COUNT ||
       Root>FAT32_MAX_DATA_CLUSTER || Root>=F->ClusterCount+2u)
        return Rc95LdrCorruptData;
    FatBytes=(uint64_t)FatSize*F->BytesPerSector;
    if(FatBytes<(uint64_t)(F->ClusterCount+2u)*4u)
        return Rc95LdrCorruptData;
    F->Mounted=1;
    F->PartitionDevice.DeviceContext=F;
    F->PartitionDevice.BlockSize=Device->BlockSize;
    F->PartitionDevice.BlockCount=PartitionBlockCount;
    F->PartitionDevice.Read=FatPartitionRead;
    F->PartitionDevice.Write=F->Device->Write?FatPartitionWrite:NULL;
    F->Interface.FileSystemContext=F; F->Interface.OpenRead=FatOpenRead;
    F->Interface.ReadAt=FatReadAt; F->Interface.Close=FatClose;
    F->Interface.DeleteFile=FatDeleteFile;
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
