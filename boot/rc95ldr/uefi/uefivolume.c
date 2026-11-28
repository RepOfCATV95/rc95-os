#include "rc95ldruedisk.h"
#include <string.h>

static RC95LDR_STATUS
ReadPartition(void *Context, uint64_t Lba, uint32_t Blocks,
              void *Buffer, size_t Capacity)
{
    RC95LDR_UEFI_FAT32_VOLUME *Volume = (RC95LDR_UEFI_FAT32_VOLUME *)Context;
    size_t Bytes;
    if (!Volume || !Volume->Mounted || !Buffer || !Blocks ||
        !Volume->BlockIo.ReadBlocks || !Volume->Device.BlockSize)
        return Rc95LdrInvalidParameter;
    if (Lba > Volume->Device.BlockCount ||
        (uint64_t)Blocks > Volume->Device.BlockCount - Lba)
        return Rc95LdrOutOfBounds;
    if (!Rc95LdrMultiplySize((size_t)Blocks, Volume->Device.BlockSize, &Bytes))
        return Rc95LdrIntegerOverflow;
    if (Bytes > Capacity) return Rc95LdrBufferTooSmall;
    return Volume->BlockIo.ReadBlocks(Volume->BlockIo.Protocol,
        Volume->BlockIo.Media.MediaId, Lba, Bytes, Buffer);
}

static RC95LDR_STATUS
WritePartition(void *Context,uint64_t Lba,uint32_t Blocks,
               const void *Buffer,size_t Capacity)
{
    RC95LDR_UEFI_FAT32_VOLUME *Volume=(RC95LDR_UEFI_FAT32_VOLUME*)Context;
    size_t Bytes;
    if(!Volume||!Volume->Mounted||!Buffer||!Blocks||
       !Volume->BlockIo.WriteBlocks||Volume->BlockIo.Media.ReadOnly)
        return Rc95LdrUnsupported;
    if(Lba>Volume->Device.BlockCount||
       (uint64_t)Blocks>Volume->Device.BlockCount-Lba)
        return Rc95LdrOutOfBounds;
    if(!Rc95LdrMultiplySize((size_t)Blocks,Volume->Device.BlockSize,&Bytes))
        return Rc95LdrIntegerOverflow;
    if(Bytes>Capacity)return Rc95LdrBufferTooSmall;
    return Volume->BlockIo.WriteBlocks(Volume->BlockIo.Protocol,
        Volume->BlockIo.Media.MediaId,Lba,Bytes,Buffer);
}

RC95LDR_STATUS
Rc95LdrUefiMountFat32Volume(const RC95LDR_UEFI_FIRMWARE_OPS *Ops,
    const RC95LDR_PARTITION_INFO *Partition,
    RC95LDR_UEFI_FAT32_VOLUME *Volume, RC95LDR_VOLUME *MountedVolume)
{
    void *Handle = NULL;
    const uint8_t *DevicePath = NULL;
    size_t DevicePathLength = 0;
    RC95LDR_STATUS Status;
    if (!Ops || !Partition || !Volume || !MountedVolume ||
        !Ops->GetHandleAt || !Ops->OpenBlockIo || !Ops->GetDevicePath ||
        Partition->FileSystem != Rc95LdrFsFat32 ||
        Partition->FirmwareHandleIndex == UINT32_MAX ||
        !Partition->BlockCount || !Partition->BlockSize)
        return Rc95LdrInvalidParameter;
    memset(Volume, 0, sizeof(*Volume));
    memset(MountedVolume, 0, sizeof(*MountedVolume));
    Status = Ops->GetHandleAt(Ops->Context, Partition->FirmwareHandleIndex, &Handle);
    if (Status != Rc95LdrSuccess) return Status;
    if (!Handle) return Rc95LdrNotFound;
    Status = Ops->OpenBlockIo(Ops->Context, Handle, &Volume->BlockIo);
    if (Status != Rc95LdrSuccess) return Status;
    if (!Volume->BlockIo.Media.MediaPresent ||
        !Volume->BlockIo.Media.BlockSize ||
        Volume->BlockIo.Media.BlockSize != Partition->BlockSize ||
        Volume->BlockIo.Media.IoAlign > 1 ||
        Volume->BlockIo.Media.LastBlock == UINT64_MAX ||
        Volume->BlockIo.Media.LastBlock + 1 != Partition->BlockCount ||
        !Volume->BlockIo.ReadBlocks)
        return Rc95LdrCorruptData;
    Status = Ops->GetDevicePath(Ops->Context, Handle, &DevicePath, &DevicePathLength);
    if (Status != Rc95LdrSuccess) return Status;
    Status = Rc95LdrUefiValidatePartitionHandle(Partition, &Volume->BlockIo,
        DevicePath, DevicePathLength);
    if (Status != Rc95LdrSuccess) return Status;
    Volume->Device.DeviceContext = Volume;
    Volume->Device.BlockCount = Partition->BlockCount;
    Volume->Device.BlockSize = Partition->BlockSize;
    Volume->Device.Read = ReadPartition;
    Volume->Device.Write = (!Volume->BlockIo.Media.ReadOnly &&
        Volume->BlockIo.WriteBlocks) ? WritePartition : NULL;
    Volume->Mounted = 1; /* FAT32 mount reads the BPB through this adapter. */
    Status = Rc95LdrFat32MountPartition(&Volume->Fat32, &Volume->Device,
        0, Partition->BlockCount);
    if (Status != Rc95LdrSuccess) { memset(Volume, 0, sizeof(*Volume)); return Status; }
    MountedVolume->BlockDevice = &Volume->Device;
    MountedVolume->FileSystem = Rc95LdrFat32GetInterface(&Volume->Fat32);
    return Rc95LdrSuccess;
}

void
Rc95LdrUefiUnmountFat32Volume(RC95LDR_UEFI_FAT32_VOLUME *Volume)
{
    if (!Volume) return;
    if (Volume->Mounted) Rc95LdrFat32Unmount(&Volume->Fat32);
    memset(Volume, 0, sizeof(*Volume));
}
