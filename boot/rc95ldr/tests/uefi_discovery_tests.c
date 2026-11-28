#include "rc95ldrdiscoveryuefi.h"
#include "rc95bcd.h"
#include "rc95bcdwalk.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define HIVE_SIZE 8192u
#define HIVE_BIN 4096u
#define HIVE_ROOT 32u
#define HIVE_INDEX 120u
#define HIVE_CHILD 136u

typedef struct _TEST_CONTEXT {
    uint8_t Hive[HIVE_SIZE];
    unsigned EspMounts, EspUnmounts, OsMounts, OsUnmounts;
    unsigned Validations;
    int FailValidation;
    int FailOsMount;
    int FailCollect;
    int MissingBcd;
    unsigned Selections;
} TEST_CONTEXT;

static void Put16(uint8_t *P, uint16_t V)
{ P[0]=(uint8_t)V; P[1]=(uint8_t)(V>>8); }
static void Put32(uint8_t *P, uint32_t V)
{ P[0]=(uint8_t)V; P[1]=(uint8_t)(V>>8); P[2]=(uint8_t)(V>>16); P[3]=(uint8_t)(V>>24); }
static uint32_t HiveChecksum(const uint8_t *P)
{
    uint32_t Sum=0; unsigned I;
    for(I=0;I<0x1fcu;I+=4) Sum^=Rc95LdrReadU32(P+I);
    if(Sum==0xffffffffu) Sum=0xfffffffeu;
    else if(Sum==0) Sum=1;
    return Sum;
}
static void Cell(uint8_t *Bin, uint32_t Offset, uint32_t Size, int Used)
{ Put32(Bin+Offset, Used ? (0u-Size) : Size); }
static void Key(uint8_t *Bin, uint32_t Offset, const char *Name,
                uint32_t SubCount, uint32_t SubList)
{
    uint8_t *P=Bin+Offset+4; size_t N=strlen(Name);
    Put16(P,0x6b6e); Put16(P+2,0x20);
    Put32(P+20,SubCount); Put32(P+28,SubList);
    Put32(P+36,0); Put32(P+40,0xffffffffu);
    Put16(P+72,(uint16_t)N); memcpy(P+76,Name,N);
}
static void MakeHive(uint8_t *Hive)
{
    uint8_t *Bin=Hive+HIVE_BIN;
    memset(Hive,0,HIVE_SIZE);
    Put32(Hive,0x66676572); Put32(Hive+4,1); Put32(Hive+8,1);
    Put32(Hive+0x14,1); Put32(Hive+0x18,5); Put32(Hive+0x1c,0);
    Put32(Hive+0x20,1); Put32(Hive+0x24,HIVE_ROOT); Put32(Hive+0x28,4096);
    Put32(Bin,0x6e696268); Put32(Bin+4,0); Put32(Bin+8,4096);
    Cell(Bin,HIVE_ROOT,88,1); Key(Bin,HIVE_ROOT,"ROOT",1,HIVE_INDEX);
    Cell(Bin,HIVE_INDEX,16,1); Put16(Bin+HIVE_INDEX+4,0x696c);
    Put16(Bin+HIVE_INDEX+6,1); Put32(Bin+HIVE_INDEX+8,HIVE_CHILD);
    Cell(Bin,HIVE_CHILD,88,1); Key(Bin,HIVE_CHILD,"Objects",0,0xffffffffu);
    Cell(Bin,224,4096-224,0);
    Put32(Hive+0x1fc,HiveChecksum(Hive));
}

static RC95LDR_STATUS FileOpen(void *Context,const char *Path,
                               void **Handle,uint64_t *Size)
{
    TEST_CONTEXT *T=(TEST_CONTEXT*)Context;
    if(strcmp(Path,"\\EFI\\Microsoft\\Boot\\BCD")!=0)
        return Rc95LdrNotFound;
    if(T->MissingBcd) return Rc95LdrNotFound;
    *Handle=T->Hive; *Size=sizeof(T->Hive); return Rc95LdrSuccess;
}
static RC95LDR_STATUS OsFileOpen(void *Context,const char *Path,void **Handle,uint64_t *Size)
{ return FileOpen(Context,Path,Handle,Size); }
static RC95LDR_STATUS FileRead(void *Context,void *Handle,uint64_t Offset,
                               void *Buffer,size_t Length)
{
    TEST_CONTEXT *T=(TEST_CONTEXT*)Context;
    if(Handle!=T->Hive || Offset>sizeof(T->Hive) ||
       Length>sizeof(T->Hive)-(size_t)Offset) return Rc95LdrOutOfBounds;
    memcpy(Buffer,T->Hive+(size_t)Offset,Length); return Rc95LdrSuccess;
}
static void FileClose(void *Context,void *Handle)
{ (void)Context; (void)Handle; }

static RC95LDR_STATUS Enumerate(void *Context,RC95LDR_PARTITION_INFO *Parts,
                                size_t Capacity,size_t *Count)
{
    static const uint8_t EspType[16]={
        0x28,0x73,0x2a,0xc1,0xf8,0xf1,0xd2,0x11,
        0xba,0x4b,0,0xa0,0xc9,0x3e,0xc9,0x3b
    };
    (void)Context;
    if(Capacity<2) return Rc95LdrBufferTooSmall;
    memset(Parts,0,2*sizeof(*Parts));
    Parts[0].Style=Rc95LdrPartitionGpt;
    Parts[0].FileSystem=Rc95LdrFsFat32;
    Parts[0].StartLba=2048; Parts[0].BlockSize=512; Parts[0].BlockCount=4096;
    Parts[0].DiskGuid[0]=0x31; Parts[0].UniqueGuid[0]=0x32;
    memcpy(Parts[0].TypeGuid,EspType,16);
    Parts[1].Style=Rc95LdrPartitionMbr;
    Parts[1].FileSystem=Rc95LdrFsBtrfs;
    Parts[1].StartLba=8192; Parts[1].BlockSize=512; Parts[1].BlockCount=65536;
    Parts[1].MbrDiskSignature=0x78563412u;
    *Count=2; return Rc95LdrSuccess;
}
static RC95LDR_STATUS BlockRead(void *Context,uint64_t Lba,uint32_t Blocks,
                               void *Buffer,size_t Capacity)
{ (void)Context; (void)Lba; (void)Blocks; (void)Buffer; (void)Capacity; return Rc95LdrSuccess; }
static RC95LDR_STATUS Mount(void *Context,const RC95LDR_PARTITION_INFO *Part,
                            RC95LDR_FILESYSTEM_KIND Kind,RC95LDR_VOLUME *Volume)
{
    TEST_CONTEXT *T=(TEST_CONTEXT*)Context;
    static RC95LDR_BLOCK_DEVICE Device={NULL,1,512,BlockRead,NULL};
    static RC95LDR_FILESYSTEM EspFs,OsFs;
    if(Kind==Rc95LdrFsFat32 && Part->StartLba==2048)
    {
        memset(&EspFs,0,sizeof(EspFs));
        ++T->EspMounts;
        EspFs.FileSystemContext=T; EspFs.OpenRead=FileOpen;
        EspFs.ReadAt=FileRead; EspFs.Close=FileClose;
        Volume->BlockDevice=&Device; Volume->FileSystem=&EspFs;
        return Rc95LdrSuccess;
    }
    if(Kind==Rc95LdrFsBtrfs && Part->StartLba==8192)
    {
        if(T->FailOsMount) return Rc95LdrIoError;
        memset(&OsFs,0,sizeof(OsFs));
        ++T->OsMounts;
        OsFs.FileSystemContext=T; OsFs.OpenRead=OsFileOpen;
        OsFs.ReadAt=FileRead; OsFs.Close=FileClose;
        Volume->BlockDevice=&Device; Volume->FileSystem=&OsFs;
        return Rc95LdrSuccess;
    }
    return Rc95LdrNotFound;
}
static void Unmount(void *Context,RC95LDR_VOLUME *Volume)
{
    TEST_CONTEXT *T=(TEST_CONTEXT*)Context;
    if(Volume->FileSystem && Volume->FileSystem->FileSystemContext==T)
    {
        if(Volume->FileSystem->OpenRead==OsFileOpen) ++T->OsUnmounts;
        else if(Volume->FileSystem->OpenRead==FileOpen) ++T->EspUnmounts;
    }
    memset(Volume,0,sizeof(*Volume));
}
static RC95LDR_STATUS Collect(void *Context,const RC95BCD_HIVE *Hive,
                              RC95LDR_BCD_BOOT_CANDIDATE *Candidates,
                              size_t Capacity,size_t *Count)
{
    static uint8_t BootDevice[32]={0};
    static uint8_t OsDevice[12]={0};
    static const uint16_t Loader[]={
        '\\','W','i','n','d','o','w','s','\\','S','y','s','t','e','m','3','2',
        '\\','w','i','n','l','o','a','d','.','e','f','i',0
    };
    static const uint16_t Root[]={'\\','W','i','n','d','o','w','s',0};
    TEST_CONTEXT *T=(TEST_CONTEXT*)Context;
    (void)Hive;
    if(T->FailCollect) return Rc95LdrIoError;
    if(Capacity<2) return Rc95LdrBufferTooSmall;
    memset(Candidates,0,2u*sizeof(*Candidates));
    BootDevice[0]=0x31; /* GPT disk GUID. */
    BootDevice[16]=0x32; /* GPT ESP unique partition GUID. */
    Put32(OsDevice,0x78563412u); 
    OsDevice[4]=0x00; OsDevice[5]=0x00; OsDevice[6]=0x40; OsDevice[7]=0x00;
    Candidates[0].DeviceElementStorage.Kind=Rc95BcdElementDevice;
    Candidates[0].DeviceElementStorage.DeviceType=RC95BCD_DEVICE_TYPE_PARTITION;
    Candidates[0].DeviceElementStorage.DeviceData=BootDevice;
    Candidates[0].DeviceElementStorage.DeviceDataLength=sizeof(BootDevice);
    Candidates[0].OsDeviceElementStorage.Kind=Rc95BcdElementDevice;
    Candidates[0].OsDeviceElementStorage.DeviceType=RC95BCD_DEVICE_TYPE_PARTITION;
    Candidates[0].OsDeviceElementStorage.DeviceData=OsDevice;
    Candidates[0].OsDeviceElementStorage.DeviceDataLength=sizeof(OsDevice);
    Candidates[0].DeviceElement=&Candidates[0].DeviceElementStorage;
    Candidates[0].OsDeviceElement=&Candidates[0].OsDeviceElementStorage;
    Candidates[0].IsWindowsLoader=1; Candidates[0].IsEnabled=1;
    Candidates[0].IsDefault=1;
    Candidates[0].ObjectCell=0x111u;
    Candidates[0].GraphicsModeDisabled=0;
    Candidates[1]=Candidates[0];
    Candidates[1].DeviceElement=&Candidates[1].DeviceElementStorage;
    Candidates[1].OsDeviceElement=&Candidates[1].OsDeviceElementStorage;
    Candidates[1].IsDefault=0;
    Candidates[1].ObjectCell=0x222u;
    Candidates[1].GraphicsModeDisabled=1;
    memcpy(Candidates[0].LoaderPathStorage,Loader,sizeof(Loader));
    Candidates[0].LoaderPathUtf16=Candidates[0].LoaderPathStorage;
    Candidates[0].LoaderPathCharacters=(uint32_t)(sizeof(Loader)/sizeof(Loader[0])-1);
    memcpy(Candidates[0].SystemRootStorage,Root,sizeof(Root));
    Candidates[0].SystemRootUtf16=Candidates[0].SystemRootStorage;
    Candidates[0].SystemRootCharacters=(uint32_t)(sizeof(Root)/sizeof(Root[0])-1);
    memcpy(Candidates[1].LoaderPathStorage,Loader,sizeof(Loader));
    Candidates[1].LoaderPathUtf16=Candidates[1].LoaderPathStorage;
    Candidates[1].LoaderPathCharacters=Candidates[0].LoaderPathCharacters;
    memcpy(Candidates[1].SystemRootStorage,Root,sizeof(Root));
    Candidates[1].SystemRootUtf16=Candidates[1].SystemRootStorage;
    Candidates[1].SystemRootCharacters=Candidates[0].SystemRootCharacters;
    *Count=2; return Rc95LdrSuccess;
}
static RC95LDR_STATUS SelectSecond(
    void *Context, const RC95BCD_HIVE *Hive,
    const RC95LDR_BCD_BOOT_CANDIDATE *Candidates, size_t Count,
    const RC95LDR_BCD_BOOT_CANDIDATE **Selected)
{
    TEST_CONTEXT *T=(TEST_CONTEXT*)Context;
    (void)Hive;
    if(!T||!Candidates||Count!=2||!Selected) return Rc95LdrInvalidParameter;
    ++T->Selections;
    *Selected=&Candidates[1];
    return Rc95LdrSuccess;
}
static RC95LDR_STATUS Validate(void *Context,const RC95LDR_VOLUME *Volume,
                               const RC95LDR_WINDOWS_INSTALLATION *Install)
{
    TEST_CONTEXT *T=(TEST_CONTEXT*)Context;
    ++T->Validations;
    assert(Install->OsPartition.FileSystem==Rc95LdrFsBtrfs);
    assert(Install->OsPartition.StartLba==8192);
    assert(Install->OsPartitionIndex==1);
    assert(Volume->FileSystem && Volume->FileSystem->FileSystemContext==T);
    return T->FailValidation ? Rc95LdrIoError : Rc95LdrSuccess;
}

static int RunDiscoveryEarlyFailure(int FailOsMount, int FailCollect, int MissingBcd)
{
    TEST_CONTEXT T;
    RC95LDR_UEFI_DISCOVERY_OPS Ops;
    RC95LDR_PARTITION_INFO Parts[2];
    RC95LDR_BCD_BOOT_CANDIDATE Candidates[2];
    RC95LDR_WINDOWS_INSTALLATION Install;
    RC95LDR_VOLUME OsVolume;
    uint8_t HiveBuffer[HIVE_SIZE];
    size_t CandidateCount=0,EspIndex=(size_t)-1;
    RC95LDR_STATUS Status;
    memset(&T,0,sizeof(T)); MakeHive(T.Hive);
    T.FailOsMount=FailOsMount; T.FailCollect=FailCollect;
    T.MissingBcd=MissingBcd;
    memset(&Ops,0,sizeof(Ops)); Ops.Context=&T; Ops.EnumeratePartitions=Enumerate;
    Ops.MountVolume=Mount; Ops.UnmountVolume=Unmount;
    Ops.CollectBootCandidates=Collect; Ops.ValidateWindowsVolume=Validate;
    memset(&OsVolume,0,sizeof(OsVolume));
    Status=Rc95LdrDiscoverUefiWindows(&Ops,Parts,2,Candidates,2,
        &CandidateCount,HiveBuffer,sizeof(HiveBuffer),&Install,&OsVolume,&EspIndex);
    assert(Status==(MissingBcd ? Rc95LdrNotFound : Rc95LdrIoError));
    assert(T.EspMounts==1 && T.EspUnmounts==1);
    assert(T.OsMounts==T.OsUnmounts);
    assert(OsVolume.FileSystem==NULL);
    assert(CandidateCount==0);
    assert(EspIndex==(size_t)-1);
    return 1;
}

static int RunDiscovery(int FailValidation)
{
    TEST_CONTEXT T;
    RC95LDR_UEFI_DISCOVERY_OPS Ops;
    RC95LDR_PARTITION_INFO Parts[2];
    RC95LDR_BCD_BOOT_CANDIDATE Candidates[2];
    RC95LDR_WINDOWS_INSTALLATION Install;
    RC95LDR_VOLUME OsVolume;
    uint8_t HiveBuffer[HIVE_SIZE];
    size_t CandidateCount=0,EspIndex=(size_t)-1;
    RC95LDR_STATUS Status;
    memset(&T,0,sizeof(T)); MakeHive(T.Hive); T.FailValidation=FailValidation;
    memset(&Ops,0,sizeof(Ops)); Ops.Context=&T; Ops.EnumeratePartitions=Enumerate;
    Ops.MountVolume=Mount; Ops.UnmountVolume=Unmount;
    Ops.CollectBootCandidates=Collect; Ops.ValidateWindowsVolume=Validate;
    memset(&OsVolume,0,sizeof(OsVolume));
    Status=Rc95LdrDiscoverUefiWindows(&Ops,Parts,2,Candidates,2,
        &CandidateCount,HiveBuffer,sizeof(HiveBuffer),&Install,&OsVolume,&EspIndex);
    if(FailValidation)
    {
        assert(Status==Rc95LdrIoError);
        assert(T.EspMounts==T.EspUnmounts);
        assert(T.OsMounts==T.OsUnmounts);
        assert(OsVolume.FileSystem==NULL);
    }
    else
    {
        assert(Status==Rc95LdrSuccess);
        assert(CandidateCount==2 && EspIndex==0);
        assert(Install.OsPartition.FileSystem==Rc95LdrFsBtrfs);
        assert(T.EspMounts==1 && T.EspUnmounts==1);
        assert(T.OsMounts==1 && T.OsUnmounts==0);
        assert(OsVolume.FileSystem!=NULL);
        Unmount(&T,&OsVolume);
        assert(T.OsMounts==T.OsUnmounts);
    }
    return 1;
}
static int RunDiscoverySelectedCandidate(void)
{
    TEST_CONTEXT T;
    RC95LDR_UEFI_DISCOVERY_OPS Ops;
    RC95LDR_PARTITION_INFO Parts[2];
    RC95LDR_BCD_BOOT_CANDIDATE Candidates[2];
    RC95LDR_WINDOWS_INSTALLATION Install;
    RC95LDR_VOLUME OsVolume;
    uint8_t HiveBuffer[HIVE_SIZE];
    size_t CandidateCount=0,EspIndex=(size_t)-1;
    RC95LDR_STATUS Status;
    memset(&T,0,sizeof(T)); MakeHive(T.Hive);
    memset(&Ops,0,sizeof(Ops)); Ops.Context=&T; Ops.SelectionContext=&T;
    Ops.EnumeratePartitions=Enumerate; Ops.MountVolume=Mount;
    Ops.UnmountVolume=Unmount; Ops.CollectBootCandidates=Collect;
    Ops.ValidateWindowsVolume=Validate; Ops.SelectBootCandidate=SelectSecond;
    memset(&OsVolume,0,sizeof(OsVolume));
    Status=Rc95LdrDiscoverUefiWindows(&Ops,Parts,2,Candidates,2,
        &CandidateCount,HiveBuffer,sizeof(HiveBuffer),&Install,&OsVolume,&EspIndex);
    assert(Status==Rc95LdrSuccess);
    assert(T.Selections==1);
    assert(CandidateCount==2 && EspIndex==0);
    assert(Install.GraphicsModeDisabled==1);
    assert(T.EspMounts==1 && T.EspUnmounts==1);
    assert(T.OsMounts==1 && T.OsUnmounts==0);
    Unmount(&T,&OsVolume);
    assert(T.OsMounts==T.OsUnmounts);
    return 1;
}
int main(void)
{
    assert(RunDiscoverySelectedCandidate());
    assert(RunDiscovery(0));
    assert(RunDiscovery(1));
    assert(RunDiscoveryEarlyFailure(1,0,0));
    assert(RunDiscoveryEarlyFailure(0,1,0));
    assert(RunDiscoveryEarlyFailure(0,0,1));
    puts("RC95LDR UEFI Btrfs discovery lifecycle tests passed");
    return 0;
}
