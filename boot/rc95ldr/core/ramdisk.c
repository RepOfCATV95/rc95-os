#include "rc95ldr_ramdisk.h"
#include <string.h>

static RC95LDR_STATUS
Rc95LdrRamDiskRange(RC95LDR_RAMDISK_CONTEXT *Context,
                    uint64_t Lba,
                    uint32_t Blocks,
                    size_t Length,
                    size_t *Offset)
{
    uint64_t ByteOffset, ByteLength;

    if (!Context || !Offset || !Context->Image || !Context->BlockSize ||
        !Blocks)
        return Rc95LdrInvalidParameter;

    if (Lba > UINT64_MAX / Context->BlockSize ||
        (uint64_t)Blocks > UINT64_MAX / Context->BlockSize)
        return Rc95LdrOutOfBounds;

    ByteOffset = Lba * Context->BlockSize;
    ByteLength = (uint64_t)Blocks * Context->BlockSize;
    if (ByteOffset > Context->ImageSize ||
        ByteLength > (uint64_t)Context->ImageSize - ByteOffset)
        return Rc95LdrOutOfBounds;
    if (ByteLength != Length)
        return Rc95LdrBufferTooSmall;
    if (ByteOffset > SIZE_MAX)
        return Rc95LdrOutOfBounds;

    *Offset = (size_t)ByteOffset;
    return Rc95LdrSuccess;
}

static RC95LDR_STATUS
Rc95LdrRamDiskRead(void *Opaque,
                   uint64_t Lba,
                   uint32_t Blocks,
                   void *Buffer,
                   size_t Length)
{
    RC95LDR_RAMDISK_CONTEXT *Context = (RC95LDR_RAMDISK_CONTEXT *)Opaque;
    size_t Offset;
    RC95LDR_STATUS Status;

    if (!Buffer)
        return Rc95LdrInvalidParameter;
    Status = Rc95LdrRamDiskRange(Context, Lba, Blocks, Length, &Offset);
    if (Status != Rc95LdrSuccess)
        return Status;

    memcpy(Buffer, Context->Image + Offset, Length);
    return Rc95LdrSuccess;
}

static RC95LDR_STATUS
Rc95LdrRamDiskWrite(void *Opaque,
                    uint64_t Lba,
                    uint32_t Blocks,
                    const void *Buffer,
                    size_t Length)
{
    RC95LDR_RAMDISK_CONTEXT *Context = (RC95LDR_RAMDISK_CONTEXT *)Opaque;
    size_t Offset;
    RC95LDR_STATUS Status;

    if (!Context || !Context->Writable)
        return Rc95LdrUnsupported;
    if (!Buffer)
        return Rc95LdrInvalidParameter;

    Status = Rc95LdrRamDiskRange(Context, Lba, Blocks, Length, &Offset);
    if (Status != Rc95LdrSuccess)
        return Status;

    /* This is the write path: mutate the RAM image, never the source file. */
    memcpy(Context->Image + Offset, Buffer, Length);
    return Rc95LdrSuccess;
}

RC95LDR_STATUS
Rc95LdrInitializeRamDisk(void *Image,
                         size_t ImageSize,
                         uint32_t BlockSize,
                         int Writable,
                         RC95LDR_RAMDISK_CONTEXT *Context,
                         RC95LDR_BLOCK_DEVICE *Device)
{
    uint64_t BlockCount;

    if (!Image || !ImageSize || !BlockSize || !Context || !Device)
        return Rc95LdrInvalidParameter;
    if (ImageSize % BlockSize)
        return Rc95LdrInvalidImage;

    BlockCount = (uint64_t)(ImageSize / BlockSize);
    if (!BlockCount)
        return Rc95LdrInvalidImage;

    memset(Context, 0, sizeof(*Context));
    Context->Image = (uint8_t *)Image;
    Context->ImageSize = ImageSize;
    Context->BlockSize = BlockSize;
    Context->BlockCount = BlockCount;
    Context->Writable = Writable ? 1u : 0u;

    memset(Device, 0, sizeof(*Device));
    Device->DeviceContext = Context;
    Device->BlockCount = BlockCount;
    Device->BlockSize = BlockSize;
    Device->Read = Rc95LdrRamDiskRead;
    Device->Write = Writable ? Rc95LdrRamDiskWrite : NULL;
    return Rc95LdrSuccess;
}
