#include "rc95ldrbioscd.h"
#include <assert.h>
#include <string.h>

#pragma pack(push, 1)
typedef struct _TEST_ELTORITO_PACKET {
    uint8_t Size, MediaType, DriveNumber, ControllerIndex;
    uint32_t BootCatalogLba;
    uint16_t DeviceSpec, BufferSegment, LoadSegment, SectorCount;
    uint32_t LoadRba;
} TEST_ELTORITO_PACKET;
#pragma pack(pop)

typedef struct _TEST_CONTEXT {
    uint8_t Memory[0x100000];
    uint8_t Disk[40][2048];
    uint8_t ReturnCarry;
    uint8_t Drive;
} TEST_CONTEXT;

static void Dual16(uint8_t *P, uint16_t V)
{
    P[0] = (uint8_t)V;
    P[1] = (uint8_t)(V >> 8);
    P[2] = (uint8_t)(V >> 8);
    P[3] = (uint8_t)V;
}
static void Dual32(uint8_t *P, uint32_t V)
{
    P[0] = (uint8_t)V;
    P[1] = (uint8_t)(V >> 8);
    P[2] = (uint8_t)(V >> 16);
    P[3] = (uint8_t)(V >> 24);
    P[4] = (uint8_t)(V >> 24);
    P[5] = (uint8_t)(V >> 16);
    P[6] = (uint8_t)(V >> 8);
    P[7] = (uint8_t)V;
}
static uint8_t *Record(uint8_t *P,uint32_t Lba,uint32_t Size,
                       uint8_t Flags,const char *Name,uint8_t Length)
{
    uint8_t N=(uint8_t)(33u+Length+(Length%2u==0?1u:0u));
    memset(P,0,N);P[0]=N;Dual32(P+2,Lba);Dual32(P+10,Size);
    P[25]=Flags;Dual16(P+28,1);P[32]=Length;
    memcpy(P+33,Name,Length);return P+N;
}
static void MakeIso(TEST_CONTEXT *C)
{
    uint8_t *D,*P;
    memset(C->Disk,0,sizeof(C->Disk));
    D=C->Disk[16];D[0]=1;memcpy(D+1,"CD001",5);D[6]=1;
    Dual32(D+80,40);Dual16(D+128,2048);
    Record(D+156,20,2048,2,"\0",1);
    D=C->Disk[17];D[0]=255;memcpy(D+1,"CD001",5);D[6]=1;
    P=C->Disk[20];P=Record(P,20,2048,2,"\0",1);
    P=Record(P,20,2048,2,"\1",1);
    P=Record(P,21,5,0,"BOOT.BIN;1",10);
    memcpy(C->Disk[21],"HELLO",5);
}
static RC95LDR_STATUS Map(void *Context, uint32_t Address, size_t Length, void **Out)
{
    TEST_CONTEXT *C = (TEST_CONTEXT *)Context;
    if (!C || !Out || Address > sizeof(C->Memory) ||
        Length > sizeof(C->Memory) - Address)
        return Rc95LdrOutOfBounds;
    *Out = C->Memory + Address;
    return Rc95LdrSuccess;
}
static void Unmap(void *Context, void *Mapping, size_t Length)
{ (void)Context; (void)Mapping; (void)Length; }
static uint8_t *MemoryRange(TEST_CONTEXT *C, uint32_t Address, size_t Length)
{
    if (C == NULL || Address > sizeof(C->Memory) ||
        Length > sizeof(C->Memory) - Address)
        return NULL;

    return C->Memory + Address;
}

static RC95LDR_STATUS Interrupt(void *Context, uint8_t Number,
                                RC95LDR_BIOS_REGISTERS *R)
{
    TEST_CONTEXT *C = (TEST_CONTEXT *)Context;
    TEST_ELTORITO_PACKET *P;
    uint32_t Address;
    if (!C || !R || Number != 0x13)
        return Rc95LdrUnsupported;
    if (R->Eax == 0x4100u)
    {
        R->Ebx = 0xAA55u;
        R->Ecx = 1u;
        return Rc95LdrSuccess;
    }
    if (R->Eax == 0x4800u)
    {
        uint8_t *Params;
        Address = (uint32_t)R->Ds * 16u + (uint16_t)R->Esi;
        Params = MemoryRange(C, Address, 30);
        if (Params == NULL)
            return Rc95LdrOutOfBounds;
        memset(Params, 0, 30);
        Params[0] = 30;
        Params[16] = 40;
        Params[24] = 0;
        Params[25] = 8; /* 2048-byte EDD blocks */
        return Rc95LdrSuccess;
    }
    if (R->Eax == 0x4200u)
    {
        uint8_t *Dap;
        uint16_t Count, Offset, Segment;
        uint64_t Lba;
        uint32_t Destination;
        Address = (uint32_t)R->Ds * 16u + (uint16_t)R->Esi;
        Dap = MemoryRange(C, Address, 16);
        if (Dap == NULL)
            return Rc95LdrOutOfBounds;
        Count = (uint16_t)(Dap[2] | ((uint16_t)Dap[3] << 8));
        Offset = (uint16_t)(Dap[4] | ((uint16_t)Dap[5] << 8));
        Segment = (uint16_t)(Dap[6] | ((uint16_t)Dap[7] << 8));
        Lba = (uint64_t)Dap[8] | ((uint64_t)Dap[9] << 8) |
              ((uint64_t)Dap[10] << 16) | ((uint64_t)Dap[11] << 24);
        Destination = (uint32_t)Segment * 16u + Offset;
        if (!Count || Lba >= 40 || Count > 40 - Lba ||
            Destination > sizeof(C->Memory) ||
            (size_t)Count * 2048u > sizeof(C->Memory) - Destination)
            return Rc95LdrOutOfBounds;
        memcpy(C->Memory + Destination, C->Disk[Lba],
               (size_t)Count * 2048u);
        return Rc95LdrSuccess;
    }
    if (R->Eax != 0x4B01u)
        return Rc95LdrUnsupported;
    Address = (uint32_t)R->Ds * 16u + (uint16_t)R->Esi;
    P = (TEST_ELTORITO_PACKET *)MemoryRange(C, Address, sizeof(*P));
    if (P == NULL)
        return Rc95LdrOutOfBounds;
    P->Size = (uint8_t)(sizeof(*P) - 1u);
    P->MediaType = 0; /* no emulation */
    P->DriveNumber = C->Drive;
    P->BootCatalogLba = 20;
    P->LoadRba = 21;
    P->LoadSegment = 0x07C0;
    P->SectorCount = 4;
    if (C->ReturnCarry) R->Eflags |= 1u;
    return Rc95LdrSuccess;
}
int main(void)
{
    TEST_CONTEXT C;
    RC95LDR_BIOS_SERVICES S;
    RC95LDR_ELTORITO_BOOT_INFO Info;
    memset(&C, 0, sizeof(C));
    memset(&S, 0, sizeof(S));
    C.Drive = 0xE0;
    MakeIso(&C);
    S.StructureSize = sizeof(S);
    S.Version = RC95LDR_BIOS_ABI_VERSION;
    S.Context = &C;
    S.InvokeInterrupt = Interrupt;
    S.MapLowMemory = Map;
    S.UnmapLowMemory = Unmap;
    S.ScratchPhysical = 0x8000;
    S.ScratchLength = 0x1000;
    S.BootDrive = C.Drive;

    /* Firmware callbacks must reject guest pointers outside low memory. */
    {
        RC95LDR_BIOS_REGISTERS Registers;
        memset(&Registers, 0, sizeof(Registers));
        Registers.Ds = 0xFFFFu;
        Registers.Esi = 0xFFFFu;
        Registers.Eax = 0x4800u;
        assert(Interrupt(&C, 0x13u, &Registers) == Rc95LdrOutOfBounds);
        Registers.Eax = 0x4200u;
        assert(Interrupt(&C, 0x13u, &Registers) == Rc95LdrOutOfBounds);
        Registers.Eax = 0x4B01u;
        assert(Interrupt(&C, 0x13u, &Registers) == Rc95LdrOutOfBounds);
    }

    assert(Rc95LdrBiosQueryElToritoBoot(&S, C.Drive, &Info) == Rc95LdrSuccess);
    assert(Info.DriveNumber == 0xE0 && Info.MediaType == 0);
    assert(Info.BootCatalogLba == 20 && Info.LoadRba == 21);
    assert(Info.SectorCount == 4 && Info.LoadSegment == 0x07C0);

    C.ReturnCarry = 1;
    assert(Rc95LdrBiosQueryElToritoBoot(&S, C.Drive, &Info) == Rc95LdrNotFound);
    C.ReturnCarry = 0;
    assert(Rc95LdrBiosQueryElToritoBoot(&S, 0xE1, &Info) == Rc95LdrCorruptData);
    assert(Rc95LdrBiosQueryElToritoBoot(&S, C.Drive, NULL) == Rc95LdrInvalidParameter);
    {
        RC95LDR_BIOS_DISK Disk;
        RC95LDR_BIOS_BLOCK_CONTEXT BlockContext;
        RC95LDR_BLOCK_DEVICE BlockDevice;
        RC95LDR_ISO9660 Iso;
        const RC95LDR_FILE_PROVIDER *Provider;
        void *File;
        char Data[8] = {0};
        size_t ReadBytes = 0;
        assert(Rc95LdrBiosMountElToritoIso(
            &S, C.Drive, &Info, &Disk, &BlockContext,
            &BlockDevice, &Iso) == Rc95LdrSuccess);
        assert(Disk.BytesPerSector == 2048 && BlockDevice.BlockSize == 2048);
        Provider = Rc95LdrIso9660GetFileProvider(&Iso);
        assert(Provider != NULL);
        assert(Provider->Open(Provider->VolumeContext,"BOOT.BIN",&File)==Rc95LdrSuccess);
        assert(Provider->Read(Provider->VolumeContext,File,Data,5,&ReadBytes)==Rc95LdrSuccess);
        assert(ReadBytes==5 && memcmp(Data,"HELLO",5)==0);
        Provider->Close(Provider->VolumeContext,File);
        Rc95LdrIso9660Unmount(&Iso);
    }
    return 0;
}
