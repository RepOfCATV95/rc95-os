#include "include/rc95ldruefiservices.h"
#include <stddef.h>
#include <string.h>

static RC95LDR_STATUS
UefiRestartSystem(void *Context)
{
    const RC95LDR_UEFI_IMAGE_CONTEXT *Image=
        (const RC95LDR_UEFI_IMAGE_CONTEXT*)Context;
    EFI_RUNTIME_SERVICES *Runtime;
    if(!Image || Image->StructureSize<sizeof(*Image) ||
       Image->Version!=RC95LDR_UEFI_ENTRY_VERSION || !Image->SystemTable ||
       Image->SystemTable->Hdr.Signature!=EFI_SYSTEM_TABLE_SIGNATURE)
        return Rc95LdrInvalidParameter;
    Runtime=Image->SystemTable->RuntimeServices;
    if(!Runtime || !Runtime->ResetSystem) return Rc95LdrUnsupported;
    Runtime->ResetSystem(EfiResetCold,EFI_SUCCESS,0,NULL);
    /* EFI ResetSystem is specified not to return. */
    return Rc95LdrIoError;
}

RC95LDR_STATUS
Rc95LdrUefiBindFirmwareMenuOps(
    const RC95LDR_UEFI_IMAGE_CONTEXT *Image,
    RC95LDR_FIRMWARE_MENU_OPS *MenuOps)
{
    if(!Image || Image->StructureSize<sizeof(*Image) ||
       Image->Version!=RC95LDR_UEFI_ENTRY_VERSION || !Image->SystemTable ||
       Image->SystemTable->Hdr.Signature!=EFI_SYSTEM_TABLE_SIGNATURE ||
       !MenuOps || MenuOps->StructureSize<sizeof(*MenuOps) ||
       MenuOps->Version!=RC95LDR_FIRMWARE_MENU_VERSION)
        return Rc95LdrInvalidParameter;
    if(!Image->SystemTable->RuntimeServices ||
       !Image->SystemTable->RuntimeServices->ResetSystem)
        return Rc95LdrUnsupported;
    MenuOps->RestartContext=(void*)Image;
    MenuOps->RestartSystem=UefiRestartSystem;
    return Rc95LdrSuccess;
}

#define RC95LDR_EFI_FILE_MODE_READ 0x0000000000000001ULL
#define RC95LDR_EFI_OPEN_PROTOCOL_GET_PROTOCOL 0x00000002u
#define RC95LDR_EFI_LOADER_DATA 2u
#define RC95LDR_EFI_INFO_BUFFER_SIZE 1024u

typedef struct _RC95LDR_EFI_TIME {
    UINT16 Year;
    UINT8 Month, Day, Hour, Minute, Second, Pad1;
    UINT32 Nanosecond;
    INT16 TimeZone;
    UINT8 Daylight, Pad2;
} RC95LDR_EFI_TIME;

typedef struct _RC95LDR_EFI_FILE_INFO {
    UINT64 Size;
    UINT64 FileSize;
    UINT64 PhysicalSize;
    RC95LDR_EFI_TIME CreateTime;
    RC95LDR_EFI_TIME LastAccessTime;
    RC95LDR_EFI_TIME ModificationTime;
    UINT64 Attribute;
    CHAR16 FileName[1];
} RC95LDR_EFI_FILE_INFO;

static int IsPowerOfTwo(UINT64 Value)
{
    return Value && !(Value & (Value - 1u));
}

static RC95LDR_STATUS MapEfiStatus(EFI_STATUS Status)
{
    if (!EFI_ERROR(Status)) return Rc95LdrSuccess;
    if (Status == EFI_OUT_OF_RESOURCES) return Rc95LdrOutOfMemory;
    if (Status == EFI_NOT_FOUND) return Rc95LdrNotFound;
    if (Status == EFI_INVALID_PARAMETER) return Rc95LdrInvalidParameter;
    if (Status == EFI_UNSUPPORTED) return Rc95LdrUnsupported;
    if (Status == EFI_BUFFER_TOO_SMALL) return Rc95LdrBufferTooSmall;
    return Rc95LdrIoError;
}

static RC95LDR_UEFI_FILE_SLOT *FindFileSlot(
    RC95LDR_UEFI_SERVICE_CONTEXT *Provider,
    const RC95LDR_MODULE_FILE *File)
{
    UINTN Index;
    if (!Provider || Provider->StructureSize < sizeof(*Provider) ||
        Provider->Version != RC95LDR_MODULE_SERVICES_VERSION ||
        Provider->Shutdown || !Provider->BootServices || !File || !File->FileContext) return NULL;
    for (Index = 0; Index < RC95LDR_UEFI_MAX_OPEN_FILES; ++Index)
    {
        RC95LDR_UEFI_FILE_SLOT *Slot = &Provider->Files[Index];
        if (Slot->InUse && Slot == File->FileContext &&
            Slot->Handle && Slot->Size == File->FileSize)
            return Slot;
    }
    return NULL;
}

static RC95LDR_STATUS RC95LDR_MODULE_ABI
UefiOpenFile(void *Context, const uint16_t *Path,
             RC95LDR_MODULE_FILE *ModuleFile)
{
    RC95LDR_UEFI_SERVICE_CONTEXT *Provider = Context;
    CHAR16 BoundedPath[RC95LDR_UEFI_FILE_PATH_CHARS];
    RC95LDR_EFI_FILE_HANDLE Handle = NULL;
    union { RC95LDR_EFI_FILE_INFO Info; UINT8 Buffer[RC95LDR_EFI_INFO_BUFFER_SIZE]; } InfoStorage;
    RC95LDR_EFI_FILE_INFO *Info = &InfoStorage.Info;
    UINTN Index, Length = 0, InfoSize = sizeof(InfoStorage.Buffer);
    EFI_STATUS EfiStatus;

    if (!Provider || Provider->StructureSize < sizeof(*Provider) ||
        Provider->Version != RC95LDR_MODULE_SERVICES_VERSION ||
        Provider->Shutdown || !Provider->Root || !Path || !ModuleFile)
        return Rc95LdrInvalidParameter;
    memset(ModuleFile, 0, sizeof(*ModuleFile));
    while (Length < RC95LDR_UEFI_FILE_PATH_CHARS && Path[Length])
    {
        BoundedPath[Length] = (CHAR16)Path[Length];
        ++Length;
    }
    if (!Length || Length == RC95LDR_UEFI_FILE_PATH_CHARS ||
        (Path[0] != '/' && Path[0] != '\\'))
        return Rc95LdrOutOfBounds;
    BoundedPath[Length] = 0;
    for (Index = 0; Index < Length; ++Index)
    {
        if (BoundedPath[Index] == '/') BoundedPath[Index] = '\\';
        if (BoundedPath[Index] < 0x20 || BoundedPath[Index] == ':')
            return Rc95LdrInvalidParameter;
        if (BoundedPath[Index] == '\\' &&
            ((Index + 1u < Length && BoundedPath[Index + 1u] == '.') &&
             (Index + 2u == Length || BoundedPath[Index + 2u] == '\\' ||
              (Index + 2u < Length && BoundedPath[Index + 2u] == '.' &&
               (Index + 3u == Length || BoundedPath[Index + 3u] == '\\')))))
            return Rc95LdrInvalidParameter;
    }

    for (Index = 0; Index < RC95LDR_UEFI_MAX_OPEN_FILES; ++Index)
        if (!Provider->Files[Index].InUse) break;
    if (Index == RC95LDR_UEFI_MAX_OPEN_FILES)
        return Rc95LdrOutOfMemory;

    EfiStatus = Provider->Root->Open(
        Provider->Root, &Handle, BoundedPath, RC95LDR_EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(EfiStatus) || !Handle)
    {
        if (Handle && Handle->Close) Handle->Close(Handle);
        return MapEfiStatus(EFI_ERROR(EfiStatus) ? EfiStatus : EFI_DEVICE_ERROR);
    }

    /* Track ownership immediately after Open succeeds. Even a malformed
       protocol instance with no Close method must remain visible to
       Quiesce/Destroy rather than becoming an untracked firmware handle. */
    Provider->Files[Index].Handle = Handle;
    Provider->Files[Index].InUse = 1;
    if (!Handle->Close)
        return Rc95LdrUnsupported;
    if (!Handle->GetInfo || !Handle->Read || !Handle->SetPosition)
    {
        if (!EFI_ERROR(Handle->Close(Handle)))
            memset(&Provider->Files[Index], 0, sizeof(Provider->Files[Index]));
        return Rc95LdrUnsupported;
    }
    memset(&InfoStorage, 0, sizeof(InfoStorage));
    EfiStatus = Handle->GetInfo(Handle, &Provider->FileInfoGuid,
                                &InfoSize, InfoStorage.Buffer);
    if (EFI_ERROR(EfiStatus) ||
        InfoSize < offsetof(RC95LDR_EFI_FILE_INFO, FileName) ||
        InfoSize > sizeof(InfoStorage.Buffer) ||
        Info->Size < offsetof(RC95LDR_EFI_FILE_INFO, FileName) ||
        Info->Size > InfoSize)
    {
        if (!EFI_ERROR(Handle->Close(Handle)))
            memset(&Provider->Files[Index], 0, sizeof(Provider->Files[Index]));
        return MapEfiStatus(EFI_ERROR(EfiStatus) ? EfiStatus : EFI_COMPROMISED_DATA);
    }
    if (Info->FileSize > RC95LDR_MODULE_MAX_IMAGE_SIZE)
    {
        if (!EFI_ERROR(Handle->Close(Handle)))
            memset(&Provider->Files[Index], 0, sizeof(Provider->Files[Index]));
        return Rc95LdrOutOfBounds;
    }

    Provider->Files[Index].Handle = Handle;
    Provider->Files[Index].Size = Info->FileSize;
    Provider->Files[Index].InUse = 1;
    ModuleFile->FileContext = &Provider->Files[Index];
    ModuleFile->FileSize = Info->FileSize;
    return Rc95LdrSuccess;
}

static RC95LDR_STATUS RC95LDR_MODULE_ABI
UefiReadFileAt(void *Context, const RC95LDR_MODULE_FILE *File,
               uint64_t Offset, void *Buffer, size_t Length)
{
    RC95LDR_UEFI_SERVICE_CONTEXT *Provider = Context;
    RC95LDR_UEFI_FILE_SLOT *Slot = FindFileSlot(Provider, File);
    UINTN ReadSize;
    EFI_STATUS EfiStatus;
    if (!Slot || (!Buffer && Length)) return Rc95LdrInvalidParameter;
    if (Offset > Slot->Size || (uint64_t)Length > Slot->Size - Offset ||
        Length > (size_t)~(UINTN)0)
        return Rc95LdrOutOfBounds;
    if (!Length) return Rc95LdrSuccess;
    EfiStatus = Slot->Handle->SetPosition(Slot->Handle, Offset);
    if (EFI_ERROR(EfiStatus)) return MapEfiStatus(EfiStatus);
    ReadSize = (UINTN)Length;
    EfiStatus = Slot->Handle->Read(Slot->Handle, &ReadSize, Buffer);
    if (EFI_ERROR(EfiStatus)) return MapEfiStatus(EfiStatus);
    return ReadSize == Length ? Rc95LdrSuccess : Rc95LdrIoError;
}

static void RC95LDR_MODULE_ABI
UefiCloseFile(void *Context, RC95LDR_MODULE_FILE *File)
{
    RC95LDR_UEFI_SERVICE_CONTEXT *Provider = Context;
    RC95LDR_UEFI_FILE_SLOT *Slot = FindFileSlot(Provider, File);
    if (!Slot || !Slot->Handle->Close) return;
    if (EFI_ERROR(Slot->Handle->Close(Slot->Handle)))
        return;
    memset(Slot, 0, sizeof(*Slot));
    File->FileContext = NULL;
    File->FileSize = 0;
}

static RC95LDR_STATUS RC95LDR_MODULE_ABI
UefiAllocatePages(void *Context, uint64_t Size, uint64_t Alignment,
                  uint64_t *Base)
{
    RC95LDR_UEFI_SERVICE_CONTEXT *Provider = Context;
    UINT64 Rounded, Pages, ExtraPages, RawPages, RawBase = 0, AlignedBase;
    UINT64 RawBytes, RawEnd;
    UINTN Index;
    EFI_STATUS Status;

    if (!Provider || Provider->StructureSize < sizeof(*Provider) ||
        Provider->Version != RC95LDR_MODULE_SERVICES_VERSION ||
        Provider->Shutdown || !Provider->BootServices || !Base || !Size)
        return Rc95LdrInvalidParameter;
    *Base = 0;
    if (!Alignment) Alignment = RC95LDR_UEFI_PAGE_SIZE;
    if (Alignment < RC95LDR_UEFI_PAGE_SIZE ||
        !IsPowerOfTwo(Alignment) || Alignment > 0x10000000ULL)
        return Rc95LdrInvalidParameter;
    if (Size > UINT64_MAX - (RC95LDR_UEFI_PAGE_SIZE - 1u))
        return Rc95LdrIntegerOverflow;

    Rounded = (Size + RC95LDR_UEFI_PAGE_SIZE - 1u) &
              ~(UINT64)(RC95LDR_UEFI_PAGE_SIZE - 1u);
    Pages = Rounded / RC95LDR_UEFI_PAGE_SIZE;
    ExtraPages = Alignment / RC95LDR_UEFI_PAGE_SIZE - 1u;
    if (Pages > (UINT64)(~(UINTN)0) - ExtraPages)
        return Rc95LdrOutOfBounds;
    RawPages = Pages + ExtraPages;
    if (RawPages > UINT64_MAX / RC95LDR_UEFI_PAGE_SIZE)
        return Rc95LdrIntegerOverflow;
    RawBytes = RawPages * RC95LDR_UEFI_PAGE_SIZE;

    for (Index = 0; Index < RC95LDR_UEFI_MAX_ALLOCATIONS; ++Index)
        if (!Provider->Allocations[Index].InUse) break;
    if (Index == RC95LDR_UEFI_MAX_ALLOCATIONS)
        return Rc95LdrOutOfMemory;

    Status = Provider->BootServices->AllocatePages(
        AllocateAnyPages, (EFI_MEMORY_TYPE)RC95LDR_EFI_LOADER_DATA,
        (UINTN)RawPages, &RawBase);
    if (EFI_ERROR(Status)) return MapEfiStatus(Status);

    if (!RawBase)
        return Rc95LdrOutOfBounds;
    if ((RawBase & (RC95LDR_UEFI_PAGE_SIZE - 1u)) != 0 ||
        RawBase > UINT64_MAX - RawBytes ||
        RawBase > UINT64_MAX - (Alignment - 1u))
    {
        Provider->BootServices->FreePages(RawBase, (UINTN)RawPages);
        return Rc95LdrOutOfBounds;
    }
    RawEnd = RawBase + RawBytes;
    AlignedBase = (RawBase + Alignment - 1u) & ~(Alignment - 1u);
    if (AlignedBase < RawBase || AlignedBase > RawEnd ||
        Rounded > RawEnd - AlignedBase ||
        AlignedBase > (UINT64)UINTPTR_MAX ||
        Rounded - 1u > (UINT64)UINTPTR_MAX - AlignedBase)
    {
        Provider->BootServices->FreePages(RawBase, (UINTN)RawPages);
        return Rc95LdrOutOfBounds;
    }

    /* Retain the complete EFI allocation. Trimming prefix/suffix pages can
       fail independently and would otherwise leak untracked firmware pages. */
    Provider->Allocations[Index].Base = AlignedBase;
    Provider->Allocations[Index].Size = Rounded;
    Provider->Allocations[Index].RequestedSize = Size;
    Provider->Allocations[Index].RawBase = RawBase;
    Provider->Allocations[Index].RawSize = RawBytes;
    Provider->Allocations[Index].InUse = 1;
    *Base = AlignedBase;
    return Rc95LdrSuccess;
}

static void RC95LDR_MODULE_ABI
UefiFreePages(void *Context, uint64_t Base, uint64_t Size)
{
    RC95LDR_UEFI_SERVICE_CONTEXT *Provider = Context;
    UINTN Index;
    if (!Provider || Provider->StructureSize < sizeof(*Provider) ||
        Provider->Version != RC95LDR_MODULE_SERVICES_VERSION ||
        Provider->Shutdown || !Provider->BootServices || !Base || !Size)
        return;
    for (Index = 0; Index < RC95LDR_UEFI_MAX_ALLOCATIONS; ++Index)
    {
        RC95LDR_UEFI_ALLOCATION_SLOT *Slot = &Provider->Allocations[Index];
        if (Slot->InUse && Slot->Base == Base &&
            Slot->RequestedSize == Size)
        {
            UINTN Pages = (UINTN)(Slot->RawSize / RC95LDR_UEFI_PAGE_SIZE);
            if (!EFI_ERROR(Provider->BootServices->FreePages(
                    Slot->RawBase, Pages)))
                memset(Slot, 0, sizeof(*Slot));
            return;
        }
    }
}

static RC95LDR_STATUS RC95LDR_MODULE_ABI
UefiValidateAllocationExtent(void *Context, uint64_t Physical,
    uint64_t Virtual, uint64_t Size, uint64_t Alignment)
{
    RC95LDR_UEFI_SERVICE_CONTEXT *Provider = Context;
    UINTN Index;
    UINT64 End;
    if (!Provider || Provider->StructureSize < sizeof(*Provider) ||
        Provider->Version != RC95LDR_MODULE_SERVICES_VERSION ||
        Provider->Shutdown || !Provider->BootServices || !Physical || Physical != Virtual || !Size ||
        !Alignment || !IsPowerOfTwo(Alignment) ||
        (Physical & (Alignment - 1u)) ||
        Size > UINT64_MAX - Physical)
        return Rc95LdrInvalidParameter;
    End = Physical + Size;
    for (Index = 0; Index < RC95LDR_UEFI_MAX_ALLOCATIONS; ++Index)
    {
        const RC95LDR_UEFI_ALLOCATION_SLOT *Slot = &Provider->Allocations[Index];
        if (Slot->InUse && Slot->Size <= UINT64_MAX - Slot->Base &&
            Physical >= Slot->Base && End >= Physical &&
            End <= Slot->Base + Slot->Size)
            return Rc95LdrSuccess;
    }
    return Rc95LdrOutOfBounds;
}

static RC95LDR_STATUS RC95LDR_MODULE_ABI
UefiQuiesceService(void *Context)
{
    return Rc95LdrUefiQuiesceModuleServices(
        (RC95LDR_UEFI_SERVICE_CONTEXT *)Context);
}


#define RC95LDR_EFI_BLOCK_IO_PROTOCOL_GUID \
    { 0x964e5b21, 0x6459, 0x11d2, { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }
#define RC95LDR_EFI_DEVICE_PATH_PROTOCOL_GUID \
    { 0x09576e91, 0x6d3f, 0x11d2, { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }
#define RC95LDR_EFI_MAX_DEVICE_PATH_BYTES (64u * 1024u)
#define RC95LDR_EFI_DISCOVERY_SCRATCH_PAGES 32u
#define RC95LDR_EFI_DISCOVERY_SCRATCH_SIZE 65536u
#define RC95LDR_EFI_MAX_IO_ALIGN 65536u

static EFI_GUID UefiBlockIoGuid = RC95LDR_EFI_BLOCK_IO_PROTOCOL_GUID;
static EFI_GUID UefiDevicePathGuid = RC95LDR_EFI_DEVICE_PATH_PROTOCOL_GUID;

static RC95LDR_STATUS RC95LDR_MODULE_ABI
UefiBlockRead(void *Protocol, UINT32 MediaId, uint64_t Lba,
              size_t ByteCount, void *Buffer)
{
    EFI_BLOCK_IO_PROTOCOL *Block = (EFI_BLOCK_IO_PROTOCOL *)Protocol;
    if (!Block || !Block->Media || !Block->ReadBlocks ||
        !Buffer || !ByteCount || ByteCount > (size_t)~(UINTN)0 ||
        !Block->Media->MediaPresent ||
        Block->Media->MediaId != MediaId ||
        !Block->Media->BlockSize ||
        ByteCount % Block->Media->BlockSize != 0)
        return Rc95LdrInvalidParameter;

    /* Do not rely on firmware to reject a wrapping/out-of-media request.
       Compute the final LBA by subtraction so LastBlock == UINT64_MAX is
       handled without an overflowing LastBlock + 1 expression. */
    {
        UINT64 Blocks = (UINT64)(ByteCount / Block->Media->BlockSize);
        if (!Blocks || (UINT64)Lba > Block->Media->LastBlock ||
            Blocks - 1u > Block->Media->LastBlock - (UINT64)Lba)
            return Rc95LdrOutOfBounds;
    }
    if (Block->Media->IoAlign &&
        (!IsPowerOfTwo(Block->Media->IoAlign) ||
         Block->Media->IoAlign > RC95LDR_EFI_MAX_IO_ALIGN ||
         ((uintptr_t)Buffer % Block->Media->IoAlign) != 0))
        return Rc95LdrInvalidParameter;
    return MapEfiStatus(Block->ReadBlocks(
        Block, MediaId, (EFI_LBA)Lba, (UINTN)ByteCount, Buffer));
}

static RC95LDR_STATUS
UefiBlockGetHandleCount(void *Context, size_t *Count)
{
    RC95LDR_UEFI_SERVICE_CONTEXT *Provider =
        (RC95LDR_UEFI_SERVICE_CONTEXT *)Context;
    if (!Provider || !Count || Provider->StructureSize < sizeof(*Provider) ||
        Provider->Version != RC95LDR_MODULE_SERVICES_VERSION ||
        Provider->Shutdown || !Provider->DiscoveryInitialized)
        return Rc95LdrInvalidParameter;
    *Count = (size_t)Provider->BlockHandleCount;
    return Rc95LdrSuccess;
}

static RC95LDR_STATUS
UefiBlockGetHandleAt(void *Context, size_t Index, void **Handle)
{
    RC95LDR_UEFI_SERVICE_CONTEXT *Provider =
        (RC95LDR_UEFI_SERVICE_CONTEXT *)Context;
    if (!Provider || !Handle || Provider->StructureSize < sizeof(*Provider) ||
        Provider->Version != RC95LDR_MODULE_SERVICES_VERSION ||
        Provider->Shutdown || !Provider->DiscoveryInitialized)
        return Rc95LdrInvalidParameter;
    if (Index >= Provider->BlockHandleCount)
        return Rc95LdrOutOfBounds;
    *Handle = Provider->BlockHandles[Index];
    return *Handle ? Rc95LdrSuccess : Rc95LdrCorruptData;
}

static RC95LDR_STATUS
UefiBlockGetDevicePath(void *Context, void *Handle,
                       const uint8_t **Path, size_t *PathLength)
{
    RC95LDR_UEFI_SERVICE_CONTEXT *Provider =
        (RC95LDR_UEFI_SERVICE_CONTEXT *)Context;
    UINTN Index, Offset = 0;
    EFI_DEVICE_PATH_PROTOCOL *DevicePath = NULL;
    EFI_STATUS Status;

    if (!Provider || !Handle || !Path || !PathLength ||
        Provider->StructureSize < sizeof(*Provider) ||
        Provider->Version != RC95LDR_MODULE_SERVICES_VERSION ||
        Provider->Shutdown || !Provider->BootServices ||
        !Provider->BootServices->HandleProtocol)
        return Rc95LdrInvalidParameter;
    for (Index = 0; Index < Provider->BlockHandleCount; ++Index)
        if (Provider->BlockHandles[Index] == Handle) break;
    if (Index == Provider->BlockHandleCount)
        return Rc95LdrNotFound;

    Status = Provider->BootServices->HandleProtocol(
        (EFI_HANDLE)Handle, &UefiDevicePathGuid, (VOID **)&DevicePath);
    if (EFI_ERROR(Status) || !DevicePath)
        return MapEfiStatus(EFI_ERROR(Status) ? Status : EFI_NOT_FOUND);

    while (Offset < RC95LDR_EFI_MAX_DEVICE_PATH_BYTES)
    {
        UINT16 NodeLength;
        if (Offset > RC95LDR_EFI_MAX_DEVICE_PATH_BYTES - 4u)
            return Rc95LdrCorruptData;
        NodeLength = (UINT16)(DevicePath->Length[0] |
                              ((UINT16)DevicePath->Length[1] << 8));
        if (NodeLength < 4u ||
            NodeLength > RC95LDR_EFI_MAX_DEVICE_PATH_BYTES - Offset)
            return Rc95LdrCorruptData;
        Offset += NodeLength;
        if (DevicePath->Type == 0x7fu)
        {
            if (DevicePath->SubType != 0xffu || NodeLength != 4u)
                return Rc95LdrCorruptData;
            *Path = (const uint8_t *)DevicePath;
            *PathLength = (size_t)Offset;
            return Rc95LdrSuccess;
        }
        DevicePath = (EFI_DEVICE_PATH_PROTOCOL *)
            ((UINT8 *)DevicePath + NodeLength);
    }
    return Rc95LdrOutOfBounds;
}

static RC95LDR_STATUS
UefiBlockOpenBlockIo(void *Context, void *Handle,
                     RC95LDR_UEFI_BLOCK_IO *Io)
{
    RC95LDR_UEFI_SERVICE_CONTEXT *Provider =
        (RC95LDR_UEFI_SERVICE_CONTEXT *)Context;
    EFI_BLOCK_IO_PROTOCOL *Block = NULL;
    EFI_STATUS Status;
    UINTN Index;
    const uint8_t *PathBytes;
    size_t PathLength;
    RC95LDR_STATUS PathStatus;

    if (!Provider || !Handle || !Io ||
        Provider->StructureSize < sizeof(*Provider) ||
        Provider->Version != RC95LDR_MODULE_SERVICES_VERSION ||
        Provider->Shutdown || !Provider->BootServices)
        return Rc95LdrInvalidParameter;
    for (Index = 0; Index < Provider->BlockHandleCount; ++Index)
        if (Provider->BlockHandles[Index] == Handle) break;
    if (Index == Provider->BlockHandleCount ||
        Index >= RC95LDR_UEFI_MAX_BLOCK_DEVICES)
        return Rc95LdrOutOfBounds;

    Status = Provider->BootServices->HandleProtocol(
        (EFI_HANDLE)Handle, &UefiBlockIoGuid, (VOID **)&Block);
    if (EFI_ERROR(Status) || !Block || !Block->Media ||
        !Block->ReadBlocks)
        return MapEfiStatus(EFI_ERROR(Status) ? Status : EFI_UNSUPPORTED);
    PathStatus = UefiBlockGetDevicePath(
        Context, Handle, &PathBytes, &PathLength);
    if (PathStatus != Rc95LdrSuccess)
        return PathStatus;
    (void)PathBytes;
    memset(Io, 0, sizeof(*Io));
    Io->Protocol = Block;
    Io->Media.MediaId = Block->Media->MediaId;
    Io->Media.BlockSize = Block->Media->BlockSize;
    Io->Media.IoAlign = Block->Media->IoAlign;
    Io->Media.LastBlock = Block->Media->LastBlock;
    Io->Media.MediaPresent = Block->Media->MediaPresent ? 1u : 0u;
    Io->Media.RemovableMedia = Block->Media->RemovableMedia ? 1u : 0u;
    Io->Media.LogicalPartition = Block->Media->LogicalPartition ? 1u : 0u;
    Io->Media.ReadOnly = Block->Media->ReadOnly ? 1u : 0u;
    Io->ReadBlocks = UefiBlockRead;

    Provider->BlockDevices[Index].Handle = (EFI_HANDLE)Handle;
    Provider->BlockDevices[Index].Protocol = Block;
    Provider->BlockDevices[Index].DevicePath = PathBytes;
    Provider->BlockDevices[Index].DevicePathBytes = (UINTN)PathLength;
    Provider->BlockDevices[Index].InUse = 1;
    return Rc95LdrSuccess;
}

static RC95LDR_STATUS
UefiInitializeBlockDiscovery(RC95LDR_UEFI_SERVICE_CONTEXT *Provider)
{
    EFI_STATUS EfiStatus;
    UINTN HandleCount = 0, Index;
    EFI_HANDLE *Handles = NULL;
    EFI_PHYSICAL_ADDRESS ScratchAddress = 0;
    UINTN ScratchAlignment = RC95LDR_UEFI_PAGE_SIZE;
    uintptr_t ScratchAddressAligned;
    size_t DiscoveredPartitionCount = 0;
    RC95LDR_UEFI_FIRMWARE_OPS Ops;
    RC95LDR_STATUS Status;

    if (!Provider || !Provider->BootServices ||
        Provider->BootServices->Hdr.HeaderSize <
            offsetof(EFI_BOOT_SERVICES, FreePool) +
            sizeof(Provider->BootServices->FreePool) ||
        Provider->BootServices->Hdr.HeaderSize <
            offsetof(EFI_BOOT_SERVICES, LocateHandleBuffer) +
            sizeof(Provider->BootServices->LocateHandleBuffer) ||
        Provider->BootServices->Hdr.HeaderSize <
            offsetof(EFI_BOOT_SERVICES, HandleProtocol) +
            sizeof(Provider->BootServices->HandleProtocol) ||
        !Provider->BootServices->LocateHandleBuffer ||
        !Provider->BootServices->FreePool ||
        !Provider->BootServices->AllocatePages ||
        !Provider->BootServices->FreePages ||
        !Provider->BootServices->HandleProtocol)
        return Rc95LdrUnsupported;

    EfiStatus = Provider->BootServices->LocateHandleBuffer(
        ByProtocol, &UefiBlockIoGuid, NULL, &HandleCount, &Handles);
    if (EfiStatus == EFI_NOT_FOUND)
    {
        Provider->BlockHandleCount = 0;
        Provider->DiscoveryInitialized = 1;
        return Rc95LdrSuccess;
    }
    if (EFI_ERROR(EfiStatus))
        return MapEfiStatus(EfiStatus);
    /* LocateHandleBuffer must return a usable array for every non-empty
       result. A successful zero-handle result may still carry an allocation. */
    if (HandleCount && !Handles)
        return Rc95LdrCorruptData;
    if (!HandleCount)
    {
        if (Handles)
            Provider->BootServices->FreePool(Handles);
        Provider->BlockHandleCount = 0;
        Provider->DiscoveryInitialized = 1;
        return Rc95LdrSuccess;
    }
    if (HandleCount > RC95LDR_UEFI_MAX_BLOCK_DEVICES)
    {
        Provider->BootServices->FreePool(Handles);
        return Rc95LdrOutOfBounds;
    }

    Provider->BlockHandles = Handles;
    Provider->BlockHandleCount = HandleCount;
    for (Index = 0; Index < HandleCount; ++Index)
    {
        EFI_BLOCK_IO_PROTOCOL *Block = NULL;
        EfiStatus = Provider->BootServices->HandleProtocol(
            Handles[Index], &UefiBlockIoGuid, (VOID **)&Block);
        if (EFI_ERROR(EfiStatus) || !Block || !Block->Media ||
            !Block->ReadBlocks)
        {
            Provider->BootServices->FreePool(Handles);
            Provider->BlockHandles = NULL;
            Provider->BlockHandleCount = 0;
            return MapEfiStatus(EFI_ERROR(EfiStatus) ? EfiStatus : EFI_UNSUPPORTED);
        }
        if (Block->Media->IoAlign)
        {
            if (!IsPowerOfTwo(Block->Media->IoAlign) ||
                Block->Media->IoAlign > RC95LDR_EFI_MAX_IO_ALIGN)
            {
                Provider->BootServices->FreePool(Handles);
                Provider->BlockHandles = NULL;
                Provider->BlockHandleCount = 0;
                return Rc95LdrUnsupported;
            }
            if (Block->Media->IoAlign > ScratchAlignment)
                ScratchAlignment = Block->Media->IoAlign;
        }
        Provider->BlockDevices[Index].Handle = Handles[Index];
        Provider->BlockDevices[Index].Protocol = Block;
        Provider->BlockDevices[Index].InUse = 1;
    }
    memset(&Ops, 0, sizeof(Ops));
    Ops.Context = Provider;
    Ops.GetHandleCount = UefiBlockGetHandleCount;
    Ops.GetHandleAt = UefiBlockGetHandleAt;
    Ops.OpenBlockIo = UefiBlockOpenBlockIo;
    Ops.GetDevicePath = UefiBlockGetDevicePath;
    Provider->DiscoveryInitialized = 1;

    if (!HandleCount)
        return Rc95LdrSuccess;

    EfiStatus = Provider->BootServices->AllocatePages(
        AllocateAnyPages, (EFI_MEMORY_TYPE)RC95LDR_EFI_LOADER_DATA,
        RC95LDR_EFI_DISCOVERY_SCRATCH_PAGES, &ScratchAddress);
    if (EFI_ERROR(EfiStatus))
        return MapEfiStatus(EfiStatus);
    if (!ScratchAddress ||
        ScratchAddress > UINT64_MAX - (ScratchAlignment - 1u))
    {
        Provider->BootServices->FreePages(
            ScratchAddress, RC95LDR_EFI_DISCOVERY_SCRATCH_PAGES);
        return Rc95LdrOutOfBounds;
    }
    Provider->DiscoveryScratchBase = ScratchAddress;
    Provider->DiscoveryScratchPages = RC95LDR_EFI_DISCOVERY_SCRATCH_PAGES;
    if (ScratchAddress + ScratchAlignment - 1u > (EFI_PHYSICAL_ADDRESS)UINTPTR_MAX)
    {
        Provider->BootServices->FreePages(
            ScratchAddress, RC95LDR_EFI_DISCOVERY_SCRATCH_PAGES);
        Provider->DiscoveryScratchBase = 0;
        Provider->DiscoveryScratchPages = 0;
        return Rc95LdrOutOfBounds;
    }
    ScratchAddressAligned = (uintptr_t)((ScratchAddress + ScratchAlignment - 1u) &
                                    ~((UINT64)ScratchAlignment - 1u));

    Status = Rc95LdrUefiEnumeratePartitions(
        &Ops, Provider->Partitions, RC95LDR_UEFI_MAX_PARTITIONS,
        &DiscoveredPartitionCount, (void *)ScratchAddressAligned,
        RC95LDR_EFI_DISCOVERY_SCRATCH_SIZE);
    if (EFI_ERROR(Provider->BootServices->FreePages(
            ScratchAddress, RC95LDR_EFI_DISCOVERY_SCRATCH_PAGES)))
        return Rc95LdrIoError;
    Provider->DiscoveryScratchBase = 0;
    Provider->DiscoveryScratchPages = 0;
    if (Status == Rc95LdrNotFound)
    {
        Provider->PartitionCount = 0;
        return Rc95LdrSuccess;
    }
    if (Status != Rc95LdrSuccess)
    {
        Provider->PartitionCount = 0;
        return Status;
    }
    if (DiscoveredPartitionCount > RC95LDR_UEFI_MAX_PARTITIONS)
    {
        Provider->PartitionCount = 0;
        return Rc95LdrCorruptData;
    }
    Provider->PartitionCount = (UINTN)DiscoveredPartitionCount;

    /* Validate every cached record against the physical device bounds before
       exposing it through the module ABI. */
    for (Index = 0; Index < Provider->PartitionCount; ++Index)
    {
        RC95LDR_PARTITION_INFO *Part = &Provider->Partitions[Index];
        if (Part->ParentDiskHandleIndex >= HandleCount ||
            !Part->BlockCount ||
            Part->StartLba > UINT64_MAX - Part->BlockCount)
        {
            Provider->PartitionCount = 0;
            return Rc95LdrCorruptData;
        }
    }
    return Rc95LdrSuccess;
}

RC95LDR_STATUS
Rc95LdrUefiGetFirmwareOps(
    RC95LDR_UEFI_SERVICE_CONTEXT *Provider,
    RC95LDR_UEFI_FIRMWARE_OPS *Operations)
{
    if (!Provider || !Operations ||
        Provider->StructureSize < sizeof(*Provider) ||
        Provider->Version != RC95LDR_MODULE_SERVICES_VERSION ||
        Provider->Shutdown || !Provider->BootServices ||
        !Provider->DiscoveryInitialized)
        return Rc95LdrInvalidParameter;

    memset(Operations, 0, sizeof(*Operations));
    Operations->Context = Provider;
    Operations->GetHandleCount = UefiBlockGetHandleCount;
    Operations->GetHandleAt = UefiBlockGetHandleAt;
    Operations->OpenBlockIo = UefiBlockOpenBlockIo;
    Operations->GetDevicePath = UefiBlockGetDevicePath;
    return Rc95LdrSuccess;
}

static RC95LDR_STATUS RC95LDR_MODULE_ABI
UefiV2GetBlockDeviceCount(void *Context, UINT32 *Count)
{
    RC95LDR_UEFI_SERVICE_CONTEXT *Provider =
        (RC95LDR_UEFI_SERVICE_CONTEXT *)Context;
    if (!Provider || !Count || !Provider->DiscoveryInitialized ||
        Provider->Shutdown)
        return Rc95LdrInvalidParameter;
    *Count = (UINT32)Provider->BlockHandleCount;
    return Rc95LdrSuccess;
}

static RC95LDR_STATUS RC95LDR_MODULE_ABI
UefiV2GetBlockDeviceInfo(void *Context, UINT32 DeviceIndex,
                         RC95LDR_MODULE_BLOCK_DEVICE_INFO *Info)
{
    RC95LDR_UEFI_SERVICE_CONTEXT *Provider =
        (RC95LDR_UEFI_SERVICE_CONTEXT *)Context;
    EFI_BLOCK_IO_PROTOCOL *Block;
    UINT64 Count;
    if (!Provider || !Info || !Provider->DiscoveryInitialized ||
        Provider->Shutdown || DeviceIndex >= Provider->BlockHandleCount)
        return Rc95LdrInvalidParameter;
    Block = Provider->BlockDevices[DeviceIndex].Protocol;
    if (!Block || !Block->Media || !Block->Media->MediaPresent ||
        !Block->Media->BlockSize || Block->Media->LastBlock == UINT64_MAX)
        return Rc95LdrNotFound;
    Count = Block->Media->LastBlock + 1u;
    memset(Info, 0, sizeof(*Info));
    Info->StructureSize = sizeof(*Info);
    Info->Version = RC95LDR_MODULE_BLOCK_DEVICE_INFO_VERSION;
    Info->BlockCount = Count;
    Info->BlockSize = Block->Media->BlockSize;
    Info->Flags = RC95LDR_MODULE_BLOCK_DEVICE_READ_ONLY;
    Info->DeviceId = (UINT64)(uintptr_t)Provider->BlockHandles[DeviceIndex];
    return Rc95LdrValidateModuleBlockDeviceInfo(Info, sizeof(*Info));
}

static RC95LDR_STATUS RC95LDR_MODULE_ABI
UefiV2GetPartitionCount(void *Context, UINT32 DeviceIndex, UINT32 *Count)
{
    RC95LDR_UEFI_SERVICE_CONTEXT *Provider =
        (RC95LDR_UEFI_SERVICE_CONTEXT *)Context;
    UINTN Index;
    UINT32 Found = 0;
    if (!Provider || !Count || !Provider->DiscoveryInitialized ||
        Provider->Shutdown || DeviceIndex >= Provider->BlockHandleCount)
        return Rc95LdrInvalidParameter;
    for (Index = 0; Index < Provider->PartitionCount; ++Index)
        if (Provider->Partitions[Index].ParentDiskHandleIndex == DeviceIndex)
            ++Found;
    *Count = Found;
    return Rc95LdrSuccess;
}

static RC95LDR_STATUS RC95LDR_MODULE_ABI
UefiV2GetPartitionInfo(void *Context, UINT32 DeviceIndex,
                       UINT32 PartitionIndex,
                       RC95LDR_MODULE_PARTITION_INFO *Info)
{
    RC95LDR_UEFI_SERVICE_CONTEXT *Provider =
        (RC95LDR_UEFI_SERVICE_CONTEXT *)Context;
    RC95LDR_MODULE_BLOCK_DEVICE_INFO Device;
    UINTN Index;
    UINT32 Found = 0;
    RC95LDR_STATUS Status;
    const RC95LDR_PARTITION_INFO *Part = NULL;

    if (!Provider || !Info || !Provider->DiscoveryInitialized ||
        Provider->Shutdown || DeviceIndex >= Provider->BlockHandleCount)
        return Rc95LdrInvalidParameter;
    Status = UefiV2GetBlockDeviceInfo(Context, DeviceIndex, &Device);
    if (Status != Rc95LdrSuccess) return Status;
    for (Index = 0; Index < Provider->PartitionCount; ++Index)
    {
        if (Provider->Partitions[Index].ParentDiskHandleIndex != DeviceIndex)
            continue;
        if (Found++ == PartitionIndex)
        {
            Part = &Provider->Partitions[Index];
            break;
        }
    }
    if (!Part) return Rc95LdrNotFound;

    memset(Info, 0, sizeof(*Info));
    Info->StructureSize = sizeof(*Info);
    Info->Version = RC95LDR_MODULE_PARTITION_INFO_VERSION;
    Info->Scheme = Part->Style == Rc95LdrPartitionGpt
        ? RC95LDR_MODULE_PARTITION_GPT : RC95LDR_MODULE_PARTITION_MBR;
    Info->StartLba = Part->StartLba;
    Info->BlockCount = Part->BlockCount;
    if (Part->Style == Rc95LdrPartitionGpt)
    {
        memcpy(Info->TypeGuid, Part->TypeGuid, sizeof(Info->TypeGuid));
        memcpy(Info->UniqueGuid, Part->UniqueGuid, sizeof(Info->UniqueGuid));
    }
    else
        Info->MbrType = Part->MbrType;
    return Rc95LdrValidateModulePartitionInfo(Info, sizeof(*Info), &Device);
}

static RC95LDR_STATUS RC95LDR_MODULE_ABI
UefiV2ReadBlocks(void *Context, UINT32 DeviceIndex, uint64_t Lba,
                 UINT32 BlockCount, void *Buffer, size_t BufferLength)
{
    RC95LDR_UEFI_SERVICE_CONTEXT *Provider =
        (RC95LDR_UEFI_SERVICE_CONTEXT *)Context;
    EFI_BLOCK_IO_PROTOCOL *Block;
    UINT64 RequiredBytes, DeviceBlocks;
    if (!Provider || !Provider->DiscoveryInitialized ||
        Provider->Shutdown || DeviceIndex >= Provider->BlockHandleCount ||
        !BlockCount || !Buffer)
        return Rc95LdrInvalidParameter;
    Block = Provider->BlockDevices[DeviceIndex].Protocol;
    if (!Block || !Block->Media || !Block->Media->MediaPresent ||
        !Block->ReadBlocks || !Block->Media->BlockSize ||
        Block->Media->LastBlock == UINT64_MAX)
        return Rc95LdrNotFound;
    DeviceBlocks = Block->Media->LastBlock + 1u;
    if (Lba > DeviceBlocks || BlockCount > DeviceBlocks - Lba ||
        (UINT64)BlockCount > UINT64_MAX / Block->Media->BlockSize)
        return Rc95LdrOutOfBounds;
    RequiredBytes = (UINT64)BlockCount * Block->Media->BlockSize;
    if (RequiredBytes != BufferLength ||
        RequiredBytes > (UINT64)(~(UINTN)0))
        return Rc95LdrBufferTooSmall;
    return UefiBlockRead(Block, Block->Media->MediaId, Lba,
                         BufferLength, Buffer);
}

RC95LDR_STATUS
Rc95LdrUefiInitializeModuleServices(
    const RC95LDR_UEFI_IMAGE_CONTEXT *Image,
    UINT32 Architecture,
    RC95LDR_UEFI_SERVICE_CONTEXT *Provider,
    RC95LDR_MODULE_SERVICES *Services)
{
    EFI_STATUS Status;
    RC95LDR_EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FileSystem = NULL;
    EFI_GUID SimpleFileSystemGuid =
        { 0x964e5b22, 0x6459, 0x11d2, { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } };
    EFI_GUID FileInfoGuid =
        { 0x09576e92, 0x6d3f, 0x11d2, { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } };

    if (!Image || !Provider || !Services ||
        Image->StructureSize < sizeof(*Image) ||
        Image->Version != RC95LDR_UEFI_ENTRY_VERSION ||
        !Image->ImageHandle || !Image->SystemTable ||
        Image->SystemTable->Hdr.Signature != EFI_SYSTEM_TABLE_SIGNATURE ||
        Image->SystemTable->Hdr.HeaderSize <
            offsetof(EFI_SYSTEM_TABLE, BootServices) +
            sizeof(Image->SystemTable->BootServices) ||
        Image->SystemTable->BootServices != Image->BootServices ||
        !Image->LoadedImage ||
        Image->LoadedImage->SystemTable != Image->SystemTable ||
        !Image->LoadedImage->DeviceHandle || !Image->BootServices ||
        Image->BootServices->Hdr.Signature != EFI_BOOT_SERVICES_SIGNATURE ||
        Image->BootServices->Hdr.HeaderSize <
            offsetof(EFI_BOOT_SERVICES, FreePages) +
            sizeof(Image->BootServices->FreePages) ||
        !Image->BootServices->OpenProtocol ||
        !Image->BootServices->CloseProtocol ||
        !Image->BootServices->AllocatePages ||
        !Image->BootServices->FreePages ||
        (Architecture != Rc95LdrModuleI386 &&
         Architecture != Rc95LdrModuleAmd64))
        return Rc95LdrInvalidParameter;

    memset(Provider, 0, sizeof(*Provider));
    memset(Services, 0, sizeof(*Services));
    Provider->StructureSize = sizeof(*Provider);
    Provider->Version = RC95LDR_MODULE_SERVICES_VERSION;
    Provider->ImageHandle = Image->ImageHandle;
    Provider->DeviceHandle = Image->LoadedImage->DeviceHandle;
    Provider->BootServices = Image->BootServices;
    Provider->SimpleFileSystemGuid = SimpleFileSystemGuid;
    Provider->FileInfoGuid = FileInfoGuid;

    Status = Image->BootServices->OpenProtocol(
        Provider->DeviceHandle, &Provider->SimpleFileSystemGuid,
        (VOID **)&FileSystem, Provider->ImageHandle, NULL,
        RC95LDR_EFI_OPEN_PROTOCOL_GET_PROTOCOL);
    if (EFI_ERROR(Status) || !FileSystem || !FileSystem->OpenVolume)
    {
        if (!EFI_ERROR(Status))
            Image->BootServices->CloseProtocol(Provider->DeviceHandle,
                &Provider->SimpleFileSystemGuid, Provider->ImageHandle, NULL);
        return MapEfiStatus(EFI_ERROR(Status) ? Status : EFI_UNSUPPORTED);
    }
    Provider->FileSystem = FileSystem;
    Status = FileSystem->OpenVolume(FileSystem, &Provider->Root);
    if (EFI_ERROR(Status) || !Provider->Root || !Provider->Root->Open ||
        !Provider->Root->Close || !Provider->Root->GetInfo)
    {
        if (Provider->Root) Provider->Root->Close(Provider->Root);
        Provider->Root = NULL;
        Image->BootServices->CloseProtocol(Provider->DeviceHandle,
            &Provider->SimpleFileSystemGuid, Provider->ImageHandle, NULL);
        Provider->FileSystem = NULL;
        return MapEfiStatus(EFI_ERROR(Status) ? Status : EFI_UNSUPPORTED);
    }

    Services->Signature = RC95LDR_MODULE_SERVICES_SIGNATURE;
    Services->StructureSize = sizeof(*Services);
    Services->Version = RC95LDR_MODULE_SERVICES_VERSION;
    Services->Firmware = Rc95LdrModuleUefi;
    Services->Architecture = Architecture;
    Services->Context = Provider;
    Services->OpenFile = UefiOpenFile;
    Services->ReadFileAt = UefiReadFileAt;
    Services->CloseFile = UefiCloseFile;
    Services->AllocatePages = UefiAllocatePages;
    Services->FreePages = UefiFreePages;
    Services->ValidateAllocationExtent = UefiValidateAllocationExtent;
    Services->Quiesce = UefiQuiesceService;
    return Rc95LdrValidateModuleServices(Services, sizeof(*Services),
        Rc95LdrModuleUefi, Architecture);
}


RC95LDR_STATUS
Rc95LdrUefiInitializeModuleServicesV2(
    const RC95LDR_UEFI_IMAGE_CONTEXT *Image,
    UINT32 Architecture,
    RC95LDR_UEFI_SERVICE_CONTEXT *Provider,
    RC95LDR_MODULE_SERVICES_V2 *Services)
{
    RC95LDR_STATUS Status;
    if (!Services) return Rc95LdrInvalidParameter;
    memset(Services, 0, sizeof(*Services));

    /* Initialize the unchanged V1 prefix first. Existing file, page,
       allocation-attestation and Quiesce callbacks retain their ABI. */
    Status = Rc95LdrUefiInitializeModuleServices(
        Image, Architecture, Provider, &Services->V1);
    if (Status != Rc95LdrSuccess) return Status;

    Status = UefiInitializeBlockDiscovery(Provider);
    if (Status != Rc95LdrSuccess)
    {
        Rc95LdrUefiDestroyModuleServices(Provider);
        memset(Services, 0, sizeof(*Services));
        return Status;
    }

    Services->V1.Version = RC95LDR_MODULE_SERVICES_V2_VERSION;
    Services->V1.StructureSize = sizeof(*Services);
    Services->GetBlockDeviceCount = UefiV2GetBlockDeviceCount;
    Services->GetBlockDeviceInfo = UefiV2GetBlockDeviceInfo;
    Services->GetPartitionCount = UefiV2GetPartitionCount;
    Services->GetPartitionInfo = UefiV2GetPartitionInfo;
    Services->ReadBlocks = UefiV2ReadBlocks;

    return Rc95LdrValidateModuleServicesV2(
        Services, sizeof(*Services), Rc95LdrModuleUefi, Architecture);
}

RC95LDR_STATUS
Rc95LdrUefiQuiesceModuleServices(RC95LDR_UEFI_SERVICE_CONTEXT *Provider)
{
    UINTN Index;
    RC95LDR_STATUS Result = Rc95LdrSuccess;

    if (!Provider || Provider->StructureSize < sizeof(*Provider) ||
        Provider->Version != RC95LDR_MODULE_SERVICES_VERSION ||
        !Provider->BootServices)
        return Rc95LdrInvalidParameter;

    /* Shutdown prevents new operations, but repeated Quiesce calls retry any
       firmware close operation that failed during an earlier attempt. */
    for (Index = 0; Index < RC95LDR_UEFI_MAX_OPEN_FILES; ++Index)
    {
        RC95LDR_UEFI_FILE_SLOT *Slot = &Provider->Files[Index];
        if (Slot->InUse && Slot->Handle)
        {
            if (!Slot->Handle->Close ||
                EFI_ERROR(Slot->Handle->Close(Slot->Handle)))
                Result = Rc95LdrIoError;
            else
                memset(Slot, 0, sizeof(*Slot));
        }
        else
            memset(Slot, 0, sizeof(*Slot));
    }
    if (Provider->Root)
    {
        if (EFI_ERROR(Provider->Root->Close(Provider->Root)))
            Result = Rc95LdrIoError;
        else
            Provider->Root = NULL;
    }
    if (Provider->FileSystem)
    {
        if (EFI_ERROR(Provider->BootServices->CloseProtocol(
                Provider->DeviceHandle, &Provider->SimpleFileSystemGuid,
                Provider->ImageHandle, NULL)))
            Result = Rc95LdrIoError;
        else
            Provider->FileSystem = NULL;
    }
    if (Provider->DiscoveryScratchBase && Provider->DiscoveryScratchPages)
    {
        if (EFI_ERROR(Provider->BootServices->FreePages(
                Provider->DiscoveryScratchBase, Provider->DiscoveryScratchPages)))
            Result = Rc95LdrIoError;
        else
        {
            Provider->DiscoveryScratchBase = 0;
            Provider->DiscoveryScratchPages = 0;
        }
    }
    if (Provider->BlockHandles)
    {
        if (EFI_ERROR(Provider->BootServices->FreePool(Provider->BlockHandles)))
            Result = Rc95LdrIoError;
        else
        {
            Provider->BlockHandles = NULL;
            Provider->BlockHandleCount = 0;
            memset(Provider->BlockDevices, 0, sizeof(Provider->BlockDevices));
            Provider->DiscoveryInitialized = 0;
        }
    }
    Provider->Shutdown = 1;
    return Result;
}

void
Rc95LdrUefiDestroyModuleServices(RC95LDR_UEFI_SERVICE_CONTEXT *Provider)
{
    UINTN Index;
    if (!Provider || !Provider->BootServices) return;
    for (Index = 0; Index < RC95LDR_UEFI_MAX_OPEN_FILES; ++Index)
    {
        RC95LDR_UEFI_FILE_SLOT *Slot = &Provider->Files[Index];
        if (Slot->InUse && Slot->Handle)
        {
            /* Preserve records that cannot be closed. Losing the only
               reference would leak firmware-owned resources permanently. */
            if (Slot->Handle->Close &&
                !EFI_ERROR(Slot->Handle->Close(Slot->Handle)))
                memset(Slot, 0, sizeof(*Slot));
        }
        else
            memset(Slot, 0, sizeof(*Slot));
    }
    for (Index = 0; Index < RC95LDR_UEFI_MAX_ALLOCATIONS; ++Index)
    {
        RC95LDR_UEFI_ALLOCATION_SLOT *Slot = &Provider->Allocations[Index];
        if (Slot->InUse)
        {
            if (!EFI_ERROR(Provider->BootServices->FreePages(
                    Slot->RawBase,
                    (UINTN)(Slot->RawSize / RC95LDR_UEFI_PAGE_SIZE))))
                memset(Slot, 0, sizeof(*Slot));
        }
        else
            memset(Slot, 0, sizeof(*Slot));
    }
    if (Provider->Root && Provider->Root->Close &&
        !EFI_ERROR(Provider->Root->Close(Provider->Root)))
        Provider->Root = NULL;
    if (Provider->DiscoveryScratchBase && Provider->DiscoveryScratchPages &&
        Provider->BootServices->FreePages &&
        !EFI_ERROR(Provider->BootServices->FreePages(
            Provider->DiscoveryScratchBase, Provider->DiscoveryScratchPages)))
    {
        Provider->DiscoveryScratchBase = 0;
        Provider->DiscoveryScratchPages = 0;
    }
    if (Provider->BlockHandles && Provider->BootServices->FreePool &&
        !EFI_ERROR(Provider->BootServices->FreePool(Provider->BlockHandles)))
    {
        Provider->BlockHandles = NULL;
        Provider->BlockHandleCount = 0;
        memset(Provider->BlockDevices, 0, sizeof(Provider->BlockDevices));
        Provider->DiscoveryInitialized = 0;
    }
    if (Provider->FileSystem && Provider->BootServices->CloseProtocol &&
        !EFI_ERROR(Provider->BootServices->CloseProtocol(
            Provider->DeviceHandle, &Provider->SimpleFileSystemGuid,
            Provider->ImageHandle, NULL)))
        Provider->FileSystem = NULL;
    Provider->Shutdown = 1;
}
