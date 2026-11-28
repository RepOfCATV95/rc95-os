#include "include/rc95ldruefiservices.h"
#include <stddef.h>
#include <string.h>

static EFI_GUID GraphicsOutputGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;

static RC95LDR_STATUS
MapVideoStatus(EFI_STATUS Status)
{
    if (!EFI_ERROR(Status)) return Rc95LdrSuccess;
    if (Status == EFI_UNSUPPORTED) return Rc95LdrUnsupported;
    if (Status == EFI_NOT_FOUND) return Rc95LdrNotFound;
    if (Status == EFI_OUT_OF_RESOURCES) return Rc95LdrOutOfMemory;
    return Rc95LdrIoError;
}

/* Translate only layouts whose memory representation is understood by BGFX.
   Arbitrary GOP bitmasks must never be mislabeled as RGB565. */
static RC95LDR_STATUS
UefiValidateFramebuffer(RC95LDR_UEFI_VIDEO *Video)
{
    RC95LDR_STATUS Status =
        Rc95LdrBgfxValidateFramebuffer(&Video->Framebuffer);
    if (Status != Rc95LdrSuccess)
        memset(&Video->Framebuffer, 0, sizeof(Video->Framebuffer));
    return Status;
}

static UINT32
UefiGopPixelFormat(const EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info)
{
    if (!Info) return 0;
    if (Info->PixelFormat == PixelRedGreenBlueReserved8BitPerColor)
        return RC95LDR_BGFX_PIXEL_RGBX8888;
    if (Info->PixelFormat == PixelBlueGreenRedReserved8BitPerColor)
        return RC95LDR_BGFX_PIXEL_BGRX8888;
    if (Info->PixelFormat == PixelBitMask &&
        Info->PixelInformation.RedMask == 0xF800u &&
        Info->PixelInformation.GreenMask == 0x07E0u &&
        Info->PixelInformation.BlueMask == 0x001Fu &&
        Info->PixelInformation.ReservedMask == 0u)
        return RC95LDR_BGFX_PIXEL_RGB565;
    return 0;
}

RC95LDR_STATUS
Rc95LdrUefiSetGraphicsMode(void *Context, UINT32 Width, UINT32 Height)
{
    RC95LDR_UEFI_VIDEO *Video = (RC95LDR_UEFI_VIDEO *)Context;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
    EFI_STATUS EfiStatus;
    UINTN InfoSize;
    UINT32 Mode;
    UINT32 Selected = 0;
    int Found = 0;

    if (!Video || Video->StructureSize < sizeof(*Video) ||
        Video->Version != RC95LDR_BGFX_VERSION)
        return Rc95LdrInvalidParameter;

    /* A failed request must not leave a descriptor for a previous GOP mode
       available to a later AcquireFramebuffer call. */
    memset(&Video->Framebuffer, 0, sizeof(Video->Framebuffer));
    if (!Video->Image || !Video->Image->BootServices ||
        !Video->Image->BootServices->FreePool ||
        Width != 1024u || Height != 768u)
        return Rc95LdrInvalidParameter;
    Gop = Video->Gop;
    if (!Gop || !Gop->Mode || !Gop->QueryMode || !Gop->SetMode)
        return Rc95LdrUnsupported;

    for (Mode = 0; Mode < Gop->Mode->MaxMode; ++Mode) {
        Info = NULL;
        InfoSize = 0;
        EfiStatus = Gop->QueryMode(Gop, Mode, &InfoSize, &Info);
        if (EFI_ERROR(EfiStatus) || !Info) {
            if (Info)
                Video->Image->BootServices->FreePool(Info);
            continue;
        }
        /* QueryMode returns firmware-allocated information. Never inspect a
           truncated record, and release every returned buffer on all paths. */
        if (InfoSize >= sizeof(*Info) &&
            Info->HorizontalResolution == Width &&
            Info->VerticalResolution == Height &&
            UefiGopPixelFormat(Info) != 0) {
            Selected = Mode;
            Found = 1;
        }
        Video->Image->BootServices->FreePool(Info);
        if (Found) break;
    }
    if (!Found) return Rc95LdrUnsupported;

    /* The previous mode's framebuffer is no longer trustworthy once
       firmware begins a mode transition. */
    memset(&Video->Framebuffer, 0, sizeof(Video->Framebuffer));
    EfiStatus = Gop->SetMode(Gop, Selected);
    if (EFI_ERROR(EfiStatus)) return MapVideoStatus(EfiStatus);
    if (!Gop->Mode || !Gop->Mode->Info ||
        Gop->Mode->SizeOfInfo < sizeof(*Gop->Mode->Info) ||
        Gop->Mode->Info->HorizontalResolution != Width ||
        Gop->Mode->Info->VerticalResolution != Height ||
        Gop->Mode->Info->PixelFormat == PixelBltOnly ||
        Gop->Mode->FrameBufferSize == 0 ||
        Gop->Mode->FrameBufferBase == 0)
        return Rc95LdrCorruptData;

    memset(&Video->Framebuffer, 0, sizeof(Video->Framebuffer));
    Video->Framebuffer.StructureSize = sizeof(Video->Framebuffer);
    Video->Framebuffer.Version = RC95LDR_BGFX_VERSION;
    Video->Framebuffer.Base = (void *)(uintptr_t)Gop->Mode->FrameBufferBase;
    Video->Framebuffer.Size = Gop->Mode->FrameBufferSize;
    Video->Framebuffer.Width = Width;
    Video->Framebuffer.Height = Height;
    Video->Framebuffer.PixelsPerScanLine =
        Gop->Mode->Info->PixelsPerScanLine;
    Video->Framebuffer.PixelFormat =
        UefiGopPixelFormat(Gop->Mode->Info);
    if (!Video->Framebuffer.PixelFormat) {
        memset(&Video->Framebuffer, 0, sizeof(Video->Framebuffer));
        return Rc95LdrUnsupported;
    }
    return UefiValidateFramebuffer(Video);
}

RC95LDR_STATUS
Rc95LdrUefiAcquireFramebuffer(void *Context,
    RC95LDR_BGFX_FRAMEBUFFER *Framebuffer)
{
    RC95LDR_UEFI_VIDEO *Video = (RC95LDR_UEFI_VIDEO *)Context;
    RC95LDR_STATUS Status;
    if (!Video || Video->StructureSize < sizeof(*Video) ||
        Video->Version != RC95LDR_BGFX_VERSION || !Framebuffer)
        return Rc95LdrInvalidParameter;
    if (!Video->Framebuffer.Base)
        return Rc95LdrNotFound;
    Status = UefiValidateFramebuffer(Video);
    if (Status != Rc95LdrSuccess)
        return Status;
    *Framebuffer = Video->Framebuffer;
    return Rc95LdrSuccess;
}

static RC95LDR_STATUS
UefiVideoReadKey(void *Context, UINT32 *Key)
{
    RC95LDR_UEFI_VIDEO *Video = (RC95LDR_UEFI_VIDEO *)Context;
    EFI_INPUT_KEY Input;
    EFI_STATUS Status;
    EFI_EVENT WaitEvent;
    UINTN EventIndex;
    EFI_SIMPLE_TEXT_INPUT_PROTOCOL *InputProtocol;
    EFI_BOOT_SERVICES *BootServices;
    if (!Video || Video->StructureSize < sizeof(*Video) ||
        Video->Version != RC95LDR_BGFX_VERSION || !Video->Image ||
        !Video->Image->SystemTable ||
        !Video->Image->SystemTable->ConIn || !Key)
        return Rc95LdrInvalidParameter;
    InputProtocol = Video->Image->SystemTable->ConIn;
    BootServices = Video->Image->BootServices;
    Status = InputProtocol->ReadKeyStroke(InputProtocol, &Input);
    if (Status == EFI_NOT_READY) {
        if (!BootServices || !BootServices->WaitForEvent ||
            !InputProtocol->WaitForKey)
            return Rc95LdrUnsupported;
        WaitEvent = InputProtocol->WaitForKey;
        Status = BootServices->WaitForEvent(1, &WaitEvent, &EventIndex);
        if (EFI_ERROR(Status)) return MapVideoStatus(Status);
        Status = InputProtocol->ReadKeyStroke(InputProtocol, &Input);
    }
    if (EFI_ERROR(Status)) return MapVideoStatus(Status);
    if (Input.UnicodeChar == CHAR_CARRIAGE_RETURN)
        *Key = RC95LDR_TEXTUI_KEY_ENTER;
    else if (Input.UnicodeChar == 0x001Bu || Input.ScanCode == SCAN_ESC)
        *Key = RC95LDR_TEXTUI_KEY_ESCAPE;
    else if (Input.ScanCode == SCAN_UP)
        *Key = RC95LDR_TEXTUI_KEY_UP;
    else if (Input.ScanCode == SCAN_DOWN)
        *Key = RC95LDR_TEXTUI_KEY_DOWN;
    else if (Input.ScanCode == SCAN_F8)
        *Key = RC95LDR_TEXTUI_KEY_F8;
    else
        *Key = Input.UnicodeChar;
    return Rc95LdrSuccess;
}

RC95LDR_STATUS
Rc95LdrUefiInitializeVideo(const RC95LDR_UEFI_IMAGE_CONTEXT *Image,
    RC95LDR_UEFI_VIDEO *Video, RC95LDR_BGFX_PLATFORM *Platform)
{
    EFI_STATUS Status;
    if (!Image || Image->StructureSize < sizeof(*Image) ||
        Image->Version != RC95LDR_UEFI_ENTRY_VERSION ||
        !Image->BootServices || !Image->SystemTable ||
        Image->SystemTable->Hdr.Signature != EFI_SYSTEM_TABLE_SIGNATURE ||
        Image->SystemTable->Hdr.HeaderSize <
            offsetof(EFI_SYSTEM_TABLE, ConIn) +
            sizeof(Image->SystemTable->ConIn) ||
        Image->SystemTable->Hdr.HeaderSize <
            offsetof(EFI_SYSTEM_TABLE, BootServices) +
            sizeof(Image->SystemTable->BootServices) ||
        Image->SystemTable->BootServices != Image->BootServices ||
        Image->BootServices->Hdr.Signature != EFI_BOOT_SERVICES_SIGNATURE ||
        Image->BootServices->Hdr.HeaderSize <
            offsetof(EFI_BOOT_SERVICES, LocateProtocol) +
            sizeof(Image->BootServices->LocateProtocol) ||
        Image->BootServices->Hdr.HeaderSize <
            offsetof(EFI_BOOT_SERVICES, FreePool) +
            sizeof(Image->BootServices->FreePool) ||
        Image->BootServices->Hdr.HeaderSize <
            offsetof(EFI_BOOT_SERVICES, WaitForEvent) +
            sizeof(Image->BootServices->WaitForEvent) ||
        !Image->BootServices->WaitForEvent ||
        !Image->BootServices->LocateProtocol ||
        !Image->BootServices->FreePool ||
        !Image->SystemTable->ConIn || !Image->SystemTable->ConIn->ReadKeyStroke ||
        !Image->SystemTable->ConIn->WaitForKey || !Image->BootServices->WaitForEvent ||
        !Video || !Platform)
        return Rc95LdrInvalidParameter;
    memset(Video, 0, sizeof(*Video));
    memset(Platform, 0, sizeof(*Platform));
    Video->StructureSize = sizeof(*Video);
    Video->Version = RC95LDR_BGFX_VERSION;
    Video->Image = Image;
    Status = Image->BootServices->LocateProtocol(
        &GraphicsOutputGuid, NULL, (VOID **)&Video->Gop);
    if (EFI_ERROR(Status) || !Video->Gop)
        return MapVideoStatus(EFI_ERROR(Status) ? Status : EFI_NOT_FOUND);

    Platform->StructureSize = sizeof(*Platform);
    Platform->Version = RC95LDR_BGFX_VERSION;
    Platform->Firmware = RC95LDR_BGFX_FIRMWARE_UEFI;
    Platform->Context = Video;
    Platform->AcquireFramebuffer = Rc95LdrUefiAcquireFramebuffer;
    Platform->SetGraphicsMode = Rc95LdrUefiSetGraphicsMode;
    Platform->ReadKey = UefiVideoReadKey;
    return Rc95LdrSuccess;
}


/* Firmware-menu GOP adapter. QueryMode owns each returned Info buffer; the
   adapter copies only validated scalar fields and always frees that buffer. */
static uint32_t UefiMenuPixelFormat(
    const EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info)
{
    return (uint32_t)UefiGopPixelFormat(Info);
}

static RC95LDR_STATUS UefiMenuEnumerateModes(
    void *Context, RC95LDR_FIRMWARE_VIDEO_MODE *Modes,
    size_t Capacity, size_t *ModeCount)
{
    RC95LDR_UEFI_VIDEO *Video = (RC95LDR_UEFI_VIDEO *)Context;
    UINT32 Mode;
    size_t Count = 0;
    if (ModeCount) *ModeCount = 0;
    if (!Video || !Modes || !ModeCount)
        return Rc95LdrInvalidParameter;
    if (!Video->Gop || !Video->Gop->Mode ||
        !Video->Gop->QueryMode)
        return Rc95LdrUnsupported;
    /* QueryMode buffers are firmware-owned and must be released even when
       QueryMode reports an error after returning a partial allocation. */
    if (!Video->Image || !Video->Image->BootServices ||
        !Video->Image->BootServices->FreePool)
        return Rc95LdrInvalidParameter;
    for (Mode = 0; Mode < Video->Gop->Mode->MaxMode; ++Mode)
    {
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info = NULL;
        UINTN InfoSize = 0;
        EFI_STATUS EfiStatus = Video->Gop->QueryMode(
            Video->Gop, Mode, &InfoSize, &Info);
        uint32_t PixelFormat;
        if (EFI_ERROR(EfiStatus) || !Info)
        {
            if (Info) Video->Image->BootServices->FreePool(Info);
            continue;
        }
        if (InfoSize < sizeof(*Info))
        {
            Video->Image->BootServices->FreePool(Info);
            continue;
        }
        PixelFormat = UefiMenuPixelFormat(Info);
        if (PixelFormat && Info->HorizontalResolution != 0 &&
            Info->VerticalResolution != 0 &&
            Info->PixelsPerScanLine >= Info->HorizontalResolution)
        {
            if (Count >= Capacity)
            {
                Video->Image->BootServices->FreePool(Info);
                *ModeCount = Count + 1u;
                return Rc95LdrBufferTooSmall;
            }
            memset(&Modes[Count], 0, sizeof(Modes[Count]));
            Modes[Count].ModeId = Mode;
            Modes[Count].Width = Info->HorizontalResolution;
            Modes[Count].Height = Info->VerticalResolution;
            Modes[Count].PixelsPerScanLine = Info->PixelsPerScanLine;
            Modes[Count].PixelFormat = PixelFormat;
            if (Video->Gop->Mode->Mode == Mode &&
                Video->Gop->Mode->Info &&
                Video->Gop->Mode->SizeOfInfo >= sizeof(*Video->Gop->Mode->Info) &&
                Video->Gop->Mode->FrameBufferBase &&
                Video->Gop->Mode->FrameBufferSize)
            {
                Modes[Count].FramebufferBase =
                    Video->Gop->Mode->FrameBufferBase;
                Modes[Count].FramebufferSize =
                    Video->Gop->Mode->FrameBufferSize;
            }
            ++Count;
        }
        Video->Image->BootServices->FreePool(Info);
    }
    *ModeCount = Count;
    return Count ? Rc95LdrSuccess : Rc95LdrNotFound;
}

static RC95LDR_STATUS UefiMenuSetMode(void *Context, uint32_t ModeId)
{
    RC95LDR_UEFI_VIDEO *Video = (RC95LDR_UEFI_VIDEO *)Context;
    EFI_STATUS Status;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
    uint32_t PixelFormat;
    if (!Video || !Video->Gop || !Video->Gop->Mode ||
        !Video->Gop->SetMode || ModeId >= Video->Gop->Mode->MaxMode)
        return Rc95LdrUnsupported;
    /* A mode switch can invalidate the old physical framebuffer even when
       the requested mode later proves unsupported or malformed. Never leave
       a stale descriptor available to MapFramebuffer/AcquireFramebuffer. */
    memset(&Video->Framebuffer, 0, sizeof(Video->Framebuffer));
    Status = Video->Gop->SetMode(Video->Gop, ModeId);
    if (EFI_ERROR(Status)) return MapVideoStatus(Status);
    if (!Video->Gop->Mode || !Video->Gop->Mode->Info ||
        Video->Gop->Mode->SizeOfInfo < sizeof(*Video->Gop->Mode->Info) ||
        !Video->Gop->Mode->FrameBufferBase ||
        !Video->Gop->Mode->FrameBufferSize)
        return Rc95LdrCorruptData;
    Info = Video->Gop->Mode->Info;
    if (!Info->HorizontalResolution || !Info->VerticalResolution)
        return Rc95LdrCorruptData;
    PixelFormat = UefiMenuPixelFormat(Info);
    if (!PixelFormat || Info->PixelsPerScanLine < Info->HorizontalResolution)
        return Rc95LdrUnsupported;
    memset(&Video->Framebuffer, 0, sizeof(Video->Framebuffer));
    Video->Framebuffer.StructureSize = sizeof(Video->Framebuffer);
    Video->Framebuffer.Version = RC95LDR_BGFX_VERSION;
    Video->Framebuffer.Base =
        (void *)(uintptr_t)Video->Gop->Mode->FrameBufferBase;
    Video->Framebuffer.Size = Video->Gop->Mode->FrameBufferSize;
    Video->Framebuffer.Width = Info->HorizontalResolution;
    Video->Framebuffer.Height = Info->VerticalResolution;
    Video->Framebuffer.PixelsPerScanLine = Info->PixelsPerScanLine;
    Video->Framebuffer.PixelFormat = PixelFormat;
    return UefiValidateFramebuffer(Video);
}

static RC95LDR_STATUS UefiMenuMapFramebuffer(
    void *Context, uint64_t Base, uint64_t Size, void **Mapping)
{
    RC95LDR_UEFI_VIDEO *Video = (RC95LDR_UEFI_VIDEO *)Context;
    if (Mapping) *Mapping = NULL;
    RC95LDR_STATUS Status;
    if (!Video || !Mapping)
        return Rc95LdrInvalidParameter;
    if (!Video->Framebuffer.Base)
        return Rc95LdrNotFound;
    /* The descriptor is mutable loader state. Revalidate its complete
       extent immediately before exposing the GOP address to the renderer;
       AcquireFramebuffer's earlier validation is not sufficient here. */
    Status = UefiValidateFramebuffer(Video);
    if (Status != Rc95LdrSuccess)
        return Status;
    if (Base != (uint64_t)(uintptr_t)Video->Framebuffer.Base ||
        Size != Video->Framebuffer.Size)
        return Rc95LdrNotFound;
    /* UEFI GOP exposes the framebuffer in the current address space. No
       speculative page mapping is performed by this adapter. */
    *Mapping = Video->Framebuffer.Base;
    return Rc95LdrSuccess;
}

RC95LDR_STATUS Rc95LdrUefiInitializeFirmwareMenuOps(
    const RC95LDR_UEFI_IMAGE_CONTEXT *Image, const RC95LDR_TEXTUI *TextUi,
    RC95LDR_UEFI_VIDEO *Video, RC95LDR_FIRMWARE_MENU_OPS *MenuOps,
    RC95LDR_TEXTUI_EXT *TextUiExtension)
{
    EFI_STATUS EfiStatus;
    RC95LDR_STATUS Status;
    if (!Image || Image->StructureSize < sizeof(*Image) ||
        Image->Version != RC95LDR_UEFI_ENTRY_VERSION ||
        !Image->SystemTable || !Image->BootServices ||
        Image->SystemTable->Hdr.Signature != EFI_SYSTEM_TABLE_SIGNATURE ||
        Image->SystemTable->Hdr.HeaderSize <
            offsetof(EFI_SYSTEM_TABLE, ConIn) + sizeof(Image->SystemTable->ConIn) ||
        Image->SystemTable->Hdr.HeaderSize <
            offsetof(EFI_SYSTEM_TABLE, BootServices) +
            sizeof(Image->SystemTable->BootServices) ||
        Image->SystemTable->BootServices != Image->BootServices ||
        Image->BootServices->Hdr.Signature != EFI_BOOT_SERVICES_SIGNATURE ||
        Image->BootServices->Hdr.HeaderSize <
            offsetof(EFI_BOOT_SERVICES, LocateProtocol) +
            sizeof(Image->BootServices->LocateProtocol) ||
        Image->BootServices->Hdr.HeaderSize <
            offsetof(EFI_BOOT_SERVICES, FreePool) +
            sizeof(Image->BootServices->FreePool) ||
        Image->BootServices->Hdr.HeaderSize <
            offsetof(EFI_BOOT_SERVICES, WaitForEvent) +
            sizeof(Image->BootServices->WaitForEvent) ||
        !TextUi || TextUi->StructureSize < sizeof(*TextUi) ||
        TextUi->Version != RC95LDR_TEXTUI_VERSION || !TextUi->Write ||
        !Video || !MenuOps || !TextUiExtension ||
        !Image->SystemTable->ConIn ||
        !Image->SystemTable->ConIn->ReadKeyStroke ||
        !Image->SystemTable->ConIn->WaitForKey ||
        !Image->BootServices->WaitForEvent ||
        !Image->BootServices->LocateProtocol ||
        !Image->BootServices->FreePool)
        return Rc95LdrInvalidParameter;

    memset(Video, 0, sizeof(*Video));
    memset(MenuOps, 0, sizeof(*MenuOps));
    memset(TextUiExtension, 0, sizeof(*TextUiExtension));
    Video->StructureSize = sizeof(*Video);
    Video->Version = RC95LDR_BGFX_VERSION;
    Video->Image = Image;
    EfiStatus = Image->BootServices->LocateProtocol(
        &GraphicsOutputGuid, NULL, (VOID **)&Video->Gop);
    if (EFI_ERROR(EfiStatus)) Video->Gop = NULL;

    MenuOps->StructureSize = sizeof(*MenuOps);
    MenuOps->Version = RC95LDR_FIRMWARE_MENU_VERSION;
    MenuOps->Context = Video;
    MenuOps->EnumerateModes = UefiMenuEnumerateModes;
    MenuOps->SetMode = UefiMenuSetMode;
    MenuOps->MapFramebuffer = UefiMenuMapFramebuffer;
    MenuOps->ReadKey = UefiVideoReadKey;
    MenuOps->TextUi = TextUi;
    MenuOps->TextUiExtension = TextUiExtension;

    TextUiExtension->StructureSize = sizeof(*TextUiExtension);
    TextUiExtension->Version = RC95LDR_TEXTUI_EXT_VERSION;
    TextUiExtension->Context = Video;
    TextUiExtension->ReadKey = UefiVideoReadKey;

    Status = Rc95LdrUefiBindFirmwareMenuOps(Image, MenuOps);
    /* Restart is an optional menu action. A firmware lacking ResetSystem
       still gets interactive boot selection, simply without that row. */
    return Status == Rc95LdrUnsupported ? Rc95LdrSuccess : Status;
}
