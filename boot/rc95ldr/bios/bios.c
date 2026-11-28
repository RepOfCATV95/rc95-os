#include "rc95ldrbios.h"
#include "rc95ldrtextui.h"
#include <string.h>

#pragma pack(push, 1)
typedef struct _BIOS_E820_ENTRY {
    uint64_t Base, Length;
    uint32_t Type, Attributes;
} BIOS_E820_ENTRY;
typedef struct _BIOS_EDD_PARAMS {
    uint16_t Size, Flags;
    uint32_t Cylinders, Heads, SectorsPerTrack;
    uint64_t TotalSectors;
    uint16_t BytesPerSector;
    uint32_t EddConfig;
} BIOS_EDD_PARAMS;
typedef struct _BIOS_DISK_ADDRESS_PACKET {
    uint8_t Size, Reserved;
    uint16_t BlockCount;
    uint16_t BufferOffset, BufferSegment;
    uint64_t Lba;
} BIOS_DISK_ADDRESS_PACKET;
#pragma pack(pop)

#define BIOS_E820_FINISHED 0xFFFFFFFFu
#define BIOS_EDD_DATA_OFFSET 32u

static int AddU64(uint64_t A, uint64_t B, uint64_t *Out)
{
    if (B > UINT64_MAX - A) return 0;
    *Out = A + B;
    return 1;
}

static int BiosCarry(const RC95LDR_BIOS_REGISTERS *R)
{
    return (R->Eflags & 1u) != 0;
}

static RC95LDR_STATUS
BiosValidateLowMemoryServices(const RC95LDR_BIOS_SERVICES *S)
{
    uint64_t End;
    if (!S || S->StructureSize < sizeof(*S) ||
        S->Version != RC95LDR_BIOS_ABI_VERSION ||
        !S->MapLowMemory || !S->UnmapLowMemory ||
        !S->ScratchPhysical || S->ScratchPhysical >= 0x100000u ||
        S->ScratchLength <= BIOS_EDD_DATA_OFFSET ||
        !AddU64(S->ScratchPhysical, S->ScratchLength, &End) ||
        End > 0x100000u ||
        (S->ScratchPhysical < 0x8000u && End > 0x5000u))
        return Rc95LdrInvalidParameter;
    return Rc95LdrSuccess;
}

RC95LDR_STATUS
Rc95LdrBiosValidateServices(const RC95LDR_BIOS_SERVICES *S)
{
    RC95LDR_STATUS Status = BiosValidateLowMemoryServices(S);
    if (Status != Rc95LdrSuccess) return Status;
    return S->InvokeInterrupt ? Rc95LdrSuccess : Rc95LdrInvalidParameter;
}

RC95LDR_STATUS
Rc95LdrBiosE820Next(void *Context, uint32_t *Continuation,
    RC95LDR_FIRMWARE_MEMORY_RANGE *Range, int *Done)
{
    const RC95LDR_BIOS_SERVICES *S =
        (const RC95LDR_BIOS_SERVICES *)Context;
    RC95LDR_BIOS_REGISTERS R;
    BIOS_E820_ENTRY *Entry;
    void *Mapping = NULL;
    RC95LDR_STATUS Status;
    if (!Continuation || !Range || !Done) return Rc95LdrInvalidParameter;
    *Done = 0;
    if (*Continuation == BIOS_E820_FINISHED) {
        *Done = 1;
        return Rc95LdrSuccess;
    }
    Status = Rc95LdrBiosValidateServices(S);
    if (Status != Rc95LdrSuccess) return Status;
    Status = S->MapLowMemory(S->Context, S->ScratchPhysical,
                             sizeof(*Entry), &Mapping);
    if (Status != Rc95LdrSuccess) return Status;
    if (!Mapping) return Rc95LdrInvalidParameter;
    memset(Mapping, 0, sizeof(*Entry));
    Entry = (BIOS_E820_ENTRY *)Mapping;
    Entry->Attributes = 1u;
    memset(&R, 0, sizeof(R));
    R.Eax = 0xE820u;
    R.Edx = RC95LDR_BIOS_E820_SIGNATURE;
    R.Ecx = sizeof(*Entry);
    R.Ebx = *Continuation;
    R.Es = (uint16_t)(S->ScratchPhysical >> 4);
    R.Edi = S->ScratchPhysical & 0xFu;
    Status = S->InvokeInterrupt(S->Context, 0x15, &R);
    if (Status == Rc95LdrSuccess && BiosCarry(&R))
        Status = Rc95LdrIoError;
    if (Status == Rc95LdrSuccess &&
        ((uint32_t)R.Eax != RC95LDR_BIOS_E820_SIGNATURE ||
         R.Ecx < 20u || !Entry->Length ||
         Entry->Base > UINT64_MAX - Entry->Length))
        Status = Rc95LdrCorruptData;
    if (Status == Rc95LdrSuccess) {
        Range->Base = Entry->Base;
        Range->Length = Entry->Length;
        Range->Attributes = (R.Ecx >= 24u) ? Entry->Attributes : 1u;
        switch (Entry->Type) {
        case 1: Range->Kind = Rc95LdrFirmwareMemoryAvailable; break;
        case 3: Range->Kind = Rc95LdrFirmwareMemoryAcpiReclaim; break;
        case 4: Range->Kind = Rc95LdrFirmwareMemoryAcpiNvs; break;
        case 5: Range->Kind = Rc95LdrFirmwareMemoryUnusable; break;
        default: Range->Kind = Rc95LdrFirmwareMemoryReserved; break;
        }
        *Continuation = R.Ebx ? R.Ebx : BIOS_E820_FINISHED;
    }
    S->UnmapLowMemory(S->Context, Mapping, sizeof(*Entry));
    return Status;
}

RC95LDR_STATUS
Rc95LdrBiosAddMemoryReservations(const RC95LDR_BIOS_SERVICES *Services,
    RC95LDR_MEMORY_RESERVATION_SET *Reservations)
{
    RC95LDR_STATUS Status = BiosValidateLowMemoryServices(Services);
    if (Status != Rc95LdrSuccess) return Status;
    if (!Reservations) return Rc95LdrInvalidParameter;
    Status = Rc95LdrReserveMemoryRange(Reservations, 0x5000u, 0x3000u,
                                       LoaderMemoryData);
    if (Status != Rc95LdrSuccess) return Status;
    return Rc95LdrReserveMemoryRange(Reservations, Services->ScratchPhysical,
                                     Services->ScratchLength, LoaderMemoryData);
}

RC95LDR_STATUS
Rc95LdrBiosCollectMemoryMap(const RC95LDR_BIOS_SERVICES *Services,
    RC95LDR_FIRMWARE_MEMORY_RANGE *Ranges, size_t Capacity, size_t *Count)
{
    RC95LDR_BIOS_MEMORY_OPS Ops;
    RC95LDR_STATUS Status = Rc95LdrBiosValidateServices(Services);
    if (Status != Rc95LdrSuccess) return Status;
    Ops.Context = (void *)Services;
    Ops.E820Next = Rc95LdrBiosE820Next;
    return Rc95LdrCollectBiosE820(&Ops, Ranges, Capacity, Count);
}

RC95LDR_STATUS
Rc95LdrBiosProbeDisk(const RC95LDR_BIOS_SERVICES *S, uint8_t Drive,
    RC95LDR_BIOS_DISK *Disk)
{
    RC95LDR_BIOS_REGISTERS R;
    BIOS_EDD_PARAMS *P;
    void *Mapping = NULL;
    RC95LDR_STATUS Status;
    if (!Disk) return Rc95LdrInvalidParameter;
    Status = Rc95LdrBiosValidateServices(S);
    if (Status != Rc95LdrSuccess) return Status;
    memset(Disk, 0, sizeof(*Disk));
    Disk->Drive = Drive;
    memset(&R, 0, sizeof(R));
    R.Eax = 0x4100u;
    R.Ebx = 0x55AAu;
    R.Edx = Drive;
    Status = S->InvokeInterrupt(S->Context, 0x13, &R);
    if (Status != Rc95LdrSuccess) return Status;
    Disk->EddSupported = !BiosCarry(&R) &&
        (uint16_t)R.Ebx == RC95LDR_BIOS_EDD_SIGNATURE &&
        (((uint16_t)R.Ecx & 1u) != 0);
    if (!Disk->EddSupported) return Rc95LdrUnsupported;

    Status = S->MapLowMemory(S->Context, S->ScratchPhysical,
                             sizeof(*P), &Mapping);
    if (Status != Rc95LdrSuccess) return Status;
    if (!Mapping) return Rc95LdrInvalidParameter;
    memset(Mapping, 0, sizeof(*P));
    P = (BIOS_EDD_PARAMS *)Mapping;
    P->Size = sizeof(*P);
    memset(&R, 0, sizeof(R));
    R.Eax = 0x4800u;
    R.Edx = Drive;
    R.Ds = (uint16_t)(S->ScratchPhysical >> 4);
    R.Esi = S->ScratchPhysical & 0xFu;
    Status = S->InvokeInterrupt(S->Context, 0x13, &R);
    if (Status == Rc95LdrSuccess && BiosCarry(&R))
        Status = Rc95LdrIoError;
    if (Status == Rc95LdrSuccess &&
        (P->Size < 26u || P->BytesPerSector < 512u ||
         P->BytesPerSector > 4096u ||
         (P->BytesPerSector & (P->BytesPerSector - 1u)) ||
         !P->TotalSectors))
        Status = Rc95LdrCorruptData;
    if (Status == Rc95LdrSuccess) {
        Disk->BytesPerSector = P->BytesPerSector;
        Disk->BlockCount = P->TotalSectors;
    }
    S->UnmapLowMemory(S->Context, Mapping, sizeof(*P));
    return Status;
}

RC95LDR_STATUS
Rc95LdrBiosReadDisk(const RC95LDR_BIOS_SERVICES *S,
    const RC95LDR_BIOS_DISK *Disk, uint64_t Lba, uint32_t Blocks,
    void *Buffer, size_t BufferLength)
{
    uint8_t *Out = (uint8_t *)Buffer;
    uint64_t EndLba;
    size_t TotalBytes, Done = 0;
    RC95LDR_STATUS Status;
    if (!Disk || !Buffer || !Blocks || !Disk->EddSupported ||
        Disk->BytesPerSector < 512u || Disk->BytesPerSector > 4096u ||
        (Disk->BytesPerSector & (Disk->BytesPerSector - 1u)) ||
        !Rc95LdrMultiplySize((size_t)Blocks, Disk->BytesPerSector, &TotalBytes) ||
        TotalBytes != BufferLength ||
        !AddU64(Lba, Blocks, &EndLba) || EndLba > Disk->BlockCount)
        return Rc95LdrInvalidParameter;
    Status = Rc95LdrBiosValidateServices(S);
    if (Status != Rc95LdrSuccess) return Status;
    if (S->ScratchLength <= BIOS_EDD_DATA_OFFSET + Disk->BytesPerSector)
        return Rc95LdrBufferTooSmall;

    while (Done < TotalBytes) {
        uint32_t Physical = S->ScratchPhysical;
        uint32_t DataPhysical = Physical + BIOS_EDD_DATA_OFFSET;
        size_t Boundary = 0x10000u - (DataPhysical & 0xFFFFu);
        size_t ChunkBytes = TotalBytes - Done;
        uint32_t ChunkBlocks;
        BIOS_DISK_ADDRESS_PACKET *Dap;
        void *Mapping = NULL;

        if (ChunkBytes > S->ScratchLength - BIOS_EDD_DATA_OFFSET)
            ChunkBytes = S->ScratchLength - BIOS_EDD_DATA_OFFSET;
        if (ChunkBytes > Boundary) ChunkBytes = Boundary;
        ChunkBlocks = (uint32_t)(ChunkBytes / Disk->BytesPerSector);
        if (ChunkBlocks > RC95LDR_BIOS_EDD_MAX_BLOCKS)
            ChunkBlocks = RC95LDR_BIOS_EDD_MAX_BLOCKS;
        if (!ChunkBlocks) return Rc95LdrBufferTooSmall;
        ChunkBytes = (size_t)ChunkBlocks * Disk->BytesPerSector;

        Status = S->MapLowMemory(S->Context, Physical,
                                 ChunkBytes + BIOS_EDD_DATA_OFFSET, &Mapping);
        if (Status != Rc95LdrSuccess) return Status;
        if (!Mapping) return Rc95LdrInvalidParameter;
        memset(Mapping, 0, sizeof(*Dap));
        Dap = (BIOS_DISK_ADDRESS_PACKET *)Mapping;
        Dap->Size = sizeof(*Dap);
        Dap->BlockCount = (uint16_t)ChunkBlocks;
        Dap->BufferOffset = (uint16_t)(DataPhysical & 0xFu);
        Dap->BufferSegment = (uint16_t)(DataPhysical >> 4);
        Dap->Lba = Lba + Done / Disk->BytesPerSector;

        {
            RC95LDR_BIOS_REGISTERS R;
            memset(&R, 0, sizeof(R));
            R.Eax = 0x4200u;
            R.Edx = Disk->Drive;
            R.Ds = (uint16_t)(Physical >> 4);
            R.Esi = Physical & 0xFu;
            Status = S->InvokeInterrupt(S->Context, 0x13, &R);
            if (Status == Rc95LdrSuccess && BiosCarry(&R))
                Status = Rc95LdrIoError;
        }
        if (Status == Rc95LdrSuccess)
            memcpy(Out + Done, (uint8_t *)Mapping + BIOS_EDD_DATA_OFFSET,
                   ChunkBytes);
        S->UnmapLowMemory(S->Context, Mapping,
                          ChunkBytes + BIOS_EDD_DATA_OFFSET);
        if (Status != Rc95LdrSuccess) return Status;
        Done += ChunkBytes;
    }
    return Rc95LdrSuccess;
}

RC95LDR_STATUS
Rc95LdrBiosWriteDisk(const RC95LDR_BIOS_SERVICES *S,
    const RC95LDR_BIOS_DISK *Disk,uint64_t Lba,uint32_t Blocks,
    const void *Buffer,size_t BufferLength)
{
    const uint8_t *Input=(const uint8_t*)Buffer;
    uint64_t EndLba;size_t TotalBytes,Done=0;RC95LDR_STATUS Status;
    if(!Disk||!Buffer||!Blocks||!Disk->EddSupported||
       Disk->BytesPerSector<512u||Disk->BytesPerSector>4096u||
       (Disk->BytesPerSector&(Disk->BytesPerSector-1u))||
       !Rc95LdrMultiplySize((size_t)Blocks,Disk->BytesPerSector,&TotalBytes)||
       TotalBytes!=BufferLength||!AddU64(Lba,Blocks,&EndLba)||
       EndLba>Disk->BlockCount)return Rc95LdrInvalidParameter;
    Status=Rc95LdrBiosValidateServices(S);
    if(Status!=Rc95LdrSuccess)return Status;
    if(S->ScratchLength<=BIOS_EDD_DATA_OFFSET+Disk->BytesPerSector)
        return Rc95LdrBufferTooSmall;
    while(Done<TotalBytes){
        uint32_t Physical=S->ScratchPhysical;
        uint32_t DataPhysical=Physical+BIOS_EDD_DATA_OFFSET;
        size_t Boundary=0x10000u-(DataPhysical&0xffffu);
        size_t ChunkBytes=TotalBytes-Done;uint32_t ChunkBlocks;
        BIOS_DISK_ADDRESS_PACKET *Dap;void *Mapping=NULL;
        if(ChunkBytes>S->ScratchLength-BIOS_EDD_DATA_OFFSET)
            ChunkBytes=S->ScratchLength-BIOS_EDD_DATA_OFFSET;
        if(ChunkBytes>Boundary)ChunkBytes=Boundary;
        ChunkBlocks=(uint32_t)(ChunkBytes/Disk->BytesPerSector);
        if(ChunkBlocks>RC95LDR_BIOS_EDD_MAX_BLOCKS)
            ChunkBlocks=RC95LDR_BIOS_EDD_MAX_BLOCKS;
        if(!ChunkBlocks)return Rc95LdrBufferTooSmall;
        ChunkBytes=(size_t)ChunkBlocks*Disk->BytesPerSector;
        Status=S->MapLowMemory(S->Context,Physical,
            ChunkBytes+BIOS_EDD_DATA_OFFSET,&Mapping);
        if(Status!=Rc95LdrSuccess)return Status;
        if(!Mapping)return Rc95LdrInvalidParameter;
        memset(Mapping,0,sizeof(*Dap));
        Dap=(BIOS_DISK_ADDRESS_PACKET*)Mapping;
        Dap->Size=sizeof(*Dap);Dap->BlockCount=(uint16_t)ChunkBlocks;
        Dap->BufferOffset=(uint16_t)(DataPhysical&0x0fu);
        Dap->BufferSegment=(uint16_t)(DataPhysical>>4);
        Dap->Lba=Lba+Done/Disk->BytesPerSector;
        memcpy((uint8_t*)Mapping+BIOS_EDD_DATA_OFFSET,Input+Done,ChunkBytes);
        {
            RC95LDR_BIOS_REGISTERS R;memset(&R,0,sizeof(R));
            R.Eax=0x4300u;R.Edx=Disk->Drive;
            R.Ds=(uint16_t)(Physical>>4);R.Esi=Physical&0x0fu;
            Status=S->InvokeInterrupt(S->Context,0x13,&R);
            if(Status==Rc95LdrSuccess&&BiosCarry(&R))Status=Rc95LdrIoError;
        }
        S->UnmapLowMemory(S->Context,Mapping,
            ChunkBytes+BIOS_EDD_DATA_OFFSET);
        if(Status!=Rc95LdrSuccess)return Status;
        Done+=ChunkBytes;
    }
    return Rc95LdrSuccess;
}

RC95LDR_STATUS
Rc95LdrBiosWriteBlocks(void *Context,uint64_t Lba,uint32_t Blocks,
    const void *Buffer,size_t BufferLength)
{
    const RC95LDR_BIOS_BLOCK_CONTEXT *Block=
        (const RC95LDR_BIOS_BLOCK_CONTEXT*)Context;
    if(!Block||!Block->Services||!Block->Disk)return Rc95LdrInvalidParameter;
    return Rc95LdrBiosWriteDisk(Block->Services,Block->Disk,Lba,Blocks,
        Buffer,BufferLength);
}

RC95LDR_STATUS
Rc95LdrBiosReadBlocks(void *Context, uint64_t Lba, uint32_t Blocks,
    void *Buffer, size_t BufferLength)
{
    const RC95LDR_BIOS_BLOCK_CONTEXT *Block =
        (const RC95LDR_BIOS_BLOCK_CONTEXT *)Context;
    if (!Block || !Block->Services || !Block->Disk)
        return Rc95LdrInvalidParameter;
    return Rc95LdrBiosReadDisk(Block->Services, Block->Disk, Lba, Blocks,
                               Buffer, BufferLength);
}

RC95LDR_STATUS
Rc95LdrBiosInitializeBlockDevice(const RC95LDR_BIOS_SERVICES *Services,
    const RC95LDR_BIOS_DISK *Disk, RC95LDR_BIOS_BLOCK_CONTEXT *Context,
    RC95LDR_BLOCK_DEVICE *Device)
{
    RC95LDR_STATUS Status = Rc95LdrBiosValidateServices(Services);
    if (Status != Rc95LdrSuccess) return Status;
    if (!Disk || !Context || !Device || !Disk->EddSupported ||
        !Disk->BlockCount || Disk->BytesPerSector < 512u ||
        Disk->BytesPerSector > 4096u ||
        (Disk->BytesPerSector & (Disk->BytesPerSector - 1u)))
        return Rc95LdrInvalidParameter;
    Context->Services = Services;
    Context->Disk = Disk;
    Device->DeviceContext = Context;
    Device->BlockCount = Disk->BlockCount;
    Device->BlockSize = Disk->BytesPerSector;
    Device->Read = Rc95LdrBiosReadBlocks;
    Device->Write = Rc95LdrBiosWriteBlocks;
    return Rc95LdrSuccess;
}

RC95LDR_STATUS
Rc95LdrBiosWriteText(void *Context, const char *Text)
{
    RC95LDR_BIOS_TEXT *T = (RC95LDR_BIOS_TEXT *)Context;
    const RC95LDR_BIOS_SERVICES *S;
    size_t i, Length;
    if (!T || !T->Services || !Text)
        return Rc95LdrInvalidParameter;
    Length = strlen(Text);
    S = T->Services;
    if (Rc95LdrBiosValidateServices(S) != Rc95LdrSuccess)
        return Rc95LdrInvalidParameter;
    for (i = 0; i < Length; ++i) {
        RC95LDR_BIOS_REGISTERS R;
        memset(&R, 0, sizeof(R));
        R.Eax = 0x0E00u | (uint8_t)Text[i];
        R.Ebx = T->VideoPage;
        if (S->InvokeInterrupt(S->Context, 0x10, &R) != Rc95LdrSuccess)
            return Rc95LdrIoError;
    }
    return Rc95LdrSuccess;
}

RC95LDR_STATUS
Rc95LdrBiosReadKey(void *Context, uint32_t *Key)
{
    RC95LDR_BIOS_TEXT *T = (RC95LDR_BIOS_TEXT *)Context;
    RC95LDR_BIOS_REGISTERS R;
    RC95LDR_STATUS Status;
    uint8_t Ascii, Scan;
    if (!T || !T->Services || !Key) return Rc95LdrInvalidParameter;
    Status = Rc95LdrBiosValidateServices(T->Services);
    if (Status != Rc95LdrSuccess) return Status;
    memset(&R, 0, sizeof(R));
    Status = T->Services->InvokeInterrupt(T->Services->Context, 0x16, &R);
    if (Status != Rc95LdrSuccess) return Status;
    Ascii = (uint8_t)R.Eax;
    Scan = (uint8_t)(R.Eax >> 8);
    if (Ascii == 13u) *Key = RC95LDR_TEXTUI_KEY_ENTER;
    else if (Ascii == 27u) *Key = RC95LDR_TEXTUI_KEY_ESCAPE;
    else if (Scan == 0x48u) *Key = RC95LDR_TEXTUI_KEY_UP;
    else if (Scan == 0x50u) *Key = RC95LDR_TEXTUI_KEY_DOWN;
    else if (Scan == 0x42u) *Key = RC95LDR_TEXTUI_KEY_F8;
    else *Key = Ascii;
    return Rc95LdrSuccess;
}

RC95LDR_STATUS
Rc95LdrBiosClearText(void *Context)
{
    RC95LDR_BIOS_TEXT *T = (RC95LDR_BIOS_TEXT *)Context;
    RC95LDR_BIOS_REGISTERS R;
    RC95LDR_STATUS Status;
    if (!T || !T->Services) return Rc95LdrInvalidParameter;
    Status = Rc95LdrBiosValidateServices(T->Services);
    if (Status != Rc95LdrSuccess) return Status;
    memset(&R, 0, sizeof(R));
    R.Eax = 0x0600u;
    R.Ebx = (uint32_t)T->BackgroundAttribute << 8;
    R.Ecx = 0;
    R.Edx = 0x314Fu;
    Status = T->Services->InvokeInterrupt(T->Services->Context, 0x10, &R);
    if (Status != Rc95LdrSuccess) return Status;
    memset(&R, 0, sizeof(R));
    R.Eax = 0x0200u;
    R.Ebx = (uint32_t)T->VideoPage << 8;
    R.Edx = 0;
    return T->Services->InvokeInterrupt(T->Services->Context, 0x10, &R);
}

RC95LDR_STATUS
Rc95LdrBiosSetTextMode(void *Context, uint32_t Columns, uint32_t Rows)
{
    RC95LDR_BIOS_TEXT *T = (RC95LDR_BIOS_TEXT *)Context;
    RC95LDR_BIOS_REGISTERS R;
    RC95LDR_STATUS Status;
    if (!T || !T->Services || Columns != 80u ||
        (Rows != 25u && Rows != 50u))
        return Rc95LdrInvalidParameter;
    Status = Rc95LdrBiosValidateServices(T->Services);
    if (Status != Rc95LdrSuccess) return Status;
    memset(&R, 0, sizeof(R));
    R.Eax = 0x0003u;
    Status = T->Services->InvokeInterrupt(T->Services->Context, 0x10, &R);
    if (Status != Rc95LdrSuccess || Rows == 25u) return Status;
    memset(&R, 0, sizeof(R));
    R.Eax = 0x1112u;
    R.Ebx = 0;
    return T->Services->InvokeInterrupt(T->Services->Context, 0x10, &R);
}

RC95LDR_STATUS
Rc95LdrBiosInitializeTextUi(const RC95LDR_BIOS_SERVICES *Services,
    RC95LDR_BIOS_TEXT *Context, RC95LDR_TEXTUI *Ui,
    RC95LDR_TEXTUI_EXT *Extension)
{
    RC95LDR_STATUS Status = Rc95LdrBiosValidateServices(Services);
    if (Status != Rc95LdrSuccess) return Status;
    if (!Context || !Ui || !Extension) return Rc95LdrInvalidParameter;
    Context->Services = Services;
    Context->VideoPage = 0;
    Context->BackgroundAttribute = 0x07u;
    memset(Context->Reserved, 0, sizeof(Context->Reserved));

    memset(Ui, 0, sizeof(*Ui));
    Ui->StructureSize = sizeof(*Ui);
    Ui->Version = RC95LDR_TEXTUI_VERSION;
    Ui->Context = Context;
    Ui->Write = Rc95LdrBiosWriteText;
    Ui->Clear = Rc95LdrBiosClearText;

    memset(Extension, 0, sizeof(*Extension));
    Extension->StructureSize = sizeof(*Extension);
    Extension->Version = RC95LDR_TEXTUI_EXT_VERSION;
    Extension->Context = Context;
    Extension->SetMode = Rc95LdrBiosSetTextMode;
    Extension->ReadKey = Rc95LdrBiosReadKey;
    return Rc95LdrSuccess;
}

RC95LDR_STATUS
Rc95LdrBiosSetTextBackground(void *Context, uint8_t Attribute)
{
    RC95LDR_BIOS_TEXT *T = (RC95LDR_BIOS_TEXT *)Context;
    if (!T || !T->Services) return Rc95LdrInvalidParameter;
    T->BackgroundAttribute = Attribute;
    return Rc95LdrSuccess;
}

RC95LDR_STATUS
Rc95LdrBiosShowErrorScreen(RC95LDR_BIOS_TEXT *Context,
    const RC95LDR_TEXTUI *Ui, const char *Title, const char *Message,
    RC95LDR_STATUS ErrorStatus, uint32_t Detail, const char *Hint)
{
    RC95LDR_STATUS Status, RestoreStatus;
    if (!Context || !Ui) return Rc95LdrInvalidParameter;
    Status = Rc95LdrBiosSetTextBackground(Context, 0x4Fu);
    if (Status == Rc95LdrSuccess)
        Status = Rc95LdrTextUiShowError(Ui, Title, Message,
                                        ErrorStatus, Detail, Hint);
    RestoreStatus = Rc95LdrBiosSetTextBackground(Context, 0x07u);
    return Status == Rc95LdrSuccess ? RestoreStatus : Status;
}
