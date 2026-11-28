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
    uint64_t Value;

    /*
     * RC95LDR's on-disk formats are little-endian and the boot/runtime
     * targets are little-endian. A byte copy avoids MSVC x86 lowering a
     * 64-bit shift by 32 into the unavailable __allshl helper.
     */
    memcpy(&Value, Buffer, sizeof(Value));
    return Value;
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


RC95LDR_STATUS
Rc95LdrWriteBlocks(
    const RC95LDR_BLOCK_DEVICE *Device,
    uint64_t Lba,
    uint32_t BlockCount,
    const void *Buffer,
    size_t BufferLength)
{
    size_t RequiredLength;

    if (Device == NULL || Device->BlockSize == 0 ||
        Buffer == NULL || BlockCount == 0)
        return Rc95LdrInvalidParameter;

    /* A missing callback is an explicit read-only device, not an I/O fault. */
    if (Device->Write == NULL)
        return Rc95LdrUnsupported;

    if (Lba > Device->BlockCount ||
        (uint64_t)BlockCount > Device->BlockCount - Lba)
        return Rc95LdrOutOfBounds;

    if (!Rc95LdrMultiplySize((size_t)BlockCount,
                             (size_t)Device->BlockSize,
                             &RequiredLength))
        return Rc95LdrIntegerOverflow;

    if (BufferLength < RequiredLength)
        return Rc95LdrBufferTooSmall;

    return Device->Write(Device->DeviceContext, Lba, BlockCount,
                         Buffer, RequiredLength);
}

/*
 * RC95LDR performs synchronous boot-time image loading. A strict stack scope
 * makes provider lifetime explicit and prevents nested scopes from silently
 * replacing a different volume binding.
 */
static const RC95LDR_FILE_PROVIDER *Rc95LdrActiveFileProvider;
/* Metadata belongs to the active volume scope, never to the provider vtable. */
static const uint16_t *Rc95LdrActiveFileSystemDriverName;

static int
Rc95LdrFileProviderValid(const RC95LDR_FILE_PROVIDER *Provider)
{
    return Provider != NULL &&
           Provider->StructureSize >= sizeof(*Provider) &&
           Provider->VolumeContext != NULL &&
           Provider->Open != NULL &&
           Provider->GetFileInformation != NULL &&
           Provider->Read != NULL &&
           Provider->Seek != NULL &&
           Provider->Close != NULL;
}

RC95LDR_STATUS
Rc95LdrBeginFileProviderScope(
    const RC95LDR_FILE_PROVIDER *Provider,
    RC95LDR_FILE_PROVIDER_SCOPE *Scope)
{
    if (Scope == NULL || !Rc95LdrFileProviderValid(Provider))
        return Rc95LdrInvalidParameter;
    if (Rc95LdrActiveFileProvider != NULL)
        return Rc95LdrUnsupported;

    Scope->Previous = Rc95LdrActiveFileProvider;
    Scope->Installed = Provider;
    Scope->Active = 1;
    Rc95LdrActiveFileSystemDriverName = NULL;
    Rc95LdrActiveFileProvider = Provider;
    return Rc95LdrSuccess;
}

RC95LDR_STATUS
Rc95LdrEndFileProviderScope(
    RC95LDR_FILE_PROVIDER_SCOPE *Scope)
{
    if (Scope == NULL || !Scope->Active ||
        Rc95LdrActiveFileProvider != Scope->Installed)
        return Rc95LdrInvalidParameter;

    Rc95LdrActiveFileProvider = Scope->Previous;
    Rc95LdrActiveFileSystemDriverName = NULL;
    Scope->Previous = NULL;
    Scope->Installed = NULL;
    Scope->Active = 0;
    return Rc95LdrSuccess;
}

const RC95LDR_FILE_PROVIDER *
Rc95LdrGetFileProvider(void)
{
    return Rc95LdrActiveFileProvider;
}

RC95LDR_STATUS
Rc95LdrSetFileSystemDriverName(
    const RC95LDR_FILE_PROVIDER *Provider,
    const uint16_t *DriverName)
{
    if (Provider == NULL || Provider != Rc95LdrActiveFileProvider ||
        DriverName == NULL || DriverName[0] == 0 ||
        Rc95LdrActiveFileSystemDriverName != NULL)
        return Rc95LdrInvalidParameter;

    Rc95LdrActiveFileSystemDriverName = DriverName;
    return Rc95LdrSuccess;
}

const uint16_t *
Rc95LdrGetFileSystemDriverName(void)
{
    return Rc95LdrActiveFileSystemDriverName;
}

RC95LDR_STATUS
Rc95LdrRunWithFileProvider(
    const RC95LDR_FILE_PROVIDER *Provider,
    RC95LDR_SCOPED_OPERATION Operation,
    void *OperationContext)
{
    RC95LDR_FILE_PROVIDER_SCOPE Scope = { 0 };
    RC95LDR_STATUS Status, EndStatus;

    if (Operation == NULL)
        return Rc95LdrInvalidParameter;

    Status = Rc95LdrBeginFileProviderScope(Provider, &Scope);
    if (Status != Rc95LdrSuccess)
        return Status;

    /* The operation is synchronous; the provider must outlive this call. */
    Status = Operation(OperationContext);
    EndStatus = Rc95LdrEndFileProviderScope(&Scope);
    if (EndStatus != Rc95LdrSuccess)
        return EndStatus;
    return Status;
}
