#include "rc95ldr_btrfs.h"
#include "rc95ldr_volumeprovider.h"
#include "rc95ldruefinativemounts.h"
#include "rc95ldrwinload.h"
#include "rc95ldruefiwinloadflow.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define IMAGE_BYTES (2u * 1024u * 1024u)
#define PARTITION_START 8u
static uint8_t Image[IMAGE_BYTES];
static uint8_t OriginalBtrfsFsNode[16384];
static unsigned Writes;
static uint64_t FailedReadLba;
static int FailReadEnabled;

static void Put16(uint8_t *P, uint16_t V)
{ P[0]=(uint8_t)V; P[1]=(uint8_t)(V>>8); }
static void Put32(uint8_t *P, uint32_t V)
{ P[0]=(uint8_t)V; P[1]=(uint8_t)(V>>8); P[2]=(uint8_t)(V>>16); P[3]=(uint8_t)(V>>24); }
static void Put64(uint8_t *P, uint64_t V)
{ Put32(P,(uint32_t)V); Put32(P+4,(uint32_t)(V>>32)); }

static RC95LDR_STATUS
ReadImage(void *Context, uint64_t Lba, uint32_t Blocks,
          void *Buffer, size_t Length)
{
    uint64_t Offset = Lba * 512u;
    (void)Context;
    if (Length != (size_t)Blocks * 512u ||
        Offset > IMAGE_BYTES || Length > IMAGE_BYTES - (size_t)Offset)
        return Rc95LdrOutOfBounds;
    if (FailReadEnabled && Lba == FailedReadLba)
        return Rc95LdrIoError;
    memcpy(Buffer, Image + (size_t)Offset, Length);
    return Rc95LdrSuccess;
}

static RC95LDR_STATUS
WriteImage(void *Context, uint64_t Lba, uint32_t Blocks,
           const void *Buffer, size_t Length)
{
    (void)Context; (void)Lba; (void)Blocks; (void)Buffer; (void)Length;
    ++Writes;
    return Rc95LdrIoError;
}

static void
MakeSuperblock(uint8_t *S)
{
    memset(S,0,4096);
    Put64(S+48,65536);
    Put64(S+64,RC95LDR_BTRFS_MAGIC);
    Put64(S+72,1);
    Put64(S+80,0x20000);
    Put64(S+88,0x30000);
    Put64(S+112,512u*1024u);
    Put64(S+120,4096);
    Put64(S+128,6);
    Put64(S+136,1);
    Put64(S+0x32bu,256);
    Put64(S+201,1);
    Put64(S+241,512u*1024u);
    Put64(S+249,4096);
    Put32(S+265,4096);
    Put32(S+144,4096);
    Put32(S+148,16384);
    Put32(S+152,16384);
    Put32(S+156,4096);
    Put32(S+160,388);
    S[0x32bu+8]=0xe4;
    Put64(S+0x32bu+17,4096);
    Put64(S+0x32bu+25,2);
    Put64(S+0x32bu+33,4096);
    Put64(S+0x32bu+41,2);
    Put32(S+0x32bu+57,4096);
    Put16(S+0x32bu+61,1);
    Put64(S+0x32bu+65,1);
    Put64(S+0x32bu+73,0);

    /* Second validated SYSTEM chunk backs the chunk-tree root node. */
    {
        uint8_t *C = S + 0x32bu + 97u;
        Put64(C,256);
        C[8]=0xe4;
        Put64(C+9,0x30000);
        Put64(C+17,0x4000);
        Put64(C+25,2);
        Put64(C+33,4096);
        Put64(C+41,2);
        Put32(C+57,4096);
        Put16(C+61,1);
        Put64(C+65,1);
        Put64(C+73,0x30000);

        /* Third SYSTEM chunk backs the superblock root-tree node. */
        C += 97u;
        Put64(C,256);
        C[8]=0xe4;
        Put64(C+9,0x20000);
        Put64(C+17,0x4000);
        Put64(C+25,2);
        Put64(C+33,4096);
        Put64(C+41,2);
        Put32(C+57,4096);
        Put16(C+61,1);
        Put64(C+65,1);
        Put64(C+73,0x20000);
        /* Fourth SYSTEM chunk backs the default filesystem-tree root. */
        C += 97u;
        Put64(C,256);
        C[8]=0xe4;
        Put64(C+9,0x50000);
        Put64(C+17,0x4000);
        Put64(C+25,2);
        Put64(C+33,4096);
        Put64(C+41,2);
        Put32(C+57,4096);
        Put16(C+61,1);
        Put64(C+65,1);
        Put64(C+73,0x50000);
    }
    Put16(S+196,RC95LDR_BTRFS_CSUM_CRC32C);
    Put32(S,Rc95LdrVeneerCrc32c(S+32,4064));
}


static void
FinishBtrfsNode(uint8_t *Node, uint32_t NodeSize, uint64_t Bytenr,
                uint64_t Generation, uint64_t Owner, uint8_t Level,
                uint32_t ItemCount, const uint8_t *FsId)
{
    memset(Node, 0, NodeSize);
    memcpy(Node + 32, FsId, 16);
    Put64(Node + 48, Bytenr);
    Put64(Node + 80, Generation);
    Put64(Node + 88, Owner);
    Put32(Node + 96, ItemCount);
    Node[100] = Level;
}

static void
MakeExtentLeaf(uint8_t *Node, uint64_t Bytenr, const uint8_t *FsId,
               int RightLeaf)
{
    uint32_t Count = 2u;
    uint32_t DataCursor = 16384u;
    uint32_t I;
    FinishBtrfsNode(Node, 16384u, Bytenr, 1, 5, 0, Count, FsId);
    for (I = 0; I < Count; ++I)
    {
        uint8_t *Entry = Node + 101u + I * 25u;
        uint8_t Payload[53];
        uint32_t PayloadLength;
        uint64_t FileOffset;
        memset(Payload, 0, sizeof(Payload));
        if (!RightLeaf)
        {
            FileOffset = I ? 7u : 5u;
            PayloadLength = I ? 24u : 22u;
            Put64(Payload, 1);
            Put64(Payload + 8, I ? 3u : 1u);
            Payload[20] = 0; /* inline */
            memcpy(Payload + 21, I ? "llo" : "h", I ? 3u : 1u);
        }
        else
        {
            FileOffset = I ? 20u : 10u;
            PayloadLength = 53;
            Put64(Payload, 1);
            Put64(Payload + 8, 5);
            Payload[20] = I ? 2u : 1u; /* preallocated / regular */
            Put64(Payload + 21, 0x40200u);
            Put64(Payload + 29, 512u);
            Put64(Payload + 37, 0);
            Put64(Payload + 45, 5u);
        }
        DataCursor -= PayloadLength;
        memcpy(Node + DataCursor, Payload, PayloadLength);
        Put64(Entry, 257);
        Entry[8] = 108;
        Put64(Entry + 9, FileOffset);
        Put32(Entry + 17, DataCursor);
        Put32(Entry + 21, PayloadLength);
    }
    Put32(Node, Rc95LdrVeneerCrc32c(Node + 32, 16384u - 32u));
}

static void
MakeExtentRoot(uint8_t *Node, const uint8_t *FsId)
{
    uint8_t *Entry;
    /* Level-two root selects a separate level-one index node. */
    FinishBtrfsNode(Node, 16384u, 0x50000u, 1, 5, 2, 1, FsId);
    Entry = Node + 101u;
    Put64(Entry, 257);
    Entry[8] = 108;
    Put64(Entry + 9, 5u);
    Put64(Entry + 17, 0x5c000u);
    Put64(Entry + 25, 1u);
    Put32(Node, Rc95LdrVeneerCrc32c(Node + 32, 16384u - 32u));
}

static void
MakeExtentInternal(uint8_t *Node, const uint8_t *FsId)
{
    uint32_t I;
    FinishBtrfsNode(Node, 16384u, 0x5c000u, 1, 5, 1, 2, FsId);
    for (I = 0; I < 2u; ++I)
    {
        uint8_t *Entry = Node + 101u + I * 33u;
        Put64(Entry, 257);
        Entry[8] = 108;
        Put64(Entry + 9, I ? 10u : 5u);
        Put64(Entry + 17, I ? 0x58000u : 0x54000u);
        Put64(Entry + 25, 1u);
    }
    Put32(Node, Rc95LdrVeneerCrc32c(Node + 32, 16384u - 32u));
}

static uint8_t UefiBtrfsDevicePath[46];
static uint64_t UefiBtrfsPartitionBlocks;
static unsigned UefiBtrfsAllocations, UefiBtrfsFrees, UefiBtrfsFailAllocation;
static RC95LDR_STATUS UefiBtrfsReadBlocks(void*,uint32_t,uint64_t,size_t,void*);
static RC95LDR_STATUS UefiBtrfsGetHandle(void *C,size_t I,void **H) { (void)C; if(I)return Rc95LdrNotFound; *H=(void*)1; return Rc95LdrSuccess; }
static RC95LDR_STATUS UefiBtrfsOpenBlockIo(void *C,void *H,RC95LDR_UEFI_BLOCK_IO *Io) { (void)C; if(H!=(void*)1||!Io)return Rc95LdrInvalidParameter; memset(Io,0,sizeof(*Io)); Io->Protocol=Image; Io->Media.MediaId=9; Io->Media.BlockSize=512; Io->Media.IoAlign=1; Io->Media.LastBlock=UefiBtrfsPartitionBlocks-1u; Io->Media.MediaPresent=1; Io->Media.LogicalPartition=1; Io->Media.ReadOnly=1; Io->ReadBlocks=UefiBtrfsReadBlocks; return Rc95LdrSuccess; }
static RC95LDR_STATUS UefiBtrfsReadBlocks(void *P,uint32_t Id,uint64_t Lba,size_t Bytes,void *Buffer) { uint64_t A=(uint64_t)PARTITION_START+Lba; (void)P; if(Id!=9||Lba>UefiBtrfsPartitionBlocks||Bytes>(size_t)(UefiBtrfsPartitionBlocks-Lba)*512u||A>IMAGE_BYTES/512u||Bytes>IMAGE_BYTES-(size_t)A*512u||Bytes%512u)return Rc95LdrOutOfBounds; if(FailReadEnabled&&A==FailedReadLba)return Rc95LdrIoError; memcpy(Buffer,Image+(size_t)A*512u,Bytes); return Rc95LdrSuccess; }
static RC95LDR_STATUS UefiBtrfsGetDevicePath(void *C,void *H,const uint8_t **P,size_t *N) { (void)C; if(H!=(void*)1)return Rc95LdrNotFound; *P=UefiBtrfsDevicePath; *N=sizeof(UefiBtrfsDevicePath); return Rc95LdrSuccess; }
static RC95LDR_STATUS UefiBtrfsAllocate(void *C,size_t N,void **P) { (void)C; ++UefiBtrfsAllocations; *P=NULL; if(UefiBtrfsFailAllocation==UefiBtrfsAllocations)return Rc95LdrOutOfMemory; *P=malloc(N); return *P?Rc95LdrSuccess:Rc95LdrOutOfMemory; }
static void UefiBtrfsFree(void *C,void *P) { (void)C; if(P){++UefiBtrfsFrees;free(P);} }
static void UefiBtrfsPut16(uint8_t *P,uint16_t V){P[0]=(uint8_t)V;P[1]=(uint8_t)(V>>8);}
static void UefiBtrfsPut32(uint8_t *P,uint32_t V){P[0]=(uint8_t)V;P[1]=(uint8_t)(V>>8);P[2]=(uint8_t)(V>>16);P[3]=(uint8_t)(V>>24);}
static void UefiBtrfsPut64(uint8_t *P,uint64_t V){UefiBtrfsPut32(P,(uint32_t)V);UefiBtrfsPut32(P+4,(uint32_t)(V>>32));}
static int TestUefiBtrfsMount(void) {
 RC95LDR_UEFI_NATIVE_MOUNT_CONTEXT C; RC95LDR_UEFI_FIRMWARE_OPS F; RC95LDR_PARTITION_INFO P; RC95LDR_VOLUME V; RC95LDR_VOLUME_PROVIDER_CONTEXT ProviderContext; RC95LDR_FILE_PROVIDER Provider; RC95LDR_FILE_PROVIDER_SCOPE ProviderScope; RC95LDR_WINLOAD_PLATFORM WinloadPlatform; RC95LDR_WINLOAD_FILE WinloadFile; RC95LDR_WINDOWS_INSTALLATION WinloadInstallation; static const uint16_t WinloadBootPath[]={'\\','b','o','o','t',0}; uint8_t ProviderData[8]; uint8_t G[16]; unsigned I;
 memset(&F,0,sizeof(F)); F.GetHandleAt=UefiBtrfsGetHandle; F.OpenBlockIo=UefiBtrfsOpenBlockIo; F.GetDevicePath=UefiBtrfsGetDevicePath;
 memset(&P,0,sizeof(P)); memset(G,0x31,sizeof(G)); P.Style=Rc95LdrPartitionGpt; P.FileSystem=Rc95LdrFsBtrfs; memcpy(P.UniqueGuid,G,16); P.StartLba=PARTITION_START; P.BlockCount=IMAGE_BYTES/512u-PARTITION_START; P.BlockSize=512; P.FirmwareHandleIndex=0; P.FirmwarePartitionNumber=1; UefiBtrfsPartitionBlocks=P.BlockCount;
 memset(UefiBtrfsDevicePath,0,sizeof(UefiBtrfsDevicePath)); UefiBtrfsDevicePath[0]=4; UefiBtrfsDevicePath[1]=1; UefiBtrfsPut16(UefiBtrfsDevicePath+2,42); UefiBtrfsPut32(UefiBtrfsDevicePath+4,1); UefiBtrfsPut64(UefiBtrfsDevicePath+8,P.StartLba); UefiBtrfsPut64(UefiBtrfsDevicePath+16,P.BlockCount); memcpy(UefiBtrfsDevicePath+24,G,16); UefiBtrfsDevicePath[40]=2; UefiBtrfsDevicePath[41]=2; UefiBtrfsDevicePath[42]=0x7f; UefiBtrfsDevicePath[43]=0xff; UefiBtrfsPut16(UefiBtrfsDevicePath+44,4);
 if(Rc95LdrUefiInitializeNativeMounts(&C,&F,UefiBtrfsAllocate,UefiBtrfsFree,NULL)!=Rc95LdrSuccess)return 110;
 for(I=1;I<=4;I++){UefiBtrfsAllocations=UefiBtrfsFrees=0;UefiBtrfsFailAllocation=I; if(Rc95LdrUefiMountNativeVolume(&C,&P,Rc95LdrFsBtrfs,&V)!=Rc95LdrOutOfMemory||UefiBtrfsAllocations!=I||UefiBtrfsFrees!=I-1u||C.Btrfs.Mounted||C.Btrfs.Scratch||V.FileSystem)return 110+(int)I;}
 UefiBtrfsFailAllocation=0;UefiBtrfsAllocations=UefiBtrfsFrees=0;
 if(Rc95LdrUefiMountNativeVolume(&C,&P,Rc95LdrFsBtrfs,&V)!=Rc95LdrSuccess||!C.Btrfs.Mounted||!V.FileSystem||!V.BlockDevice||V.BlockDevice->Write)return 116;
 /* Exercise the exact Btrfs filesystem through the same generic,
    read-only provider ABI used by loader-scoped file access. */
 if(Rc95LdrInitializeVolumeFileProvider(&V,&ProviderContext,&Provider)!=Rc95LdrSuccess)return 117;
 memset(&ProviderScope,0,sizeof(ProviderScope));
 if(Rc95LdrBeginFileProviderScope(&Provider,&ProviderScope)!=Rc95LdrSuccess)return 117;
 memset(&WinloadPlatform,0,sizeof(WinloadPlatform));
 WinloadPlatform.StructureSize=sizeof(WinloadPlatform);
 memset(&WinloadInstallation,0,sizeof(WinloadInstallation));
 WinloadInstallation.OsPartition.FileSystem=Rc95LdrFsBtrfs;
 if(Rc95LdrUefiSetWinloadProviderAttestation(
        &WinloadPlatform,&WinloadInstallation,1)!=Rc95LdrSuccess||
    WinloadPlatform.BootFileSystemKind!=Rc95LdrWinloadFsBtrfs||
    WinloadPlatform.BootFileSystemCapabilities!=
        (RC95LDR_WINLOAD_FS_CAP_READ_ONLY|RC95LDR_WINLOAD_FS_CAP_BTRFS_NATIVE))
    return 117;
 memset(&WinloadFile,0,sizeof(WinloadFile));
 if(Rc95LdrWinloadFileOpen(&WinloadPlatform,WinloadBootPath,&WinloadFile)!=Rc95LdrSuccess||
    !WinloadFile.FileContext||WinloadFile.FileSize!=5u)return 117;
 memset(ProviderData,0xa5,sizeof(ProviderData));
 if(Rc95LdrWinloadFileReadAt(&WinloadPlatform,&WinloadFile,0,ProviderData,5u)!=Rc95LdrSuccess||
    memcmp(ProviderData,"hello",5u))return 117;
 ProviderData[0]=0xa5u;
 if(Rc95LdrWinloadFileReadAt(&WinloadPlatform,&WinloadFile,4u,ProviderData,2u)!=Rc95LdrOutOfBounds||
    ProviderData[0]!=0xa5u)return 117;
 /* A metadata read failure must cross the Btrfs -> volume provider ->
    WINLOAD boundary unchanged, without exposing staging-buffer contents. */
 memset(ProviderData,0xa5,sizeof(ProviderData));
 FailedReadLba=PARTITION_START+(0x50000u/512u);
 FailReadEnabled=1;
 if(Rc95LdrWinloadFileReadAt(&WinloadPlatform,&WinloadFile,0,ProviderData,5u)!=Rc95LdrIoError)
    return 119;
 for(I=0;I<5u;I++)if(ProviderData[I]!=0xa5u)return 119;
 FailReadEnabled=0;
 Rc95LdrWinloadFileClose(&WinloadPlatform,&WinloadFile);
 if(WinloadFile.FileContext)return 117;
 if(Rc95LdrEndFileProviderScope(&ProviderScope)!=Rc95LdrSuccess)return 117;
 Rc95LdrUefiUnmountNativeVolume(&C,&V); if(V.FileSystem||C.Btrfs.Mounted||C.Btrfs.Scratch||C.Btrfs.ItemBuffer||C.Btrfs.InlineBuffer||C.Btrfs.StagingBuffer||UefiBtrfsFrees!=4u)return 118;
 Rc95LdrUefiDestroyNativeMounts(&C); return 0;
}

int main(void)
{
    RC95LDR_BLOCK_DEVICE Device;
    RC95LDR_BTRFS_VOLUME Btrfs;
    RC95LDR_VOLUME Volume;
    RC95LDR_BTRFS_FILESYSTEM_CONTEXT FsContext;
    RC95LDR_FILESYSTEM FileSystem;
    RC95LDR_VOLUME_PROVIDER_CONTEXT ProviderContext;
    RC95LDR_FILE_PROVIDER FileProvider;
    void *FsHandle, *FsHandle2, *ProviderHandle;
    void *HandleSlots[RC95LDR_BTRFS_FILE_HANDLES];
    size_t HandleIndex;
    uint64_t FsFileSize;
    size_t ProviderBytes;
    RC95LDR_FILE_INFORMATION ProviderInfo;
    uint8_t FsItemBuffer[16384], FsInlineBuffer[16384], FsReadBuffer[8];
    uint8_t FsStagingBuffer[32];
    uint8_t Scratch[16384], ReadBuffer[512], ItemBuffer[160];
    uint64_t DirectoryInode, DirectoryLocationOffset, ResolvedInodeId;
    RC95LDR_BTRFS_INODE ResolvedInode;
    uint8_t DirectoryType;
    size_t ItemLength = 0;
    RC95LDR_BTRFS_INODE RootInode;
    RC95LDR_BTRFS_FILE_EXTENT FileExtent;
    uint8_t InlineData[16], ExtentItemBuffer[64];
    uint64_t PartitionBlocks = (IMAGE_BYTES / 512u) - PARTITION_START;
    uint8_t *Superblock = Image + PARTITION_START * 512u + 65536u;

    memset(&Device,0,sizeof(Device));
    Device.BlockCount = IMAGE_BYTES / 512u;
    Device.BlockSize = 512u;
    Device.Read = ReadImage;
    Device.Write = WriteImage;
    memset(Image + PARTITION_START * 512u,0x5a,512u);
    MakeSuperblock(Superblock);

    /* One-leaf chunk tree. Its sole CHUNK_ITEM maps logical 0x40000 to
       partition-relative physical 0x40000 on this single device. */
    {
        uint8_t *Node = Image + PARTITION_START * 512u + 0x30000u;
        uint8_t *Item;
        memset(Node,0,16384);
        Put64(Node+48,0x30000);
        Put64(Node+80,1);
        Put64(Node+88,3); /* BTRFS_CHUNK_TREE_OBJECTID */
        Put32(Node+96,1);
        Node[100]=0;
        Put64(Node+101,256);
        Node[109]=0xe4;
        Put64(Node+110,0x40000);
        Put32(Node+118,16384u-80u);
        Put32(Node+122,80);
        Item=Node+16384u-80u;
        Put64(Item,0x4000);
        Put64(Item+8,2);
        Put64(Item+16,4096);
        Put64(Item+24,2);
        Put32(Item+32,4096);
        Put32(Item+36,4096);
        Put32(Item+40,4096);
        Put16(Item+44,1);
        Put64(Item+48,1);
        Put64(Item+56,0x40000);
        Put32(Node,Rc95LdrVeneerCrc32c(Node+32,16384u-32u));
    }

    if (Rc95LdrBtrfsMountPartition(&Btrfs,&Device,PARTITION_START,
            PartitionBlocks,Scratch,sizeof(Scratch)) != Rc95LdrSuccess)
        return 1;
    if (!Rc95LdrBtrfsGetSuperblock(&Btrfs) ||
        Rc95LdrBtrfsGetSuperblock(&Btrfs)->TotalBytes != 512u*1024u ||
        Btrfs.SelectedSuperblockOffset != 65536u)
        return 2;


    /* Root-tree leaf contains the default filesystem (subvolume 5) root item. */
    {
        uint8_t *Node = Image + PARTITION_START * 512u + 0x20000u;
        uint8_t *Item;
        memset(Node,0,16384);
        Put64(Node+48,0x20000);
        Put64(Node+80,1);
        Put64(Node+88,1); /* BTRFS_ROOT_TREE_OBJECTID */
        Put32(Node+96,1);
        Node[100]=0;
        Put64(Node+101,5);
        Node[109]=132; /* BTRFS_ROOT_ITEM_KEY */
        Put64(Node+110,0);
        Put32(Node+118,16384u-239u);
        Put32(Node+122,239);
        Item=Node+16384u-239u;
        Put64(Item+160,1);
        Put64(Item+168,256);
        Put64(Item+176,0x50000);
        Item[238]=0;
        Put32(Node,Rc95LdrVeneerCrc32c(Node+32,16384u-32u));
    }


    /* Filesystem-tree leaf: root inode, DIR_ITEM, target inode and inline extent. */
    {
        uint8_t *Node = Image + PARTITION_START * 512u + 0x50000u;
        uint8_t *RootItem, *Dir, *FileItem, *ExtentItem;
        uint32_t RootOffset=16384u-160u;
        uint32_t DirOffset=RootOffset-34u;
        uint32_t FileOffset=DirOffset-160u;
        uint32_t ExtentOffset=FileOffset-26u;
        memset(Node,0,16384);
        Put64(Node+48,0x50000);
        Put64(Node+80,1);
        Put64(Node+88,5);
        Put32(Node+96,4);
        Node[100]=0;
        Put64(Node+101,256);
        Node[109]=1;
        Put64(Node+110,0);
        Put32(Node+118,RootOffset);
        Put32(Node+122,160);
        Put64(Node+126,256);
        Node[134]=84;
        Put64(Node+135,308198373ull); /* crc32c(~1, "boot") */
        Put32(Node+143,DirOffset);
        Put32(Node+147,34);
        Put64(Node+151,257);
        Node[159]=1;
        Put64(Node+160,0);
        Put32(Node+168,FileOffset);
        Put32(Node+172,160);
        Put64(Node+176,257);
        Node[184]=108;
        Put64(Node+185,0);
        Put32(Node+193,ExtentOffset);
        Put32(Node+197,26);
        RootItem=Node+RootOffset;
        Put64(RootItem,1);
        Put32(RootItem+52,0040755u);
        Dir=Node+DirOffset;
        Put64(Dir,257);
        Dir[8]=1;
        Put64(Dir+9,0);
        Put64(Dir+17,1);
        Put16(Dir+25,0);
        Put16(Dir+27,4);
        Dir[29]=1;
        memcpy(Dir+30,"boot",4);
        FileItem=Node+FileOffset;
        Put64(FileItem,1);
        Put64(FileItem+16,5);
        Put32(FileItem+52,0100644u);
        ExtentItem=Node+ExtentOffset;
        Put64(ExtentItem,1);
        Put64(ExtentItem+8,5);
        ExtentItem[20]=0;
        memcpy(ExtentItem+21,"hello",5);
        Put32(Node,Rc95LdrVeneerCrc32c(Node+32,16384u-32u));
        memcpy(OriginalBtrfsFsNode,Node,sizeof(OriginalBtrfsFsNode));
    }

    if (Rc95LdrBtrfsBindVolume(&Btrfs,&Volume) != Rc95LdrSuccess ||
        !Volume.BlockDevice || Volume.FileSystem != NULL ||
        Volume.BlockDevice->BlockCount != 1024u ||
        Volume.BlockDevice->Write != NULL)
        return 3;

    if (Rc95LdrBtrfsLoadChunkTree(&Btrfs,Scratch,sizeof(Scratch)) !=
        Rc95LdrSuccess || !Btrfs.ChunkTreeLoaded || Btrfs.ChunkCount != 5)
        return 9;
    if (Rc95LdrBtrfsLoadRootTree(&Btrfs,Scratch,sizeof(Scratch)) !=
        Rc95LdrSuccess || !Btrfs.RootTreeLoaded ||
        Btrfs.FsTreeRoot != 0x50000 || Btrfs.FsTreeGeneration != 1 ||
        Btrfs.FsTreeLevel != 0)
        return 14;
    /* Rebuild the root tree as a multi-level tree. The root item is now
       one level below the superblock root; loading must follow child keys,
       validate child generations, and reject a repeated child reference. */
    {
        uint8_t SavedRootTree[16384];
        uint8_t *RootNode = Image + PARTITION_START * 512u + 0x20000u;
        uint8_t *RootLeaf = Image + PARTITION_START * 512u + 0x24000u;
        uint8_t *Entry;
        memcpy(SavedRootTree, RootNode, sizeof(SavedRootTree));
        memcpy(RootLeaf, SavedRootTree, sizeof(SavedRootTree));
        Put64(RootLeaf + 48u, 0x24000u);
        Put32(RootLeaf, Rc95LdrVeneerCrc32c(
            RootLeaf + 32u, 16384u - 32u));
        Btrfs.Chunks[8].Logical = 0x24000u;
        Btrfs.Chunks[8].Length = 0x4000u;
        Btrfs.Chunks[8].Physical = 0x24000u;
        Btrfs.Chunks[8].Type = 2u;
        Btrfs.Chunks[8].DeviceId = 1u;
        Btrfs.ChunkCount = 9u;
        Btrfs.Superblock.RootLevel = 1u;
        FinishBtrfsNode(RootNode, 16384u, 0x20000u, 1u, 1u, 1u, 1u,
                        Btrfs.Superblock.FsId);
        Entry = RootNode + 101u;
        Put64(Entry, 5u);
        Entry[8] = 132u;
        Put64(Entry + 9u, 0u);
        Put64(Entry + 17u, 0x24000u);
        Put64(Entry + 25u, 1u);
        Put32(RootNode, Rc95LdrVeneerCrc32c(
            RootNode + 32u, 16384u - 32u));
        if (Rc95LdrBtrfsLoadRootTree(&Btrfs, Scratch, sizeof(Scratch)) !=
                Rc95LdrSuccess || !Btrfs.RootTreeLoaded ||
            Btrfs.FsTreeRoot != 0x50000u || Btrfs.FsTreeLevel != 0u)
            return 90;

        /* A child generation inconsistent with the pointer is corruption. */
        Put64(Entry + 25u, 2u);
        Put32(RootNode, Rc95LdrVeneerCrc32c(
            RootNode + 32u, 16384u - 32u));
        if (Rc95LdrBtrfsLoadRootTree(&Btrfs, Scratch, sizeof(Scratch)) !=
                Rc95LdrCorruptData || Btrfs.RootTreeLoaded)
            return 91;

        /* Root-tree child addresses must be node-aligned. */
        Put64(Entry + 25u, 1u);
        Put64(Entry + 17u, 0x24001u);
        Put32(RootNode, Rc95LdrVeneerCrc32c(
            RootNode + 32u, 16384u - 32u));
        if (Rc95LdrBtrfsLoadRootTree(&Btrfs, Scratch, sizeof(Scratch)) !=
                Rc95LdrCorruptData || Btrfs.RootTreeLoaded)
            return 100;
        Put64(Entry + 17u, 0x24000u);

        /* Root-tree separator keys are strictly increasing as well. */
        Put32(RootNode + 96u, 2u);
        Entry = RootNode + 101u + 33u;
        Put64(Entry, 4u);
        Entry[8] = 132u;
        Put64(Entry + 9u, 0u);
        Put64(Entry + 17u, 0x24000u);
        Put64(Entry + 25u, 1u);
        Put32(RootNode, Rc95LdrVeneerCrc32c(
            RootNode + 32u, 16384u - 32u));
        if (Rc95LdrBtrfsLoadRootTree(&Btrfs, Scratch, sizeof(Scratch)) !=
                Rc95LdrCorruptData || Btrfs.RootTreeLoaded)
            return 101;

        /* Distinct separator keys may not alias the same child node. */
        Put64(Entry + 25u, 1u);
        Put32(RootNode + 96u, 2u);
        Entry = RootNode + 101u + 33u;
        Put64(Entry, 6u);
        Entry[8] = 132u;
        Put64(Entry + 9u, 0u);
        Put64(Entry + 17u, 0x24000u);
        Put64(Entry + 25u, 1u);
        Put32(RootNode, Rc95LdrVeneerCrc32c(
            RootNode + 32u, 16384u - 32u));
        if (Rc95LdrBtrfsLoadRootTree(&Btrfs, Scratch, sizeof(Scratch)) !=
                Rc95LdrCorruptData || Btrfs.RootTreeLoaded)
            return 92;

        memcpy(RootNode, SavedRootTree, sizeof(SavedRootTree));
        Btrfs.Superblock.RootLevel = 0u;
        Btrfs.ChunkCount = 5u;
        if (Rc95LdrBtrfsLoadRootTree(&Btrfs, Scratch, sizeof(Scratch)) !=
                Rc95LdrSuccess || !Btrfs.RootTreeLoaded ||
            Btrfs.FsTreeRoot != 0x50000u || Btrfs.FsTreeLevel != 0u)
            return 93;
    }

    if (Rc95LdrBtrfsInitializeFileSystem(&FsContext,&Btrfs,
            Scratch,sizeof(Scratch),FsItemBuffer,sizeof(FsItemBuffer),
            FsInlineBuffer,sizeof(FsInlineBuffer),FsStagingBuffer,
            sizeof(FsStagingBuffer),&FileSystem) !=
            Rc95LdrSuccess ||
        Rc95LdrBtrfsBindVolumeWithFileSystem(&Btrfs,&Volume,&FileSystem) !=
            Rc95LdrSuccess || Volume.FileSystem != &FileSystem)
        return 34;
    if (FileSystem.OpenRead(FileSystem.FileSystemContext,"/boot",
            &FsHandle,&FsFileSize) != Rc95LdrSuccess || FsFileSize != 5)
        return 35;
    /* The transaction workspace is mandatory at initialization: neither a
       null pointer nor a zero-capacity buffer can back nonempty ReadAt calls. */
    {
        RC95LDR_BTRFS_FILESYSTEM_CONTEXT InvalidContext;
        RC95LDR_FILESYSTEM InvalidFileSystem;
        if (Rc95LdrBtrfsInitializeFileSystem(&InvalidContext, &Btrfs,
                Scratch, sizeof(Scratch), FsItemBuffer, sizeof(FsItemBuffer),
                FsInlineBuffer, sizeof(FsInlineBuffer), NULL, 0,
                &InvalidFileSystem) != Rc95LdrInvalidParameter ||
            Rc95LdrBtrfsInitializeFileSystem(&InvalidContext, &Btrfs,
                Scratch, sizeof(Scratch), FsItemBuffer, sizeof(FsItemBuffer),
                FsInlineBuffer, sizeof(FsInlineBuffer), FsStagingBuffer, 0,
                &InvalidFileSystem) != Rc95LdrInvalidParameter)
            return 79;
    }
    memset(FsReadBuffer,0,sizeof(FsReadBuffer));
    if (FileSystem.ReadAt(FileSystem.FileSystemContext,FsHandle,0,
            FsReadBuffer,5) != Rc95LdrSuccess ||
        memcmp(FsReadBuffer,"hello",5) != 0)
        return 36;
    {
        uint8_t BoundaryByte = 0xa5u;

        /* Zero-byte reads are valid at EOF, but not beyond it. */
        if (FileSystem.ReadAt(FileSystem.FileSystemContext, FsHandle,
                FsFileSize, NULL, 0) != Rc95LdrSuccess ||
            FileSystem.ReadAt(FileSystem.FileSystemContext, FsHandle,
                FsFileSize + 1u, NULL, 0) != Rc95LdrOutOfBounds)
            return 66;

        /* Non-empty reads may end exactly at EOF, but may not cross it. */
        if (FileSystem.ReadAt(FileSystem.FileSystemContext, FsHandle,
                FsFileSize - 1u, &BoundaryByte, 1u) != Rc95LdrSuccess ||
            BoundaryByte != (uint8_t)'o')
            return 67;
        BoundaryByte = 0xa5u;
        if (FileSystem.ReadAt(FileSystem.FileSystemContext, FsHandle,
                FsFileSize - 1u, &BoundaryByte, 2u) != Rc95LdrOutOfBounds ||
            BoundaryByte != 0xa5u)
            return 68;

        if (FileSystem.ReadAt(FileSystem.FileSystemContext, NULL,
                0, &BoundaryByte, 1u) != Rc95LdrInvalidParameter)
            return 69;
    }
    if (FileSystem.OpenRead(FileSystem.FileSystemContext,"/boot",
            &FsHandle2,&FsFileSize) != Rc95LdrSuccess ||
        FileSystem.ReadAt(FileSystem.FileSystemContext,FsHandle2,1,
            FsReadBuffer,4) != Rc95LdrSuccess ||
        memcmp(FsReadBuffer,"ello",4) != 0 ||
        FileSystem.ReadAt(FileSystem.FileSystemContext,FsHandle,0,
            FsReadBuffer,5) != Rc95LdrSuccess ||
        memcmp(FsReadBuffer,"hello",5) != 0)
        return 39;
    FileSystem.Close(FileSystem.FileSystemContext,FsHandle2);
    FileSystem.Close(FileSystem.FileSystemContext,FsHandle);
    if (FileSystem.ReadAt(FileSystem.FileSystemContext,FsHandle,0,
            FsReadBuffer,1) != Rc95LdrInvalidParameter)
        return 40;
    /* Reusing the just-freed slot must not resurrect its previous token. */
    if (FileSystem.OpenRead(FileSystem.FileSystemContext,"/boot",
            &FsHandle2,&FsFileSize) != Rc95LdrSuccess ||
        FileSystem.ReadAt(FileSystem.FileSystemContext,FsHandle,0,
            FsReadBuffer,1) != Rc95LdrInvalidParameter ||
        FileSystem.ReadAt(FileSystem.FileSystemContext,FsHandle2,0,
            FsReadBuffer,1) != Rc95LdrSuccess || FsReadBuffer[0] != 'h')
        return 54;
    FileSystem.Close(FileSystem.FileSystemContext,FsHandle2);

    /* Every advertised slot is usable; exhaustion is reported without
       leaking a partially initialized handle or changing the size output. */
    for (HandleIndex = 0; HandleIndex < RC95LDR_BTRFS_FILE_HANDLES;
         ++HandleIndex)
    {
        if (FileSystem.OpenRead(FileSystem.FileSystemContext, "/boot",
                &HandleSlots[HandleIndex], &FsFileSize) != Rc95LdrSuccess ||
            !HandleSlots[HandleIndex] || FsFileSize != 5u)
            return 70;
    }
    FsHandle2 = (void *)(uintptr_t)1u;
    FsFileSize = UINT64_MAX;
    if (FileSystem.OpenRead(FileSystem.FileSystemContext, "/boot",
            &FsHandle2, &FsFileSize) != Rc95LdrOutOfMemory ||
        FsHandle2 != NULL || FsFileSize != 0u)
        return 71;
    for (HandleIndex = 0; HandleIndex < RC95LDR_BTRFS_FILE_HANDLES;
         ++HandleIndex)
        FileSystem.Close(FileSystem.FileSystemContext, HandleSlots[HandleIndex]);

    /* The generic loader-scoped provider must consume this filesystem
       without any FreeLoader loader-block structures. */
    if (Rc95LdrInitializeVolumeFileProvider(&Volume,&ProviderContext,
            &FileProvider) != Rc95LdrSuccess ||
        FileProvider.Open(FileProvider.VolumeContext,"/boot",
            &ProviderHandle) != Rc95LdrSuccess)
        return 37;
    if (FileProvider.GetFileInformation(FileProvider.VolumeContext,
            ProviderHandle, &ProviderInfo) != Rc95LdrSuccess ||
        ProviderInfo.Size != 5u)
        return 49;
    if (FileProvider.Read(FileProvider.VolumeContext,ProviderHandle,
            FsReadBuffer,2,&ProviderBytes) != Rc95LdrSuccess ||
        ProviderBytes != 2 || memcmp(FsReadBuffer,"he",2) != 0 ||
        FileProvider.Read(FileProvider.VolumeContext,ProviderHandle,
            FsReadBuffer,3,&ProviderBytes) != Rc95LdrSuccess ||
        ProviderBytes != 3 || memcmp(FsReadBuffer,"llo",3) != 0)
        return 38;
    if (FileProvider.Seek(FileProvider.VolumeContext,ProviderHandle,1u) !=
            Rc95LdrSuccess ||
        FileProvider.Read(FileProvider.VolumeContext,ProviderHandle,
            FsReadBuffer,4,&ProviderBytes) != Rc95LdrSuccess ||
        ProviderBytes != 4 || memcmp(FsReadBuffer,"ello",4) != 0 ||
        FileProvider.Seek(FileProvider.VolumeContext,ProviderHandle,5u) !=
            Rc95LdrSuccess ||
        FileProvider.Read(FileProvider.VolumeContext,ProviderHandle,
            FsReadBuffer,0,&ProviderBytes) != Rc95LdrSuccess ||
        ProviderBytes != 0 ||
        FileProvider.Seek(FileProvider.VolumeContext,ProviderHandle,6u) !=
            Rc95LdrOutOfBounds)
        return 50;
    FileProvider.Close(FileProvider.VolumeContext,ProviderHandle);
    if (FileProvider.Read(FileProvider.VolumeContext,ProviderHandle,
            FsReadBuffer,1,&ProviderBytes) != Rc95LdrInvalidParameter)
        return 51;

    if (Rc95LdrBtrfsFindFsTreeItem(&Btrfs,256,1,0,Scratch,sizeof(Scratch),
            ItemBuffer,sizeof(ItemBuffer),&ItemLength) != Rc95LdrSuccess ||
        ItemLength != sizeof(ItemBuffer) || Rc95LdrReadU64(ItemBuffer) != 1)
        return 15;
    if (Rc95LdrBtrfsReadInode(&Btrfs,256,Scratch,sizeof(Scratch),
            &RootInode) != Rc95LdrSuccess ||
        RootInode.Generation != 1 || RootInode.Mode != 0040755u ||
        (RootInode.Mode & 0170000u) != 0040000u)
        return 18;
    if (Rc95LdrBtrfsFindDirectoryEntry(&Btrfs,256,(const uint8_t *)"boot",4,
            Scratch,sizeof(Scratch),&DirectoryInode,&DirectoryType,
            &DirectoryLocationOffset) != Rc95LdrSuccess ||
        DirectoryInode != 257 || DirectoryType != 1 || DirectoryLocationOffset != 0)
        return 19;
    if (Rc95LdrBtrfsFindDirectoryEntry(&Btrfs,256,(const uint8_t *)"missing",7,
            Scratch,sizeof(Scratch),&DirectoryInode,&DirectoryType,
            &DirectoryLocationOffset) != Rc95LdrNotFound)
        return 20;
    {
        /* Directory lookup must clear caller outputs when the filesystem
           leaf cannot be read; stale inode/type values must not escape. */
        DirectoryInode = UINT64_MAX;
        DirectoryType = 0xa5u;
        DirectoryLocationOffset = UINT64_MAX;
        FailedReadLba = PARTITION_START + 0x50000u / 512u;
        FailReadEnabled = 1;
        if (Rc95LdrBtrfsFindDirectoryEntry(&Btrfs,256,
                (const uint8_t *)"boot",4,Scratch,sizeof(Scratch),
                &DirectoryInode,&DirectoryType,&DirectoryLocationOffset) !=
                Rc95LdrIoError || DirectoryInode != 0u ||
            DirectoryType != 0u || DirectoryLocationOffset != 0u)
            return 106;
        FailReadEnabled = 0;
    }
    {
        /* A later path-component miss must also leave outputs unpublished. */
        ResolvedInodeId = UINT64_MAX;
        memset(&ResolvedInode, 0xa5, sizeof(ResolvedInode));
        if (Rc95LdrBtrfsResolvePath(&Btrfs, 256,
                (const uint8_t *)"/missing", 8, Scratch, sizeof(Scratch),
                &ResolvedInodeId, &ResolvedInode) != Rc95LdrNotFound ||
            ResolvedInodeId != 0u)
            return 108;
        {
            const uint8_t *Bytes = (const uint8_t *)&ResolvedInode;
            size_t I;
            for (I = 0; I < sizeof(ResolvedInode); ++I)
                if (Bytes[I] != 0u)
                    return 108;
        }
    }
    {
        /* Path resolution must not leak a previous result if its initial
           inode/tree read fails. The resolver publishes outputs only after
           the entire path has been resolved successfully. */
        ResolvedInodeId = UINT64_MAX;
        memset(&ResolvedInode, 0xa5, sizeof(ResolvedInode));
        FailedReadLba = PARTITION_START + 0x50000u / 512u;
        FailReadEnabled = 1;
        if (Rc95LdrBtrfsResolvePath(&Btrfs, 256,
                (const uint8_t *)"/boot", 5, Scratch, sizeof(Scratch),
                &ResolvedInodeId, &ResolvedInode) != Rc95LdrIoError ||
            ResolvedInodeId != 0u)
            return 107;
        {
            const uint8_t *Bytes = (const uint8_t *)&ResolvedInode;
            size_t I;
            for (I = 0; I < sizeof(ResolvedInode); ++I)
                if (Bytes[I] != 0u)
                    return 107;
        }
        FailReadEnabled = 0;
    }
    {
        uint8_t *FsNode = Image + PARTITION_START * 512u + 0x50000u;
        uint32_t DirOffset = 16384u - 160u - 34u;
        uint16_t SavedDataLength = (uint16_t)(FsNode[DirOffset + 25u] |
            ((uint16_t)FsNode[DirOffset + 26u] << 8));
        Put16(FsNode + DirOffset + 25u, 0xffffu);
        Put32(FsNode, Rc95LdrVeneerCrc32c(FsNode + 32u, 16384u - 32u));
        if (Rc95LdrBtrfsFindDirectoryEntry(&Btrfs,256,
                (const uint8_t *)"boot",4,Scratch,sizeof(Scratch),
                &DirectoryInode,&DirectoryType,&DirectoryLocationOffset) !=
                Rc95LdrCorruptData)
            return 58;
        Put16(FsNode + DirOffset + 25u, SavedDataLength);
        Put32(FsNode, Rc95LdrVeneerCrc32c(FsNode + 32u, 16384u - 32u));
    }
    {
        uint8_t *FsNode = Image + PARTITION_START * 512u + 0x50000u;
        uint32_t DirOffset = 16384u - 160u - 34u;
        uint8_t SavedLocationType = FsNode[DirOffset + 8u];
        uint8_t SavedEntryType = FsNode[DirOffset + 29u];

        /* A directory entry that points at a subvolume root is valid Btrfs
           metadata, but this inode-only resolver must fail closed. */
        FsNode[DirOffset + 8u] = 132u; /* BTRFS_ROOT_ITEM_KEY */
        Put32(FsNode, Rc95LdrVeneerCrc32c(FsNode + 32u, 16384u - 32u));
        if (Rc95LdrBtrfsFindDirectoryEntry(&Btrfs, 256,
                (const uint8_t *)"boot", 4, Scratch, sizeof(Scratch),
                &DirectoryInode, &DirectoryType,
                &DirectoryLocationOffset) != Rc95LdrUnsupported)
            return 62;
        FsNode[DirOffset + 8u] = SavedLocationType;

        /* DT_UNKNOWN and valid special-file types are not corrupt records;
           they are simply outside the supported boot-file path contract. */
        FsNode[DirOffset + 29u] = 0u;
        Put32(FsNode, Rc95LdrVeneerCrc32c(FsNode + 32u, 16384u - 32u));
        if (Rc95LdrBtrfsFindDirectoryEntry(&Btrfs, 256,
                (const uint8_t *)"boot", 4, Scratch, sizeof(Scratch),
                &DirectoryInode, &DirectoryType,
                &DirectoryLocationOffset) != Rc95LdrSuccess ||
            Rc95LdrBtrfsResolvePath(&Btrfs, 256,
                (const uint8_t *)"/boot", 5, Scratch, sizeof(Scratch),
                &ResolvedInodeId, &ResolvedInode) != Rc95LdrUnsupported)
            return 63;

        FsNode[DirOffset + 29u] = 3u; /* character device */
        Put32(FsNode, Rc95LdrVeneerCrc32c(FsNode + 32u, 16384u - 32u));
        if (Rc95LdrBtrfsResolvePath(&Btrfs, 256,
                (const uint8_t *)"/boot", 5, Scratch, sizeof(Scratch),
                &ResolvedInodeId, &ResolvedInode) != Rc95LdrUnsupported)
            return 64;

        /* A mismatch between two supported types is genuine metadata
           inconsistency, not an unsupported-feature result. */
        FsNode[DirOffset + 29u] = 2u; /* says directory; inode is regular */
        Put32(FsNode, Rc95LdrVeneerCrc32c(FsNode + 32u, 16384u - 32u));
        if (Rc95LdrBtrfsResolvePath(&Btrfs, 256,
                (const uint8_t *)"/boot", 5, Scratch, sizeof(Scratch),
                &ResolvedInodeId, &ResolvedInode) != Rc95LdrCorruptData)
            return 65;

        FsNode[DirOffset + 8u] = SavedLocationType;
        FsNode[DirOffset + 29u] = SavedEntryType;
        Put32(FsNode, Rc95LdrVeneerCrc32c(FsNode + 32u, 16384u - 32u));
    }
    if (Rc95LdrBtrfsResolvePath(&Btrfs,256,(const uint8_t *)"/boot",5,
            Scratch,sizeof(Scratch),&ResolvedInodeId,&ResolvedInode) !=
            Rc95LdrSuccess || ResolvedInodeId != 257 ||
        (ResolvedInode.Mode & 0170000u) != 0100000u)
        return 21;
    if (Rc95LdrBtrfsResolvePath(&Btrfs,256,(const uint8_t *)"../boot",7,
            Scratch,sizeof(Scratch),&ResolvedInodeId,&ResolvedInode) !=
            Rc95LdrSuccess || ResolvedInodeId != 257)
        return 22;
    if (Rc95LdrBtrfsResolvePath(&Btrfs,256,(const uint8_t *)"./boot",6,
            Scratch,sizeof(Scratch),&ResolvedInodeId,&ResolvedInode) !=
            Rc95LdrSuccess || ResolvedInodeId != 257)
        return 55;
    if (Rc95LdrBtrfsResolvePath(&Btrfs,256,(const uint8_t *)"/boot/",6,
            Scratch,sizeof(Scratch),&ResolvedInodeId,&ResolvedInode) !=
            Rc95LdrNotFound)
        return 56;
    if (Rc95LdrBtrfsResolvePath(&Btrfs,256,(const uint8_t *)"boot/.",6,
            Scratch,sizeof(Scratch),&ResolvedInodeId,&ResolvedInode) !=
            Rc95LdrNotFound ||
        Rc95LdrBtrfsResolvePath(&Btrfs,256,(const uint8_t *)"boot/..",7,
            Scratch,sizeof(Scratch),&ResolvedInodeId,&ResolvedInode) !=
            Rc95LdrNotFound)
        return 61;
    {
        static const uint8_t EmbeddedNul[] = {'b','o','o','t',0,'x'};
        if (Rc95LdrBtrfsResolvePath(&Btrfs,256,EmbeddedNul,
                sizeof(EmbeddedNul),Scratch,sizeof(Scratch),
                &ResolvedInodeId,&ResolvedInode) != Rc95LdrInvalidParameter)
            return 57;
    }
    if (Rc95LdrBtrfsFindFsTreeItem(&Btrfs,257,1,0,Scratch,sizeof(Scratch),
            ItemBuffer,sizeof(ItemBuffer),&ItemLength) != Rc95LdrNotFound)
        return 16;
    if (Rc95LdrBtrfsReadFileExtent(&Btrfs,257,0,Scratch,sizeof(Scratch),
            &FileExtent,ExtentItemBuffer,sizeof(ExtentItemBuffer),
            InlineData,sizeof(InlineData)) != Rc95LdrSuccess ||
        FileExtent.Type != 0 || FileExtent.InlineLength != 5 ||
        memcmp(InlineData,"hello",5) != 0)
        return 23;
    /* Successor lookup must terminate cleanly at the end of the
       matching key range; it must not confuse a missing successor with a
       malformed leaf or walk outside the bounded tree. */
    if (Rc95LdrBtrfsFindFsTreeItemAfter(&Btrfs,257,108,0,
            Scratch,sizeof(Scratch),ExtentItemBuffer,sizeof(ExtentItemBuffer),
            &ItemLength,&DirectoryLocationOffset) != Rc95LdrNotFound)
        return 33;
    if (Rc95LdrBtrfsReadFileExtent(&Btrfs,257,3,Scratch,sizeof(Scratch),
            &FileExtent,ExtentItemBuffer,sizeof(ExtentItemBuffer),
            InlineData,sizeof(InlineData)) != Rc95LdrSuccess ||
        FileExtent.FileOffset != 0 || FileExtent.InlineLength != 5)
        return 26;
    if (Rc95LdrBtrfsFindFsTreeItemAtOrBefore(&Btrfs,257,108,3,
            Scratch,sizeof(Scratch),ExtentItemBuffer,sizeof(ExtentItemBuffer),
            &ItemLength,&DirectoryLocationOffset) != Rc95LdrSuccess ||
        DirectoryLocationOffset != 0 || ItemLength != 26)
        return 27;
    if (Rc95LdrBtrfsReadFileExtent(&Btrfs,257,0,Scratch,sizeof(Scratch),
            &FileExtent,ExtentItemBuffer,sizeof(ExtentItemBuffer),
            InlineData,2) != Rc95LdrBufferTooSmall)
        return 24;
    {
        uint8_t *FsNode = Image + PARTITION_START * 512u + 0x50000u;
        uint32_t ExtentOffset = 16384u - 160u - 34u - 160u - 26u;
        FsNode[ExtentOffset + 16u] = 1; /* compressed inline extent */
        Put32(FsNode,Rc95LdrVeneerCrc32c(FsNode+32,16384u-32u));
        if (Rc95LdrBtrfsReadFileExtent(&Btrfs,257,0,Scratch,sizeof(Scratch),
                &FileExtent,ExtentItemBuffer,sizeof(ExtentItemBuffer),
                InlineData,sizeof(InlineData)) != Rc95LdrUnsupported)
            return 25;
        FsNode[ExtentOffset + 16u] = 0;
        Put32(FsNode,Rc95LdrVeneerCrc32c(FsNode+32,16384u-32u));
    }
    if (Rc95LdrBtrfsFindFsTreeItem(&Btrfs,256,1,0,Scratch,sizeof(Scratch),
            ItemBuffer,32,&ItemLength) != Rc95LdrBufferTooSmall ||
        ItemLength != sizeof(ItemBuffer))
        return 17;

    /*
     * Exercise the extent data path independently of B-tree item decoding:
     * regular extents must use the logical chunk map, while preallocated
     * extents synthesize zeroes without touching the block device.
     */
    {
        RC95LDR_BTRFS_FILE_EXTENT DataExtent;
        uint8_t Data[32];
        uint8_t *DiskData = Image + PARTITION_START * 512u + 0x40200u;
        size_t I;

        for (I = 0; I < 512u; ++I)
            DiskData[I] = (uint8_t)(I ^ 0x5au);
        memset(&DataExtent, 0, sizeof(DataExtent));
        DataExtent.Type = 1u;
        DataExtent.FileOffset = 0;
        DataExtent.DiskBytenr = 0x40200u;
        DataExtent.DiskNumBytes = 512u;
        DataExtent.NumBytes = 512u;
        memset(Data, 0, sizeof(Data));
        if (Rc95LdrBtrfsReadExtentData(&Btrfs, &DataExtent, 7u,
                sizeof(Data), Scratch, sizeof(Scratch), Data) !=
                Rc95LdrSuccess)
            return 28;
        for (I = 0; I < sizeof(Data); ++I)
            if (Data[I] != (uint8_t)((I + 7u) ^ 0x5au))
                return 29;

        memset(&DataExtent, 0, sizeof(DataExtent));
        DataExtent.Type = 2u;
        DataExtent.FileOffset = 100u;
        DataExtent.NumBytes = sizeof(Data);
        memset(Data, 0xa5, sizeof(Data));
        if (Rc95LdrBtrfsReadExtentData(&Btrfs, &DataExtent, 100u,
                sizeof(Data), Scratch, sizeof(Scratch), Data) !=
                Rc95LdrSuccess)
            return 30;
        for (I = 0; I < sizeof(Data); ++I)
            if (Data[I] != 0)
                return 31;
        if (Rc95LdrBtrfsReadExtentData(&Btrfs, &DataExtent, 99u, 1u,
                Scratch, sizeof(Scratch), Data) != Rc95LdrOutOfBounds)
            return 32;

        /* Reject malformed disk-range metadata before issuing block reads. */
        DataExtent.Type = 1u;
        DataExtent.FileOffset = 0u;
        DataExtent.NumBytes = 16u;
        DataExtent.DiskBytenr = 0x40200u;
        DataExtent.DiskNumBytes = 512u;
        DataExtent.ExtentOffset = 513u;
        if (Rc95LdrBtrfsReadExtentData(&Btrfs, &DataExtent, 0u, 1u,
                Scratch, sizeof(Scratch), Data) != Rc95LdrCorruptData)
            return 64;

        DataExtent.ExtentOffset = 500u;
        DataExtent.DiskNumBytes = 512u;
        if (Rc95LdrBtrfsReadExtentData(&Btrfs, &DataExtent, 0u, 13u,
                Scratch, sizeof(Scratch), Data) != Rc95LdrOutOfBounds)
            return 65;

        DataExtent.ExtentOffset = 16u;
        DataExtent.DiskBytenr = UINT64_MAX - 7u;
        if (Rc95LdrBtrfsReadExtentData(&Btrfs, &DataExtent, 0u, 1u,
                Scratch, sizeof(Scratch), Data) != Rc95LdrIntegerOverflow)
            return 66;

        DataExtent.DiskBytenr = 0x40200u;
        DataExtent.ExtentOffset = 0u;
        DataExtent.DiskNumBytes = 8u;
        DataExtent.NumBytes = 16u;
        if (Rc95LdrBtrfsReadExtentData(&Btrfs, &DataExtent, 0u, 9u,
                Scratch, sizeof(Scratch), Data) != Rc95LdrOutOfBounds)
            return 67;
    }

    {
        uint64_t Physical = 0;
        if (Rc95LdrBtrfsMapLogical(&Btrfs,0x40010,4096,&Physical) !=
            Rc95LdrSuccess || Physical != 0x40010)
            return 10;
        if (Rc95LdrBtrfsMapLogical(&Btrfs,0x50000,4096,&Physical) !=
            Rc95LdrNotFound)
            return 11;
    }
    {
        uint8_t *Node = Image + PARTITION_START * 512u + 0x30000u;
        Node[0] ^= 0x01u;
        if (Rc95LdrBtrfsLoadChunkTree(&Btrfs,Scratch,sizeof(Scratch)) !=
            Rc95LdrCorruptData || Btrfs.ChunkTreeLoaded)
            return 12;
        Node[0] ^= 0x01u;
        Put32(Node,Rc95LdrVeneerCrc32c(Node+32,16384u-32u));
        if (Rc95LdrBtrfsLoadChunkTree(&Btrfs,Scratch,sizeof(Scratch)) !=
            Rc95LdrSuccess)
            return 13;
    }

    /* Exposed LBAs are partition-relative, not physical-disk LBAs. */
    if (Rc95LdrReadBlocks(Volume.BlockDevice,0,1,ReadBuffer,sizeof(ReadBuffer)) !=
        Rc95LdrSuccess || ReadBuffer[0] != 0x5a)
        return 4;
    if (Rc95LdrReadBlocks(Volume.BlockDevice,1024,1,ReadBuffer,sizeof(ReadBuffer)) !=
        Rc95LdrOutOfBounds)
        return 5;
    if (Writes != 0)
        return 6;

    /* The mounted descriptor is snapshotted; caller callback edits do not
       redirect the volume's reads. */
    Device.Read = NULL;
    if (Rc95LdrReadBlocks(Volume.BlockDevice,0,1,ReadBuffer,sizeof(ReadBuffer)) !=
        Rc95LdrSuccess || ReadBuffer[0] != 0x5a)
        return 7;


    /*
     * Replace the one-leaf fixture with a three-node filesystem tree. The
     * first extent is in the left leaf; subsequent extents are in the right
     * leaf. This verifies successor lookup across a leaf boundary and a
     * single ReadAt spanning inline, regular, sparse and preallocated ranges.
     */
    {
        uint8_t *Root = Image + PARTITION_START * 512u + 0x50000u;
        uint8_t *Left = Image + PARTITION_START * 512u + 0x54000u;
        uint8_t *Right = Image + PARTITION_START * 512u + 0x58000u;
        uint8_t *Internal = Image + PARTITION_START * 512u + 0x5c000u;
        uint8_t MultiRead[32];
        uint8_t *DiskData = Image + PARTITION_START * 512u + 0x40200u;
        uint8_t *OriginalFsNode =
            Image + PARTITION_START * 512u + 0x50000u;
        size_t I;
        uint64_t NextOffset = 0;
        size_t NextLength = 0;
        uint32_t SavedDataOffset;

        /* Increase the on-disk inode size before opening the independent
           handle; the handle token is opaque and never exposes slot storage. */
        Put64(OriginalFsNode + (16384u - 160u - 34u - 160u) + 16u, 30u);
        Put32(OriginalFsNode, Rc95LdrVeneerCrc32c(
            OriginalFsNode + 32u, 16384u - 32u));
        if (FileSystem.OpenRead(FileSystem.FileSystemContext, "/boot",
                &FsHandle, &FsFileSize) != Rc95LdrSuccess || FsFileSize != 30)
            return 41;

        Btrfs.Chunks[5].Logical = 0x54000u;
        Btrfs.Chunks[5].Length = 0x4000u;
        Btrfs.Chunks[5].Physical = 0x54000u;
        Btrfs.Chunks[5].Type = 2u;
        Btrfs.Chunks[5].DeviceId = 1u;
        Btrfs.Chunks[6].Logical = 0x58000u;
        Btrfs.Chunks[6].Length = 0x4000u;
        Btrfs.Chunks[6].Physical = 0x58000u;
        Btrfs.Chunks[6].Type = 2u;
        Btrfs.Chunks[6].DeviceId = 1u;
        Btrfs.Chunks[7].Logical = 0x5c000u;
        Btrfs.Chunks[7].Length = 0x4000u;
        Btrfs.Chunks[7].Physical = 0x5c000u;
        Btrfs.Chunks[7].Type = 2u;
        Btrfs.Chunks[7].DeviceId = 1u;
        Btrfs.ChunkCount = 8u;

        MakeExtentRoot(Root, Btrfs.Superblock.FsId);
        MakeExtentInternal(Internal, Btrfs.Superblock.FsId);
        Btrfs.FsTreeRoot = 0x50000u;
        Btrfs.FsTreeGeneration = 1u;
        Btrfs.FsTreeLevel = 2u;
        MakeExtentLeaf(Left, 0x54000u, Btrfs.Superblock.FsId, 0);
        MakeExtentLeaf(Right, 0x58000u, Btrfs.Superblock.FsId, 1);
        for (I = 0; I < 512u; ++I)
            DiskData[I] = (uint8_t)(I ^ 0x5au);

        if (Rc95LdrBtrfsFindFsTreeItemAfter(&Btrfs, 257, 108, 5,
                Scratch, sizeof(Scratch), ExtentItemBuffer,
                sizeof(ExtentItemBuffer), &NextLength, &NextOffset) !=
                Rc95LdrSuccess || NextOffset != 7u || NextLength != 24u)
            return 42;
        /* Exact-key and predecessor searches must descend through both
           internal levels and select the correct leaf by its separator key. */
        if (Rc95LdrBtrfsFindFsTreeItem(&Btrfs, 257, 108, 5,
                Scratch, sizeof(Scratch), ExtentItemBuffer,
                sizeof(ExtentItemBuffer), &NextLength) != Rc95LdrSuccess ||
            NextLength != 24u)
            return 84;
        if (Rc95LdrBtrfsFindFsTreeItem(&Btrfs, 257, 108, 10,
                Scratch, sizeof(Scratch), ExtentItemBuffer,
                sizeof(ExtentItemBuffer), &NextLength) != Rc95LdrSuccess ||
            NextLength != 53u)
            return 85;
        if (Rc95LdrBtrfsFindFsTreeItem(&Btrfs, 257, 108, 20,
                Scratch, sizeof(Scratch), ExtentItemBuffer,
                sizeof(ExtentItemBuffer), &NextLength) != Rc95LdrSuccess ||
            NextLength != 53u)
            return 86;
        if (Rc95LdrBtrfsFindFsTreeItemAtOrBefore(&Btrfs, 257, 108, 15,
                Scratch, sizeof(Scratch), ExtentItemBuffer,
                sizeof(ExtentItemBuffer), &NextLength, &NextOffset) !=
                Rc95LdrSuccess || NextOffset != 10u || NextLength != 53u)
            return 87;
        if (Rc95LdrBtrfsFindFsTreeItemAfter(&Btrfs, 257, 108, 7,
                Scratch, sizeof(Scratch), ExtentItemBuffer,
                sizeof(ExtentItemBuffer), &NextLength, &NextOffset) !=
                Rc95LdrSuccess || NextOffset != 10u || NextLength != 53u)
            return 88;
        if (Rc95LdrBtrfsFindFsTreeItemAfter(&Btrfs, 257, 108, 10,
                Scratch, sizeof(Scratch), ExtentItemBuffer,
                sizeof(ExtentItemBuffer), &NextLength, &NextOffset) !=
                Rc95LdrSuccess || NextOffset != 20u || NextLength != 53u)
            return 89;
        /* Child generations are authenticated against the selected root
           generation at every internal level. */
        Put64(Internal + 101u + 25u, 2u);
        Put32(Internal, Rc95LdrVeneerCrc32c(
            Internal + 32u, 16384u - 32u));
        if (Rc95LdrBtrfsFindFsTreeItem(&Btrfs, 257, 108, 5,
                Scratch, sizeof(Scratch), ExtentItemBuffer,
                sizeof(ExtentItemBuffer), &NextLength) != Rc95LdrCorruptData)
            return 94;
        Put64(Internal + 101u + 25u, 1u);

        /* Internal separator keys must be strictly increasing. */
        Put64(Internal + 101u + 33u + 9u, 4u);
        Put32(Internal, Rc95LdrVeneerCrc32c(
            Internal + 32u, 16384u - 32u));
        if (Rc95LdrBtrfsFindFsTreeItem(&Btrfs, 257, 108, 10,
                Scratch, sizeof(Scratch), ExtentItemBuffer,
                sizeof(ExtentItemBuffer), &NextLength) != Rc95LdrCorruptData)
            return 95;
        Put64(Internal + 101u + 33u + 9u, 10u);

        /* A repeated leaf pointer is rejected during full successor scans;
           it must not cause duplicate keys to be silently accepted. */
        Put64(Internal + 101u + 33u + 17u, 0x54000u);
        Put32(Internal, Rc95LdrVeneerCrc32c(
            Internal + 32u, 16384u - 32u));
        if (Rc95LdrBtrfsFindFsTreeItemAfter(&Btrfs, 257, 108, 5,
                Scratch, sizeof(Scratch), ExtentItemBuffer,
                sizeof(ExtentItemBuffer), &NextLength, &NextOffset) !=
                Rc95LdrCorruptData)
            return 96;
        Put64(Internal + 101u + 33u + 17u, 0x58000u);
        Put32(Internal, Rc95LdrVeneerCrc32c(
            Internal + 32u, 16384u - 32u));

        /* A later-leaf I/O failure must override an earlier candidate and
           leave successor outputs unpublished. The traversal is a complete
           ordered scan, so it cannot return a partial best match. */
        FailedReadLba = PARTITION_START + 0x58000u / 512u;
        FailReadEnabled = 1;
        NextOffset = UINT64_MAX;
        NextLength = SIZE_MAX;
        memset(ExtentItemBuffer, 0xa5, sizeof(ExtentItemBuffer));
        if (Rc95LdrBtrfsFindFsTreeItemAfter(&Btrfs, 257, 108, 5,
                Scratch, sizeof(Scratch), ExtentItemBuffer,
                sizeof(ExtentItemBuffer), &NextLength, &NextOffset) !=
                Rc95LdrIoError || NextOffset != 0u || NextLength != 0u)
            return 100;
        for (I = 0; I < sizeof(ExtentItemBuffer); ++I)
            if (ExtentItemBuffer[I] != 0xa5u)
                return 101;
        FailReadEnabled = 0;

        /* A corrupt later leaf must also invalidate an earlier candidate.
           Checksum validation is part of the complete traversal contract. */
        Right[0] ^= 0x01u;
        NextOffset = UINT64_MAX;
        NextLength = SIZE_MAX;
        memset(ExtentItemBuffer, 0xa5, sizeof(ExtentItemBuffer));
        if (Rc95LdrBtrfsFindFsTreeItemAfter(&Btrfs, 257, 108, 5,
                Scratch, sizeof(Scratch), ExtentItemBuffer,
                sizeof(ExtentItemBuffer), &NextLength, &NextOffset) !=
                Rc95LdrCorruptData || NextOffset != 0u || NextLength != 0u)
            return 102;
        for (I = 0; I < sizeof(ExtentItemBuffer); ++I)
            if (ExtentItemBuffer[I] != 0xa5u)
                return 103;
        Right[0] ^= 0x01u;

        /* Failure to read the internal index node must not publish a match
           from a partially traversed tree either. */
        FailedReadLba = PARTITION_START + 0x5c000u / 512u;
        FailReadEnabled = 1;
        NextOffset = UINT64_MAX;
        NextLength = SIZE_MAX;
        memset(ExtentItemBuffer, 0xa5, sizeof(ExtentItemBuffer));
        if (Rc95LdrBtrfsFindFsTreeItemAfter(&Btrfs, 257, 108, 5,
                Scratch, sizeof(Scratch), ExtentItemBuffer,
                sizeof(ExtentItemBuffer), &NextLength, &NextOffset) !=
                Rc95LdrIoError || NextOffset != 0u || NextLength != 0u)
            return 104;
        for (I = 0; I < sizeof(ExtentItemBuffer); ++I)
            if (ExtentItemBuffer[I] != 0xa5u)
                return 105;
        FailReadEnabled = 0;

        /* Reject levels beyond the implementation's bounded depth. */
        Root[100u] = 9u;
        Put32(Root, Rc95LdrVeneerCrc32c(
            Root + 32u, 16384u - 32u));
        if (Rc95LdrBtrfsFindFsTreeItem(&Btrfs, 257, 108, 5,
                Scratch, sizeof(Scratch), ExtentItemBuffer,
                sizeof(ExtentItemBuffer), &NextLength) != Rc95LdrCorruptData)
            return 97;
        Root[100u] = 2u;
        Put32(Root, Rc95LdrVeneerCrc32c(
            Root + 32u, 16384u - 32u));

        /* Child bytenrs must be node-aligned and child generations nonzero. */
        Put64(Internal + 101u + 17u, 0x54001u);
        Put32(Internal, Rc95LdrVeneerCrc32c(
            Internal + 32u, 16384u - 32u));
        if (Rc95LdrBtrfsFindFsTreeItem(&Btrfs, 257, 108, 5,
                Scratch, sizeof(Scratch), ExtentItemBuffer,
                sizeof(ExtentItemBuffer), &NextLength) != Rc95LdrCorruptData)
            return 98;
        Put64(Internal + 101u + 17u, 0x54000u);
        Put64(Internal + 101u + 25u, 0u);
        Put32(Internal, Rc95LdrVeneerCrc32c(
            Internal + 32u, 16384u - 32u));
        if (Rc95LdrBtrfsFindFsTreeItem(&Btrfs, 257, 108, 5,
                Scratch, sizeof(Scratch), ExtentItemBuffer,
                sizeof(ExtentItemBuffer), &NextLength) != Rc95LdrCorruptData)
            return 99;
        Put64(Internal + 101u + 25u, 1u);
        Put32(Internal, Rc95LdrVeneerCrc32c(
            Internal + 32u, 16384u - 32u));
        if (Rc95LdrBtrfsFindFsTreeItemAfter(&Btrfs, 257, 108, 5,
                Scratch, sizeof(Scratch), ExtentItemBuffer, 20u,
                &NextLength, &NextOffset) != Rc95LdrBufferTooSmall ||
            NextOffset != 7u || NextLength != 24u)
            return 48;

        /* Successor lookup must clear output metadata on an exhausted key
           range and reject undersized node scratch before walking the tree. */
        NextOffset = UINT64_MAX;
        NextLength = SIZE_MAX;
        if (Rc95LdrBtrfsFindFsTreeItemAfter(&Btrfs, 257, 108, 20u,
                Scratch, sizeof(Scratch), ExtentItemBuffer,
                sizeof(ExtentItemBuffer), &NextLength, &NextOffset) !=
                Rc95LdrNotFound || NextOffset != 0u || NextLength != 0u)
            return 82;
        NextOffset = UINT64_MAX;
        NextLength = SIZE_MAX;
        if (Rc95LdrBtrfsFindFsTreeItemAfter(&Btrfs, 257, 108, 5u,
                Scratch, sizeof(Scratch) - 1u, ExtentItemBuffer,
                sizeof(ExtentItemBuffer), &NextLength, &NextOffset) !=
                Rc95LdrBufferTooSmall || NextOffset != 0u || NextLength != 0u)
            return 83;

        /* A valid gap is NotFound, while a leaf payload outside the node
           remains CorruptData rather than being mistaken for a sparse hole. */
        if (Rc95LdrBtrfsReadFileExtent(&Btrfs, 257, 6, Scratch,
                sizeof(Scratch), &FileExtent, ExtentItemBuffer,
                sizeof(ExtentItemBuffer), InlineData, sizeof(InlineData)) !=
                Rc95LdrNotFound)
            return 52;
        SavedDataOffset = Rc95LdrReadU32(Left + 118u);
        Put32(Left + 118u, 16383u);
        Put32(Left, Rc95LdrVeneerCrc32c(Left + 32u, 16384u - 32u));
        if (Rc95LdrBtrfsReadFileExtent(&Btrfs, 257, 5, Scratch,
                sizeof(Scratch), &FileExtent, ExtentItemBuffer,
                sizeof(ExtentItemBuffer), InlineData, sizeof(InlineData)) !=
                Rc95LdrCorruptData)
            return 53;
        Put32(Left + 118u, SavedDataOffset);
        Put32(Left, Rc95LdrVeneerCrc32c(Left + 32u, 16384u - 32u));

        if (Rc95LdrBtrfsReadFileExtent(&Btrfs, 257, 20u, Scratch,
                sizeof(Scratch), &FileExtent, ExtentItemBuffer,
                sizeof(ExtentItemBuffer), InlineData, sizeof(InlineData)) !=
                Rc95LdrSuccess || FileExtent.Type != 2u ||
            Rc95LdrBtrfsReadExtentData(&Btrfs, &FileExtent, 20u, 5u,
                Scratch, sizeof(Scratch), FsReadBuffer) != Rc95LdrSuccess)
            return 59;
        for (I = 0; I < 5u; ++I)
            if (FsReadBuffer[I] != 0)
                return 60; /* preallocated extents read as logical zeroes */

        /* Partial reads must begin at the requested byte within an inline
           extent and continue at the correct disk offset in the next extent. */
        /* Fail the regular-extent sector after staging the inline prefix.
           The caller's destination must remain unchanged on failure. */
        memset(MultiRead, 0xa5, sizeof(MultiRead));
        FailedReadLba = PARTITION_START + (0x40200u / 512u);
        FailReadEnabled = 1;
        if (FileSystem.ReadAt(FileSystem.FileSystemContext, FsHandle, 8u,
                MultiRead, 7u) != Rc95LdrIoError)
            return 68;
        for (I = 0; I < 7u; ++I)
            if (MultiRead[I] != 0xa5u)
                return 69;
        FailReadEnabled = 0;

        /* A request exactly equal to staging capacity is permitted. */
        FsContext.StagingBufferLength = 7u;
        if (FileSystem.ReadAt(FileSystem.FileSystemContext, FsHandle, 8u,
                MultiRead, 7u) != Rc95LdrSuccess ||
            MultiRead[0] != (uint8_t)'l' ||
            MultiRead[1] != (uint8_t)'o')
            return 72;

        /* Oversized transactions are rejected before touching destination. */
        memset(MultiRead, 0xa5, sizeof(MultiRead));
        FsContext.StagingBufferLength = 6u;
        if (FileSystem.ReadAt(FileSystem.FileSystemContext, FsHandle, 8u,
                MultiRead, 7u) != Rc95LdrBufferTooSmall)
            return 70;
        for (I = 0; I < 7u; ++I)
            if (MultiRead[I] != 0xa5u)
                return 71;
        FsContext.StagingBufferLength = sizeof(FsStagingBuffer);

        memset(MultiRead, 0xa5, sizeof(MultiRead));
        if (FileSystem.ReadAt(FileSystem.FileSystemContext, FsHandle, 8u,
                MultiRead, 7u) != Rc95LdrSuccess ||
            MultiRead[0] != (uint8_t)'l' ||
            MultiRead[1] != (uint8_t)'o')
            return 61;
        for (I = 0; I < 5u; ++I)
            if (MultiRead[2u + I] != (uint8_t)(I ^ 0x5au))
                return 62; /* starts at the third byte of regular extent */

        /* The read has already staged the leading hole, inline bytes,
           and regular extent when it asks the B-tree successor lookup for
           the next extent at file offset 15. Corrupt the successor leaf's
           identity (while keeping its checksum valid): the lookup must
           fail closed and the caller destination must remain untouched. */
        {
            uint64_t SavedBytenr = Rc95LdrReadU64(Right + 48u);
            Put64(Right + 48u, 0x5c000u);
            Put32(Right, Rc95LdrVeneerCrc32c(
                Right + 32u, 16384u - 32u));
            memset(MultiRead, 0xa5, sizeof(MultiRead));
            if (FileSystem.ReadAt(FileSystem.FileSystemContext, FsHandle,
                    0u, MultiRead, 20u) != Rc95LdrCorruptData)
                return 73;
            for (I = 0; I < 20u; ++I)
                if (MultiRead[I] != 0xa5u)
                    return 74;
            Put64(Right + 48u, SavedBytenr);
            Put32(Right, Rc95LdrVeneerCrc32c(
                Right + 32u, 16384u - 32u));
        }

        /* Corrupt the generation in the regular extent that follows
           already-staged sparse/inline data. The extent decoder must reject
           the mismatched generation without committing the staged prefix. */
        {
            uint32_t PayloadOffset = Rc95LdrReadU32(Right + 118u);
            uint64_t SavedGeneration = Rc95LdrReadU64(Right + PayloadOffset);
            Put64(Right + PayloadOffset, 2u);
            Put32(Right, Rc95LdrVeneerCrc32c(
                Right + 32u, 16384u - 32u));
            memset(MultiRead, 0xa5, sizeof(MultiRead));
            if (FileSystem.ReadAt(FileSystem.FileSystemContext, FsHandle,
                    0u, MultiRead, 20u) != Rc95LdrCorruptData)
                return 80;
            for (I = 0; I < 20u; ++I)
                if (MultiRead[I] != 0xa5u)
                    return 81;
            Put64(Right + PayloadOffset, SavedGeneration);
            Put32(Right, Rc95LdrVeneerCrc32c(
                Right + 32u, 16384u - 32u));
        }

        /* A malformed payload range in the successor leaf is discovered
           only after the read has staged its preceding hole and extents.
           Preserve the destination even though the leaf checksum is valid. */
        {
            uint32_t SavedOffset = Rc95LdrReadU32(Right + 143u);
            Put32(Right + 143u, 16383u);
            Put32(Right, Rc95LdrVeneerCrc32c(
                Right + 32u, 16384u - 32u));
            memset(MultiRead, 0xa5, sizeof(MultiRead));
            if (FileSystem.ReadAt(FileSystem.FileSystemContext, FsHandle,
                    0u, MultiRead, 20u) != Rc95LdrCorruptData)
                return 75;
            for (I = 0; I < 20u; ++I)
                if (MultiRead[I] != 0xa5u)
                    return 76;
            Put32(Right + 143u, SavedOffset);
            Put32(Right, Rc95LdrVeneerCrc32c(
                Right + 32u, 16384u - 32u));
        }

        /* Fail the successor leaf's block read itself. This distinguishes
           device-I/O propagation from malformed-node validation and proves
           that the staged prefix is still not externally visible. */
        memset(MultiRead, 0xa5, sizeof(MultiRead));
        FailedReadLba = PARTITION_START + (0x58000u / 512u);
        FailReadEnabled = 1;
        if (FileSystem.ReadAt(FileSystem.FileSystemContext, FsHandle,
                0u, MultiRead, 20u) != Rc95LdrIoError)
            return 77;
        for (I = 0; I < 20u; ++I)
            if (MultiRead[I] != 0xa5u)
                return 78;
        FailReadEnabled = 0;

        /* A partial read wholly inside the regular extent must honor its
           file-offset-to-disk-offset delta, not restart at the extent base. */
        if (FileSystem.ReadAt(FileSystem.FileSystemContext, FsHandle, 12u,
                MultiRead, 3u) != Rc95LdrSuccess ||
            MultiRead[0] != (uint8_t)(2u ^ 0x5au) ||
            MultiRead[1] != (uint8_t)(3u ^ 0x5au) ||
            MultiRead[2] != (uint8_t)(4u ^ 0x5au))
            return 63;

        memset(MultiRead, 0xa5, sizeof(MultiRead));
        if (FileSystem.ReadAt(FileSystem.FileSystemContext, FsHandle, 0,
                MultiRead, 30u) != Rc95LdrSuccess)
            return 43;
        for (I = 0; I < 5u; ++I)
            if (MultiRead[I] != 0)
                return 44; /* leading sparse hole */
        if (MultiRead[5] != (uint8_t)'h' || MultiRead[6] != 0 ||
            memcmp(MultiRead + 7, "llo", 3) != 0)
            return 45;
        for (I = 0; I < 5u; ++I)
            if (MultiRead[10u + I] != (uint8_t)(I ^ 0x5au))
                return 46; /* regular extent after the leaf transition */
        for (I = 15u; I < 30u; ++I)
            if (MultiRead[I] != 0)
                return 47; /* sparse gap, preallocation and EOF tail */
        FileSystem.Close(FileSystem.FileSystemContext, FsHandle);
    }

    /* Restore the original on-disk single-leaf tree: the host traversal cases
       above deliberately replace it with a synthetic multi-level tree. */
    memcpy(Image + PARTITION_START * 512u + 0x50000u,
           OriginalBtrfsFsNode, sizeof(OriginalBtrfsFsNode));
    { int UefiStatus=TestUefiBtrfsMount(); if(UefiStatus)return UefiStatus; }

    Rc95LdrBtrfsUnmount(&Btrfs);
    if (Rc95LdrBtrfsGetSuperblock(&Btrfs) != NULL ||
        Rc95LdrReadBlocks(Volume.BlockDevice,0,1,ReadBuffer,sizeof(ReadBuffer)) !=
        Rc95LdrInvalidParameter)
        return 8;

    puts("RC95LDR Btrfs volume binding tests passed");
    return 0;
}
