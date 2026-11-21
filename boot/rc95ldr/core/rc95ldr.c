#include "rc95ldr.h"

#include <string.h>

void
Rc95LdrInitializeContext(
    RC95LDR_BOOT_CONTEXT *Context,
    void *PlatformContext)
{
    if (Context == NULL)
        return;

    memset(Context, 0, sizeof(*Context));
    Context->StructureSize = (uint32_t)sizeof(*Context);
    Context->Version = 1;
    Context->CurrentStage = Rc95LdrStageStartup;
    Context->PlatformContext = PlatformContext;
}

void
Rc95LdrSetStage(
    RC95LDR_BOOT_CONTEXT *Context,
    RC95LDR_STAGE Stage)
{
    if (Context != NULL)
        Context->CurrentStage = Stage;
}

RC95LDR_STATUS
Rc95LdrFail(
    RC95LDR_BOOT_CONTEXT *Context,
    RC95LDR_STATUS Status,
    uint32_t Detail)
{
    if (Context != NULL)
    {
        Context->LastError.Stage = Context->CurrentStage;
        Context->LastError.Status = Status;
        Context->LastError.Detail = Detail;
    }

    return Status;
}

int
Rc95LdrRangeValid(
    size_t TotalLength,
    size_t Offset,
    size_t Length)
{
    return (Offset <= TotalLength) && (Length <= TotalLength - Offset);
}

int
Rc95LdrAddSize(
    size_t Left,
    size_t Right,
    size_t *Result)
{
    if (Result == NULL || Right > ((size_t)-1) - Left)
        return 0;

    *Result = Left + Right;
    return 1;
}

int
Rc95LdrMultiplySize(
    size_t Left,
    size_t Right,
    size_t *Result)
{
    if (Result == NULL || (Left != 0 && Right > ((size_t)-1) / Left))
        return 0;

    *Result = Left * Right;
    return 1;
}

uint16_t
Rc95LdrReadU16(const void *Buffer)
{
    const uint8_t *Bytes = (const uint8_t *)Buffer;
    return (uint16_t)(Bytes[0] | ((uint16_t)Bytes[1] << 8));
}

uint32_t
Rc95LdrReadU32(const void *Buffer)
{
    const uint8_t *Bytes = (const uint8_t *)Buffer;
    return (uint32_t)Bytes[0] |
           ((uint32_t)Bytes[1] << 8) |
           ((uint32_t)Bytes[2] << 16) |
           ((uint32_t)Bytes[3] << 24);
}

uint64_t
Rc95LdrReadU64(const void *Buffer)
{
    const uint8_t *Bytes = (const uint8_t *)Buffer;
    return (uint64_t)Rc95LdrReadU32(Bytes) |
           ((uint64_t)Rc95LdrReadU32(Bytes + 4) << 32);
}

RC95LDR_STATUS
Rc95LdrReadBlocks(
    const RC95LDR_BLOCK_DEVICE *Device,
    uint64_t Lba,
    uint32_t BlockCount,
    void *Buffer,
    size_t BufferLength)
{
    size_t RequiredLength;

    if (Device == NULL || Device->Read == NULL ||
        Device->BlockSize == 0 || Buffer == NULL || BlockCount == 0)
        return Rc95LdrInvalidParameter;

    if (Lba > Device->BlockCount ||
        (uint64_t)BlockCount > Device->BlockCount - Lba)
        return Rc95LdrOutOfBounds;

    if (!Rc95LdrMultiplySize((size_t)BlockCount,
                             (size_t)Device->BlockSize,
                             &RequiredLength))
        return Rc95LdrIntegerOverflow;

    if (BufferLength < RequiredLength)
        return Rc95LdrBufferTooSmall;

    return Device->Read(Device->DeviceContext, Lba, BlockCount,
                        Buffer, RequiredLength);
}
