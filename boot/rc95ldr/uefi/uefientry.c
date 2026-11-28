#include "include/rc95ldruefientry.h"
#include "include/rc95ldruefiservices.h"
#include <stddef.h>
#include <string.h>

static EFI_GUID LoadedImageGuid = EFI_LOADED_IMAGE_PROTOCOL_GUID;

static int HeaderContains(UINT32 HeaderSize, size_t Offset, size_t Size)
{
    return Offset <= HeaderSize && Size <= (size_t)HeaderSize - Offset;
}

static int NativeArchitectureMatches(UINT32 Architecture)
{
#if defined(_M_IX86) || defined(__i386__)
    return Architecture == Rc95LdrModuleI386;
#elif defined(_M_AMD64) || defined(_M_X64) || \
      defined(_AMD64_) || defined(__x86_64__) || defined(__amd64__)
    return Architecture == Rc95LdrModuleAmd64;
#else
    (void)Architecture;
    return 0;
#endif
}

static int ValidDevicePath(const EFI_DEVICE_PATH_PROTOCOL *Path,
                           UINTN PathBytes)
{
    const UINT8 *Bytes = (const UINT8 *)Path;
    UINTN Offset = 0;

    if (!Path || PathBytes < sizeof(EFI_DEVICE_PATH_PROTOCOL))
        return 0;

    while (Offset < PathBytes)
    {
        UINT16 NodeLength;
        UINT8 Type, SubType;

        if (PathBytes - Offset < sizeof(EFI_DEVICE_PATH_PROTOCOL))
            return 0;
        Type = Bytes[Offset];
        SubType = Bytes[Offset + 1];
        NodeLength = (UINT16)(Bytes[Offset + 2] |
                              ((UINT16)Bytes[Offset + 3] << 8));
        if (NodeLength < sizeof(EFI_DEVICE_PATH_PROTOCOL) ||
            NodeLength > PathBytes - Offset)
            return 0;

        Offset += NodeLength;
        if (Type == 0x7fu)
        {
            if (NodeLength != 4u || SubType != 0xffu)
                return 0;
            return Offset == PathBytes;
        }
    }
    return 0;
}

static RC95LDR_STATUS
ValidateModuleServiceRecord(const RC95LDR_MODULE_HANDOFF *Record,
                            UINT32 ExpectedArchitecture)
{
    const RC95LDR_MODULE_SERVICES *Services;
    if (!Record || !Record->Services ||
        Record->ServicesSize < offsetof(RC95LDR_MODULE_SERVICES, Version) +
                               sizeof(Services->Version) ||
        Record->ServicesSize > RC95LDR_MODULE_MAX_SERVICES_SIZE)
        return Rc95LdrCorruptData;

    Services = (const RC95LDR_MODULE_SERVICES *)(uintptr_t)Record->Services;
    if (Services->Version == RC95LDR_MODULE_SERVICES_V2_VERSION)
        return Rc95LdrValidateModuleServicesV2(
            (const RC95LDR_MODULE_SERVICES_V2 *)Services,
            (size_t)Record->ServicesSize, Rc95LdrModuleUefi,
            ExpectedArchitecture);
    if (Services->Version == RC95LDR_MODULE_SERVICES_VERSION)
        return Rc95LdrValidateModuleServices(
            Services, (size_t)Record->ServicesSize,
            Rc95LdrModuleUefi, ExpectedArchitecture);
    return Rc95LdrUnsupported;
}

RC95LDR_STATUS
Rc95LdrUefiInitializeImage(EFI_HANDLE ImageHandle,
                           EFI_SYSTEM_TABLE *SystemTable,
                           RC95LDR_UEFI_IMAGE_CONTEXT *Context)
{
    EFI_STATUS EfiStatus;
    EFI_LOADED_IMAGE_PROTOCOL *LoadedImage = NULL;
    EFI_BOOT_SERVICES *BootServices;

    if (!ImageHandle || !SystemTable || !Context)
        return Rc95LdrInvalidParameter;

    memset(Context, 0, sizeof(*Context));

    if (SystemTable->Hdr.Signature != EFI_SYSTEM_TABLE_SIGNATURE ||
        SystemTable->Hdr.Revision < EFI_1_02_SYSTEM_TABLE_REVISION ||
        !HeaderContains(SystemTable->Hdr.HeaderSize,
                        offsetof(EFI_SYSTEM_TABLE, BootServices),
                        sizeof(SystemTable->BootServices)) ||
        !SystemTable->BootServices)
        return Rc95LdrCorruptData;

    BootServices = SystemTable->BootServices;
    if (BootServices->Hdr.Signature != EFI_BOOT_SERVICES_SIGNATURE ||
        BootServices->Hdr.Revision < EFI_1_02_SYSTEM_TABLE_REVISION ||
        !HeaderContains(BootServices->Hdr.HeaderSize,
                        offsetof(EFI_BOOT_SERVICES, HandleProtocol),
                        sizeof(BootServices->HandleProtocol)) ||
        !HeaderContains(BootServices->Hdr.HeaderSize,
                        offsetof(EFI_BOOT_SERVICES, LoadImage),
                        sizeof(BootServices->LoadImage)) ||
        !HeaderContains(BootServices->Hdr.HeaderSize,
                        offsetof(EFI_BOOT_SERVICES, StartImage),
                        sizeof(BootServices->StartImage)) ||
        !HeaderContains(BootServices->Hdr.HeaderSize,
                        offsetof(EFI_BOOT_SERVICES, UnloadImage),
                        sizeof(BootServices->UnloadImage)) ||
        !HeaderContains(BootServices->Hdr.HeaderSize,
                        offsetof(EFI_BOOT_SERVICES, FreePool),
                        sizeof(BootServices->FreePool)) ||
        !HeaderContains(BootServices->Hdr.HeaderSize,
                        offsetof(EFI_BOOT_SERVICES, OpenProtocol),
                        sizeof(BootServices->OpenProtocol)) ||
        !HeaderContains(BootServices->Hdr.HeaderSize,
                        offsetof(EFI_BOOT_SERVICES, CloseProtocol),
                        sizeof(BootServices->CloseProtocol)) ||
        !HeaderContains(BootServices->Hdr.HeaderSize,
                        offsetof(EFI_BOOT_SERVICES, AllocatePages),
                        sizeof(BootServices->AllocatePages)) ||
        !HeaderContains(BootServices->Hdr.HeaderSize,
                        offsetof(EFI_BOOT_SERVICES, FreePages),
                        sizeof(BootServices->FreePages)) ||
        !BootServices->HandleProtocol || !BootServices->LoadImage ||
        !BootServices->StartImage || !BootServices->UnloadImage ||
        !BootServices->FreePool || !BootServices->OpenProtocol ||
        !BootServices->CloseProtocol || !BootServices->AllocatePages ||
        !BootServices->FreePages)
        return Rc95LdrUnsupported;

    EfiStatus = BootServices->HandleProtocol(
        ImageHandle, &LoadedImageGuid, (VOID **)&LoadedImage);
    if (EFI_ERROR(EfiStatus) || !LoadedImage)
        return Rc95LdrNotFound;

    if (LoadedImage->SystemTable != SystemTable ||
        !LoadedImage->ImageBase || !LoadedImage->ImageSize ||
        LoadedImage->ImageSize > RC95LDR_MODULE_MAX_IMAGE_SIZE ||
        LoadedImage->ImageSize > (UINT64)UINTPTR_MAX ||
        (uintptr_t)LoadedImage->ImageBase > UINTPTR_MAX -
            (UINTN)LoadedImage->ImageSize)
        return Rc95LdrCorruptData;

    Context->StructureSize = sizeof(*Context);
    Context->Version = RC95LDR_UEFI_ENTRY_VERSION;
    Context->ImageHandle = ImageHandle;
    Context->SystemTable = SystemTable;
    Context->BootServices = BootServices;
    Context->LoadedImage = LoadedImage;
    return Rc95LdrSuccess;
}

EFI_STATUS
Rc95LdrUefiLoadAndStartImage(
    const RC95LDR_UEFI_IMAGE_CONTEXT *Context,
    EFI_DEVICE_PATH_PROTOCOL *DevicePath,
    UINTN DevicePathBytes,
    const VOID *LoadOptions,
    UINTN LoadOptionsBytes,
    EFI_HANDLE *ChildImageHandle)
{
    EFI_STATUS Status;
    EFI_HANDLE Child = NULL;
    EFI_LOADED_IMAGE_PROTOCOL *ChildLoadedImage = NULL;
    UINTN ExitDataSize = 0;
    CHAR16 *ExitData = NULL;

    if (ChildImageHandle) *ChildImageHandle = NULL;
    if (!Context || Context->StructureSize < sizeof(*Context) ||
        Context->Version != RC95LDR_UEFI_ENTRY_VERSION ||
        !Context->ImageHandle || !Context->SystemTable ||
        !Context->BootServices || !Context->LoadedImage ||
        Context->SystemTable->Hdr.Signature != EFI_SYSTEM_TABLE_SIGNATURE ||
        Context->SystemTable->BootServices != Context->BootServices ||
        Context->LoadedImage->SystemTable != Context->SystemTable ||
        !Context->BootServices->HandleProtocol ||
        !Context->BootServices->LoadImage ||
        !Context->BootServices->StartImage ||
        !Context->BootServices->UnloadImage ||
        !Context->BootServices->FreePool ||
        DevicePathBytes > 64u * 1024u ||
        !ValidDevicePath(DevicePath, DevicePathBytes) ||
        (LoadOptionsBytes && !LoadOptions) ||
        LoadOptionsBytes > 64u * 1024u || LoadOptionsBytes > UINT32_MAX ||
        !ChildImageHandle)
        return EFI_INVALID_PARAMETER;

    Status = Context->BootServices->LoadImage(
        TRUE, Context->ImageHandle, DevicePath, NULL, 0, &Child);
    if (EFI_ERROR(Status) || !Child)
    {
        if (Child && EFI_ERROR(Context->BootServices->UnloadImage(Child)))
            *ChildImageHandle = Child;
        return EFI_ERROR(Status) ? Status : EFI_LOAD_ERROR;
    }

    Status = Context->BootServices->HandleProtocol(
        Child, &LoadedImageGuid, (VOID **)&ChildLoadedImage);
    if (EFI_ERROR(Status) || !ChildLoadedImage ||
        ChildLoadedImage->SystemTable != Context->SystemTable)
    {
        EFI_STATUS UnloadStatus = Context->BootServices->UnloadImage(Child);
        if (EFI_ERROR(UnloadStatus)) *ChildImageHandle = Child;
        return EFI_ERROR(Status) ? Status : EFI_LOAD_ERROR;
    }
    if (LoadOptionsBytes)
    {
        ChildLoadedImage->LoadOptions = (VOID *)LoadOptions;
        ChildLoadedImage->LoadOptionsSize = (UINT32)LoadOptionsBytes;
    }

    *ChildImageHandle = Child;
    Status = Context->BootServices->StartImage(
        Child, &ExitDataSize, &ExitData);
    if (ExitData && Context->BootServices->FreePool)
        Context->BootServices->FreePool(ExitData);

    /* StartImage is synchronous. If the child returns, reclaim its image
       regardless of its exit status; a successful OS handoff never returns. */
    {
        EFI_STATUS UnloadStatus = Context->BootServices->UnloadImage(Child);
        *ChildImageHandle = EFI_ERROR(UnloadStatus) ? Child : NULL;
        if (!EFI_ERROR(Status) && EFI_ERROR(UnloadStatus))
            Status = UnloadStatus;
    }
    return Status;
}

RC95LDR_STATUS
Rc95LdrUefiGetModuleHandoff(
    const RC95LDR_UEFI_IMAGE_CONTEXT *Context,
    UINT32 ExpectedKind,
    UINT32 ExpectedArchitecture,
    const RC95LDR_MODULE_HANDOFF **Handoff)
{
    const RC95LDR_MODULE_HANDOFF *Record;
    RC95LDR_STATUS Status;

    if (Handoff) *Handoff = NULL;
    if (!Context || !Handoff ||
        Context->StructureSize < sizeof(*Context) ||
        Context->Version != RC95LDR_UEFI_ENTRY_VERSION ||
        !Context->LoadedImage || !Context->SystemTable ||
        Context->LoadedImage->SystemTable != Context->SystemTable ||
        !Context->LoadedImage->LoadOptions ||
        Context->LoadedImage->LoadOptionsSize < sizeof(*Record) ||
        Context->LoadedImage->LoadOptionsSize > 64u * 1024u)
        return Rc95LdrInvalidParameter;

    Record = (const RC95LDR_MODULE_HANDOFF *)
        Context->LoadedImage->LoadOptions;
    Status = Rc95LdrValidateModuleHandoff(
        Record, Context->LoadedImage->LoadOptionsSize, ExpectedKind,
        Rc95LdrModuleUefi, ExpectedArchitecture);
    if (Status != Rc95LdrSuccess) return Status;
    if (!(Record->Flags & RC95LDR_MODULE_FLAG_FIRMWARE_MANAGED_ENTRY) ||
        Record->EntryPoint != Record->ImageBase ||
        Record->ImageBase != (UINT64)(uintptr_t)Context->LoadedImage->ImageBase ||
        Record->ImageSize != Context->LoadedImage->ImageSize)
        return Rc95LdrCorruptData;

    Status = ValidateModuleServiceRecord(Record, ExpectedArchitecture);
    if (Status != Rc95LdrSuccess) return Status;

    *Handoff = Record;
    return Rc95LdrSuccess;
}

EFI_STATUS
Rc95LdrUefiLoadAndStartModule(
    const RC95LDR_UEFI_IMAGE_CONTEXT *Context,
    EFI_DEVICE_PATH_PROTOCOL *DevicePath,
    UINTN DevicePathBytes,
    UINT32 ModuleKind,
    UINT32 Architecture,
    const RC95LDR_MODULE_HANDOFF *ParentHandoff,
    UINTN ParentHandoffSize,
    EFI_HANDLE *ChildImageHandle)
{
    EFI_STATUS Status;
    EFI_HANDLE Child = NULL;
    EFI_LOADED_IMAGE_PROTOCOL *ChildLoadedImage = NULL;
    RC95LDR_MODULE_HANDOFF Handoff;
    RC95LDR_UEFI_SERVICE_CONTEXT ServiceContext;
    RC95LDR_UEFI_IMAGE_CONTEXT ChildContext;
    RC95LDR_MODULE_SERVICES_V2 Services;
    RC95LDR_STATUS ServiceStatus;
    UINTN ExitDataSize = 0;
    CHAR16 *ExitData = NULL;
    UINT64 ImageBase;

    if (ChildImageHandle) *ChildImageHandle = NULL;
    if (!Context || Context->StructureSize < sizeof(*Context) ||
        Context->Version != RC95LDR_UEFI_ENTRY_VERSION ||
        !Context->ImageHandle || !Context->SystemTable ||
        !Context->BootServices || !Context->LoadedImage ||
        Context->SystemTable->Hdr.Signature != EFI_SYSTEM_TABLE_SIGNATURE ||
        Context->SystemTable->BootServices != Context->BootServices ||
        Context->LoadedImage->SystemTable != Context->SystemTable ||
        !Context->BootServices->LoadImage ||
        !Context->BootServices->HandleProtocol ||
        !Context->BootServices->StartImage ||
        !Context->BootServices->UnloadImage ||
        !Context->BootServices->FreePool || !ChildImageHandle ||
        ModuleKind < Rc95LdrModuleRc95Ldr ||
        ModuleKind > Rc95LdrModuleWinload ||
        !NativeArchitectureMatches(Architecture) ||
        ((ParentHandoff == NULL) != (ParentHandoffSize == 0)) ||
        ParentHandoffSize > 64u * 1024u ||
        (ParentHandoff && ParentHandoffSize < sizeof(*ParentHandoff)) ||
        DevicePathBytes > 64u * 1024u ||
        !ValidDevicePath(DevicePath, DevicePathBytes))
        return EFI_INVALID_PARAMETER;

    if (ParentHandoff)
    {
        Status = Rc95LdrValidateModuleHandoff(
            ParentHandoff, ParentHandoffSize,
            ParentHandoff->ModuleKind, Rc95LdrModuleUefi, Architecture);
        if (Status != Rc95LdrSuccess) return EFI_INVALID_PARAMETER;
        Status = ValidateModuleServiceRecord(ParentHandoff, Architecture);
        if (Status != Rc95LdrSuccess) return EFI_INVALID_PARAMETER;
    }

    Status = Context->BootServices->LoadImage(
        TRUE, Context->ImageHandle, DevicePath, NULL, 0, &Child);
    if (EFI_ERROR(Status) || !Child)
    {
        if (Child && EFI_ERROR(Context->BootServices->UnloadImage(Child)))
            *ChildImageHandle = Child;
        return EFI_ERROR(Status) ? Status : EFI_LOAD_ERROR;
    }

    Status = Context->BootServices->HandleProtocol(
        Child, &LoadedImageGuid, (VOID **)&ChildLoadedImage);
    if (EFI_ERROR(Status) || !ChildLoadedImage ||
        ChildLoadedImage->SystemTable != Context->SystemTable ||
        !ChildLoadedImage->ImageBase || !ChildLoadedImage->ImageSize ||
        ChildLoadedImage->ImageSize > RC95LDR_MODULE_MAX_IMAGE_SIZE ||
        ChildLoadedImage->ImageSize > (UINT64)UINTPTR_MAX)
    {
        EFI_STATUS UnloadStatus = Context->BootServices->UnloadImage(Child);
        if (EFI_ERROR(UnloadStatus)) *ChildImageHandle = Child;
        return EFI_ERROR(Status) ? Status : EFI_LOAD_ERROR;
    }

    ImageBase = (UINT64)(uintptr_t)ChildLoadedImage->ImageBase;
    if (ImageBase > UINT64_MAX - ChildLoadedImage->ImageSize ||
        ChildLoadedImage->ImageSize - 1u >
            (UINT64)UINTPTR_MAX - ImageBase)
    {
        EFI_STATUS UnloadStatus = Context->BootServices->UnloadImage(Child);
        if (EFI_ERROR(UnloadStatus)) *ChildImageHandle = Child;
        return EFI_LOAD_ERROR;
    }

    memset(&ChildContext, 0, sizeof(ChildContext));
    ChildContext.StructureSize = sizeof(ChildContext);
    ChildContext.Version = RC95LDR_UEFI_ENTRY_VERSION;
    ChildContext.ImageHandle = Child;
    ChildContext.SystemTable = Context->SystemTable;
    ChildContext.BootServices = Context->BootServices;
    ChildContext.LoadedImage = ChildLoadedImage;

    memset(&ServiceContext, 0, sizeof(ServiceContext));
    memset(&Services, 0, sizeof(Services));
    ServiceStatus = Rc95LdrUefiInitializeModuleServicesV2(
        &ChildContext, Architecture, &ServiceContext, &Services);
    if (ServiceStatus != Rc95LdrSuccess)
    {
        EFI_STATUS UnloadStatus =
            Context->BootServices->UnloadImage(Child);
        if (EFI_ERROR(UnloadStatus)) *ChildImageHandle = Child;
        return ServiceStatus == Rc95LdrOutOfMemory
            ? EFI_OUT_OF_RESOURCES : EFI_UNSUPPORTED;
    }

    memset(&Handoff, 0, sizeof(Handoff));
    Handoff.Signature = RC95LDR_MODULE_SIGNATURE;
    Handoff.StructureSize = sizeof(Handoff);
    Handoff.Version = RC95LDR_MODULE_HANDOFF_VERSION;
    Handoff.ModuleKind = ModuleKind;
    Handoff.Firmware = Rc95LdrModuleUefi;
    Handoff.Architecture = Architecture;
    Handoff.Flags = RC95LDR_MODULE_FLAG_FIRMWARE_MANAGED_ENTRY;
    Handoff.ImageBase = ImageBase;
    Handoff.ImageSize = ChildLoadedImage->ImageSize;
    /* EFI firmware owns the actual PE/COFF entry-point transfer. ImageBase
       is a validated in-image token, not a callable entry-point address. */
    Handoff.EntryPoint = ImageBase;
    Handoff.Services = (UINT64)(uintptr_t)&Services;
    Handoff.ServicesSize = sizeof(Services);
    Handoff.ParentHandoff = (UINT64)(uintptr_t)ParentHandoff;
    Handoff.ParentHandoffSize = ParentHandoffSize;

    ChildLoadedImage->LoadOptions = &Handoff;
    ChildLoadedImage->LoadOptionsSize = sizeof(Handoff);
    *ChildImageHandle = Child;
    Status = Context->BootServices->StartImage(
        Child, &ExitDataSize, &ExitData);
    if (ExitData) Context->BootServices->FreePool(ExitData);
    Rc95LdrUefiDestroyModuleServices(&ServiceContext);

    {
        EFI_STATUS UnloadStatus =
            Context->BootServices->UnloadImage(Child);
        if (EFI_ERROR(UnloadStatus))
        {
            /* The stack handoff expires on return. If firmware cannot unload
               the child, clear LoadOptions before exposing its handle. */
            ChildLoadedImage->LoadOptions = NULL;
            ChildLoadedImage->LoadOptionsSize = 0;
            *ChildImageHandle = Child;
        }
        else
            *ChildImageHandle = NULL;
        if (!EFI_ERROR(Status) && EFI_ERROR(UnloadStatus))
            Status = UnloadStatus;
    }
    return Status;
}

EFI_STATUS EFIAPI
Rc95LdrUefiImageEntry(EFI_HANDLE ImageHandle,
                      EFI_SYSTEM_TABLE *SystemTable)
{
    RC95LDR_UEFI_IMAGE_CONTEXT Context;
    RC95LDR_STATUS Status;

    Status = Rc95LdrUefiInitializeImage(ImageHandle, SystemTable, &Context);
    if (Status == Rc95LdrSuccess)
        return EFI_SUCCESS;
    if (Status == Rc95LdrInvalidParameter)
        return EFI_INVALID_PARAMETER;
    if (Status == Rc95LdrNotFound)
        return EFI_NOT_FOUND;
    if (Status == Rc95LdrUnsupported)
        return EFI_UNSUPPORTED;
    return EFI_COMPROMISED_DATA;
}
