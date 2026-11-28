#include "rc95ldruefientry.h"
#include "rc95ldruefiservices.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

static EFI_SYSTEM_TABLE SystemTable;
static EFI_BOOT_SERVICES BootServices;
static EFI_LOADED_IMAGE_PROTOCOL LoadedImage;
static EFI_LOADED_IMAGE_PROTOCOL ChildLoadedImage;
static RC95LDR_EFI_SIMPLE_FILE_SYSTEM_PROTOCOL MockFileSystem;
static RC95LDR_EFI_FILE_PROTOCOL MockRootFile;
static unsigned LoadCalls, StartCalls, UnloadCalls;
static int ModuleHandoffSeen;
static int ModuleHandoffValidated;
static int FailUnload;
static unsigned OpenProtocolCalls, CloseProtocolCalls;

static EFI_STATUS EFIAPI MockFileClose(RC95LDR_EFI_FILE_PROTOCOL *File)
{
    (void)File;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI MockFileOpen(
    RC95LDR_EFI_FILE_PROTOCOL *Root, RC95LDR_EFI_FILE_HANDLE *NewFile,
    CHAR16 *Path, UINT64 Mode, UINT64 Attributes)
{
    (void)Root; (void)NewFile; (void)Path; (void)Mode; (void)Attributes;
    return EFI_UNSUPPORTED;
}

static EFI_STATUS EFIAPI MockFileGetInfo(
    RC95LDR_EFI_FILE_PROTOCOL *File, EFI_GUID *Guid, UINTN *Size, VOID *Buffer)
{
    (void)File; (void)Guid; (void)Size; (void)Buffer;
    return EFI_UNSUPPORTED;
}

static EFI_STATUS EFIAPI MockOpenVolume(
    RC95LDR_EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FileSystem,
    RC95LDR_EFI_FILE_HANDLE *Root)
{
    (void)FileSystem;
    fprintf(stderr, "uefi mock open volume\\n");
    *Root = &MockRootFile;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI MockOpenProtocol(
    EFI_HANDLE Handle, EFI_GUID *Protocol, VOID **Interface,
    EFI_HANDLE Agent, EFI_HANDLE Controller, UINT32 Attributes)
{
    (void)Protocol; (void)Controller;
    fprintf(stderr, "uefi mock open protocol\\n");
    assert(Handle == ChildLoadedImage.DeviceHandle);
    assert(Agent == (EFI_HANDLE)(UINTN)2);
    assert(Attributes == EFI_OPEN_PROTOCOL_GET_PROTOCOL);
    ++OpenProtocolCalls;
    *Interface = &MockFileSystem;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI MockCloseProtocol(
    EFI_HANDLE Handle, EFI_GUID *Protocol, EFI_HANDLE Agent,
    EFI_HANDLE Controller)
{
    (void)Handle; (void)Protocol; (void)Controller;
    assert(Agent == (EFI_HANDLE)(UINTN)2);
    fprintf(stderr, "uefi mock close protocol\\n");
    ++CloseProtocolCalls;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI MockAllocatePages(
    EFI_ALLOCATE_TYPE Type, EFI_MEMORY_TYPE MemoryType, UINTN Pages,
    EFI_PHYSICAL_ADDRESS *Memory)
{
    (void)Type; (void)MemoryType; (void)Pages; (void)Memory;
    return EFI_OUT_OF_RESOURCES;
}

static EFI_STATUS EFIAPI MockFreePages(
    EFI_PHYSICAL_ADDRESS Memory, UINTN Pages)
{
    (void)Memory; (void)Pages;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI MockHandleProtocol(
    EFI_HANDLE Handle, EFI_GUID *Protocol, VOID **Interface)
{
    (void)Protocol;
    if (Handle == (EFI_HANDLE)(UINTN)1)
        *Interface = &LoadedImage;
    else if (Handle == (EFI_HANDLE)(UINTN)2)
        *Interface = &ChildLoadedImage;
    else
        return EFI_NOT_FOUND;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI MockLoadImage(
    BOOLEAN BootPolicy, EFI_HANDLE ParentImageHandle,
    EFI_DEVICE_PATH_PROTOCOL *DevicePath, VOID *SourceBuffer,
    UINTN SourceSize, EFI_HANDLE *ImageHandle)
{
    (void)BootPolicy; (void)ParentImageHandle; (void)DevicePath;
    (void)SourceBuffer; (void)SourceSize;
    ++LoadCalls;
    *ImageHandle = (EFI_HANDLE)(UINTN)2;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI MockStartImage(
    EFI_HANDLE ImageHandle, UINTN *ExitDataSize, CHAR16 **ExitData)
{
    (void)ImageHandle;
    ++StartCalls;
    fprintf(stderr, "uefi mock start image\\n");
    if (ChildLoadedImage.LoadOptions &&
        ChildLoadedImage.LoadOptionsSize == sizeof(RC95LDR_MODULE_HANDOFF))
    {
        const RC95LDR_MODULE_HANDOFF *Handoff =
            (const RC95LDR_MODULE_HANDOFF *)ChildLoadedImage.LoadOptions;
        assert(Handoff->Signature == RC95LDR_MODULE_SIGNATURE);
        assert(Handoff->ModuleKind == Rc95LdrModuleVeneer);
        assert(Handoff->Firmware == Rc95LdrModuleUefi);
        assert(Handoff->Architecture == Rc95LdrModuleAmd64);
        assert(Handoff->Flags == RC95LDR_MODULE_FLAG_FIRMWARE_MANAGED_ENTRY);
        assert(Handoff->ImageBase == 0x200000);
        assert(Handoff->EntryPoint == Handoff->ImageBase);
        assert(Handoff->ParentHandoff == 0);
        assert(Handoff->Services != 0);
        assert(Handoff->ServicesSize == sizeof(RC95LDR_MODULE_SERVICES_V2));
        {
            const RC95LDR_MODULE_SERVICES_V2 *Services =
                (const RC95LDR_MODULE_SERVICES_V2 *)(uintptr_t)Handoff->Services;
            const RC95LDR_UEFI_SERVICE_CONTEXT *Provider = Services->V1.Context;
            assert(Services->V1.Version == RC95LDR_MODULE_SERVICES_V2_VERSION);
            assert(Services->GetBlockDeviceCount && Services->GetBlockDeviceInfo &&
                   Services->GetPartitionCount && Services->GetPartitionInfo &&
                   Services->ReadBlocks);
            assert(Provider->DeviceHandle == ChildLoadedImage.DeviceHandle);
        }
        {
            RC95LDR_UEFI_IMAGE_CONTEXT ChildContext;
            const RC95LDR_MODULE_HANDOFF *Validated = NULL;
            memset(&ChildContext, 0, sizeof(ChildContext));
            ChildContext.StructureSize = sizeof(ChildContext);
            ChildContext.Version = RC95LDR_UEFI_ENTRY_VERSION;
            ChildContext.SystemTable = &SystemTable;
            ChildContext.LoadedImage = &ChildLoadedImage;
            assert(Rc95LdrUefiGetModuleHandoff(&ChildContext,
                Rc95LdrModuleVeneer, Rc95LdrModuleAmd64,
                &Validated) == Rc95LdrSuccess);
            assert(Validated == Handoff);
            {
                const RC95LDR_MODULE_SERVICES *ModuleServices =
                    (const RC95LDR_MODULE_SERVICES *)(uintptr_t)Handoff->Services;
                RC95LDR_UEFI_SERVICE_CONTEXT *Provider =
                    (RC95LDR_UEFI_SERVICE_CONTEXT *)ModuleServices->Context;
                assert(ModuleServices->Quiesce(ModuleServices->Context)
                    == Rc95LdrSuccess);
                assert(Provider->Shutdown == 1);
            }
            ModuleHandoffValidated = 1;
        }
        ModuleHandoffSeen = 1;
    }
    *ExitDataSize = 0;
    *ExitData = NULL;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI MockFreePool(VOID *Buffer)
{
    (void)Buffer;
    return EFI_SUCCESS;
}


static EFI_STATUS EFIAPI MockLocateHandleBuffer(
    EFI_LOCATE_SEARCH_TYPE SearchType, EFI_GUID *Protocol,
    VOID *SearchKey, UINTN *Count, EFI_HANDLE **Buffer)
{
    (void)SearchType; (void)Protocol; (void)SearchKey;
    if (!Count || !Buffer) return EFI_INVALID_PARAMETER;
    *Count = 0;
    *Buffer = NULL;
    return EFI_NOT_FOUND;
}
static EFI_STATUS EFIAPI MockUnloadImage(EFI_HANDLE ImageHandle)
{
    (void)ImageHandle;
    ++UnloadCalls;
    return FailUnload ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}

static RC95LDR_STATUS RC95LDR_MODULE_ABI MockV2OpenFile(void *Context, const uint16_t *Path, RC95LDR_MODULE_FILE *File)
{ (void)Context; (void)Path; (void)File; return Rc95LdrUnsupported; }
static RC95LDR_STATUS RC95LDR_MODULE_ABI MockV2ReadFileAt(void *Context, const RC95LDR_MODULE_FILE *File, uint64_t Offset, void *Buffer, size_t Length)
{ (void)Context; (void)File; (void)Offset; (void)Buffer; (void)Length; return Rc95LdrUnsupported; }
static void RC95LDR_MODULE_ABI MockV2CloseFile(void *Context, RC95LDR_MODULE_FILE *File)
{ (void)Context; (void)File; }
static RC95LDR_STATUS RC95LDR_MODULE_ABI MockV2AllocatePages(void *Context, uint64_t Size, uint64_t Alignment, uint64_t *Base)
{ (void)Context; (void)Size; (void)Alignment; (void)Base; return Rc95LdrUnsupported; }
static void RC95LDR_MODULE_ABI MockV2FreePages(void *Context, uint64_t Base, uint64_t Size)
{ (void)Context; (void)Base; (void)Size; }
static RC95LDR_STATUS RC95LDR_MODULE_ABI MockV2ValidateExtent(void *Context, uint64_t Physical, uint64_t Virtual, uint64_t Size, uint64_t Alignment)
{ (void)Context; (void)Physical; (void)Virtual; (void)Size; (void)Alignment; return Rc95LdrUnsupported; }
static RC95LDR_STATUS RC95LDR_MODULE_ABI MockV2Quiesce(void *Context)
{ (void)Context; return Rc95LdrSuccess; }

static RC95LDR_STATUS RC95LDR_MODULE_ABI MockV2DeviceCount(void *Context, uint32_t *Count)
{ (void)Context; *Count = 0; return Rc95LdrSuccess; }
static RC95LDR_STATUS RC95LDR_MODULE_ABI MockV2DeviceInfo(void *Context, uint32_t Index, RC95LDR_MODULE_BLOCK_DEVICE_INFO *Info)
{ (void)Context; (void)Index; (void)Info; return Rc95LdrNotFound; }
static RC95LDR_STATUS RC95LDR_MODULE_ABI MockV2PartitionCount(void *Context, uint32_t Index, uint32_t *Count)
{ (void)Context; (void)Index; *Count = 0; return Rc95LdrSuccess; }
static RC95LDR_STATUS RC95LDR_MODULE_ABI MockV2PartitionInfo(void *Context, uint32_t Index, uint32_t Partition, RC95LDR_MODULE_PARTITION_INFO *Info)
{ (void)Context; (void)Index; (void)Partition; (void)Info; return Rc95LdrNotFound; }
static RC95LDR_STATUS RC95LDR_MODULE_ABI MockV2ReadBlocks(void *Context, uint32_t Index, uint64_t Lba, uint32_t Blocks, void *Buffer, size_t Capacity)
{ (void)Context; (void)Index; (void)Lba; (void)Blocks; (void)Buffer; (void)Capacity; return Rc95LdrUnsupported; }

static void InitializeTables(void)
{
    memset(&SystemTable, 0, sizeof(SystemTable));
    memset(&BootServices, 0, sizeof(BootServices));
    memset(&LoadedImage, 0, sizeof(LoadedImage));
    memset(&ChildLoadedImage, 0, sizeof(ChildLoadedImage));
    memset(&MockFileSystem, 0, sizeof(MockFileSystem));
    memset(&MockRootFile, 0, sizeof(MockRootFile));
    SystemTable.Hdr.Signature = EFI_SYSTEM_TABLE_SIGNATURE;
    SystemTable.Hdr.Revision = EFI_2_31_SYSTEM_TABLE_REVISION;
    SystemTable.Hdr.HeaderSize = sizeof(SystemTable);
    SystemTable.BootServices = &BootServices;
    BootServices.Hdr.Signature = EFI_BOOT_SERVICES_SIGNATURE;
    BootServices.Hdr.Revision = EFI_2_31_SYSTEM_TABLE_REVISION;
    BootServices.Hdr.HeaderSize = sizeof(BootServices);
    BootServices.HandleProtocol = MockHandleProtocol;
    BootServices.OpenProtocol = MockOpenProtocol;
    BootServices.CloseProtocol = MockCloseProtocol;
    BootServices.AllocatePages = MockAllocatePages;
    BootServices.FreePages = MockFreePages;
    BootServices.LoadImage = MockLoadImage;
    BootServices.StartImage = MockStartImage;
    BootServices.UnloadImage = MockUnloadImage;
    BootServices.FreePool = MockFreePool;
    BootServices.LocateHandleBuffer = MockLocateHandleBuffer;
    MockFileSystem.OpenVolume = MockOpenVolume;
    MockRootFile.Open = MockFileOpen;
    MockRootFile.Close = MockFileClose;
    MockRootFile.GetInfo = MockFileGetInfo;
    LoadedImage.SystemTable = &SystemTable;
    LoadedImage.DeviceHandle = (EFI_HANDLE)(UINTN)3;
    ChildLoadedImage.SystemTable = &SystemTable;
    ChildLoadedImage.DeviceHandle = (EFI_HANDLE)(UINTN)4;
    ChildLoadedImage.ImageBase = (VOID *)(UINTN)0x200000;
    ChildLoadedImage.ImageSize = 0x30000;
    LoadedImage.ImageBase = (VOID *)(UINTN)0x100000;
    LoadedImage.ImageSize = 0x20000;
    LoadCalls = StartCalls = UnloadCalls = 0;
    ModuleHandoffSeen = 0;
    ModuleHandoffValidated = 0;
    FailUnload = 0;
    OpenProtocolCalls = CloseProtocolCalls = 0;
}

int main(void)
{
    RC95LDR_UEFI_IMAGE_CONTEXT Context;
    EFI_HANDLE Child = NULL;
    EFI_DEVICE_PATH_PROTOCOL EndNode;
    RC95LDR_STATUS Status;

    fprintf(stderr,"uefi checkpoint 1\\n");
    InitializeTables();
    Status = Rc95LdrUefiInitializeImage(
        (EFI_HANDLE)(UINTN)1, &SystemTable, &Context);
    assert(Status == Rc95LdrSuccess);
    assert(Context.ImageHandle == (EFI_HANDLE)(UINTN)1);
    assert(Context.SystemTable == &SystemTable);
    assert(Context.BootServices == &BootServices);
    assert(Context.LoadedImage == &LoadedImage);

    memset(&EndNode, 0, sizeof(EndNode));
    EndNode.Type = 0x7f;
    EndNode.SubType = 0xff;
    EndNode.Length[0] = 4;
    assert(Rc95LdrUefiLoadAndStartImage(&Context, &EndNode,
        sizeof(EndNode), NULL, 0, &Child) == EFI_SUCCESS);
    assert(LoadCalls == 1 && StartCalls == 1 && UnloadCalls == 1);
    assert(Child == NULL);

    fprintf(stderr,"uefi checkpoint 2\\n");
    assert(Rc95LdrUefiLoadAndStartModule(&Context, &EndNode,
        sizeof(EndNode), Rc95LdrModuleVeneer, Rc95LdrModuleAmd64,
        NULL, 0, &Child) == EFI_SUCCESS);
    assert(ModuleHandoffSeen && ModuleHandoffValidated);
    assert(OpenProtocolCalls == 1 && CloseProtocolCalls == 1);
    assert(Child == NULL);
    assert(LoadCalls == 2 && StartCalls == 2 && UnloadCalls == 2);

    fprintf(stderr,"uefi checkpoint 3\\n");
    FailUnload = 1;
    assert(Rc95LdrUefiLoadAndStartModule(&Context, &EndNode,
        sizeof(EndNode), Rc95LdrModuleVeneer, Rc95LdrModuleAmd64,
        NULL, 0, &Child) == EFI_DEVICE_ERROR);
    assert(Child == (EFI_HANDLE)(UINTN)2);
    assert(ChildLoadedImage.LoadOptions == NULL);
    assert(ChildLoadedImage.LoadOptionsSize == 0);
    FailUnload = 0;
    assert(BootServices.UnloadImage(Child) == EFI_SUCCESS);
    Child = NULL;

    /* A UEFI child may receive the additive V2 table. The V1 prefix
       remains byte-for-byte compatible, but the UEFI handoff validator must
       select the V2 validator instead of rejecting Version=2 as an ABI error. */
    {
        RC95LDR_MODULE_HANDOFF V2Handoff;
        RC95LDR_MODULE_SERVICES_V2 V2Services;
        RC95LDR_UEFI_IMAGE_CONTEXT ChildContext;
        const RC95LDR_MODULE_HANDOFF *Validated = NULL;
        memset(&V2Services, 0, sizeof(V2Services));
        V2Services.V1.Signature = RC95LDR_MODULE_SERVICES_SIGNATURE;
        V2Services.V1.StructureSize = sizeof(V2Services);
        V2Services.V1.Version = RC95LDR_MODULE_SERVICES_V2_VERSION;
        V2Services.V1.Firmware = Rc95LdrModuleUefi;
        V2Services.V1.Architecture = Rc95LdrModuleAmd64;
        V2Services.V1.OpenFile = MockV2OpenFile;
        V2Services.V1.ReadFileAt = MockV2ReadFileAt;
        V2Services.V1.CloseFile = MockV2CloseFile;
        V2Services.V1.AllocatePages = MockV2AllocatePages;
        V2Services.V1.FreePages = MockV2FreePages;
        V2Services.V1.ValidateAllocationExtent = MockV2ValidateExtent;
        V2Services.V1.Quiesce = MockV2Quiesce;
        V2Services.GetBlockDeviceCount = MockV2DeviceCount;
        V2Services.GetBlockDeviceInfo = MockV2DeviceInfo;
        V2Services.GetPartitionCount = MockV2PartitionCount;
        V2Services.GetPartitionInfo = MockV2PartitionInfo;
        V2Services.ReadBlocks = MockV2ReadBlocks;
        memset(&V2Handoff, 0, sizeof(V2Handoff));
        V2Handoff.Signature = RC95LDR_MODULE_SIGNATURE;
        V2Handoff.StructureSize = sizeof(V2Handoff);
        V2Handoff.Version = RC95LDR_MODULE_HANDOFF_VERSION;
        V2Handoff.ModuleKind = Rc95LdrModuleVeneer;
        V2Handoff.Firmware = Rc95LdrModuleUefi;
        V2Handoff.Architecture = Rc95LdrModuleAmd64;
        V2Handoff.Flags = RC95LDR_MODULE_FLAG_FIRMWARE_MANAGED_ENTRY;
        V2Handoff.ImageBase = (UINT64)(uintptr_t)ChildLoadedImage.ImageBase;
        V2Handoff.ImageSize = ChildLoadedImage.ImageSize;
        V2Handoff.EntryPoint = V2Handoff.ImageBase;
        V2Handoff.Services = (UINT64)(uintptr_t)&V2Services;
        V2Handoff.ServicesSize = sizeof(V2Services);
        ChildLoadedImage.LoadOptions = &V2Handoff;
        ChildLoadedImage.LoadOptionsSize = sizeof(V2Handoff);
        memset(&ChildContext, 0, sizeof(ChildContext));
        ChildContext.StructureSize = sizeof(ChildContext);
        ChildContext.Version = RC95LDR_UEFI_ENTRY_VERSION;
        ChildContext.SystemTable = &SystemTable;
        ChildContext.LoadedImage = &ChildLoadedImage;
        assert(Rc95LdrUefiGetModuleHandoff(&ChildContext,
            Rc95LdrModuleVeneer, Rc95LdrModuleAmd64,
            &Validated) == Rc95LdrSuccess);
        assert(Validated == &V2Handoff);
        ChildLoadedImage.LoadOptions = NULL;
        ChildLoadedImage.LoadOptionsSize = 0;
    }

    fprintf(stderr,"uefi checkpoint 4\\n");
    SystemTable.Hdr.Signature = 0;
    assert(Rc95LdrUefiInitializeImage(
        (EFI_HANDLE)(UINTN)1, &SystemTable, &Context) ==
        Rc95LdrCorruptData);

    InitializeTables();
    LoadedImage.ImageBase = (VOID *)(uintptr_t)(UINTPTR_MAX - 4u);
    LoadedImage.ImageSize = 16;
    assert(Rc95LdrUefiInitializeImage(
        (EFI_HANDLE)(UINTN)1, &SystemTable, &Context) ==
        Rc95LdrCorruptData);

    InitializeTables();
    BootServices.Hdr.HeaderSize =
        (UINT32)offsetof(EFI_BOOT_SERVICES, StartImage);
    assert(Rc95LdrUefiInitializeImage(
        (EFI_HANDLE)(UINTN)1, &SystemTable, &Context) ==
        Rc95LdrUnsupported);

    return 0;
}
