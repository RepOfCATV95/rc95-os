#include "include/rc95ldrbios.h"
#include <string.h>

#define VBE_MODE_1024X768 0x0118u
#define VBE_LINEAR_FRAMEBUFFER 0x4000u
#define VBE_MODE_INFO_BYTES 256u
#define VBE_CONTROLLER_INFO_BYTES 512u
#define VBE_MODE_LIST_MAX 128u
#define VBE_MODE_LIST_BYTES (VBE_MODE_LIST_MAX * 2u)

static uint16_t Read16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t Read32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Prefer VBE 3.0 linear pitch and color masks when supplied. Older VBE
   implementations leave these fields zero and use the legacy fields. */
static int
DecodeModeLayout(const uint8_t *Info, uint32_t *Pitch,
                 uint32_t *PixelFormat)
{
    uint32_t BitsPerPixel = Info[25];
    uint32_t RedSize, RedPosition, GreenSize, GreenPosition;
    uint32_t BlueSize, BluePosition, ReservedSize, ReservedPosition;
    uint32_t LinearMasks = Info[54] || Info[56] || Info[58] || Info[60];
    *Pitch = Read16(Info + 50);
    if (!*Pitch) *Pitch = Read16(Info + 16);
    if (LinearMasks) {
        RedSize = Info[54]; RedPosition = Info[55];
        GreenSize = Info[56]; GreenPosition = Info[57];
        BlueSize = Info[58]; BluePosition = Info[59];
        ReservedSize = Info[60]; ReservedPosition = Info[61];
    } else {
        RedSize = Info[31]; RedPosition = Info[32];
        GreenSize = Info[33]; GreenPosition = Info[34];
        BlueSize = Info[35]; BluePosition = Info[36];
        ReservedSize = Info[37]; ReservedPosition = Info[38];
    }
    if (BitsPerPixel == 16u && RedSize == 5u && RedPosition == 11u &&
        GreenSize == 6u && GreenPosition == 5u &&
        BlueSize == 5u && BluePosition == 0u && ReservedSize == 0u) {
        *PixelFormat = RC95LDR_BGFX_PIXEL_RGB565;
        return *Pitch != 0u;
    }
    if (BitsPerPixel == 32u && RedSize == 8u && GreenSize == 8u &&
        GreenPosition == 8u && BlueSize == 8u &&
        (ReservedSize == 0u ||
         (ReservedSize == 8u && ReservedPosition == 24u))) {
        if (RedPosition == 16u && BluePosition == 0u) {
            *PixelFormat = RC95LDR_BGFX_PIXEL_BGRX8888;
            return *Pitch != 0u;
        }
        if (RedPosition == 0u && BluePosition == 16u) {
            *PixelFormat = RC95LDR_BGFX_PIXEL_RGBX8888;
            return *Pitch != 0u;
        }
    }
    return 0;
}

static RC95LDR_STATUS
QueryMode(RC95LDR_BIOS_VIDEO *Video, uint16_t ModeId, uint8_t **ModeInfo)
{
    const RC95LDR_BIOS_SERVICES *Services;
    RC95LDR_BIOS_REGISTERS Registers;
    RC95LDR_STATUS Status;
    void *Mapping = NULL;

    if (!Video || !Video->Text.Services || !ModeInfo)
        return Rc95LdrInvalidParameter;
    Services = Video->Text.Services;
    Status = Rc95LdrBiosValidateServices(Services);
    if (Status != Rc95LdrSuccess) return Status;
    if (Services->ScratchLength < VBE_MODE_INFO_BYTES)
        return Rc95LdrBufferTooSmall;
    Status = Services->MapLowMemory(Services->Context,
        Services->ScratchPhysical, VBE_MODE_INFO_BYTES, &Mapping);
    if (Status != Rc95LdrSuccess) return Status;
    if (!Mapping) return Rc95LdrInvalidParameter;
    memset(Mapping, 0, VBE_MODE_INFO_BYTES);
    memset(&Registers, 0, sizeof(Registers));
    Registers.Eax = 0x4F01u;
    Registers.Ecx = ModeId;
    Registers.Es = (uint16_t)(Services->ScratchPhysical >> 4);
    Registers.Edi = Services->ScratchPhysical & 0x0Fu;
    Status = Services->InvokeInterrupt(Services->Context, 0x10, &Registers);
    if (Status == Rc95LdrSuccess &&
        ((uint16_t)Registers.Eax != 0x004Fu ||
         !(Read16((const uint8_t *)Mapping) & 0x0080u) ||
         !Read16((const uint8_t *)Mapping + 18) ||
         !Read16((const uint8_t *)Mapping + 20)))
        Status = Rc95LdrUnsupported;
    if (Status == Rc95LdrSuccess)
        *ModeInfo = (uint8_t *)Mapping;
    else
        Services->UnmapLowMemory(Services->Context, Mapping,
                                 VBE_MODE_INFO_BYTES);
    return Status;
}

static RC95LDR_STATUS
BiosSetGraphicsModeById(RC95LDR_BIOS_VIDEO *Video, uint16_t ModeId,
                        uint32_t Width, uint32_t Height)
{
    const RC95LDR_BIOS_SERVICES *Services;
    RC95LDR_BIOS_REGISTERS Registers;
    RC95LDR_STATUS Status;
    uint8_t *Info = NULL;
    uint32_t Pitch, PixelFormat;
    uint16_t BitsPerPixel;
    uint32_t FramebufferBase, BytesPerPixel;
    uint64_t FramebufferSize;

    if (!Video || !Width || !Height)
        return Rc95LdrInvalidParameter;
    memset(&Video->Framebuffer, 0, sizeof(Video->Framebuffer));
    Services = Video->Text.Services;
    Status = QueryMode(Video, ModeId, &Info);
    if (Status != Rc95LdrSuccess) return Status;

    BitsPerPixel = Info[25];
    FramebufferBase = Read32(Info + 40);
    if (Read16(Info + 18) != Width || Read16(Info + 20) != Height ||
        !FramebufferBase || (BitsPerPixel != 16u && BitsPerPixel != 32u) ||
        Info[27] != 6u || !DecodeModeLayout(Info, &Pitch, &PixelFormat) ||
        Pitch < Width * (BitsPerPixel / 8u)) {
        Services->UnmapLowMemory(Services->Context, Info,
                                 VBE_MODE_INFO_BYTES);
        return Rc95LdrUnsupported;
    }

    BytesPerPixel = BitsPerPixel / 8u;
    if (Pitch % BytesPerPixel) {
        Services->UnmapLowMemory(Services->Context, Info,
                                 VBE_MODE_INFO_BYTES);
        return Rc95LdrCorruptData;
    }
    FramebufferSize = (uint64_t)Pitch * Height;
    if (FramebufferSize > UINT32_MAX ||
        FramebufferSize > (0x100000000ULL - FramebufferBase)) {
        Services->UnmapLowMemory(Services->Context, Info,
                                 VBE_MODE_INFO_BYTES);
        return Rc95LdrIntegerOverflow;
    }

    memset(&Registers, 0, sizeof(Registers));
    Registers.Eax = 0x4F02u;
    Registers.Ebx = ModeId | VBE_LINEAR_FRAMEBUFFER;
    Status = Services->InvokeInterrupt(Services->Context, 0x10, &Registers);
    Services->UnmapLowMemory(Services->Context, Info, VBE_MODE_INFO_BYTES);
    if (Status != Rc95LdrSuccess || (uint16_t)Registers.Eax != 0x004Fu)
        return Status == Rc95LdrSuccess ? Rc95LdrIoError : Status;

    memset(&Video->Framebuffer, 0, sizeof(Video->Framebuffer));
    Video->Framebuffer.StructureSize = sizeof(Video->Framebuffer);
    Video->Framebuffer.Version = RC95LDR_BGFX_VERSION;
    Video->Framebuffer.Base = (void *)(uintptr_t)FramebufferBase;
    Video->Framebuffer.Size = FramebufferSize;
    Video->Framebuffer.Width = Width;
    Video->Framebuffer.Height = Height;
    Video->Framebuffer.PixelsPerScanLine = Pitch / BytesPerPixel;
    Video->Framebuffer.PixelFormat = PixelFormat;
    Status = Rc95LdrBgfxValidateFramebuffer(&Video->Framebuffer);
    if (Status != Rc95LdrSuccess)
        memset(&Video->Framebuffer, 0, sizeof(Video->Framebuffer));
    return Status;
}

RC95LDR_STATUS
Rc95LdrBiosSetGraphicsMode(void *Context, uint32_t Width, uint32_t Height)
{
    RC95LDR_BIOS_VIDEO *Video = (RC95LDR_BIOS_VIDEO *)Context;
    if (!Video || Width != 1024u || Height != 768u)
        return Rc95LdrInvalidParameter;
    return BiosSetGraphicsModeById(Video, VBE_MODE_1024X768, Width, Height);
}

RC95LDR_STATUS
Rc95LdrBiosAcquireFramebuffer(void *Context,
                              RC95LDR_BGFX_FRAMEBUFFER *Framebuffer)
{
    RC95LDR_BIOS_VIDEO *Video = (RC95LDR_BIOS_VIDEO *)Context;
    RC95LDR_STATUS Status;
    if (!Video || !Framebuffer) return Rc95LdrInvalidParameter;
    if (!Video->Framebuffer.Base) return Rc95LdrNotFound;
    Status = Rc95LdrBgfxValidateFramebuffer(&Video->Framebuffer);
    if (Status != Rc95LdrSuccess) {
        memset(&Video->Framebuffer, 0, sizeof(Video->Framebuffer));
        return Status;
    }
    *Framebuffer = Video->Framebuffer;
    return Rc95LdrSuccess;
}

RC95LDR_STATUS
Rc95LdrBiosEnumerateFirmwareModes(void *Context,
    RC95LDR_FIRMWARE_VIDEO_MODE *Modes, size_t Capacity, size_t *Count)
{
    RC95LDR_BIOS_VIDEO *Video = (RC95LDR_BIOS_VIDEO *)Context;
    const RC95LDR_BIOS_SERVICES *Services;
    RC95LDR_BIOS_REGISTERS Registers;
    uint8_t *Controller = NULL, *List = NULL;
    uint32_t ListPhysical;
    size_t Index, Found = 0;
    int Terminated = 0;
    RC95LDR_STATUS Status;
    if (!Video || !Modes || !Capacity || !Count)
        return Rc95LdrInvalidParameter;
    *Count = 0;
    Services = Video->Text.Services;
    Status = Rc95LdrBiosValidateServices(Services);
    if (Status != Rc95LdrSuccess) return Status;
    if (Services->ScratchLength < VBE_CONTROLLER_INFO_BYTES)
        return Rc95LdrBufferTooSmall;
    Status = Services->MapLowMemory(Services->Context,
        Services->ScratchPhysical, VBE_CONTROLLER_INFO_BYTES,
        (void **)&Controller);
    if (Status != Rc95LdrSuccess) return Status;
    if (!Controller) return Rc95LdrInvalidParameter;
    memset(Controller, 0, VBE_CONTROLLER_INFO_BYTES);
    memcpy(Controller, "VBE2", 4);
    memset(&Registers, 0, sizeof(Registers));
    Registers.Eax = 0x4F00u;
    Registers.Es = (uint16_t)(Services->ScratchPhysical >> 4);
    Registers.Edi = Services->ScratchPhysical & 0x0Fu;
    Status = Services->InvokeInterrupt(Services->Context, 0x10, &Registers);
    if (Status == Rc95LdrSuccess &&
        ((uint16_t)Registers.Eax != 0x004Fu ||
         memcmp(Controller, "VESA", 4) != 0 ||
         Read16(Controller + 4) < 0x0200u))
        Status = Rc95LdrUnsupported;
    /* A VBE far pointer is segment * 16 + offset. The offset is not
       restricted to the low nibble, so bitwise OR misdecodes valid pointers
       whose offset overlaps the segment's low physical-address bits. */
    ListPhysical = ((uint32_t)Read16(Controller + 16) << 4) +
                   (uint32_t)Read16(Controller + 14);
    Services->UnmapLowMemory(Services->Context, Controller,
                             VBE_CONTROLLER_INFO_BYTES);
    if (Status != Rc95LdrSuccess) return Status;
    if (!ListPhysical || ListPhysical >= 0x100000u ||
        VBE_MODE_LIST_BYTES > 0x100000u - ListPhysical)
        return Rc95LdrCorruptData;
    Status = Services->MapLowMemory(Services->Context, ListPhysical,
                                    VBE_MODE_LIST_BYTES, (void **)&List);
    if (Status != Rc95LdrSuccess) return Status;
    if (!List) return Rc95LdrInvalidParameter;

    for (Index = 0; Index < VBE_MODE_LIST_MAX; ++Index) {
        uint16_t ModeId = Read16(List + Index * 2u);
        uint8_t *Info = NULL;
        uint32_t Width, Height, Pitch, Bpp, Bytes, Base, Format;
        uint64_t Size;
        if (ModeId == 0xFFFFu) { Terminated = 1; break; }
        if (ModeId & 0xC000u) continue;
        Status = QueryMode(Video, ModeId, &Info);
        if (Status != Rc95LdrSuccess) continue;
        Width = Read16(Info + 18); Height = Read16(Info + 20);
        Pitch = Read16(Info + 16); Bpp = Info[25];
        Base = Read32(Info + 40);
        if ((Read16(Info) & 0x0080u) && Info[27] == 6u &&
            Width >= 640u && Width <= 4096u &&
            Height >= 400u && Height <= 2160u && Base &&
            (Bpp == 16u || Bpp == 32u)) {
            Bytes = Bpp / 8u;
            Format = 0u;
            if (DecodeModeLayout(Info, &Pitch, &Format) &&
                Pitch >= Width * Bytes && Pitch % Bytes == 0u) {
                Size = (uint64_t)Pitch * Height;
                if (Format && Size <= UINT32_MAX &&
                    Size <= 0x100000000ULL - Base && Found < Capacity) {
                    size_t Duplicate;
                    for (Duplicate = 0; Duplicate < Found; ++Duplicate)
                        if (Modes[Duplicate].ModeId == ModeId) break;
                    if (Duplicate == Found) {
                        memset(&Modes[Found], 0, sizeof(Modes[Found]));
                        Modes[Found].ModeId = ModeId;
                        Modes[Found].Width = Width;
                        Modes[Found].Height = Height;
                        Modes[Found].PixelsPerScanLine = Pitch / Bytes;
                        Modes[Found].PixelFormat = Format;
                        Modes[Found].FramebufferBase = Base;
                        Modes[Found].FramebufferSize = Size;
                        ++Found;
                    }
                }
            }
        }
        Services->UnmapLowMemory(Services->Context, Info, VBE_MODE_INFO_BYTES);
    }
    Services->UnmapLowMemory(Services->Context, List, VBE_MODE_LIST_BYTES);
    if (!Terminated) return Rc95LdrCorruptData;
    *Count = Found;
    return Found ? Rc95LdrSuccess : Rc95LdrNotFound;
}
RC95LDR_STATUS
Rc95LdrBiosSetFirmwareVideoMode(void *Context, uint32_t ModeId)
{
    RC95LDR_BIOS_VIDEO *Video = (RC95LDR_BIOS_VIDEO *)Context;
    RC95LDR_FIRMWARE_VIDEO_MODE Modes[VBE_MODE_LIST_MAX];
    size_t Count = 0, Index;
    RC95LDR_STATUS Status;
    if (!Video) return Rc95LdrInvalidParameter;
    Status = Rc95LdrBiosEnumerateFirmwareModes(Video, Modes,
        VBE_MODE_LIST_MAX, &Count);
    if (Status != Rc95LdrSuccess) return Status;
    for (Index = 0; Index < Count; ++Index)
        if (Modes[Index].ModeId == ModeId)
            return BiosSetGraphicsModeById(Video, (uint16_t)ModeId,
                Modes[Index].Width, Modes[Index].Height);
    return Rc95LdrUnsupported;
}
RC95LDR_STATUS
Rc95LdrBiosMapFirmwareFramebuffer(void *Context,uint64_t Base,
    uint64_t Size,void **Mapping)
{
    RC95LDR_BIOS_VIDEO *Video=(RC95LDR_BIOS_VIDEO *)Context;
    RC95LDR_STATUS Status;
    if(!Video||!Mapping||!Base||!Size)return Rc95LdrInvalidParameter;
    *Mapping = NULL;
    if(!Video->Framebuffer.Base)return Rc95LdrNotFound;
    Status = Rc95LdrBgfxValidateFramebuffer(&Video->Framebuffer);
    if(Status != Rc95LdrSuccess) {
        memset(&Video->Framebuffer, 0, sizeof(Video->Framebuffer));
        return Status;
    }
    if((uint64_t)(uintptr_t)Video->Framebuffer.Base!=Base||
       Video->Framebuffer.Size!=Size)return Rc95LdrNotFound;
    /* Native BIOS startup enters with paging disabled and identity mapping;
       the VBE linear framebuffer is therefore directly addressable. */
    *Mapping=(void *)(uintptr_t)Base;
    return Rc95LdrSuccess;
}

static RC95LDR_STATUS
BiosVideoSetTextMode(void *Context, uint32_t Columns, uint32_t Rows)
{
    RC95LDR_BIOS_VIDEO *Video = (RC95LDR_BIOS_VIDEO *)Context;
    if (!Video) return Rc95LdrInvalidParameter;
    memset(&Video->Framebuffer, 0, sizeof(Video->Framebuffer));
    return Rc95LdrBiosSetTextMode(&Video->Text, Columns, Rows);
}

static RC95LDR_STATUS
BiosVideoReadKey(void *Context, uint32_t *Key)
{
    RC95LDR_BIOS_VIDEO *Video = (RC95LDR_BIOS_VIDEO *)Context;
    if (!Video) return Rc95LdrInvalidParameter;
    return Rc95LdrBiosReadKey(&Video->Text, Key);
}

RC95LDR_STATUS
Rc95LdrBiosInitializeVideo(const RC95LDR_BIOS_SERVICES *Services,
    RC95LDR_BIOS_VIDEO *Video, RC95LDR_BGFX_PLATFORM *Platform)
{
    RC95LDR_STATUS Status;
    if (!Video || !Platform) return Rc95LdrInvalidParameter;
    memset(Video, 0, sizeof(*Video));
    Status = Rc95LdrBiosValidateServices(Services);
    if (Status != Rc95LdrSuccess) return Status;
    Video->Text.Services = Services;
    Video->Text.VideoPage = 0;
    Video->Text.BackgroundAttribute = 0x07u;
    memset(Platform, 0, sizeof(*Platform));
    Platform->StructureSize = sizeof(*Platform);
    Platform->Version = RC95LDR_BGFX_VERSION;
    Platform->Firmware = RC95LDR_BGFX_FIRMWARE_BIOS;
    Platform->Context = Video;
    Platform->AcquireFramebuffer = Rc95LdrBiosAcquireFramebuffer;
    Platform->SetGraphicsMode = Rc95LdrBiosSetGraphicsMode;
    Platform->SetTextMode = BiosVideoSetTextMode;
    Platform->ReadKey = BiosVideoReadKey;
    return Rc95LdrSuccess;
}
