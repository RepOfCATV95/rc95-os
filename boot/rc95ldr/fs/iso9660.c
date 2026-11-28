#include "rc95ldr_iso9660.h"
#include <string.h>

#define ISO_FLAG_HIDDEN 0x01u
#define ISO_FLAG_DIRECTORY 0x02u
#define ISO_FLAG_MULTI_EXTENT 0x80u
#define ISO_DESCRIPTOR_PVD 1u
#define ISO_DESCRIPTOR_SVD 2u
#define ISO_DESCRIPTOR_TERMINATOR 255u
#define ISO_DESCRIPTOR_ID "CD001"

typedef struct _ISO_ENTRY {
    uint64_t Extent;
    uint64_t Size;
    uint8_t Flags;
    uint8_t NameLength;
    uint8_t Name[255];
} ISO_ENTRY;

static int IsoDual16(const uint8_t *P, uint16_t *Value)
{
    uint16_t L=(uint16_t)(P[0]|((uint16_t)P[1]<<8));
    uint16_t B=(uint16_t)(((uint16_t)P[2]<<8)|P[3]);
    if (L != B)
        return 0;
    *Value = L;
    return 1;
}
static int IsoDual32(const uint8_t *P, uint32_t *Value)
{
    uint32_t L=(uint32_t)P[0]|((uint32_t)P[1]<<8)|
        ((uint32_t)P[2]<<16)|((uint32_t)P[3]<<24);
    uint32_t B=((uint32_t)P[4]<<24)|((uint32_t)P[5]<<16)|
        ((uint32_t)P[6]<<8)|(uint32_t)P[7];
    if (L != B)
        return 0;
    *Value = L;
    return 1;
}
static int IsoRange(uint64_t Start,uint64_t Count,uint64_t Limit)
{
    return Start<=Limit && Count<=Limit-Start;
}
static RC95LDR_STATUS IsoReadBlock(const RC95LDR_ISO9660 *V,
    uint64_t Lba,void *Buffer)
{
    if(!V||!V->Mounted||!Buffer)return Rc95LdrInvalidParameter;
    if(Lba>=V->VolumeBlockCount)return Rc95LdrOutOfBounds;
    return Rc95LdrReadBlocks(V->Device,Lba,1,Buffer,
                             RC95LDR_ISO9660_LOGICAL_BLOCK_SIZE);
}
static int IsoJolietEscape(const uint8_t *D)
{
    return D[88]=='%' && D[89]=='/' &&
        (D[90]=='@'||D[90]=='C'||D[90]=='E');
}
static RC95LDR_STATUS IsoReadDescriptor(const RC95LDR_BLOCK_DEVICE *Device,
    uint64_t Lba,uint8_t *Buffer)
{
    if(!Device||Device->BlockSize!=RC95LDR_ISO9660_LOGICAL_BLOCK_SIZE||
       !Device->Read||Lba>=Device->BlockCount)
        return Rc95LdrInvalidParameter;
    return Rc95LdrReadBlocks(Device,Lba,1,Buffer,
                             RC95LDR_ISO9660_LOGICAL_BLOCK_SIZE);
}
static RC95LDR_STATUS IsoParseRoot(const uint8_t *D,uint64_t VolumeBlocks,
    uint64_t *Extent,uint64_t *Length)
{
    const uint8_t *R=D+156;
    uint32_t Lba,Bytes;
    uint16_t Sequence;
    uint8_t N;
    uint64_t Start;
    if(R[0]<34)return Rc95LdrCorruptData;
    N=R[32];
    if(!(R[25]&ISO_FLAG_DIRECTORY)||N!=1||R[33]!=0||
       !IsoDual32(R+2,&Lba)||!IsoDual32(R+10,&Bytes)||
       !IsoDual16(R+28,&Sequence)||Sequence==0||Bytes==0)
        return Rc95LdrCorruptData;
    Start=(uint64_t)Lba+R[1];
    if(Bytes==0||!IsoRange(Start,((uint64_t)Bytes+2047u)/2048u,VolumeBlocks))
        return Rc95LdrOutOfBounds;
    *Extent=Start; *Length=Bytes;
    return Rc95LdrSuccess;
}
RC95LDR_STATUS Rc95LdrIso9660Mount(RC95LDR_ISO9660 *V,
    const RC95LDR_BLOCK_DEVICE *Device)
{
    uint8_t D[RC95LDR_ISO9660_LOGICAL_BLOCK_SIZE];
    uint8_t Pvd[RC95LDR_ISO9660_LOGICAL_BLOCK_SIZE];
    uint8_t Joliet[RC95LDR_ISO9660_LOGICAL_BLOCK_SIZE];
    uint32_t VolumeBlocks; uint16_t BlockSize;
    uint64_t RootLba=0,RootSize=0;
    uint64_t JolietRootLba=0,JolietRootSize=0;
    int HavePvd=0,HaveJoliet=0,Terminated=0;
    uint64_t Lba;
    RC95LDR_STATUS S;
    if(!V||!Device||!Device->Read||
       Device->BlockSize!=RC95LDR_ISO9660_LOGICAL_BLOCK_SIZE||
       Device->BlockCount<17)
        return Rc95LdrInvalidParameter;
    memset(V,0,sizeof(*V));
    for(Lba=16;Lba<80&&Lba<Device->BlockCount;Lba++){
        S=IsoReadDescriptor(Device,Lba,D);
        if(S!=Rc95LdrSuccess)return S;
        if(memcmp(D+1,ISO_DESCRIPTOR_ID,5)!=0||D[6]!=1)
            return Rc95LdrCorruptData;
        if(D[0]==ISO_DESCRIPTOR_TERMINATOR){Terminated=1;break;}
        if(D[0]==ISO_DESCRIPTOR_PVD&&!HavePvd){
            if(!IsoDual32(D+80,&VolumeBlocks)||
               !IsoDual16(D+128,&BlockSize)||
               BlockSize!=RC95LDR_ISO9660_LOGICAL_BLOCK_SIZE||
               VolumeBlocks==0||VolumeBlocks>Device->BlockCount)
                return Rc95LdrCorruptData;
            memcpy(Pvd,D,sizeof(Pvd)); HavePvd=1;
        }else if(D[0]==ISO_DESCRIPTOR_SVD&&IsoJolietEscape(D)&&!HaveJoliet){
            memcpy(Joliet,D,sizeof(Joliet)); HaveJoliet=1;
        }
    }
    if(!Terminated||!HavePvd)return Rc95LdrCorruptData;
    S=IsoParseRoot(Pvd,VolumeBlocks,&RootLba,&RootSize);
    if(S!=Rc95LdrSuccess)return S;
    if(HaveJoliet){
        uint32_t JolietBlocks; uint16_t JolietBlockSize;
        if(!IsoDual32(Joliet+80,&JolietBlocks)||
           !IsoDual16(Joliet+128,&JolietBlockSize)||
           JolietBlockSize!=RC95LDR_ISO9660_LOGICAL_BLOCK_SIZE||
           JolietBlocks!=VolumeBlocks)
            HaveJoliet=0;
        else if(IsoParseRoot(Joliet,VolumeBlocks,&JolietRootLba,&JolietRootSize)!=Rc95LdrSuccess)
            HaveJoliet=0;
    }
    V->Device=Device; V->VolumeBlockCount=VolumeBlocks;
    V->RootExtentLba=HaveJoliet?JolietRootLba:RootLba;
    V->RootSize=HaveJoliet?JolietRootSize:RootSize;
    V->Joliet=(uint8_t)HaveJoliet; V->Mounted=1;
    V->Interface.FileSystemContext=V;
    V->Interface.OpenRead=NULL;
    V->Interface.ReadAt=NULL;
    V->Interface.Close=NULL;
    V->Interface.DeleteFile=NULL;
    V->FileProvider.StructureSize=sizeof(V->FileProvider);
    V->FileProvider.VolumeContext=V;
    return Rc95LdrSuccess;
}
static int IsoNameEqual(const RC95LDR_ISO9660 *V,
    const uint8_t *Id,uint8_t IdLength,const char *Wanted)
{
    size_t I=0,J=0,End=IdLength;
    if(V->Joliet){
        if(IdLength&1u)return 0;
        End=IdLength/2u;
        while(I<End){
            uint16_t C=(uint16_t)(((uint16_t)Id[I*2]<<8)|Id[I*2+1]);
            if(C==';')break;
            if(C>0x7f)return 0;
            {
                unsigned char A=(unsigned char)C,B=(unsigned char)Wanted[J];
                if(A>='a'&&A<='z')A=(unsigned char)(A-32);
                if(B>='a'&&B<='z')B=(unsigned char)(B-32);
                if(!B||A!=B)return 0;
            }
            I++;J++;
        }
    }else{
        while(I<End&&Id[I]!=';'){
            unsigned char A=Id[I],B=(unsigned char)Wanted[J];
            if(A>='a'&&A<='z')A=(unsigned char)(A-32);
            if(B>='a'&&B<='z')B=(unsigned char)(B-32);
            if(!B||A!=B)return 0;
            I++;J++;
        }
    }
    if(J&&Wanted[J-1]=='.')J--;
    return Wanted[J]==0;
}
static RC95LDR_STATUS IsoDecodeEntry(const RC95LDR_ISO9660 *V,
    const uint8_t *R,size_t Available,ISO_ENTRY *E)
{
    uint32_t Extent,Size;
    uint16_t Sequence;
    uint8_t Len;
    if(Available<34||R[0]<34||R[0]>Available)return Rc95LdrCorruptData;
    Len=R[32];
    if(!Len||(size_t)33+Len+(Len%2u==0?1u:0u)>R[0]||
       !IsoDual32(R+2,&Extent)||!IsoDual32(R+10,&Size)||
       !IsoDual16(R+28,&Sequence)||Sequence==0)
        return Rc95LdrCorruptData;
    if(R[26]||R[27]||(R[25]&ISO_FLAG_MULTI_EXTENT))
        return Rc95LdrUnsupported;
    if(!IsoRange((uint64_t)Extent+R[1],
        ((uint64_t)Size+2047u)/2048u,V->VolumeBlockCount))
        return Rc95LdrOutOfBounds;
    E->Extent=(uint64_t)Extent+R[1]; E->Size=Size;
    E->Flags=R[25]; E->NameLength=Len;
    memcpy(E->Name,R+33,Len);
    return Rc95LdrSuccess;
}
static int IsoHandleValid(const RC95LDR_ISO9660 *V,const void *Handle)
{
    unsigned I;
    if(!V||!Handle)return 0;
    for(I=0;I<RC95LDR_ISO9660_MAX_OPEN_FILES;I++)
        if(Handle==&V->Files[I]&&V->Files[I].InUse)
            return 1;
    return 0;
}
static RC95LDR_STATUS IsoFindEntry(RC95LDR_ISO9660 *V,
    uint64_t DirExtent,uint64_t DirSize,const char *Name,ISO_ENTRY *Found)
{
    uint8_t B[RC95LDR_ISO9660_LOGICAL_BLOCK_SIZE];
    uint64_t Offset=0;
    while(Offset<DirSize){
        uint64_t Lba=DirExtent+Offset/2048u;
        size_t InBlock=(size_t)(Offset%2048u);
        size_t Limit=2048u-InBlock;
        const uint8_t *R;
        uint8_t RecordLength;
        ISO_ENTRY E;
        RC95LDR_STATUS S=IsoReadBlock(V,Lba,B);
        if(S!=Rc95LdrSuccess)return S;
        R=B+InBlock;
        if(R[0]==0){Offset+=Limit;continue;}
        RecordLength=R[0];
        if(RecordLength>Limit)return Rc95LdrCorruptData;
        S=IsoDecodeEntry(V,R,Limit,&E);
        if(S!=Rc95LdrSuccess)return S;
        if(!(E.NameLength==1&&(E.Name[0]==0||E.Name[0]==1))&&
           IsoNameEqual(V,E.Name,E.NameLength,Name)){
            *Found=E;return Rc95LdrSuccess;
        }
        Offset+=RecordLength;
    }
    return Rc95LdrNotFound;
}
static RC95LDR_STATUS IsoResolve(RC95LDR_ISO9660 *V,
    const char *Path,ISO_ENTRY *Found)
{
    char Part[RC95LDR_ISO9660_MAX_PATH+1];
    uint64_t DirExtent=V->RootExtentLba,DirSize=V->RootSize;
    size_t P=0;
    if(!Path||!*Path)return Rc95LdrInvalidParameter;
    while(Path[P]=='/'||Path[P]=='\\')P++;
    if(!Path[P])return Rc95LdrInvalidParameter;
    for(;;){
        size_t N=0;
        ISO_ENTRY E;
        while(Path[P]&&Path[P]!='/'&&Path[P]!='\\'){
            if(N>=RC95LDR_ISO9660_MAX_PATH)return Rc95LdrOutOfBounds;
            Part[N++]=Path[P++];
        }
        if(!N)return Rc95LdrInvalidParameter;
        Part[N]=0;
        while(Path[P]=='/'||Path[P]=='\\')P++;
        {
            RC95LDR_STATUS S=IsoFindEntry(V,DirExtent,DirSize,Part,&E);
            if(S!=Rc95LdrSuccess)return S;
        }
        if(Path[P]){
            if(!(E.Flags&ISO_FLAG_DIRECTORY))return Rc95LdrNotFound;
            DirExtent=E.Extent;DirSize=E.Size;
        }else{
            if(E.Flags&ISO_FLAG_DIRECTORY)return Rc95LdrInvalidParameter;
            *Found=E;return Rc95LdrSuccess;
        }
    }
}
static RC95LDR_STATUS IsoOpenRead(void *Context,const char *Path,
    void **Handle,uint64_t *Size)
{
    RC95LDR_ISO9660 *V=Context;
    ISO_ENTRY E; unsigned I; RC95LDR_STATUS S;
    if(!V||!V->Mounted||!Handle||!Size)return Rc95LdrInvalidParameter;
    *Handle=NULL;*Size=0;
    S=IsoResolve(V,Path,&E);if(S!=Rc95LdrSuccess)return S;
    for(I=0;I<RC95LDR_ISO9660_MAX_OPEN_FILES;I++)
        if(!V->Files[I].InUse){
            V->Files[I].ExtentLba=E.Extent;V->Files[I].Size=E.Size;
            V->Files[I].Position=0;V->Files[I].InUse=1;
            V->Files[I].IsDirectory=0;*Handle=&V->Files[I];*Size=E.Size;
            return Rc95LdrSuccess;
        }
    return Rc95LdrOutOfMemory;
}
static RC95LDR_STATUS IsoReadAt(void *Context,void *Handle,uint64_t Offset,
    void *Buffer,size_t Length)
{
    RC95LDR_ISO9660 *V=Context;
    RC95LDR_ISO9660_FILE *F=Handle;
    uint8_t Bounce[RC95LDR_ISO9660_LOGICAL_BLOCK_SIZE];
    size_t Done=0;
    if(!V||!V->Mounted||!IsoHandleValid(V,F)||(!Buffer&&Length))
        return Rc95LdrInvalidParameter;
    if(Offset>F->Size||Length>F->Size-Offset)return Rc95LdrOutOfBounds;
    while(Done<Length){
        uint64_t Pos=Offset+Done,Lba=F->ExtentLba+Pos/2048u;
        size_t InBlock=(size_t)(Pos%2048u);
        size_t Chunk=2048u-InBlock;
        RC95LDR_STATUS S;
        if(Chunk>Length-Done)Chunk=Length-Done;
        S=IsoReadBlock(V,Lba,Bounce);if(S!=Rc95LdrSuccess)return S;
        memcpy((uint8_t*)Buffer+Done,Bounce+InBlock,Chunk);Done+=Chunk;
    }
    return Rc95LdrSuccess;
}
static RC95LDR_STATUS IsoInterfaceOpen(void *Context,const char *Path,
    void **Handle,uint64_t *Size)
{return IsoOpenRead(Context,Path,Handle,Size);}
static RC95LDR_STATUS IsoInterfaceReadAt(void *Context,void *Handle,
    uint64_t Offset,void *Buffer,size_t Length)
{return IsoReadAt(Context,Handle,Offset,Buffer,Length);}
static void IsoClose(void *Context,void *Handle)
{
    RC95LDR_ISO9660 *V=Context;
    RC95LDR_ISO9660_FILE *F=Handle;
    if(IsoHandleValid(V,F))memset(F,0,sizeof(*F));
}
static RC95LDR_STATUS IsoProviderOpen(void *Context,const char *Path,void **Handle)
{
    uint64_t Size;return IsoOpenRead(Context,Path,Handle,&Size);
}
static RC95LDR_STATUS IsoProviderInfo(void *Context,void *Handle,
    RC95LDR_FILE_INFORMATION *Info)
{
    RC95LDR_ISO9660 *V=Context;
    RC95LDR_ISO9660_FILE *F=Handle;
    if(!IsoHandleValid(V,F)||!Info)return Rc95LdrInvalidParameter;
    Info->Size=F->Size;Info->Attributes=1u;return Rc95LdrSuccess;
}
static RC95LDR_STATUS IsoProviderRead(void *Context,void *Handle,
    void *Buffer,size_t Length,size_t *Read)
{
    RC95LDR_ISO9660 *V=Context;
    RC95LDR_ISO9660_FILE *F=Handle;size_t N;RC95LDR_STATUS S;
    if(!IsoHandleValid(V,F)||!Read)return Rc95LdrInvalidParameter;
    *Read=0;N=Length;
    if((uint64_t)N>F->Size-F->Position)N=(size_t)(F->Size-F->Position);
    S=IsoReadAt(Context,Handle,F->Position,Buffer,N);
    if(S==Rc95LdrSuccess){F->Position+=N;*Read=N;}return S;
}
static RC95LDR_STATUS IsoProviderSeek(void *Context,void *Handle,uint64_t Pos)
{
    RC95LDR_ISO9660 *V=Context;
    RC95LDR_ISO9660_FILE *F=Handle;
    if(!IsoHandleValid(V,F))return Rc95LdrInvalidParameter;
    if(Pos>F->Size)return Rc95LdrOutOfBounds;
    F->Position=Pos;return Rc95LdrSuccess;
}
static void IsoProviderClose(void *Context,void *Handle)
{IsoClose(Context,Handle);}
void Rc95LdrIso9660Unmount(RC95LDR_ISO9660 *V)
{if(V)memset(V,0,sizeof(*V));}
const RC95LDR_FILESYSTEM *Rc95LdrIso9660GetInterface(RC95LDR_ISO9660 *V)
{
    if(!V||!V->Mounted)return NULL;
    V->Interface.OpenRead=IsoInterfaceOpen;V->Interface.ReadAt=IsoInterfaceReadAt;
    V->Interface.Close=IsoClose;return &V->Interface;
}
const RC95LDR_FILE_PROVIDER *Rc95LdrIso9660GetFileProvider(RC95LDR_ISO9660 *V)
{
    if(!V||!V->Mounted)return NULL;
    V->FileProvider.Open=IsoProviderOpen;
    V->FileProvider.GetFileInformation=IsoProviderInfo;
    V->FileProvider.Read=IsoProviderRead;
    V->FileProvider.Seek=IsoProviderSeek;
    V->FileProvider.Close=IsoProviderClose;
    return &V->FileProvider;
}
RC95LDR_STATUS Rc95LdrIso9660BindVolume(RC95LDR_ISO9660 *Iso,RC95LDR_VOLUME *Volume)
{
    if(!Iso||!Iso->Mounted||!Volume)return Rc95LdrInvalidParameter;
    Volume->BlockDevice=Iso->Device;
    Volume->FileSystem=Rc95LdrIso9660GetInterface(Iso);
    return Volume->FileSystem?Rc95LdrSuccess:Rc95LdrInvalidParameter;
}
