#include "rc95ldruefiservices.h"
#include <assert.h>
#include <stddef.h>
#include <string.h>

typedef struct _TEST_TIME {
    UINT16 Year; UINT8 Month, Day, Hour, Minute, Second, Pad1;
    UINT32 Nanosecond; INT16 TimeZone; UINT8 Daylight, Pad2;
} TEST_TIME;
typedef struct _TEST_FILE_INFO {
    UINT64 Size, FileSize, PhysicalSize;
    TEST_TIME CreateTime, LastAccessTime, ModificationTime;
    UINT64 Attribute;
    CHAR16 FileName[1];
} TEST_FILE_INFO;

static RC95LDR_EFI_SIMPLE_FILE_SYSTEM_PROTOCOL Fs;
static RC95LDR_EFI_FILE_PROTOCOL Root, Child;
static EFI_BOOT_SERVICES Bs;
static EFI_SYSTEM_TABLE SystemTable;
static EFI_LOADED_IMAGE_PROTOCOL Loaded;
static UINT8 FileBytes[16] = "UEFI-SVC-TEST";
static UINT8 FakeDisk[32u * 512u];
static EFI_BLOCK_IO_MEDIA FakeMedia;
static EFI_BLOCK_IO_PROTOCOL FakeBlockIo;
static EFI_DEVICE_PATH_PROTOCOL FakeDevicePath;
static EFI_HANDLE FakeBlockHandle = (EFI_HANDLE)(UINTN)0x77;
static int ReturnFakeBlockHandle;
static int ReturnNullBlockHandleBuffer;
static UINT64 FilePosition;
static int ForceShortRead;
static int FailCloseProtocol, FailFileCloseCount;
static unsigned CloseFileCalls, CloseProtocolCalls, FreePageCalls;
static EFI_PHYSICAL_ADDRESS LastFreeMemory;
static UINTN LastFreePages;
static int FailFreePages, ForceMisalignedAllocation;
static int ForceNullFileSystem, ForceOpenVolumeFailure;
static int ForceMissingFileClose;
#if defined(_MSC_VER)
__declspec(align(8192)) static UINT8 PageBacking[1024 * 1024];
#else
static UINT8 PageBacking[1024 * 1024] __attribute__((aligned(8192)));
#endif
static UINTN NextPageOffset = 4096;

static EFI_STATUS EFIAPI TestFileClose(RC95LDR_EFI_FILE_PROTOCOL *This)
{
    (void)This;
    ++CloseFileCalls;
    if (FailFileCloseCount)
    {
        --FailFileCloseCount;
        return EFI_DEVICE_ERROR;
    }
    return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI TestFileSetPosition(
    RC95LDR_EFI_FILE_PROTOCOL *This, UINT64 Position)
{
    (void)This;
    if (Position > sizeof(FileBytes)) return EFI_INVALID_PARAMETER;
    FilePosition = Position; return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI TestFileRead(
    RC95LDR_EFI_FILE_PROTOCOL *This, UINTN *Size, VOID *Buffer)
{
    UINTN Available;
    (void)This;
    if (!Size || (!Buffer && *Size)) return EFI_INVALID_PARAMETER;
    Available = sizeof(FileBytes) - (UINTN)FilePosition;
    if (*Size > Available) *Size = Available;
    if (ForceShortRead && *Size) --(*Size);
    memcpy(Buffer, FileBytes + FilePosition, *Size);
    FilePosition += *Size;
    return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI TestFileGetInfo(
    RC95LDR_EFI_FILE_PROTOCOL *This, EFI_GUID *Guid, UINTN *Size, VOID *Buffer)
{
    TEST_FILE_INFO *Info = (TEST_FILE_INFO *)Buffer;
    UINTN Needed = offsetof(TEST_FILE_INFO, FileName) + sizeof(CHAR16);
    (void)This; (void)Guid;
    if (!Size || !Buffer || *Size < Needed) return EFI_BUFFER_TOO_SMALL;
    memset(Buffer, 0, Needed);
    Info->Size = Needed;
    Info->FileSize = sizeof(FileBytes);
    Info->PhysicalSize = sizeof(FileBytes);
    Info->FileName[0] = 'x';
    *Size = Needed;
    return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI TestRootOpen(
    RC95LDR_EFI_FILE_PROTOCOL *This, RC95LDR_EFI_FILE_HANDLE *NewHandle,
    CHAR16 *Path, UINT64 Mode, UINT64 Attributes)
{
    (void)This; (void)Mode; (void)Attributes;
    assert(Path && Path[0] == '\\');
    memset(&Child, 0, sizeof(Child));
    Child.Close = ForceMissingFileClose ? NULL : TestFileClose;
    Child.Read = TestFileRead;
    Child.SetPosition = TestFileSetPosition;
    Child.GetInfo = TestFileGetInfo;
    *NewHandle = &Child;
    return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI TestOpenVolume(
    RC95LDR_EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *This,
    RC95LDR_EFI_FILE_HANDLE *Volume)
{
    (void)This;
    if (ForceOpenVolumeFailure) return EFI_DEVICE_ERROR;
    *Volume = &Root;
    return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI TestOpenProtocol(
    EFI_HANDLE Handle, EFI_GUID *Guid, VOID **Interface,
    EFI_HANDLE Agent, EFI_HANDLE Controller, UINT32 Attributes)
{
    (void)Guid; (void)Agent; (void)Controller;
    assert(Handle == Loaded.DeviceHandle);
    assert(Attributes == EFI_OPEN_PROTOCOL_GET_PROTOCOL);
    *Interface = ForceNullFileSystem ? NULL : &Fs;
    return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI TestCloseProtocol(
    EFI_HANDLE Handle, EFI_GUID *Guid, EFI_HANDLE Agent, EFI_HANDLE Controller)
{
    (void)Handle; (void)Guid; (void)Agent; (void)Controller;
    ++CloseProtocolCalls;
    return FailCloseProtocol ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}
static EFI_STATUS EFIAPI TestAllocatePages(
    EFI_ALLOCATE_TYPE Type, EFI_MEMORY_TYPE MemoryType,
    UINTN Pages, EFI_PHYSICAL_ADDRESS *Memory)
{
    (void)Type; (void)MemoryType;
    assert(Pages && NextPageOffset + Pages * 4096 < sizeof(PageBacking));
    *Memory = (EFI_PHYSICAL_ADDRESS)(uintptr_t)
        (PageBacking + NextPageOffset + (ForceMisalignedAllocation ? 1u : 0u));
    NextPageOffset += Pages * 4096 + 4096;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI TestLocateHandleBuffer(
    EFI_LOCATE_SEARCH_TYPE SearchType, EFI_GUID *Protocol,
    VOID *SearchKey, UINTN *Count, EFI_HANDLE **Buffer)
{
    (void)SearchType; (void)Protocol; (void)SearchKey;
    if (!Count || !Buffer) return EFI_INVALID_PARAMETER;
    if (ReturnFakeBlockHandle)
    {
        *Count = 1;
        *Buffer = ReturnNullBlockHandleBuffer ? NULL : &FakeBlockHandle;
        return EFI_SUCCESS;
    }
    *Count = 0;
    *Buffer = NULL;
    return EFI_NOT_FOUND;
}

static EFI_STATUS EFIAPI TestBlockRead(
    EFI_BLOCK_IO_PROTOCOL *This, UINT32 MediaId, EFI_LBA Lba,
    UINTN BufferSize, VOID *Buffer)
{
    UINT64 Offset = (UINT64)Lba * 512u;
    (void)This;
    if (MediaId != FakeMedia.MediaId || !Buffer ||
        Offset > sizeof(FakeDisk) || BufferSize > sizeof(FakeDisk) - Offset)
        return EFI_INVALID_PARAMETER;
    memcpy(Buffer, FakeDisk + (size_t)Offset, BufferSize);
    return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI TestHandleProtocol(
    EFI_HANDLE Handle, EFI_GUID *Guid, VOID **Interface)
{
    if (!Handle || !Guid || !Interface || Handle != FakeBlockHandle)
        return EFI_INVALID_PARAMETER;
    if (Guid->Data1 == 0x964e5b21u)
        *Interface = &FakeBlockIo;
    else if (Guid->Data1 == 0x09576e91u)
        *Interface = &FakeDevicePath;
    else
        return EFI_NOT_FOUND;
    return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI TestFreePool(VOID *Buffer)
{
    (void)Buffer;
    return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI TestFreePages(EFI_PHYSICAL_ADDRESS Memory, UINTN Pages)
{
    assert(Memory && Pages);
    ++FreePageCalls;
    LastFreeMemory = Memory;
    LastFreePages = Pages;
    return FailFreePages ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}

int main(void)
{
    RC95LDR_UEFI_IMAGE_CONTEXT Image;
    RC95LDR_UEFI_SERVICE_CONTEXT Provider, FailedProvider, NoCloseProvider, V2Provider;
    RC95LDR_MODULE_SERVICES Services, FailedServices, NoCloseServices;
    RC95LDR_MODULE_SERVICES_V2 V2Services;
    UINT32 V2DeviceCount = UINT32_MAX, V2PartitionCount = UINT32_MAX;
    RC95LDR_MODULE_BLOCK_DEVICE_INFO V2DeviceInfo;
    RC95LDR_MODULE_PARTITION_INFO V2PartitionInfo;
    UINT8 BlockReadBuffer[512];
    RC95LDR_MODULE_FILE File;
    uint64_t Base = 0;
    uint64_t RawBase = 0;
    UINTN RawPages = 0;
    UINTN FreeCallsBefore;
    UINT8 ReadBuffer[4];
    RC95LDR_MODULE_FILE OpenFiles[RC95LDR_UEFI_MAX_OPEN_FILES];
    UINTN Index;

    memset(&Image, 0, sizeof(Image));
    memset(&Bs, 0, sizeof(Bs));
    memset(&Loaded, 0, sizeof(Loaded));
    memset(&SystemTable, 0, sizeof(SystemTable));
    memset(&Root, 0, sizeof(Root));
    memset(&Fs, 0, sizeof(Fs));
    Bs.Hdr.Signature = EFI_BOOT_SERVICES_SIGNATURE;
    Bs.Hdr.HeaderSize = sizeof(Bs);
    SystemTable.Hdr.Signature = EFI_SYSTEM_TABLE_SIGNATURE;
    SystemTable.Hdr.HeaderSize = sizeof(SystemTable);
    SystemTable.BootServices = &Bs;
    Bs.OpenProtocol = TestOpenProtocol;
    Bs.CloseProtocol = TestCloseProtocol;
    Bs.LocateHandleBuffer = TestLocateHandleBuffer;
    Bs.FreePool = TestFreePool;
    Bs.HandleProtocol = TestHandleProtocol;
    Bs.AllocatePages = TestAllocatePages;
    Bs.FreePages = TestFreePages;
    Fs.OpenVolume = TestOpenVolume;
    Root.Open = TestRootOpen;
    Root.Close = TestFileClose;
    Root.GetInfo = TestFileGetInfo;
    Loaded.DeviceHandle = (EFI_HANDLE)(UINTN)0x55;
    Loaded.SystemTable = &SystemTable;
    Image.StructureSize = sizeof(Image);
    Image.Version = RC95LDR_UEFI_ENTRY_VERSION;
    Image.ImageHandle = (EFI_HANDLE)(UINTN)0x44;
    Image.SystemTable = &SystemTable;
    Image.BootServices = &Bs;
    Image.LoadedImage = &Loaded;

    {
        UINT32 SavedHeaderSize = Bs.Hdr.HeaderSize;
        Bs.Hdr.HeaderSize = (UINT32)offsetof(EFI_BOOT_SERVICES, FreePages);
        assert(Rc95LdrUefiInitializeModuleServices(&Image,
            Rc95LdrModuleAmd64, &Provider, &Services) == Rc95LdrInvalidParameter);
        Bs.Hdr.HeaderSize = SavedHeaderSize;
    }
    {
        EFI_OPEN_PROTOCOL SavedOpenProtocol = Bs.OpenProtocol;
        Bs.OpenProtocol = NULL;
        assert(Rc95LdrUefiInitializeModuleServices(&Image,
            Rc95LdrModuleAmd64, &Provider, &Services) == Rc95LdrInvalidParameter);
        Bs.OpenProtocol = SavedOpenProtocol;
    }

    ForceNullFileSystem = 1;
    assert(Rc95LdrUefiInitializeModuleServices(&Image, Rc95LdrModuleAmd64,
        &FailedProvider, &FailedServices) == Rc95LdrUnsupported);
    assert(CloseProtocolCalls == 1);
    ForceNullFileSystem = 0;
    ForceOpenVolumeFailure = 1;
    assert(Rc95LdrUefiInitializeModuleServices(&Image, Rc95LdrModuleAmd64,
        &FailedProvider, &FailedServices) == Rc95LdrIoError);
    assert(CloseProtocolCalls == 2);
    ForceOpenVolumeFailure = 0;
    CloseProtocolCalls = 0;

    /* The concrete V2 provider must preserve the complete V1 prefix while
       publishing all five block/partition callbacks. An empty firmware
       Block I/O handle set is valid and is reported as an empty inventory. */
    assert(Rc95LdrUefiInitializeModuleServicesV2(&Image,
        Rc95LdrModuleAmd64, &V2Provider, &V2Services) == Rc95LdrSuccess);
    assert(V2Services.V1.Version == RC95LDR_MODULE_SERVICES_V2_VERSION);
    assert(V2Services.V1.StructureSize == sizeof(V2Services));
    assert(V2Services.V1.Context == &V2Provider);
    assert(Rc95LdrValidateModuleServicesV2(&V2Services,
        sizeof(V2Services), Rc95LdrModuleUefi,
        Rc95LdrModuleAmd64) == Rc95LdrSuccess);
    assert(V2Services.GetBlockDeviceCount(V2Services.V1.Context,
        &V2DeviceCount) == Rc95LdrSuccess);
    assert(V2DeviceCount == 0);
    assert(V2Services.GetPartitionCount(V2Services.V1.Context, 0,
        &V2DeviceCount) == Rc95LdrInvalidParameter);
    Rc95LdrUefiDestroyModuleServices(&V2Provider);
    CloseProtocolCalls = 0;

    /* A firmware LocateHandleBuffer success with a nonzero count but a
       null handle array is malformed; reject it before indexing the array. */
    ReturnFakeBlockHandle = 1;
    ReturnNullBlockHandleBuffer = 1;
    assert(Rc95LdrUefiInitializeModuleServicesV2(&Image,
        Rc95LdrModuleAmd64, &V2Provider, &V2Services) == Rc95LdrCorruptData);
    ReturnNullBlockHandleBuffer = 0;
    ReturnFakeBlockHandle = 0;

    /* Exercise the concrete EFI Block I/O and Device Path adapters with a
       synthetic MBR disk. The V2 view must retain its MBR type and route
       reads through EFI_BLOCK_IO_PROTOCOL.ReadBlocks. */
    memset(FakeDisk, 0, sizeof(FakeDisk));
    memset(&FakeMedia, 0, sizeof(FakeMedia));
    memset(&FakeBlockIo, 0, sizeof(FakeBlockIo));
    memset(&FakeDevicePath, 0, sizeof(FakeDevicePath));
    FakeDisk[440] = 0x78; FakeDisk[441] = 0x56;
    FakeDisk[442] = 0x34; FakeDisk[443] = 0x12;
    FakeDisk[446] = 0x00;
    FakeDisk[450] = 0x83;
    FakeDisk[454] = 1;
    FakeDisk[458] = 10;
    FakeDisk[510] = 0x55; FakeDisk[511] = 0xaa;
    FakeMedia.MediaId = 7;
    FakeMedia.MediaPresent = TRUE;
    FakeMedia.BlockSize = 512;
    FakeMedia.IoAlign = 1;
    FakeMedia.LastBlock = 31;
    FakeBlockIo.Media = &FakeMedia;
    FakeBlockIo.ReadBlocks = TestBlockRead;
    FakeDevicePath.Type = 0x7f;
    FakeDevicePath.SubType = 0xff;
    FakeDevicePath.Length[0] = 4;
    FakeDevicePath.Length[1] = 0;
    ReturnFakeBlockHandle = 1;
    assert(Rc95LdrUefiInitializeModuleServicesV2(&Image,
        Rc95LdrModuleAmd64, &V2Provider, &V2Services) == Rc95LdrSuccess);
    assert(V2Services.GetBlockDeviceCount(V2Services.V1.Context,
        &V2DeviceCount) == Rc95LdrSuccess && V2DeviceCount == 1);
    assert(V2Services.GetBlockDeviceInfo(V2Services.V1.Context, 0,
        &V2DeviceInfo) == Rc95LdrSuccess);
    assert(V2DeviceInfo.BlockCount == 32 && V2DeviceInfo.BlockSize == 512);
    assert(V2Services.GetPartitionCount(V2Services.V1.Context, 0,
        &V2PartitionCount) == Rc95LdrSuccess && V2PartitionCount == 1);
    assert(V2Services.GetPartitionInfo(V2Services.V1.Context, 0, 0,
        &V2PartitionInfo) == Rc95LdrSuccess);
    assert(V2PartitionInfo.Scheme == RC95LDR_MODULE_PARTITION_MBR);
    assert(V2PartitionInfo.MbrType == 0x83);
    assert(V2Services.ReadBlocks(V2Services.V1.Context, 0, 0, 1,
        BlockReadBuffer, sizeof(BlockReadBuffer)) == Rc95LdrSuccess);
    assert(BlockReadBuffer[510] == 0x55 && BlockReadBuffer[511] == 0xaa);
    /* Firmware block adapter rejects reads that would cross the media end,
       including multi-block requests starting at the final valid LBA. */
    assert(V2Services.ReadBlocks(V2Services.V1.Context, 0, 31, 1,
        BlockReadBuffer, sizeof(BlockReadBuffer)) == Rc95LdrSuccess);
    assert(V2Services.ReadBlocks(V2Services.V1.Context, 0, 32, 1,
        BlockReadBuffer, sizeof(BlockReadBuffer)) == Rc95LdrOutOfBounds);
    assert(V2Services.ReadBlocks(V2Services.V1.Context, 0, 31, 2,
        BlockReadBuffer, sizeof(BlockReadBuffer)) == Rc95LdrOutOfBounds);
    assert(V2Services.ReadBlocks(V2Services.V1.Context, 0, UINT64_MAX, 1,
        BlockReadBuffer, sizeof(BlockReadBuffer)) == Rc95LdrOutOfBounds);
    Rc95LdrUefiDestroyModuleServices(&V2Provider);
    ReturnFakeBlockHandle = 0;
    CloseProtocolCalls = 0;

    /* A successful Open with a malformed protocol lacking Close must remain
       tracked so a later Quiesce can retry cleanup if the method appears. */
    assert(Rc95LdrUefiInitializeModuleServices(&Image, Rc95LdrModuleAmd64,
        &NoCloseProvider, &NoCloseServices) == Rc95LdrSuccess);
    {
        static const uint16_t NoClosePath[] = { '\\', 'N', 0 };
        ForceMissingFileClose = 1;
        assert(NoCloseServices.OpenFile(NoCloseServices.Context,
            NoClosePath, &File) == Rc95LdrUnsupported);
        assert(NoCloseProvider.Files[0].InUse);
        assert(NoCloseServices.Quiesce(NoCloseServices.Context)
            == Rc95LdrIoError);
        assert(NoCloseProvider.Files[0].InUse);
        ForceMissingFileClose = 0;
        Child.Close = TestFileClose;
        assert(NoCloseServices.Quiesce(NoCloseServices.Context)
            == Rc95LdrSuccess);
        assert(!NoCloseProvider.Files[0].InUse);
    }

    /* Reset protocol-close accounting before the V1 retry sequence. */
    CloseProtocolCalls = 0;
    /* Isolate the V1 file-close assertion from the preceding cleanup tests. */
    CloseFileCalls = 0;
    assert(Rc95LdrUefiInitializeModuleServices(&Image, Rc95LdrModuleAmd64,
        &Provider, &Services) == Rc95LdrSuccess);
    assert(Rc95LdrValidateModuleServices(&Services, sizeof(Services),
        Rc95LdrModuleUefi, Rc95LdrModuleAmd64) == Rc95LdrSuccess);
    assert(Services.Context == &Provider);

    {
        static const uint16_t Path[] = { '\\', 'E','F','I','/','B','O','O','T','/','X',0 };
        assert(Services.OpenFile(Services.Context, Path, &File) == Rc95LdrSuccess);
        assert(File.FileSize == sizeof(FileBytes));
        assert(Services.ReadFileAt(Services.Context, &File, 2,
            ReadBuffer, sizeof(ReadBuffer)) == Rc95LdrSuccess);
        assert(memcmp(ReadBuffer, "FI-S", sizeof(ReadBuffer)) == 0);
        assert(Services.ReadFileAt(Services.Context, &File,
            File.FileSize, ReadBuffer, 1) == Rc95LdrOutOfBounds);
        ForceShortRead = 1;
        assert(Services.ReadFileAt(Services.Context, &File, 0,
            ReadBuffer, sizeof(ReadBuffer)) == Rc95LdrIoError);
        ForceShortRead = 0;
        Services.CloseFile(Services.Context, &File);
        assert(File.FileContext == NULL && File.FileSize == 0);
        assert(CloseFileCalls == 1);
    }

    {
        static const uint16_t RetryPath[] = { '\\', 'R', 0 };
        assert(Services.OpenFile(Services.Context, RetryPath, &File)
            == Rc95LdrSuccess);
        FailFileCloseCount = 1;
        Services.CloseFile(Services.Context, &File);
        assert(File.FileContext != NULL && File.FileSize == sizeof(FileBytes));
        assert(Services.ReadFileAt(Services.Context, &File, 0,
            ReadBuffer, sizeof(ReadBuffer)) == Rc95LdrSuccess);
        Services.CloseFile(Services.Context, &File);
        assert(File.FileContext == NULL && File.FileSize == 0);
    }

    {
        static const uint16_t Traversal[] = { '\\', '.', '.', '\\', 'X', 0 };
        uint16_t LongPath[RC95LDR_UEFI_FILE_PATH_CHARS + 1u];
        assert(Services.OpenFile(Services.Context, Traversal, &File)
            == Rc95LdrInvalidParameter);
        LongPath[0] = '\\';
        for (Index = 1; Index < RC95LDR_UEFI_FILE_PATH_CHARS; ++Index)
            LongPath[Index] = 'A';
        LongPath[RC95LDR_UEFI_FILE_PATH_CHARS] = 0;
        assert(Services.OpenFile(Services.Context, LongPath, &File)
            == Rc95LdrOutOfBounds);
        static const uint16_t SinglePath[] = { '\\', 'X', 0 };
        for (Index = 0; Index < RC95LDR_UEFI_MAX_OPEN_FILES; ++Index)
            assert(Services.OpenFile(Services.Context, SinglePath,
                &OpenFiles[Index]) == Rc95LdrSuccess);
        assert(Services.OpenFile(Services.Context, SinglePath, &File)
            == Rc95LdrOutOfMemory);
        for (Index = 0; Index < RC95LDR_UEFI_MAX_OPEN_FILES; ++Index)
            Services.CloseFile(Services.Context, &OpenFiles[Index]);
    }

    assert(Services.AllocatePages(Services.Context, UINT64_MAX, 4096, &Base)
        == Rc95LdrIntegerOverflow);
    assert(Services.AllocatePages(Services.Context, 4096, 3000, &Base)
        == Rc95LdrInvalidParameter);
    ForceMisalignedAllocation = 1;
    assert(Services.AllocatePages(Services.Context, 4096, 4096, &Base)
        == Rc95LdrOutOfBounds);
    assert(Base == 0 && LastFreePages == 1u);
    ForceMisalignedAllocation = 0;
    /* The malformed allocation was freed; reuse its test backing range so
       the next allocation exercises a nonzero alignment prefix. */
    NextPageOffset = 4096;
    assert(Services.AllocatePages(Services.Context, 5000, 8192, &Base)
        == Rc95LdrSuccess);
    assert(Base && (Base & 8191u) == 0);
    assert(Services.ValidateAllocationExtent(Services.Context, Base,
        Base, 5000, 8192) == Rc95LdrSuccess);
    assert(Services.ValidateAllocationExtent(Services.Context, Base,
        Base + 1, 5000, 8192) == Rc95LdrInvalidParameter);
    assert(Provider.Allocations[0].Base == Base);
    RawBase = Provider.Allocations[0].RawBase;
    RawPages = (UINTN)(Provider.Allocations[0].RawSize / 4096u);
    assert(RawBase != Base && RawPages == 3u);

    /* A mismatched free must not release a live allocation. A firmware
       FreePages failure must also preserve the tracking record for retry. */
    Services.FreePages(Services.Context, Base, 4096);
    Services.FreePages(Services.Context, Base, 5001);
    assert(Services.ValidateAllocationExtent(Services.Context, Base,
        Base, 1, 4096) == Rc95LdrSuccess);
    FailFreePages = 1;
    Services.FreePages(Services.Context, Base, 5000);
    assert(LastFreeMemory == RawBase && LastFreePages == RawPages);
    assert(Services.ValidateAllocationExtent(Services.Context, Base,
        Base, 1, 4096) == Rc95LdrSuccess);
    FailFreePages = 0;
    Services.FreePages(Services.Context, Base, 5000);
    assert(LastFreeMemory == RawBase && LastFreePages == RawPages);
    assert(Services.ValidateAllocationExtent(Services.Context, Base,
        Base, 1, 4096) == Rc95LdrOutOfBounds);
    FreeCallsBefore = FreePageCalls;
    Services.FreePages(Services.Context, Base, 5000);
    assert(FreePageCalls == FreeCallsBefore);

    for (Index = 0; Index < RC95LDR_UEFI_MAX_ALLOCATIONS; ++Index)
    {
        uint64_t Allocation = 0;
        assert(Services.AllocatePages(Services.Context, 4096, 4096,
            &Allocation) == Rc95LdrSuccess);
        if (Index == 0) Base = Allocation;
    }
    {
        uint64_t Exhausted = 0;
        assert(Services.AllocatePages(Services.Context, 4096, 4096,
            &Exhausted) == Rc95LdrOutOfMemory);
    }

    {
        static const uint16_t SinglePath[] = { '\\', 'X', 0 };
        assert(Services.OpenFile(Services.Context, SinglePath, &File)
            == Rc95LdrSuccess);
        FailFileCloseCount = 1;
        FailCloseProtocol = 1;
        assert(Services.Quiesce(Services.Context) == Rc95LdrIoError);
        assert(Provider.Shutdown == 1);
        FailCloseProtocol = 0;
        /* Failed file/protocol closes remain tracked and are retried. */
        assert(Services.Quiesce(Services.Context) == Rc95LdrSuccess);
        assert(Services.Quiesce(Services.Context) == Rc95LdrSuccess);
        assert(Services.ReadFileAt(Services.Context, &File, 0,
            ReadBuffer, sizeof(ReadBuffer)) == Rc95LdrInvalidParameter);
        assert(Services.OpenFile(Services.Context, SinglePath, &File)
            == Rc95LdrInvalidParameter);
        assert(Services.AllocatePages(Services.Context, 4096, 4096, &Base)
            == Rc95LdrInvalidParameter);
        assert(Services.ValidateAllocationExtent(Services.Context, Base,
            Base, 1, 4096) == Rc95LdrInvalidParameter);
        FreeCallsBefore = FreePageCalls;
        Services.FreePages(Services.Context, Base, 4096);
        assert(FreePageCalls == FreeCallsBefore);
    }
    assert(CloseProtocolCalls == 2);
    assert(CloseFileCalls >= RC95LDR_UEFI_MAX_OPEN_FILES + 5u);
    FreeCallsBefore = FreePageCalls;
    FailFreePages = 1;
    Rc95LdrUefiDestroyModuleServices(&Provider);
    assert(CloseProtocolCalls == 2);
    assert(FreePageCalls == FreeCallsBefore + RC95LDR_UEFI_MAX_ALLOCATIONS);
    assert(Provider.Allocations[0].InUse);
    FailFreePages = 0;
    FreeCallsBefore = FreePageCalls;
    Rc95LdrUefiDestroyModuleServices(&Provider);
    assert(FreePageCalls == FreeCallsBefore + RC95LDR_UEFI_MAX_ALLOCATIONS);
    assert(!Provider.Allocations[0].InUse);
    assert(LastFreePages == 1u);
    assert(Services.Context == &Provider);
    return 0;
}
