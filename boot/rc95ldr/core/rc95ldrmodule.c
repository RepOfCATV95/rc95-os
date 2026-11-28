#include "rc95ldrmodule.h"

#define RC95LDR_MODULE_MAX_PARENT_HANDOFF (64u * 1024u)

static int AddU64(uint64_t A, uint64_t B, uint64_t *End)
{
    if (B > UINT64_MAX - A) return 0;
    *End = A + B;
    return 1;
}


RC95LDR_STATUS
Rc95LdrValidateModuleHandoff(const RC95LDR_MODULE_HANDOFF *Handoff,
                             size_t AvailableBytes,
                             uint32_t ExpectedKind,
                             uint32_t ExpectedFirmware,
                             uint32_t ExpectedArchitecture)
{
    uint64_t ImageEnd, ParentEnd;

    if (!Handoff || AvailableBytes < sizeof(*Handoff))
        return Rc95LdrInvalidParameter;

    if (Handoff->Signature != RC95LDR_MODULE_SIGNATURE ||
        Handoff->StructureSize < sizeof(*Handoff) ||
        Handoff->StructureSize > AvailableBytes ||
        Handoff->Version != RC95LDR_MODULE_HANDOFF_VERSION ||
        Handoff->ModuleKind != ExpectedKind ||
        Handoff->Firmware != ExpectedFirmware ||
        Handoff->Architecture != ExpectedArchitecture ||
        Handoff->Reserved != 0 ||
        (Handoff->Flags & ~RC95LDR_MODULE_VALID_FLAGS) != 0)
        return Rc95LdrInvalidParameter;

    if (ExpectedKind < Rc95LdrModuleRc95Ldr ||
        ExpectedKind > Rc95LdrModuleWinload ||
        (ExpectedFirmware != Rc95LdrModuleBios &&
         ExpectedFirmware != Rc95LdrModuleUefi) ||
        (ExpectedArchitecture != Rc95LdrModuleI386 &&
         ExpectedArchitecture != Rc95LdrModuleAmd64))
        return Rc95LdrUnsupported;

    if (!Handoff->ImageBase || !Handoff->ImageSize ||
        Handoff->ImageSize > RC95LDR_MODULE_MAX_IMAGE_SIZE ||
        !AddU64(Handoff->ImageBase, Handoff->ImageSize, &ImageEnd) ||
        ImageEnd - 1u > (uint64_t)UINTPTR_MAX)
        return Rc95LdrOutOfBounds;

    if (Handoff->EntryPoint < Handoff->ImageBase ||
        Handoff->EntryPoint >= ImageEnd)
        return Rc95LdrOutOfBounds;

    if (Handoff->ServicesSize)
    {
        uint64_t ServicesEnd;
        if (!Handoff->Services ||
            Handoff->ServicesSize > RC95LDR_MODULE_MAX_SERVICES_SIZE ||
            !AddU64(Handoff->Services, Handoff->ServicesSize, &ServicesEnd) ||
            ServicesEnd - 1u > (uint64_t)UINTPTR_MAX)
            return Rc95LdrOutOfBounds;
        /* A service table may live outside the image allocation, but it may
           not straddle the image boundary: partial embedding has ambiguous
           ownership and can expose an out-of-allocation tail. */
        if (Handoff->Services < ImageEnd &&
            ServicesEnd > Handoff->ImageBase &&
            (Handoff->Services < Handoff->ImageBase || ServicesEnd > ImageEnd))
            return Rc95LdrOutOfBounds;
    }
    if (!Handoff->ServicesSize && Handoff->Services)
        return Rc95LdrInvalidParameter;

    if (Handoff->ParentHandoffSize)
    {
        if (!Handoff->ParentHandoff ||
            Handoff->ParentHandoffSize > RC95LDR_MODULE_MAX_PARENT_HANDOFF ||
            !AddU64(Handoff->ParentHandoff,
                    Handoff->ParentHandoffSize, &ParentEnd) ||
            ParentEnd - 1u > (uint64_t)UINTPTR_MAX)
            return Rc95LdrOutOfBounds;
    }
    else if (Handoff->ParentHandoff)
        return Rc95LdrInvalidParameter;

    return Rc95LdrSuccess;
}

RC95LDR_STATUS
Rc95LdrInvokeModule(const RC95LDR_MODULE_HANDOFF *Handoff,
                    size_t AvailableBytes,
                    uint32_t ExpectedKind,
                    uint32_t ExpectedFirmware,
                    uint32_t ExpectedArchitecture,
                    RC95LDR_MODULE_TRANSFER Transfer,
                    void *TransferContext)
{
    RC95LDR_STATUS Status;

    if (!Transfer) return Rc95LdrInvalidParameter;
    Status = Rc95LdrValidateModuleHandoff(
        Handoff, AvailableBytes, ExpectedKind,
        ExpectedFirmware, ExpectedArchitecture);
    if (Status != Rc95LdrSuccess) return Status;

    /* The transfer callback owns the architecture-specific ABI transition.
       No function-pointer cast or guessed calling convention is permitted. */
    return Transfer(TransferContext, Handoff->EntryPoint, Handoff);
}


static RC95LDR_STATUS
ValidateModuleServicesPrefix(const RC95LDR_MODULE_SERVICES *Services,
                             size_t AvailableBytes,
                             uint32_t ExpectedFirmware,
                             uint32_t ExpectedArchitecture,
                             uint32_t ExpectedVersion)
{
    if (!Services || AvailableBytes < sizeof(*Services) ||
        AvailableBytes > RC95LDR_MODULE_MAX_SERVICES_SIZE ||
        Services->Signature != RC95LDR_MODULE_SERVICES_SIGNATURE ||
        Services->StructureSize < sizeof(*Services) ||
        Services->StructureSize > AvailableBytes ||
        Services->Version != ExpectedVersion ||
        Services->Firmware != ExpectedFirmware ||
        Services->Architecture != ExpectedArchitecture ||
        Services->Flags != 0 || !Services->OpenFile ||
        !Services->ReadFileAt || !Services->CloseFile ||
        !Services->AllocatePages || !Services->FreePages ||
        !Services->ValidateAllocationExtent || !Services->Quiesce)
        return Rc95LdrInvalidParameter;
    if ((ExpectedFirmware != Rc95LdrModuleBios &&
         ExpectedFirmware != Rc95LdrModuleUefi) ||
        (ExpectedArchitecture != Rc95LdrModuleI386 &&
         ExpectedArchitecture != Rc95LdrModuleAmd64))
        return Rc95LdrUnsupported;
    return Rc95LdrSuccess;
}

RC95LDR_STATUS
Rc95LdrValidateModuleServices(const RC95LDR_MODULE_SERVICES *Services,
                              size_t AvailableBytes,
                              uint32_t ExpectedFirmware,
                              uint32_t ExpectedArchitecture)
{
    return ValidateModuleServicesPrefix(Services, AvailableBytes,
        ExpectedFirmware, ExpectedArchitecture,
        RC95LDR_MODULE_SERVICES_VERSION);
}

RC95LDR_STATUS
Rc95LdrValidateModuleServicesV2(const RC95LDR_MODULE_SERVICES_V2 *Services,
                                size_t AvailableBytes,
                                uint32_t ExpectedFirmware,
                                uint32_t ExpectedArchitecture)
{
    RC95LDR_STATUS Status;
    if (!Services || AvailableBytes < sizeof(*Services) ||
        AvailableBytes > RC95LDR_MODULE_MAX_SERVICES_SIZE)
        return Rc95LdrInvalidParameter;
    Status = ValidateModuleServicesPrefix(&Services->V1, AvailableBytes,
        ExpectedFirmware, ExpectedArchitecture,
        RC95LDR_MODULE_SERVICES_V2_VERSION);
    if (Status != Rc95LdrSuccess) return Status;
    if (Services->V1.StructureSize < sizeof(*Services) ||
        !Services->GetBlockDeviceCount || !Services->GetBlockDeviceInfo ||
        !Services->GetPartitionCount || !Services->GetPartitionInfo ||
        !Services->ReadBlocks)
        return Rc95LdrInvalidParameter;
    return Rc95LdrSuccess;
}

RC95LDR_STATUS
Rc95LdrValidateModuleBlockDeviceInfo(
    const RC95LDR_MODULE_BLOCK_DEVICE_INFO *Info, size_t AvailableBytes)
{
    if (!Info || AvailableBytes < sizeof(*Info) ||
        Info->StructureSize < sizeof(*Info) ||
        Info->StructureSize > AvailableBytes ||
        Info->Version != RC95LDR_MODULE_BLOCK_DEVICE_INFO_VERSION ||
        !Info->BlockCount || Info->BlockSize < 512u ||
        Info->BlockSize > 65536u ||
        (Info->BlockSize & (Info->BlockSize - 1u)) ||
        Info->Flags != RC95LDR_MODULE_BLOCK_DEVICE_READ_ONLY ||
        Info->Reserved)
        return Rc95LdrInvalidParameter;
    if (Info->BlockCount > UINT64_MAX / Info->BlockSize)
        return Rc95LdrOutOfBounds;
    return Rc95LdrSuccess;
}

RC95LDR_STATUS
Rc95LdrValidateModulePartitionInfo(
    const RC95LDR_MODULE_PARTITION_INFO *Info, size_t AvailableBytes,
    const RC95LDR_MODULE_BLOCK_DEVICE_INFO *Device)
{
    uint64_t End;
    size_t I;
    uint8_t GuidOrType = 0;
    if (!Info || !Device)
        return Rc95LdrInvalidParameter;
    {
        RC95LDR_STATUS DeviceStatus =
            Rc95LdrValidateModuleBlockDeviceInfo(Device, sizeof(*Device));
        if (DeviceStatus != Rc95LdrSuccess) return DeviceStatus;
    }
    if (AvailableBytes < sizeof(*Info) ||
        Info->StructureSize < sizeof(*Info) ||
        Info->StructureSize > AvailableBytes ||
        Info->Version != RC95LDR_MODULE_PARTITION_INFO_VERSION ||
        Info->Flags != 0 ||
        (Info->Scheme != RC95LDR_MODULE_PARTITION_GPT &&
         Info->Scheme != RC95LDR_MODULE_PARTITION_MBR) ||
        !Info->BlockCount || Info->StartLba > UINT64_MAX - Info->BlockCount)
        return Rc95LdrInvalidParameter;
    for (I = 0; I < sizeof(Info->Reserved); ++I)
        if (Info->Reserved[I]) return Rc95LdrInvalidParameter;
    End = Info->StartLba + Info->BlockCount;
    if (End > Device->BlockCount) return Rc95LdrOutOfBounds;
    if (Info->Scheme == RC95LDR_MODULE_PARTITION_GPT) {
        uint8_t UniqueGuidOrType = 0;
        for (I = 0; I < sizeof(Info->TypeGuid); ++I) {
            GuidOrType |= Info->TypeGuid[I];
            UniqueGuidOrType |= Info->UniqueGuid[I];
        }
        /* GPT partitions require nonzero type and unique GUIDs; MbrType is
           not meaningful for GPT and must be zero. */
        if (!GuidOrType || !UniqueGuidOrType || Info->MbrType)
            return Rc95LdrInvalidParameter;
    } else {
        if (!Info->MbrType) return Rc95LdrInvalidParameter;
        for (I = 0; I < sizeof(Info->TypeGuid) + sizeof(Info->UniqueGuid); ++I)
            if (((const uint8_t *)Info->TypeGuid)[I])
                return Rc95LdrInvalidParameter;
    }
    return Rc95LdrSuccess;
}
