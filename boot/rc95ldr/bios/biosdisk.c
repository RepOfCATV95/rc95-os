#include "include/rc95ldrbiosdisk.h"
#include "rc95ldr_ntfs_geometry.h"
#include "../veneer/include/veneer.h"
#include <string.h>

#define GPT_SIGNATURE 0x5452415020494645ULL
#define GPT_MAX_ENTRIES 128u
#define GPT_ENTRY_SIZE 128u
#define GPT_HEADER_MIN 92u
#define MBR_SIGNATURE 0xaa55u
#define BIOS_EXTENDED 0x05u
#define BIOS_EXTENDED_LBA 0x0fu
#define BIOS_PROTECTIVE 0xeeu

static uint32_t BiosCrc32(const uint8_t *Data,size_t Length)
{
    uint32_t C=0xffffffffu; size_t I; unsigned B;
    for(I=0;I<Length;I++){C^=Data[I];for(B=0;B<8;B++)C=(C>>1)^((C&1)?0xedb88320u:0);}
    return ~C;
}
static int Add64(uint64_t A,uint64_t B,uint64_t *O)
{if(B>UINT64_MAX-A)return 0;*O=A+B;return 1;}
static int Overlap(uint64_t A,uint64_t AL,uint64_t B,uint64_t BL)
{uint64_t AE,BE;if(!Add64(A,AL,&AE)||!Add64(B,BL,&BE))return 1;return A<BE&&B<AE;}
static RC95LDR_FILESYSTEM_KIND DetectFs(const uint8_t *B,uint32_t Sector)
{
    if(Sector>=512&&B[510]==0x55&&B[511]==0xaa&&B[3]=='N'&&B[4]=='T'&&B[5]=='F'&&B[6]=='S'&&B[7]==' ')
        return Rc95LdrFsNtfs;
    if(Sector>=512&&B[510]==0x55&&B[511]==0xaa&&
       Rc95LdrReadU16(B+11)==Sector&&B[13]&&B[16]&&
       Rc95LdrReadU16(B+17)==0&&Rc95LdrReadU16(B+22)==0&&
       Rc95LdrReadU32(B+36)!=0&&Rc95LdrReadU32(B+44)>=2)
        return Rc95LdrFsFat32;
    return Rc95LdrFsUnknown;
}
static int BiosDiskGeometryValid(const RC95LDR_BIOS_DISK *Disk)
{
    return Disk && Disk->EddSupported && Disk->BlockCount &&
           Disk->BytesPerSector >= 512u &&
           Disk->BytesPerSector <= 4096u &&
           !(Disk->BytesPerSector & (Disk->BytesPerSector - 1u));
}

static RC95LDR_STATUS ReadDisk(const RC95LDR_BIOS_DISK_DISCOVERY *D,
    uint64_t Lba,uint32_t Blocks,void *Buffer,size_t Length)
{
    if(!D||!D->BlockContext||!D->Disk)return Rc95LdrInvalidParameter;
    return Rc95LdrBiosReadDisk(D->BlockContext->Services,D->Disk,Lba,Blocks,Buffer,Length);
}
static RC95LDR_STATUS AddPart(RC95LDR_PARTITION_INFO *P,size_t Cap,size_t *N,
    uint32_t Style,uint64_t Start,uint64_t Count,uint32_t Sector,
    const uint8_t *Type,const uint8_t *Unique,uint32_t Number)
{
    size_t I;
    if(!Count||Start>=UINT64_MAX||Count>UINT64_MAX-Start)return Rc95LdrCorruptData;
    if(*N>=Cap)return Rc95LdrBufferTooSmall;
    for(I=0;I<*N;I++){
        if(P[I].Style==Style&&Overlap(P[I].StartLba,P[I].BlockCount,Start,Count))
            return Rc95LdrCorruptData;
        if(Style==Rc95LdrPartitionGpt&&Unique&&memcmp(P[I].UniqueGuid,Unique,16)==0)
            return Rc95LdrCorruptData;
    }
    memset(&P[*N],0,sizeof(P[*N]));
    P[*N].Style=Style;P[*N].StartLba=Start;P[*N].BlockCount=Count;
    P[*N].BlockSize=Sector;P[*N].FirmwareHandleIndex=UINT32_MAX;
    P[*N].ParentDiskHandleIndex=0;P[*N].FirmwarePartitionNumber=Number;
    if(Type)memcpy(P[*N].TypeGuid,Type,16);
    if(Unique)memcpy(P[*N].UniqueGuid,Unique,16);
    ++*N;return Rc95LdrSuccess;
}
static RC95LDR_STATUS ParseGpt(const RC95LDR_BIOS_DISK_DISCOVERY *D,
    RC95LDR_PARTITION_INFO *P,size_t Cap,size_t *N)
{
    uint8_t *S=D->Scratch,*H=S,*E;uint32_t HSize,StoredCrc,Count,Size,EntriesCrc;
    uint64_t EntryLba,First,Last,End;size_t Bytes,Blocks,ReadBytes,I;
    RC95LDR_STATUS St;
    if(D->ScratchLength<D->Disk->BytesPerSector*2u)return Rc95LdrBufferTooSmall;
    St=ReadDisk(D,1,1,H,D->Disk->BytesPerSector);if(St!=Rc95LdrSuccess)return St;
    if(Rc95LdrReadU64(H)!=GPT_SIGNATURE)return Rc95LdrInvalidImage;
    HSize=Rc95LdrReadU32(H+12);StoredCrc=Rc95LdrReadU32(H+16);
    if(HSize<GPT_HEADER_MIN||HSize>D->Disk->BytesPerSector)return Rc95LdrCorruptData;
    memset(H+16,0,4);if(BiosCrc32(H,HSize)!=StoredCrc)return Rc95LdrCorruptData;
    H[16]=(uint8_t)StoredCrc;H[17]=(uint8_t)(StoredCrc>>8);H[18]=(uint8_t)(StoredCrc>>16);H[19]=(uint8_t)(StoredCrc>>24);
    if(Rc95LdrReadU32(H+8)<0x00010000u||Rc95LdrReadU32(H+20)!=0||
       Rc95LdrReadU64(H+24)!=1||Rc95LdrReadU64(H+32)>=D->Disk->BlockCount||
       Rc95LdrReadU64(H+40)>Rc95LdrReadU64(H+48)||
       Rc95LdrReadU64(H+48)>=D->Disk->BlockCount)return Rc95LdrCorruptData;
    First=Rc95LdrReadU64(H+40);Last=Rc95LdrReadU64(H+48);
    EntryLba=Rc95LdrReadU64(H+72);Count=Rc95LdrReadU32(H+80);
    Size=Rc95LdrReadU32(H+84);EntriesCrc=Rc95LdrReadU32(H+88);
    if(!Count||Count>GPT_MAX_ENTRIES||Size!=GPT_ENTRY_SIZE||EntryLba<2||
       !Rc95LdrMultiplySize(Count,Size,&Bytes))return Rc95LdrCorruptData;
    Blocks=(Bytes+D->Disk->BytesPerSector-1u)/D->Disk->BytesPerSector;
    if(Blocks>UINT32_MAX||!Rc95LdrMultiplySize(Blocks,D->Disk->BytesPerSector,&ReadBytes)||
       ReadBytes>D->ScratchLength-D->Disk->BytesPerSector||
       !Add64(EntryLba,Blocks,&End)||End>First)return Rc95LdrBufferTooSmall;
    E=S+D->Disk->BytesPerSector;
    St=ReadDisk(D,EntryLba,(uint32_t)Blocks,E,ReadBytes);if(St!=Rc95LdrSuccess)return St;
    if(BiosCrc32(E,Bytes)!=EntriesCrc)return Rc95LdrCorruptData;
    for(I=0;I<Count;I++){
        const uint8_t *X=E+I*Size;uint64_t A=Rc95LdrReadU64(X+32),Z=Rc95LdrReadU64(X+40);
        if(!memcmp(X,"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0",16))continue;
        if(A<First||Z>Last||Z<A)return Rc95LdrCorruptData;
        St=AddPart(P,Cap,N,Rc95LdrPartitionGpt,A,Z-A+1,D->Disk->BytesPerSector,X,X+16,(uint32_t)I+1);
        if(St!=Rc95LdrSuccess)return St;
        memcpy(P[*N-1].DiskGuid,H+56,16);
        P[*N-1].FileSystem=Rc95LdrFsUnknown;
        St=ReadDisk(D,A,1,S,D->Disk->BytesPerSector);if(St!=Rc95LdrSuccess)return St;
        P[*N-1].FileSystem=DetectFs(S,D->Disk->BytesPerSector);
    }
    return Rc95LdrSuccess;
}
static RC95LDR_STATUS ParseMbr(const RC95LDR_BIOS_DISK_DISCOVERY *D,
    RC95LDR_PARTITION_INFO *P,size_t Cap,size_t *N)
{
    uint8_t *B=D->Scratch;uint8_t Entries[64];uint32_t I;uint64_t ExtendedBase=0,ExtendedEnd=0,Next=0;RC95LDR_STATUS St;
    St=ReadDisk(D,0,1,B,D->Disk->BytesPerSector);if(St!=Rc95LdrSuccess)return St;
    if(Rc95LdrReadU16(B+510)!=MBR_SIGNATURE)return Rc95LdrInvalidImage;
    { uint32_t DiskSignature=Rc95LdrReadU32(B+440);
    memcpy(Entries,B+446,sizeof(Entries));
    for(I=0;I<4;I++){
        const uint8_t *E=Entries+I*16;uint8_t Type=E[4];uint64_t Start=Rc95LdrReadU32(E+8),Count=Rc95LdrReadU32(E+12);
        if(!Type||!Count)continue;
        if(Type==BIOS_PROTECTIVE)return ParseGpt(D,P,Cap,N);
        if(Start>=D->Disk->BlockCount||Count>D->Disk->BlockCount-Start)return Rc95LdrCorruptData;
        if(Type==BIOS_EXTENDED||Type==BIOS_EXTENDED_LBA){if(ExtendedBase||Start>=D->Disk->BlockCount||Count>D->Disk->BlockCount-Start)return Rc95LdrCorruptData;ExtendedBase=Start;ExtendedEnd=Start+Count;Next=Start;continue;}
        St=AddPart(P,Cap,N,Rc95LdrPartitionMbr,Start,Count,D->Disk->BytesPerSector,NULL,NULL,I+1);
        if(St!=Rc95LdrSuccess)return St;
        St=ReadDisk(D,Start,1,B,D->Disk->BytesPerSector);if(St!=Rc95LdrSuccess)return St;
        P[*N-1].MbrDiskSignature=DiskSignature;
        P[*N-1].FileSystem=DetectFs(B,D->Disk->BytesPerSector);
    }
    for(I=0;ExtendedBase&&I<128;I++){
        uint8_t Type,NextType;uint64_t Rel,Count,Start,NextRel;
        St=ReadDisk(D,Next,1,B,D->Disk->BytesPerSector);if(St!=Rc95LdrSuccess)return St;
        if(Rc95LdrReadU16(B+510)!=MBR_SIGNATURE)return Rc95LdrCorruptData;
        Type=B[450];Rel=Rc95LdrReadU32(B+454);Count=Rc95LdrReadU32(B+458);
        NextType=B[466];NextRel=Rc95LdrReadU32(B+470);
        if(Type&&Count){
            if(!Add64(Next,Rel,&Start)||Start<ExtendedBase||Start>=ExtendedEnd||Count>ExtendedEnd-Start)return Rc95LdrCorruptData;
            St=AddPart(P,Cap,N,Rc95LdrPartitionMbr,Start,Count,D->Disk->BytesPerSector,NULL,NULL,5+I);
            if(St!=Rc95LdrSuccess)return St;
            St=ReadDisk(D,Start,1,B,D->Disk->BytesPerSector);if(St!=Rc95LdrSuccess)return St;
            P[*N-1].MbrDiskSignature=DiskSignature;
            P[*N-1].FileSystem=DetectFs(B,D->Disk->BytesPerSector);
        }
        if(NextType!=BIOS_EXTENDED&&NextType!=BIOS_EXTENDED_LBA)break;
        if(!NextRel||!Add64(ExtendedBase,NextRel,&Next)||Next<ExtendedBase||Next>=ExtendedEnd)return Rc95LdrCorruptData;
    }
    if(ExtendedBase&&I==128)return Rc95LdrCorruptData;
    }
    return Rc95LdrSuccess;
}
RC95LDR_STATUS Rc95LdrBiosEnumeratePartitions(void *Context,
    RC95LDR_PARTITION_INFO *P,size_t Cap,size_t *Count)
{
    RC95LDR_BIOS_DISK_DISCOVERY *D=Context;RC95LDR_STATUS St;
    if (!D || !D->BlockContext || !D->BlockContext->Services ||
        D->BlockContext->Disk != D->Disk || !BiosDiskGeometryValid(D->Disk) ||
        !P || !Cap || !Count || !D->Scratch)
        return Rc95LdrInvalidParameter;
    if (D->ScratchLength < D->Disk->BytesPerSector)
        return Rc95LdrBufferTooSmall;
    D->Device.DeviceContext=(void*)D->BlockContext;
    D->Device.BlockCount=D->Disk->BlockCount;
    D->Device.BlockSize=D->Disk->BytesPerSector;
    D->Device.Read=Rc95LdrBiosReadBlocks;
    D->Device.Write=Rc95LdrBiosWriteBlocks;
    *Count=0;St=ParseMbr(D,P,Cap,Count);
    if(St==Rc95LdrInvalidImage)return Rc95LdrNotFound;
    if(St!=Rc95LdrSuccess)return St;
    if(!*Count)return Rc95LdrNotFound;

    /* BTRFS superblocks are partition-relative, not at disk LBA zero.
       Probe each discovered partition through VENEER after Stage 1 has
       established the checked EDD block-device interface. */
    if(D->ScratchLength>=RC95LDR_BTRFS_SUPERBLOCK_SIZE)
    {
        size_t Index;
        for(Index=0;Index<*Count;Index++)
        {
            RC95LDR_VENEER_CONTEXT VContext;
            RC95LDR_BTRFS_SUPERBLOCK Superblock;
            uint64_t SuperblockOffset;
            RC95LDR_STATUS VStatus;
            /* Do not issue Btrfs mirror reads against volumes already
               positively identified as FAT32 or NTFS. Their discovery and
               boot paths remain isolated from the Btrfs probe. */
            if(P[Index].FileSystem!=Rc95LdrFsUnknown)
                continue;
            VContext.Device=&D->Device;
            VContext.PartitionStartLba=P[Index].StartLba;
            VContext.PartitionBlockCount=P[Index].BlockCount;
            VStatus=Rc95LdrVeneerDiscoverBtrfs(&VContext,D->Scratch,
                D->ScratchLength,&Superblock,&SuperblockOffset);
            if(VStatus==Rc95LdrSuccess)
                P[Index].FileSystem=Rc95LdrFsBtrfs;
            else if(VStatus!=Rc95LdrNotFound&&VStatus!=Rc95LdrInvalidImage&&
                    VStatus!=Rc95LdrUnsupported&&VStatus!=Rc95LdrCorruptData&&
                    VStatus!=Rc95LdrIoError)
                return VStatus;
        }
    }
    return Rc95LdrSuccess;
}
static RC95LDR_STATUS PrepareDiscoveryDevice(const RC95LDR_BIOS_DISK_DISCOVERY *D)
{
    if (!D || !D->BlockContext || !D->BlockContext->Services ||
        !D->BlockContext->Disk || D->BlockContext->Disk != D->Disk ||
        !BiosDiskGeometryValid(D->Disk) || !D->Scratch ||
        D->ScratchLength < D->Disk->BytesPerSector ||
        D->Device.DeviceContext != (void *)D->BlockContext ||
       D->Device.BlockCount!=D->Disk->BlockCount||
       D->Device.BlockSize!=D->Disk->BytesPerSector||
       D->Device.Read!=Rc95LdrBiosReadBlocks)return Rc95LdrInvalidParameter;
    return Rc95LdrSuccess;
}
RC95LDR_STATUS Rc95LdrBiosMountFat32(RC95LDR_BIOS_DISK_DISCOVERY *D,
    const RC95LDR_PARTITION_INFO *P,RC95LDR_BIOS_FAT32_VOLUME *M)
{
    RC95LDR_STATUS S;if(!D||!P||!M||P->FileSystem!=Rc95LdrFsFat32)return Rc95LdrInvalidParameter;
    S=PrepareDiscoveryDevice(D);if(S!=Rc95LdrSuccess)return S;
    memset(M,0,sizeof(*M));
    S=Rc95LdrFat32MountPartition(&M->Fat32,&D->Device,P->StartLba,P->BlockCount);
    if(S==Rc95LdrSuccess)S=Rc95LdrFat32BindVolume(&M->Fat32,&M->Volume);
    if(S!=Rc95LdrSuccess)Rc95LdrFat32Unmount(&M->Fat32);
    return S;
}
void Rc95LdrBiosUnmountFat32(RC95LDR_BIOS_FAT32_VOLUME *M)
{if(M){Rc95LdrFat32Unmount(&M->Fat32);memset(M,0,sizeof(*M));}}
RC95LDR_STATUS Rc95LdrBiosMountNtfs(const RC95LDR_BIOS_DISK_DISCOVERY *D,
    const RC95LDR_PARTITION_INFO *P,RC95LDR_NTFS_VOLUME *M)
{
    RC95LDR_STATUS S;if(!D||!P||!M||P->FileSystem!=Rc95LdrFsNtfs)return Rc95LdrInvalidParameter;
    S=PrepareDiscoveryDevice(D);if(S!=Rc95LdrSuccess)return S;
    return Rc95LdrNtfsMountPartition(M,&D->Device,P->StartLba,P->BlockCount,
        D->Scratch,D->ScratchLength);
}


void Rc95LdrBiosUnmountNtfs(RC95LDR_NTFS_VOLUME *Mounted)
{
    Rc95LdrNtfsUnmount(Mounted);
}

RC95LDR_STATUS
Rc95LdrBiosMountBtrfs(
    const RC95LDR_BIOS_DISK_DISCOVERY *Discovery,
    const RC95LDR_PARTITION_INFO *Partition,
    void *ItemBuffer, size_t ItemBufferLength,
    void *InlineBuffer, size_t InlineBufferLength,
    void *StagingBuffer, size_t StagingBufferLength,
    RC95LDR_BIOS_BTRFS_VOLUME *Mounted)
{
    RC95LDR_STATUS Status;

    if (!Discovery || !Partition || !Mounted ||
        Partition->FileSystem != Rc95LdrFsBtrfs ||
        !ItemBuffer || !InlineBuffer || !StagingBuffer)
        return Rc95LdrInvalidParameter;

    /* A failed remount must not leave a previous provider or block binding
       reachable through the caller's mount object. */
    memset(Mounted, 0, sizeof(*Mounted));
    Status = PrepareDiscoveryDevice(Discovery);
    if (Status != Rc95LdrSuccess)
        return Status;
    if (Discovery->ScratchLength < 4096u)
        return Rc95LdrBufferTooSmall;

    Status = Rc95LdrBtrfsMountPartition(&Mounted->Btrfs,
        &Discovery->Device, Partition->StartLba, Partition->BlockCount,
        Discovery->Scratch, Discovery->ScratchLength);
    if (Status != Rc95LdrSuccess)
        goto Failure;

    if (Discovery->ScratchLength < Mounted->Btrfs.Superblock.NodeSize) {
        Status = Rc95LdrBufferTooSmall;
        goto Failure;
    }
    Status = Rc95LdrBtrfsLoadChunkTree(&Mounted->Btrfs,
        Discovery->Scratch, Discovery->ScratchLength);
    if (Status != Rc95LdrSuccess)
        goto Failure;
    Status = Rc95LdrBtrfsLoadRootTree(&Mounted->Btrfs,
        Discovery->Scratch, Discovery->ScratchLength);
    if (Status != Rc95LdrSuccess)
        goto Failure;
    Status = Rc95LdrBtrfsInitializeFileSystem(
        &Mounted->FileSystemContext, &Mounted->Btrfs,
        Discovery->Scratch, Discovery->ScratchLength,
        ItemBuffer, ItemBufferLength, InlineBuffer, InlineBufferLength,
        StagingBuffer, StagingBufferLength, &Mounted->FileSystem);
    if (Status != Rc95LdrSuccess)
        goto Failure;
    Status = Rc95LdrBtrfsBindVolumeWithFileSystem(
        &Mounted->Btrfs, &Mounted->Volume, &Mounted->FileSystem);
    if (Status != Rc95LdrSuccess)
        goto Failure;
    return Rc95LdrSuccess;

Failure:
    Rc95LdrBtrfsUnmount(&Mounted->Btrfs);
    memset(Mounted, 0, sizeof(*Mounted));
    return Status;
}

RC95LDR_STATUS
Rc95LdrBiosBeginBtrfsFileProvider(RC95LDR_BIOS_BTRFS_VOLUME *Mounted)
{
    RC95LDR_STATUS Status;
    if (!Mounted || !Mounted->Btrfs.Mounted ||
        Mounted->Volume.FileSystem != &Mounted->FileSystem ||
        !Mounted->FileSystem.OpenRead || !Mounted->FileSystem.ReadAt ||
        !Mounted->FileSystem.Close)
        return Rc95LdrInvalidParameter;
    if (Mounted->ProviderScopeActive)
        return Rc95LdrInvalidParameter;

    Status = Rc95LdrInitializeVolumeFileProvider(
        &Mounted->Volume, &Mounted->ProviderContext, &Mounted->Provider);
    if (Status != Rc95LdrSuccess)
        return Status;
    Status = Rc95LdrBeginFileProviderScope(
        &Mounted->Provider, &Mounted->ProviderScope);
    if (Status != Rc95LdrSuccess) {
        memset(&Mounted->ProviderContext, 0, sizeof(Mounted->ProviderContext));
        memset(&Mounted->Provider, 0, sizeof(Mounted->Provider));
        memset(&Mounted->ProviderScope, 0, sizeof(Mounted->ProviderScope));
        return Status;
    }
    Mounted->ProviderScopeActive = 1u;
    return Rc95LdrSuccess;
}

RC95LDR_STATUS
Rc95LdrBiosEndBtrfsFileProvider(RC95LDR_BIOS_BTRFS_VOLUME *Mounted)
{
    RC95LDR_STATUS Status;
    if (!Mounted || !Mounted->ProviderScopeActive)
        return Rc95LdrInvalidParameter;
    Status = Rc95LdrEndFileProviderScope(&Mounted->ProviderScope);
    if (Status != Rc95LdrSuccess)
        return Status;
    Mounted->ProviderScopeActive = 0u;
    memset(&Mounted->ProviderContext, 0, sizeof(Mounted->ProviderContext));
    memset(&Mounted->Provider, 0, sizeof(Mounted->Provider));
    memset(&Mounted->ProviderScope, 0, sizeof(Mounted->ProviderScope));
    return Rc95LdrSuccess;
}

void
Rc95LdrBiosUnmountBtrfs(RC95LDR_BIOS_BTRFS_VOLUME *Mounted)
{
    if (!Mounted)
        return;
    /* Never invalidate the active global provider's backing volume. A caller
       must unwind the provider scope (in LIFO order) before unmounting. */
    if (Mounted->ProviderScopeActive &&
        Rc95LdrBiosEndBtrfsFileProvider(Mounted) != Rc95LdrSuccess)
        return;
    Rc95LdrBtrfsUnmount(&Mounted->Btrfs);
    memset(Mounted, 0, sizeof(*Mounted));
}

static const char * const BiosBcdPathsGpt[] = {
    "\\EFI\\Microsoft\\Boot\\BCD",
    "\\Boot\\BCD"
};
static const char * const BiosBcdPathsMbr[] = {
    "\\Boot\\BCD",
    "\\EFI\\Microsoft\\Boot\\BCD"
};

RC95LDR_STATUS
Rc95LdrDiscoverBiosWindows(
    RC95LDR_BIOS_DISK_DISCOVERY *Discovery,
    const RC95LDR_BIOS_DISCOVERY_OPS *Ops,
    RC95LDR_PARTITION_INFO *Partitions, size_t PartitionCapacity,
    RC95LDR_BCD_BOOT_CANDIDATE *Candidates, size_t CandidateCapacity,
    size_t *CandidateCountOut, void *HiveBuffer, size_t HiveCapacity,
    RC95LDR_WINDOWS_INSTALLATION *Installation,
    RC95LDR_NTFS_VOLUME *WindowsVolume, size_t *BcdPartitionIndex)
{
    size_t PartitionCount=0,Index,PathIndex,CandidateCount=0,ImageSize=0;
    RC95LDR_STATUS Status,LastStatus=Rc95LdrNotFound;
    int BcdStoreFound=0,CorruptBcdStoreFound=0;
    RC95LDR_BIOS_FAT32_VOLUME FatVolume;
    RC95LDR_NTFS_VOLUME NtfsVolume;
    RC95BCD_HIVE Hive;

    if(!Discovery||!Ops||
       !Partitions||!PartitionCapacity||!Candidates||!CandidateCapacity||
       !CandidateCountOut||!HiveBuffer||!HiveCapacity||!Installation||
       !WindowsVolume||!BcdPartitionIndex)
        return Rc95LdrInvalidParameter;

    if (Ops->BtrfsWindowsVolume &&
        (!Ops->ValidateBtrfsWindowsVolume ||
         !Ops->BtrfsItemBuffer || !Ops->BtrfsItemBufferLength ||
         !Ops->BtrfsInlineBuffer || !Ops->BtrfsInlineBufferLength ||
         !Ops->BtrfsStagingBuffer || !Ops->BtrfsStagingBufferLength))
        return Rc95LdrInvalidParameter;

    memset(Installation,0,sizeof(*Installation));
    if (Ops->BtrfsWindowsVolume)
        memset(Ops->BtrfsWindowsVolume,0,sizeof(*Ops->BtrfsWindowsVolume));
    memset(WindowsVolume,0,sizeof(*WindowsVolume));
    *CandidateCountOut=0;
    *BcdPartitionIndex=(size_t)-1;

    Status=Rc95LdrBiosEnumeratePartitions(Discovery,Partitions,
        PartitionCapacity,&PartitionCount);
    if(Status!=Rc95LdrSuccess)return Status;
    if(PartitionCount>PartitionCapacity)return Rc95LdrCorruptData;

    for(Index=0;Index<PartitionCount;Index++)
    {
        const char * const *Paths;
        size_t PathCount;
        int HiveLoaded=0;
        RC95LDR_WINDOWS_INSTALLATION Trial;

        if(Partitions[Index].FileSystem!=Rc95LdrFsFat32)
            continue;

        Paths=(Partitions[Index].Style==Rc95LdrPartitionGpt)?
            BiosBcdPathsGpt:BiosBcdPathsMbr;
        PathCount=RC95LDR_ARRAY_COUNT(BiosBcdPathsGpt);
        memset(&FatVolume,0,sizeof(FatVolume));
        memset(&NtfsVolume,0,sizeof(NtfsVolume));

        Status=Rc95LdrBiosMountFat32(Discovery,&Partitions[Index],&FatVolume);
        if(Status!=Rc95LdrSuccess){LastStatus=Status;continue;}

        for(PathIndex=0;PathIndex<PathCount;PathIndex++)
        {
            ImageSize=0;
            Status=Rc95BcdLoadFromVolume(&FatVolume.Volume,Paths[PathIndex],
                HiveBuffer,HiveCapacity,&ImageSize,&Hive);
            if(Status==Rc95LdrSuccess){HiveLoaded=1;BcdStoreFound=1;break;}
            if(Status==Rc95LdrInvalidImage||Status==Rc95LdrCorruptData)
                CorruptBcdStoreFound=1;
            if(Status!=Rc95LdrNotFound)LastStatus=Status;
        }

        if(!HiveLoaded)
        {
            Rc95LdrBiosUnmountFat32(&FatVolume);
            continue;
        }

        CandidateCount=0;
        Status=Rc95BcdCollectWindowsBootCandidates(&Hive,Candidates,
            CandidateCapacity,&CandidateCount);
        if(Status==Rc95LdrSuccess&&CandidateCount<=CandidateCapacity)
            Status=Rc95LdrFilterWindowsBootCandidates(Partitions,
                PartitionCount,Candidates,&CandidateCount);
        else if(Status==Rc95LdrSuccess)
            Status=Rc95LdrCorruptData;

        if(Status==Rc95LdrSuccess)
        {
            size_t Pass,Attempt,Best,PassLimit;
            uint32_t LastOrder;
            int HaveLast,Validated=0,MenuSelection=0;
            const RC95LDR_BCD_BOOT_CANDIDATE *SelectedCandidate=NULL;

            /* The selection callback runs while this exact hive and its
               candidate storage are alive. Validate the returned pointer
               before allowing it to influence the mount transaction. */
            if(Ops->SelectBootCandidate){
                Status=Ops->SelectBootCandidate(Ops->SelectionContext,&Hive,
                    Candidates,CandidateCount,&SelectedCandidate);
                if(Status!=Rc95LdrSuccess){
                    Rc95LdrBiosUnmountFat32(&FatVolume);
                    return Status;
                }
                if(!SelectedCandidate){
                    Rc95LdrBiosUnmountFat32(&FatVolume);
                    return Rc95LdrInvalidParameter;
                }
                {
                    size_t SelectedIndex;
                    for(SelectedIndex=0;SelectedIndex<CandidateCount;SelectedIndex++)
                        if(SelectedCandidate==&Candidates[SelectedIndex])break;
                    if(SelectedIndex==CandidateCount){
                        Rc95LdrBiosUnmountFat32(&FatVolume);
                        return Rc95LdrInvalidParameter;
                    }
                }
                MenuSelection=1;
            }

            /* Keep the common policy's default-first/display-order semantics,
               but continue to the next BCD entry if the selected NTFS volume
               does not contain a usable Windows installation. */
            PassLimit=MenuSelection?1u:2u;
            for(Pass=0;Pass<PassLimit&&!Validated;Pass++)
            {
                size_t AttemptLimit=MenuSelection?1u:CandidateCount;
                HaveLast=0;
                LastOrder=0;
                for(Attempt=0;Attempt<AttemptLimit&&!Validated;Attempt++)
                {
                    Best=(size_t)-1;
                    {
                        size_t Scan;
                        for(Scan=0;Scan<CandidateCount;Scan++)
                        {
                            const RC95LDR_BCD_BOOT_CANDIDATE *Candidate=&Candidates[Scan];
                            if(!Candidate->IsWindowsLoader||!Candidate->IsEnabled||
                               (MenuSelection && Candidate!=SelectedCandidate)||
                               (!MenuSelection && Pass==0&&!Candidate->IsDefault)||
                               (!MenuSelection && Pass==1&&Candidate->IsDefault)||
                               (!MenuSelection && HaveLast&&Candidate->DisplayOrder<=LastOrder))
                                continue;
                            if(Best==(size_t)-1||
                               Candidate->DisplayOrder<Candidates[Best].DisplayOrder)
                                Best=Scan;
                        }
                    }
                    if(Best==(size_t)-1)break;
                    LastOrder=Candidates[Best].DisplayOrder;
                    HaveLast=1;

                    Status=Rc95LdrResolveWindowsBootCandidate(
                        Partitions,PartitionCount,&Candidates[Best],&Trial);
                    if(Status!=Rc95LdrSuccess)
                    {
                        if(Status==Rc95LdrInvalidImage||Status==Rc95LdrCorruptData)
                            CorruptBcdStoreFound=1;
                        continue;
                    }
                    if(Trial.OsPartition.FileSystem==Rc95LdrFsBtrfs)
                    {
                        RC95LDR_BIOS_BTRFS_VOLUME *BtrfsVolume =
                            Ops->BtrfsWindowsVolume;
                        if (!BtrfsVolume)
                        {
                            /* Do not silently boot a lower-priority NTFS
                               entry when the selected BCD entry names Btrfs. */
                            Rc95LdrBiosUnmountFat32(&FatVolume);
                            if(Ops->TextUi)
                                (void)Rc95LdrTextUiShowError(Ops->TextUi,
                                    "Btrfs boot provider is not configured",
                                    "The selected BCD entry names a Btrfs operating-system partition, but this BIOS invocation did not provide Btrfs workspaces and a Btrfs validation callback.",
                                    Rc95LdrUnsupported,RC95LDR_DETAIL_BTRFS_PROVIDER_UNAVAILABLE,
                                    "Configure the native BIOS Btrfs provider and retry. The selected BCD entry will not be replaced by an unrelated installation.");
                            return Rc95LdrUnsupported;
                        }

                        Status = Rc95LdrBiosMountBtrfs(Discovery,
                            &Trial.OsPartition, Ops->BtrfsItemBuffer,
                            Ops->BtrfsItemBufferLength, Ops->BtrfsInlineBuffer,
                            Ops->BtrfsInlineBufferLength, Ops->BtrfsStagingBuffer,
                            Ops->BtrfsStagingBufferLength, BtrfsVolume);
                        if (Status == Rc95LdrSuccess)
                            Status = Rc95LdrBiosBeginBtrfsFileProvider(BtrfsVolume);
                        if (Status == Rc95LdrSuccess)
                            Status = Ops->ValidateBtrfsWindowsVolume(
                                Ops->Context, &BtrfsVolume->Volume, &Trial);
                        if (Status == Rc95LdrSuccess)
                        {
                            *Installation = Trial;
                            *CandidateCountOut = CandidateCount;
                            *BcdPartitionIndex = Index;
                            Rc95LdrBiosUnmountFat32(&FatVolume);
                            /* Btrfs mount and provider scope are caller-owned
                               through the subsequent WINLOAD transaction. */
                            return Rc95LdrSuccess;
                        }
                        if (BtrfsVolume->ProviderScopeActive)
                            (void)Rc95LdrBiosEndBtrfsFileProvider(BtrfsVolume);
                        Rc95LdrBiosUnmountBtrfs(BtrfsVolume);
                        continue;
                    }
                    Status=Rc95LdrBiosMountNtfs(Discovery,&Trial.OsPartition,
                        &NtfsVolume);
                    if(Status!=Rc95LdrSuccess)continue;
                    Status=(Ops->ValidateWindowsVolume?
                        Ops->ValidateWindowsVolume:
                        Rc95LdrBiosValidateWindowsVolume)(Ops->Context,
                        &NtfsVolume,&Trial);
                    if(Status==Rc95LdrSuccess)
                    {
                        *Installation=Trial;
                        *WindowsVolume=NtfsVolume;
                        *CandidateCountOut=CandidateCount;
                        *BcdPartitionIndex=Index;
                        Rc95LdrBiosUnmountFat32(&FatVolume);
                        return Rc95LdrSuccess;
                    }
                    Rc95LdrBiosUnmountNtfs(&NtfsVolume);
                }
            }
            /* An explicit user choice is authoritative. Do not fall through
               to another candidate or another BCD store if its volume fails. */
            if(MenuSelection && !Validated){
                Rc95LdrBiosUnmountFat32(&FatVolume);
                return Status==Rc95LdrSuccess?Rc95LdrNotFound:Status;
            }
        }

        if(Status==Rc95LdrInvalidImage||Status==Rc95LdrCorruptData)
            CorruptBcdStoreFound=1;
        Rc95LdrBiosUnmountFat32(&FatVolume);
        LastStatus=Status;
    }

    if(Ops->TextUi)
    {
        if(CorruptBcdStoreFound)
            (void)Rc95LdrTextUiShowBcdError(Ops->TextUi,
                Rc95LdrCorruptData,0xBCD00002u);
        else if(!BcdStoreFound&&LastStatus==Rc95LdrNotFound)
            (void)Rc95LdrTextUiShowBcdError(Ops->TextUi,
                Rc95LdrNotFound,0xBCD00001u);
    }
    if(CorruptBcdStoreFound)return Rc95LdrCorruptData;
    return LastStatus;
}

static int ReservationOverlap(const RC95LDR_MEMORY_RESERVATION_SET *R,
    uint64_t Base,uint64_t Length)
{
    size_t I;
    if(!R||R->Count>RC95LDR_MEMORYMAP_MAX_RESERVATIONS)return 1;
    for(I=0;I<R->Count;I++)
        if(Overlap(Base,Length,R->Entries[I].Base,R->Entries[I].Length))return 1;
    return 0;
}
RC95LDR_STATUS
Rc95LdrBiosValidateManifest(
    const RC95LDR_BIOS_DISK *D,
    uint64_t ManifestLba,
    const RC95LDR_MEMORY_RESERVATION_SET *Reservations,
    const RC95LDR_BIOS_IMAGE_MANIFEST *M)
{
    uint64_t ManifestBlocks, ManifestEnd, EntryEnd;
    uint64_t SegmentBlocks, SegmentEnd;
    size_t I, J;
    int EntryCovered = 0;

    if (!D || !Reservations || !M ||
        Reservations->Count > RC95LDR_MEMORYMAP_MAX_RESERVATIONS ||
        !D->EddSupported ||
        (D->BytesPerSector != 512u && D->BytesPerSector != 1024u &&
         D->BytesPerSector != 2048u && D->BytesPerSector != 4096u) ||
        !D->BlockCount)
        return Rc95LdrInvalidParameter;

    ManifestBlocks = (sizeof(*M) + D->BytesPerSector - 1u) /
                     D->BytesPerSector;
    if (!ManifestBlocks || ManifestLba >= D->BlockCount ||
        ManifestBlocks > D->BlockCount - ManifestLba ||
        !Add64(ManifestLba, ManifestBlocks, &ManifestEnd))
        return Rc95LdrOutOfBounds;

    if (M->Signature != RC95LDR_BIOS_IMAGE_SIGNATURE ||
        M->Version != RC95LDR_BIOS_IMAGE_VERSION ||
        M->HeaderSize != sizeof(*M) || M->Flags ||
        !M->SegmentCount ||
        M->SegmentCount > RC95LDR_BIOS_IMAGE_MAX_SEGMENTS)
        return Rc95LdrCorruptData;

    for (I = 0; I < M->SegmentCount; ++I)
    {
        const RC95LDR_BIOS_IMAGE_SEGMENT *X = &M->Segments[I];

        if (X->Flags || !X->ByteLength ||
            X->ByteLength % D->BytesPerSector ||
            X->LoadAddress < 0x100000u ||
            X->LoadAddress > (uint64_t)UINTPTR_MAX ||
            X->ByteLength > (uint64_t)UINTPTR_MAX - X->LoadAddress ||
            X->ByteLength > SIZE_MAX ||
            X->Lba >= D->BlockCount)
            return Rc95LdrCorruptData;

        SegmentBlocks = X->ByteLength / D->BytesPerSector;
        if (!SegmentBlocks || SegmentBlocks > UINT32_MAX ||
            SegmentBlocks > D->BlockCount - X->Lba ||
            !Add64(X->Lba, SegmentBlocks, &SegmentEnd))
            return Rc95LdrOutOfBounds;

        if (X->Lba < ManifestEnd && ManifestLba < SegmentEnd)
            return Rc95LdrCorruptData;

        if (!Add64(X->LoadAddress, X->ByteLength, &EntryEnd))
            return Rc95LdrIntegerOverflow;

        if (ReservationOverlap(Reservations, X->LoadAddress, X->ByteLength))
            return Rc95LdrOutOfBounds;

        if (M->ImageEntry >= X->LoadAddress && M->ImageEntry < EntryEnd)
            EntryCovered = 1;

        for (J = 0; J < I; ++J)
        {
            const RC95LDR_BIOS_IMAGE_SEGMENT *Previous = &M->Segments[J];
            uint64_t PreviousBlocks =
                Previous->ByteLength / D->BytesPerSector;

            if (Overlap(X->LoadAddress, X->ByteLength,
                        Previous->LoadAddress, Previous->ByteLength) ||
                Overlap(X->Lba, SegmentBlocks,
                        Previous->Lba, PreviousBlocks))
                return Rc95LdrCorruptData;
        }
    }

    if (!EntryCovered)
        return Rc95LdrCorruptData;

    /* Unused records must be zero so future format extensions cannot be
       confused with hidden payload descriptors by older readers. */
    for (I = M->SegmentCount; I < RC95LDR_BIOS_IMAGE_MAX_SEGMENTS; ++I)
    {
        const uint8_t *Bytes = (const uint8_t *)&M->Segments[I];
        size_t K;
        for (K = 0; K < sizeof(M->Segments[I]); ++K)
            if (Bytes[K] != 0)
                return Rc95LdrCorruptData;
    }

    return Rc95LdrSuccess;
}

RC95LDR_STATUS Rc95LdrBiosLoadManifestImage(
    const RC95LDR_BIOS_SERVICES *S,const RC95LDR_BIOS_DISK *D,
    uint64_t ManifestLba,const RC95LDR_MEMORY_RESERVATION_SET *Reservations,
    RC95LDR_BIOS_IMAGE_MANIFEST *M)
{
    uint64_t ManifestBlocks;
    size_t ManifestReadBytes, I;
    uint8_t ManifestBuffer[4096];
    RC95LDR_STATUS St;

    if (!S || !D || !M || !Reservations ||
        (D->BytesPerSector != 512u && D->BytesPerSector != 1024u &&
         D->BytesPerSector != 2048u && D->BytesPerSector != 4096u))
        return Rc95LdrInvalidParameter;

    ManifestBlocks = (sizeof(*M) + D->BytesPerSector - 1u) /
                     D->BytesPerSector;
    if (!ManifestBlocks || ManifestBlocks > UINT32_MAX ||
        ManifestLba >= D->BlockCount ||
        ManifestBlocks > D->BlockCount - ManifestLba)
        return Rc95LdrOutOfBounds;

    ManifestReadBytes = (size_t)ManifestBlocks * D->BytesPerSector;
    if (ManifestReadBytes > sizeof(ManifestBuffer))
        return Rc95LdrBufferTooSmall;

    St = Rc95LdrBiosReadDisk(S, D, ManifestLba,
        (uint32_t)ManifestBlocks, ManifestBuffer, ManifestReadBytes);
    if (St != Rc95LdrSuccess)
        return St;

    memcpy(M, ManifestBuffer, sizeof(*M));
    St = Rc95LdrBiosValidateManifest(D, ManifestLba, Reservations, M);
    if (St != Rc95LdrSuccess)
        return St;

    for (I = 0; I < M->SegmentCount; ++I)
    {
        RC95LDR_BIOS_IMAGE_SEGMENT *X = &M->Segments[I];
        uint32_t Blocks = (uint32_t)(X->ByteLength / D->BytesPerSector);
        uint8_t *Destination = (uint8_t *)(uintptr_t)X->LoadAddress;

        St = Rc95LdrBiosReadDisk(S, D, X->Lba, Blocks,
            Destination, (size_t)X->ByteLength);
        if (St != Rc95LdrSuccess)
            return St;
        if (BiosCrc32(Destination, (size_t)X->ByteLength) != X->Crc32)
            return Rc95LdrCorruptData;
    }

    return Rc95LdrSuccess;
}
