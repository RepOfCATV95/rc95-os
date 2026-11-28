#include "include/rc95ldrbioscd.h"
#include <string.h>

/* El Torito INT 13h AH=4Bh, AL=01h status packet. */
#pragma pack(push, 1)
typedef struct _RC95LDR_ELTORITO_STATUS_PACKET {
    uint8_t Size;
    uint8_t MediaType;
    uint8_t DriveNumber;
    uint8_t ControllerIndex;
    uint32_t BootCatalogLba;
    uint16_t DeviceSpec;
    uint16_t BufferSegment;
    uint16_t LoadSegment;
    uint16_t SectorCount;
    uint32_t LoadRba;
} RC95LDR_ELTORITO_STATUS_PACKET;
#pragma pack(pop)

RC95LDR_STATUS
Rc95LdrBiosQueryElToritoBoot(
    const RC95LDR_BIOS_SERVICES *Services,
    uint8_t Drive,
    RC95LDR_ELTORITO_BOOT_INFO *BootInfo)
{
    RC95LDR_BIOS_REGISTERS Registers;
    RC95LDR_ELTORITO_STATUS_PACKET *Packet;
    void *Mapping = NULL;
    uint32_t Physical;
    RC95LDR_STATUS Status;

    if (!BootInfo)
        return Rc95LdrInvalidParameter;
    memset(BootInfo, 0, sizeof(*BootInfo));
    Status = Rc95LdrBiosValidateServices(Services);
    if (Status != Rc95LdrSuccess)
        return Status;
    if (Services->ScratchLength < sizeof(*Packet) ||
        Services->ScratchPhysical >= 0x100000u ||
        Services->ScratchLength > 0x100000u - Services->ScratchPhysical)
        return Rc95LdrOutOfBounds;

    Physical = Services->ScratchPhysical;
    Status = Services->MapLowMemory(Services->Context, Physical,
                                    sizeof(*Packet), &Mapping);
    if (Status != Rc95LdrSuccess)
        return Status;
    if (!Mapping)
        return Rc95LdrInvalidParameter;

    Packet = (RC95LDR_ELTORITO_STATUS_PACKET *)Mapping;
    memset(Packet, 0, sizeof(*Packet));
    Packet->Size = (uint8_t)(sizeof(*Packet) - 1u);
    memset(&Registers, 0, sizeof(Registers));
    Registers.Eax = 0x4B01u;
    Registers.Edx = Drive;
    Registers.Ds = (uint16_t)(Physical >> 4);
    Registers.Esi = Physical & 0x0Fu;
    Status = Services->InvokeInterrupt(Services->Context, 0x13, &Registers);
    if (Status != Rc95LdrSuccess)
    {
        Services->UnmapLowMemory(Services->Context, Mapping, sizeof(*Packet));
        return Status;
    }
    if (Registers.Eflags & 1u)
    {
        Services->UnmapLowMemory(Services->Context, Mapping, sizeof(*Packet));
        return Rc95LdrNotFound;
    }
    if (Packet->Size < sizeof(*Packet) - 1u ||
        Packet->MediaType > 4u ||
        Packet->DriveNumber != Drive ||
        !Packet->BootCatalogLba)
        Status = Rc95LdrCorruptData;
    else
    {
        BootInfo->DriveNumber = Packet->DriveNumber;
        BootInfo->MediaType = Packet->MediaType;
        BootInfo->BootCatalogLba = Packet->BootCatalogLba;
        BootInfo->LoadRba = Packet->LoadRba;
        BootInfo->LoadSegment = Packet->LoadSegment;
        BootInfo->SectorCount = Packet->SectorCount;
        Status = Rc95LdrSuccess;
    }
    Services->UnmapLowMemory(Services->Context, Mapping, sizeof(*Packet));
    return Status;
}

RC95LDR_STATUS
Rc95LdrBiosMountElToritoIso(
    const RC95LDR_BIOS_SERVICES *Services,
    uint8_t Drive,
    RC95LDR_ELTORITO_BOOT_INFO *BootInfo,
    RC95LDR_BIOS_DISK *Disk,
    RC95LDR_BIOS_BLOCK_CONTEXT *BlockContext,
    RC95LDR_BLOCK_DEVICE *BlockDevice,
    RC95LDR_ISO9660 *Volume)
{
    RC95LDR_ELTORITO_BOOT_INFO LocalInfo;
    RC95LDR_STATUS Status;
    if (!Disk || !BlockContext || !BlockDevice || !Volume)
        return Rc95LdrInvalidParameter;
    if (!BootInfo)
        BootInfo = &LocalInfo;
    Status = Rc95LdrBiosQueryElToritoBoot(Services, Drive, BootInfo);
    if (Status != Rc95LdrSuccess)
        return Status;
    if (BootInfo->MediaType != 0)
        return Rc95LdrUnsupported;
    Status = Rc95LdrBiosProbeDisk(Services, Drive, Disk);
    if (Status != Rc95LdrSuccess)
        return Status;
    if (Disk->BytesPerSector != RC95LDR_ISO9660_LOGICAL_BLOCK_SIZE)
        return Rc95LdrUnsupported;
    if (BootInfo->BootCatalogLba >= Disk->BlockCount ||
        BootInfo->LoadRba >= Disk->BlockCount)
        return Rc95LdrOutOfBounds;
    if (BootInfo->SectorCount != 0)
    {
        uint64_t LoadBlocks =
            ((uint64_t)BootInfo->SectorCount + 3u) / 4u;
        if (LoadBlocks > Disk->BlockCount - BootInfo->LoadRba)
            return Rc95LdrOutOfBounds;
    }
    Status = Rc95LdrBiosInitializeBlockDevice(
        Services, Disk, BlockContext, BlockDevice);
    if (Status != Rc95LdrSuccess)
        return Status;
    Status = Rc95LdrIso9660Mount(Volume, BlockDevice);
    if (Status != Rc95LdrSuccess)
        memset(Volume, 0, sizeof(*Volume));
    return Status;
}
