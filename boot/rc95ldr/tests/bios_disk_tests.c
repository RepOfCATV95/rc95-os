#include "rc95ldrbiosdisk.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#pragma pack(push,1)
typedef struct _TEST_DAP {
    uint8_t Size,Reserved;
    uint16_t Count,Offset,Segment;
    uint64_t Lba;
} TEST_DAP;
#pragma pack(pop)

typedef struct _TEST_BIOS {
    uint8_t *LowMemory;
    uint8_t *Disk;
    size_t DiskBytes;
    uint32_t SectorSize;
} TEST_BIOS;

static RC95LDR_STATUS TestMap(void *Context,uint32_t Physical,size_t Length,void **Mapping)
{
    TEST_BIOS *B=Context;
    if(!B||!Mapping||Physical>0x100000u||Length>0x100000u-Physical)
        return Rc95LdrOutOfBounds;
    *Mapping=B->LowMemory+Physical;
    return Rc95LdrSuccess;
}
static void TestUnmap(void *Context,void *Mapping,size_t Length)
{(void)Context;(void)Mapping;(void)Length;}
static RC95LDR_STATUS TestInterrupt(void *Context,uint8_t Interrupt,RC95LDR_BIOS_REGISTERS *R)
{
    TEST_BIOS *B=Context;uint32_t DapPhysical,BufferPhysical;TEST_DAP *Dap;
    uint64_t ByteOffset;size_t Bytes;
    if(!B||!R||Interrupt!=0x13)return Rc95LdrUnsupported;
    if((R->Eax&0xffffu)!=0x4200u&&(R->Eax&0xffffu)!=0x4300u)
        return Rc95LdrUnsupported;
    DapPhysical=(uint32_t)R->Ds*16u+(uint16_t)R->Esi;
    if(DapPhysical>0x100000u-sizeof(TEST_DAP))return Rc95LdrOutOfBounds;
    Dap=(TEST_DAP*)(B->LowMemory+DapPhysical);
    if(Dap->Size!=sizeof(*Dap)||!Dap->Count)return Rc95LdrCorruptData;
    BufferPhysical=(uint32_t)Dap->Segment*16u+Dap->Offset;
    Bytes=(size_t)Dap->Count*B->SectorSize;
    if(BufferPhysical>0x100000u||Bytes>0x100000u-BufferPhysical||
       Dap->Lba>UINT64_MAX/B->SectorSize)return Rc95LdrOutOfBounds;
    ByteOffset=Dap->Lba*B->SectorSize;
    if(ByteOffset>B->DiskBytes||Bytes>B->DiskBytes-(size_t)ByteOffset)
        return Rc95LdrOutOfBounds;
    if((R->Eax&0xffffu)==0x4200u)
        memcpy(B->LowMemory+BufferPhysical,B->Disk+(size_t)ByteOffset,Bytes);
    else
        memcpy(B->Disk+(size_t)ByteOffset,B->LowMemory+BufferPhysical,Bytes);
    R->Eflags&=~1u;
    return Rc95LdrSuccess;
}


static const char BtrfsProviderTestData[] = "BIOS-BTRFS-WINLOAD";
static RC95LDR_STATUS TestBtrfsFsOpen(void *Context,const char *Path,
    void **Handle,uint64_t *FileSize)
{
    (void)Context;
    if(!Path||strcmp(Path,"/Windows/ntoskrnl.exe")||!Handle||!FileSize)
        return Rc95LdrNotFound;
    *Handle=(void*)BtrfsProviderTestData;
    *FileSize=sizeof(BtrfsProviderTestData)-1u;
    return Rc95LdrSuccess;
}
static RC95LDR_STATUS TestBtrfsFsReadAt(void *Context,void *Handle,
    uint64_t Offset,void *Buffer,size_t Length)
{
    size_t Total=sizeof(BtrfsProviderTestData)-1u;
    (void)Context;
    if(Handle!=(void*)BtrfsProviderTestData||(!Buffer&&Length))
        return Rc95LdrInvalidParameter;
    if(Offset>Total||(uint64_t)Length>Total-Offset)
        return Rc95LdrOutOfBounds;
    if(Length)memcpy(Buffer,BtrfsProviderTestData+(size_t)Offset,Length);
    return Rc95LdrSuccess;
}
static void TestBtrfsFsClose(void *Context,void *Handle)
{(void)Context;(void)Handle;}
static void TestBiosBtrfsProviderScope(void)
{
    RC95LDR_BIOS_BTRFS_VOLUME Mounted;
    const RC95LDR_FILE_PROVIDER *Provider;
    RC95LDR_FILE_INFORMATION Information;
    void *Handle=NULL;
    size_t BytesRead=0;
    char Buffer[8]={0};
    memset(&Mounted,0,sizeof(Mounted));
    Mounted.Btrfs.Mounted=1u;
    Mounted.FileSystem.OpenRead=TestBtrfsFsOpen;
    Mounted.FileSystem.ReadAt=TestBtrfsFsReadAt;
    Mounted.FileSystem.Close=TestBtrfsFsClose;
    Mounted.Volume.FileSystem=&Mounted.FileSystem;
    assert(Rc95LdrBiosBeginBtrfsFileProvider(&Mounted)==Rc95LdrSuccess);
    assert(Mounted.ProviderScopeActive==1u);
    assert(Rc95LdrBiosBeginBtrfsFileProvider(&Mounted)==Rc95LdrInvalidParameter);
    Provider=Rc95LdrGetFileProvider();
    assert(Provider==&Mounted.Provider);
    assert(Provider->Open(Provider->VolumeContext,"/Windows/ntoskrnl.exe",
        &Handle)==Rc95LdrSuccess);
    assert(Handle!=NULL);
    assert(Provider->GetFileInformation(Provider->VolumeContext,Handle,
        &Information)==Rc95LdrSuccess);
    assert(Information.Size==sizeof(BtrfsProviderTestData)-1u);
    assert(Provider->Read(Provider->VolumeContext,Handle,Buffer,4,&BytesRead)==
        Rc95LdrSuccess&&BytesRead==4u&&memcmp(Buffer,"BIOS",4)==0);
    assert(Provider->Seek(Provider->VolumeContext,Handle,5)==Rc95LdrSuccess);
    memset(Buffer,0,sizeof(Buffer));BytesRead=0;
    assert(Provider->Read(Provider->VolumeContext,Handle,Buffer,4,&BytesRead)==
        Rc95LdrSuccess&&BytesRead==4u&&memcmp(Buffer,"BTRF",4)==0);
    Provider->Close(Provider->VolumeContext,Handle);
    assert(Rc95LdrBiosEndBtrfsFileProvider(&Mounted)==Rc95LdrSuccess);
    assert(!Mounted.ProviderScopeActive&&Rc95LdrGetFileProvider()==NULL);
    assert(Rc95LdrBiosEndBtrfsFileProvider(&Mounted)==Rc95LdrInvalidParameter);
    Rc95LdrBiosUnmountBtrfs(&Mounted);
}

static void InitValidManifest(RC95LDR_BIOS_IMAGE_MANIFEST *M)
{
    memset(M, 0, sizeof(*M));
    M->Signature = RC95LDR_BIOS_IMAGE_SIGNATURE;
    M->Version = RC95LDR_BIOS_IMAGE_VERSION;
    M->HeaderSize = (uint16_t)sizeof(*M);
    M->SegmentCount = 1;
    M->ImageEntry = 0x00100000u;
    M->Segments[0].Lba = 3;
    M->Segments[0].ByteLength = 512;
    M->Segments[0].LoadAddress = 0x00100000u;
}

static void TestManifestValidation(void)
{
    RC95LDR_BIOS_DISK Disk;
    RC95LDR_BIOS_IMAGE_MANIFEST M;
    RC95LDR_MEMORY_RESERVATION_SET Reservations;

    memset(&Disk, 0, sizeof(Disk));
    memset(&Reservations, 0, sizeof(Reservations));
    Disk.EddSupported = 1;
    Disk.BytesPerSector = 512;
    Disk.BlockCount = 128;

    InitValidManifest(&M);
    assert(Rc95LdrBiosValidateManifest(&Disk, 1, &Reservations, &M) ==
        Rc95LdrSuccess);

    M.Segments[0].Lba = 2; /* overlaps the two-sector manifest */
    assert(Rc95LdrBiosValidateManifest(&Disk, 1, &Reservations, &M) ==
        Rc95LdrCorruptData);

    InitValidManifest(&M);
    M.Segments[0].ByteLength = 513;
    assert(Rc95LdrBiosValidateManifest(&Disk, 1, &Reservations, &M) ==
        Rc95LdrCorruptData);

    InitValidManifest(&M);
    M.Segments[0].Lba = Disk.BlockCount - 1;
    M.Segments[0].ByteLength = 1024;
    assert(Rc95LdrBiosValidateManifest(&Disk, 1, &Reservations, &M) ==
        Rc95LdrOutOfBounds);

    InitValidManifest(&M);
    M.Segments[0].LoadAddress = 0x00100000u;
    Reservations.Count = 1;
    Reservations.Entries[0].Base = 0x00100000u;
    Reservations.Entries[0].Length = 4096;
    assert(Rc95LdrBiosValidateManifest(&Disk, 1, &Reservations, &M) ==
        Rc95LdrOutOfBounds);
    memset(&Reservations, 0, sizeof(Reservations));

    InitValidManifest(&M);
    M.ImageEntry = 0x00200000u;
    assert(Rc95LdrBiosValidateManifest(&Disk, 1, &Reservations, &M) ==
        Rc95LdrCorruptData);

#if UINTPTR_MAX > 0xffffffffu
    InitValidManifest(&M);
    M.Segments[0].LoadAddress = 0x100000000ULL;
    M.ImageEntry = 0x100000000ULL;
    assert(Rc95LdrBiosValidateManifest(&Disk, 1, &Reservations, &M) ==
        Rc95LdrSuccess);
#endif

    InitValidManifest(&M);
    M.Segments[0].Flags = 1;
    assert(Rc95LdrBiosValidateManifest(&Disk, 1, &Reservations, &M) ==
        Rc95LdrCorruptData);

    InitValidManifest(&M);
    M.SegmentCount = 2;
    M.Segments[1] = M.Segments[0];
    assert(Rc95LdrBiosValidateManifest(&Disk, 1, &Reservations, &M) ==
        Rc95LdrCorruptData);

    InitValidManifest(&M);
    M.Segments[1].Lba = 9; /* nonzero unused descriptor is ambiguous */
    assert(Rc95LdrBiosValidateManifest(&Disk, 1, &Reservations, &M) ==
        Rc95LdrCorruptData);

    Disk.BytesPerSector = 768;
    assert(Rc95LdrBiosValidateManifest(&Disk, 1, &Reservations, &M) ==
        Rc95LdrInvalidParameter);
}

int main(void)
{
    TestManifestValidation();
    TestBiosBtrfsProviderScope();
    TEST_BIOS Fake;
    RC95LDR_BIOS_SERVICES Services;
    RC95LDR_BIOS_DISK Disk;
    RC95LDR_BIOS_BLOCK_CONTEXT Block;
    RC95LDR_BIOS_DISK_DISCOVERY Discovery;
    RC95LDR_BLOCK_DEVICE BlockDevice;
    uint8_t WriteBuffer[512];
    RC95LDR_PARTITION_INFO Parts[4];
    RC95LDR_BIOS_FAT32_VOLUME Mounted;
    RC95LDR_BIOS_BTRFS_VOLUME BtrfsMounted;
    uint8_t BtrfsItemBuffer[16384], BtrfsInlineBuffer[16384];
    uint8_t BtrfsStagingBuffer[4096];
    size_t Count=0;
    RC95LDR_BIOS_DISCOVERY_OPS DiscoveryOps;
    uint8_t *Mbr,*Boot,*Entry;
    const uint32_t Sector=512u;
    const uint64_t DiskSectors=120000u,PartitionStart=2048u,PartitionSectors=100000u;

    memset(&DiscoveryOps,0,sizeof(DiscoveryOps));
    assert(Rc95LdrDiscoverBiosWindows(NULL,&DiscoveryOps,NULL,0,NULL,0,
        NULL,NULL,0,NULL,NULL,NULL)==Rc95LdrInvalidParameter);
    assert(Rc95LdrBiosValidateWindowsVolume(NULL,NULL,NULL)==
        Rc95LdrInvalidParameter);

    memset(&Fake,0,sizeof(Fake));
    Fake.LowMemory=calloc(1,0x100000u);
    Fake.DiskBytes=(size_t)DiskSectors*Sector;
    Fake.Disk=calloc(1,Fake.DiskBytes);
    Fake.SectorSize=Sector;
    assert(Fake.LowMemory&&Fake.Disk);
    Mbr=Fake.Disk;
    Mbr[510]=0x55;Mbr[511]=0xaa;
    Mbr[440]=0x78;Mbr[441]=0x56;Mbr[442]=0x34;Mbr[443]=0x12;
    Entry=Mbr+446;Entry[4]=0x0c;
    Entry[8]=(uint8_t)PartitionStart;
    Entry[9]=(uint8_t)(PartitionStart>>8);
    Entry[10]=(uint8_t)(PartitionStart>>16);
    Entry[11]=(uint8_t)(PartitionStart>>24);
    Entry[12]=(uint8_t)PartitionSectors;
    Entry[13]=(uint8_t)(PartitionSectors>>8);
    Entry[14]=(uint8_t)(PartitionSectors>>16);
    Entry[15]=(uint8_t)(PartitionSectors>>24);

    Boot=Fake.Disk+(size_t)PartitionStart*Sector;
    Boot[3]='M';Boot[4]='S';Boot[5]='D';Boot[6]='O';Boot[7]='S';
    Boot[11]=0;Boot[12]=2;Boot[13]=1;Boot[14]=32;Boot[15]=0;Boot[16]=2;
    Boot[17]=0;Boot[18]=0;Boot[22]=0;Boot[23]=0;
    Boot[32]=(uint8_t)PartitionSectors;
    Boot[33]=(uint8_t)(PartitionSectors>>8);
    Boot[34]=(uint8_t)(PartitionSectors>>16);
    Boot[35]=(uint8_t)(PartitionSectors>>24);
    Boot[36]=0x20;Boot[37]=0x03; /* 800 sectors per FAT */
    Boot[44]=2;Boot[48]=1;Boot[50]=0;
    Boot[510]=0x55;Boot[511]=0xaa;

    memset(&Services,0,sizeof(Services));
    Services.StructureSize=sizeof(Services);Services.Version=RC95LDR_BIOS_ABI_VERSION;
    Services.Context=&Fake;Services.InvokeInterrupt=TestInterrupt;
    Services.MapLowMemory=TestMap;Services.UnmapLowMemory=TestUnmap;
    Services.ScratchPhysical=0x8000;Services.ScratchLength=0x10000;
    Services.BootDrive=0x80;

    memset(&Disk,0,sizeof(Disk));Disk.Drive=0x80;Disk.EddSupported=1;
    Disk.BytesPerSector=Sector;Disk.BlockCount=DiskSectors;
    Block.Services=&Services;Block.Disk=&Disk;
    memset(WriteBuffer,0xA7,sizeof(WriteBuffer));
    assert(Rc95LdrBiosInitializeBlockDevice(&Services,&Disk,&Block,
        &BlockDevice)==Rc95LdrSuccess);
    assert(BlockDevice.Write!=NULL);
    assert(BlockDevice.Write(BlockDevice.DeviceContext,100,1,WriteBuffer,
        sizeof(WriteBuffer))==Rc95LdrSuccess);
    assert(memcmp(Fake.Disk+100u*Sector,WriteBuffer,sizeof(WriteBuffer))==0);

    memset(&Discovery,0,sizeof(Discovery));
    Discovery.BlockContext=&Block;Discovery.Disk=&Disk;
    Discovery.Scratch=calloc(1,0x10000);Discovery.ScratchLength=0x10000;
    assert(Discovery.Scratch);
    {
        RC95LDR_BIOS_DISK InvalidDisk = Disk;
        RC95LDR_BIOS_DISK_DISCOVERY InvalidDiscovery = Discovery;

        InvalidDiscovery.Disk = &InvalidDisk;
        InvalidDisk.BytesPerSector = 256u;
        Count = 0;
        assert(Rc95LdrBiosEnumeratePartitions(
            &InvalidDiscovery, Parts, 4, &Count) == Rc95LdrInvalidParameter);
        assert(Count == 0);

        InvalidDisk.BytesPerSector = 768u;
        assert(Rc95LdrBiosEnumeratePartitions(
            &InvalidDiscovery, Parts, 4, &Count) == Rc95LdrInvalidParameter);
    }

    assert(Rc95LdrBiosEnumeratePartitions(&Discovery,Parts,4,&Count)==Rc95LdrSuccess);
    assert(Count==1&&Parts[0].Style==Rc95LdrPartitionMbr);
    assert(Parts[0].StartLba==PartitionStart&&Parts[0].BlockCount==PartitionSectors);
    assert(Parts[0].MbrDiskSignature==0x12345678u);
    assert(Parts[0].FileSystem==Rc95LdrFsFat32);
    assert(Rc95LdrBiosMountFat32(&Discovery,&Parts[0],&Mounted)==Rc95LdrSuccess);
    assert(Mounted.Volume.FileSystem!=NULL&&Mounted.Volume.BlockDevice!=NULL);
    Rc95LdrBiosUnmountFat32(&Mounted);

    /* The BIOS Btrfs mount helper must reject non-Btrfs partitions without
       publishing a partially initialized volume/provider. */
    memset(&BtrfsMounted, 0xa5, sizeof(BtrfsMounted));
    assert(Rc95LdrBiosMountBtrfs(&Discovery, &Parts[0],
        BtrfsItemBuffer, sizeof(BtrfsItemBuffer),
        BtrfsInlineBuffer, sizeof(BtrfsInlineBuffer),
        BtrfsStagingBuffer, sizeof(BtrfsStagingBuffer),
        &BtrfsMounted) == Rc95LdrInvalidParameter);
    assert(!BtrfsMounted.Btrfs.Mounted &&
        BtrfsMounted.Volume.FileSystem == NULL &&
        BtrfsMounted.Volume.BlockDevice == NULL);

    /* A partition classified as Btrfs but lacking a valid superblock must
       fail closed and leave no live BIOS mount object. */
    Parts[0].FileSystem = Rc95LdrFsBtrfs;
    memset(&BtrfsMounted, 0xa5, sizeof(BtrfsMounted));
    assert(Rc95LdrBiosMountBtrfs(&Discovery, &Parts[0],
        BtrfsItemBuffer, sizeof(BtrfsItemBuffer),
        BtrfsInlineBuffer, sizeof(BtrfsInlineBuffer),
        BtrfsStagingBuffer, sizeof(BtrfsStagingBuffer),
        &BtrfsMounted) == Rc95LdrInvalidImage);
    assert(!BtrfsMounted.Btrfs.Mounted &&
        BtrfsMounted.Volume.FileSystem == NULL &&
        BtrfsMounted.Volume.BlockDevice == NULL);

    free(Discovery.Scratch);free(Fake.Disk);free(Fake.LowMemory);
    return 0;
}
