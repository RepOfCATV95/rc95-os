#include "rc95ldruedisk.h"
#include <string.h>

#define GPT_HEADER_MIN 92u
#define GPT_ENTRY_MIN 128u
#define GPT_MAX_ENTRIES 4096u
#define GPT_SIGNATURE 0x5452415020494645ULL

static uint32_t
Crc32(const uint8_t *Data, size_t Length)
{
    uint32_t Crc=0xffffffffu;
    size_t i; unsigned b;
    for(i=0;i<Length;i++){
        Crc^=Data[i];
        for(b=0;b<8;b++) Crc=(Crc>>1)^((Crc&1)?0xedb88320u:0);
    }
    return ~Crc;
}

static RC95LDR_STATUS
ReadDevice(const RC95LDR_UEFI_BLOCK_IO *Io, uint64_t Lba,
           uint32_t Blocks, void *Buffer, size_t Capacity)
{
    size_t Bytes;
    if(!Io||!Io->ReadBlocks||!Io->Media.BlockSize||!Blocks)
        return Rc95LdrInvalidParameter;
    if(Io->Media.LastBlock==UINT64_MAX ||
       Lba>Io->Media.LastBlock+1 ||
       (uint64_t)Blocks>Io->Media.LastBlock+1-Lba)
        return Rc95LdrOutOfBounds;
    if(!Rc95LdrMultiplySize(Blocks,Io->Media.BlockSize,&Bytes))
        return Rc95LdrIntegerOverflow;
    if(Bytes>Capacity) return Rc95LdrBufferTooSmall;
    return Io->ReadBlocks(Io->Protocol,Io->Media.MediaId,Lba,Bytes,Buffer);
}

RC95LDR_STATUS
Rc95LdrParseUefiDevicePath(const uint8_t *Path,size_t PathLength,
    uint32_t *PartitionNumber,uint64_t *StartLba,uint64_t *BlockCount,
    uint8_t PartitionGuid[16])
{
    size_t Offset=0; int Found=0,Ended=0;
    if(!Path||!PathLength||!PartitionNumber||!StartLba||
       !BlockCount||!PartitionGuid) return Rc95LdrInvalidParameter;
    *PartitionNumber=0; *StartLba=0; *BlockCount=0;
    memset(PartitionGuid,0,16);
    while(Offset<PathLength){
        uint8_t Type,SubType; uint16_t Length;
        if(PathLength-Offset<4) return Rc95LdrCorruptData;
        Type=Path[Offset]; SubType=Path[Offset+1];
        Length=Rc95LdrReadU16(Path+Offset+2);
        if(Length<4||Length>PathLength-Offset) return Rc95LdrCorruptData;
        if(Type==4 && SubType==1){
            if(Length<42||Found) return Rc95LdrCorruptData;
            *PartitionNumber=Rc95LdrReadU32(Path+Offset+4);
            *StartLba=Rc95LdrReadU64(Path+Offset+8);
            *BlockCount=Rc95LdrReadU64(Path+Offset+16);
            if(Path[Offset+41]==2)
                memcpy(PartitionGuid,Path+Offset+24,16);
            Found=1;
        }
        Offset+=Length;
        if(Type==0x7f){
            if(Length!=4 || (SubType!=0x01 && SubType!=0xff))
                return Rc95LdrCorruptData;
            if(SubType==0xff){Ended=1;break;}
        }
    }
    if(!Ended || Offset!=PathLength) return Rc95LdrCorruptData;
    return Found?Rc95LdrSuccess:Rc95LdrNotFound;
}

static RC95LDR_FILESYSTEM_KIND
DetectFileSystem(const RC95LDR_UEFI_BLOCK_IO *Io,uint64_t Start,
                 void *Scratch,size_t ScratchLength)
{
    const uint8_t *B=(const uint8_t*)Scratch;
    if(ReadDevice(Io,Start,1,Scratch,ScratchLength)!=Rc95LdrSuccess)
        return Rc95LdrFsUnknown;
    if(Io->Media.BlockSize>=512 && B[3]=='N'&&B[4]=='T'&&B[5]=='F'&&B[6]=='S'&&
       B[7]==' '&&B[510]==0x55&&B[511]==0xaa)
        return Rc95LdrFsNtfs;
    if(Io->Media.BlockSize>=512 && B[510]==0x55&&B[511]==0xaa &&
       Rc95LdrReadU16(B+11)==Io->Media.BlockSize &&
       B[13] && B[16] && Rc95LdrReadU16(B+17)==0 &&
       Rc95LdrReadU16(B+22)==0 && Rc95LdrReadU32(B+36)!=0 &&
       Rc95LdrReadU32(B+44)>=2)
        return Rc95LdrFsFat32;
    return Rc95LdrFsUnknown;
}

static RC95LDR_STATUS
ParseDiskGpt(const RC95LDR_UEFI_BLOCK_IO *Io,size_t DiskIndex,
    RC95LDR_PARTITION_INFO *Parts,size_t Capacity,size_t *Count,
    uint8_t *Scratch,size_t ScratchLength)
{
    uint8_t *Header=Scratch,*Entries;
    uint32_t HeaderSize,HeaderCrc,EntryCount,EntrySize,EntriesCrc;
    uint64_t EntryLba,UsableFirst,UsableLast;
    size_t EntryBytes,ReadBytes,EntryBlocks,i,j;
    RC95LDR_STATUS Status;
    if(Io->Media.BlockSize<512||ScratchLength<Io->Media.BlockSize)
        return Rc95LdrBufferTooSmall;
    Status=ReadDevice(Io,1,1,Header,ScratchLength);
    if(Status!=Rc95LdrSuccess) return Status;
    if(Rc95LdrReadU64(Header)!=GPT_SIGNATURE) return Rc95LdrInvalidImage;
    HeaderSize=Rc95LdrReadU32(Header+12);
    HeaderCrc=Rc95LdrReadU32(Header+16);
    if(HeaderSize<GPT_HEADER_MIN||HeaderSize>Io->Media.BlockSize)
        return Rc95LdrCorruptData;
    {
        uint32_t Saved=HeaderCrc;
        memset(Header+16,0,4);
        if(Crc32(Header,HeaderSize)!=Saved){
            Header[16]=(uint8_t)Saved; Header[17]=(uint8_t)(Saved>>8);
            Header[18]=(uint8_t)(Saved>>16); Header[19]=(uint8_t)(Saved>>24);
            return Rc95LdrCorruptData;
        }
        Header[16]=(uint8_t)Saved; Header[17]=(uint8_t)(Saved>>8);
        Header[18]=(uint8_t)(Saved>>16); Header[19]=(uint8_t)(Saved>>24);
    }
    if(Rc95LdrReadU32(Header+8)<0x00010000u ||
       Rc95LdrReadU32(Header+20)!=0 ||
       Rc95LdrReadU64(Header+24)!=1 ||
       Rc95LdrReadU64(Header+32)<=1 ||
       Rc95LdrReadU64(Header+32)>Io->Media.LastBlock ||
       Rc95LdrReadU64(Header+40)>Rc95LdrReadU64(Header+48) ||
       Rc95LdrReadU64(Header+48)>=Rc95LdrReadU64(Header+32) ||
       Rc95LdrReadU64(Header+48)>Io->Media.LastBlock ||
       memcmp(Header+56,"\\0\\0\\0\\0\\0\\0\\0\\0\\0\\0\\0\\0\\0\\0\\0\\0",16)==0)
        return Rc95LdrCorruptData;
    UsableFirst=Rc95LdrReadU64(Header+40);
    UsableLast=Rc95LdrReadU64(Header+48);
    EntryLba=Rc95LdrReadU64(Header+72);
    EntryCount=Rc95LdrReadU32(Header+80);
    EntrySize=Rc95LdrReadU32(Header+84);
    EntriesCrc=Rc95LdrReadU32(Header+88);
    if(!EntryCount||EntryCount>GPT_MAX_ENTRIES||EntrySize<GPT_ENTRY_MIN||
       EntrySize>4096u||(EntrySize&7u)||EntryLba<2)
        return Rc95LdrCorruptData;
    if(!Rc95LdrMultiplySize(EntryCount,EntrySize,&EntryBytes))
        return Rc95LdrIntegerOverflow;
    {
        size_t Rounded;
        if(!Rc95LdrAddSize(EntryBytes,(size_t)Io->Media.BlockSize-1,&Rounded))
            return Rc95LdrIntegerOverflow;
        EntryBlocks=Rounded/Io->Media.BlockSize;
    }
    if(EntryBlocks>UINT32_MAX ||
       !Rc95LdrMultiplySize((size_t)EntryBlocks,Io->Media.BlockSize,&ReadBytes))
        return Rc95LdrIntegerOverflow;
    if(ReadBytes>ScratchLength-Io->Media.BlockSize)
        return Rc95LdrBufferTooSmall;
    if(EntryLba>UsableFirst || EntryBlocks>UsableFirst-EntryLba)
        return Rc95LdrCorruptData;
    Entries=Scratch+Io->Media.BlockSize;
    Status=ReadDevice(Io,EntryLba,(uint32_t)EntryBlocks,Entries,
                      ScratchLength-Io->Media.BlockSize);
    if(Status!=Rc95LdrSuccess) return Status;
    if(Crc32(Entries,EntryBytes)!=EntriesCrc) return Rc95LdrCorruptData;

    for(i=0;i<EntryCount;i++){
        const uint8_t *E=Entries+i*EntrySize;
        uint64_t First,Last;
        RC95LDR_PARTITION_INFO P;
        if(memcmp(E,"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0",16)==0)
            continue;
        First=Rc95LdrReadU64(E+32); Last=Rc95LdrReadU64(E+40);
        if(First<UsableFirst||Last>UsableLast||Last<First)
            return Rc95LdrCorruptData;
        if(*Count>=Capacity) return Rc95LdrBufferTooSmall;
        memset(&P,0,sizeof(P));
        P.Style=Rc95LdrPartitionGpt;
        memcpy(P.TypeGuid,E,16);
        memcpy(P.DiskGuid,Header+56,16);
        memcpy(P.UniqueGuid,E+16,16);
        P.StartLba=First; P.BlockCount=Last-First+1;
        P.BlockSize=Io->Media.BlockSize;
        P.FirmwareHandleIndex=(uint32_t)DiskIndex;
        P.ParentDiskHandleIndex=(uint32_t)DiskIndex;
        P.FirmwarePartitionNumber=(uint32_t)(i+1);
        if(memcmp(P.UniqueGuid,"\\0\\0\\0\\0\\0\\0\\0\\0\\0\\0\\0\\0\\0\\0\\0\\0",16)==0)
            return Rc95LdrCorruptData;
        P.FileSystem=DetectFileSystem(Io,First,Header,ScratchLength);
        for(j=0;j<*Count;j++){
            const RC95LDR_PARTITION_INFO *Existing=&Parts[j];
            if(memcmp(Existing->UniqueGuid,P.UniqueGuid,16)==0)
                return Rc95LdrCorruptData;
            if(Existing->ParentDiskHandleIndex==P.ParentDiskHandleIndex &&
               !(Last<Existing->StartLba ||
                 First>=Existing->StartLba+Existing->BlockCount))
                return Rc95LdrCorruptData;
        }
        Parts[(*Count)++]=P;
    }
    return Rc95LdrSuccess;
}

RC95LDR_STATUS
Rc95LdrUefiEnumerateGptPartitions(const RC95LDR_UEFI_FIRMWARE_OPS *Ops,
    RC95LDR_PARTITION_INFO *Partitions,size_t PartitionCapacity,
    size_t *PartitionCount,void *Scratch,size_t ScratchLength)
{
    size_t HandleCount,Index,Count=0;
    RC95LDR_STATUS Status;
    if(!Ops||!Ops->GetHandleCount||!Ops->GetHandleAt||!Ops->OpenBlockIo||
       !Ops->GetDevicePath||!Partitions||!PartitionCount||!Scratch)
        return Rc95LdrInvalidParameter;
    *PartitionCount=0;
    Status=Ops->GetHandleCount(Ops->Context,&HandleCount);
    if(Status!=Rc95LdrSuccess) return Status;
    for(Index=0;Index<HandleCount;Index++){
        void *Handle=NULL; RC95LDR_UEFI_BLOCK_IO Io;
        Status=Ops->GetHandleAt(Ops->Context,Index,&Handle);
        if(Status!=Rc95LdrSuccess||!Handle) continue;
        memset(&Io,0,sizeof(Io));
        Status=Ops->OpenBlockIo(Ops->Context,Handle,&Io);
        if(Status!=Rc95LdrSuccess||!Io.Media.MediaPresent||
           !Io.ReadBlocks||!Io.Media.BlockSize)
            continue;
        if(!Io.Media.LogicalPartition) {
            Status=ParseDiskGpt(&Io,Index,Partitions,PartitionCapacity,&Count,
                                (uint8_t*)Scratch,ScratchLength);
            if(Status!=Rc95LdrSuccess && Status!=Rc95LdrInvalidImage)
                return Status;
        }
    }
    /* Second pass: associate logical partition handles after all physical
     * disks have been parsed, independent of firmware handle ordering. */
    for(Index=0;Index<HandleCount;Index++){
        void *Handle=NULL; RC95LDR_UEFI_BLOCK_IO Io;
        const uint8_t *Path=NULL; size_t PathLength=0;
        uint32_t PartNo; uint64_t Start,Blocks; uint8_t Guid[16];
        size_t PIndex;
        Status=Ops->GetHandleAt(Ops->Context,Index,&Handle);
        if(Status!=Rc95LdrSuccess||!Handle) continue;
        memset(&Io,0,sizeof(Io));
        Status=Ops->OpenBlockIo(Ops->Context,Handle,&Io);
        if(Status!=Rc95LdrSuccess||!Io.Media.MediaPresent||
           !Io.Media.LogicalPartition) continue;
        Status=Ops->GetDevicePath(Ops->Context,Handle,&Path,&PathLength);
        if(Status!=Rc95LdrSuccess) continue;
        Status=Rc95LdrParseUefiDevicePath(Path,PathLength,
            &PartNo,&Start,&Blocks,Guid);
        if(Status!=Rc95LdrSuccess||PartNo==0||Blocks==0) continue;
        for(PIndex=0;PIndex<Count;PIndex++){
            if(memcmp(Partitions[PIndex].UniqueGuid,Guid,16)==0 &&
               Partitions[PIndex].StartLba==Start &&
               Partitions[PIndex].BlockCount==Blocks){
                Partitions[PIndex].FirmwareHandleIndex=(uint32_t)Index;
                Partitions[PIndex].FirmwarePartitionNumber=PartNo;
                break;
            }
        }
    }
    *PartitionCount=Count;
    return Count?Rc95LdrSuccess:Rc95LdrNotFound;
}
