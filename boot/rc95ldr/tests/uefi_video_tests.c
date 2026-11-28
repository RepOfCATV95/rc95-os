#include "rc95ldruefiservices.h"
#include <assert.h>
#include <stddef.h>
#include <string.h>

static EFI_BOOT_SERVICES BootServices;
static EFI_SYSTEM_TABLE SystemTable;
static EFI_SIMPLE_TEXT_INPUT_PROTOCOL ConsoleInput;
static EFI_GRAPHICS_OUTPUT_PROTOCOL Gop;
static EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE GopMode;
static EFI_GRAPHICS_OUTPUT_MODE_INFORMATION ModeInfo[4];
/* Large enough for the highest tested mode (1024x768x32). */
static UINT8 Framebuffer[1024u * 768u * 4u];
static int GopAvailable;
static UINT32 SetModeCalls;
static EFI_STATUS SetModeStatusOverride;
static UINTN FramebufferSizeOverride;
static int SetModeNullInfo;
static UINTN SetModeInfoSizeOverride;
static int SetModeZeroFramebufferBase;
static int SetModeZeroFramebufferSize;
static int QueryModeErrorMode = -1;
static int QueryModeNullInfoMode = -1;
static UINT32 FreePoolCalls;
static UINTN QueryModeInfoSizeOverride;

static EFI_STATUS EFIAPI TestReadKey(
    EFI_SIMPLE_TEXT_INPUT_PROTOCOL *This, EFI_INPUT_KEY *Key)
{
    (void)This;
    if (Key) memset(Key, 0, sizeof(*Key));
    return EFI_NOT_READY;
}

static EFI_STATUS EFIAPI TestWaitForEvent(
    UINTN NumberOfEvents, EFI_EVENT *Events, UINTN *Index)
{
    (void)NumberOfEvents; (void)Events;
    if (Index) *Index = 0;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI TestFreePool(VOID *Buffer)
{
    (void)Buffer;
    ++FreePoolCalls;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI TestQueryMode(
    EFI_GRAPHICS_OUTPUT_PROTOCOL *This, UINT32 Mode,
    UINTN *InfoSize, EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **Info)
{
    (void)This;
    if (!InfoSize || !Info || Mode >= 4u) return EFI_INVALID_PARAMETER;
    *InfoSize = QueryModeInfoSizeOverride ?
        QueryModeInfoSizeOverride : sizeof(ModeInfo[Mode]);
    *Info = (int)Mode == QueryModeNullInfoMode ?
        NULL : &ModeInfo[Mode];
    /* Model firmware that returns an allocated record with an error. */
    if ((int)Mode == QueryModeErrorMode) return EFI_UNSUPPORTED;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI TestSetMode(
    EFI_GRAPHICS_OUTPUT_PROTOCOL *This, UINT32 Mode)
{
    (void)This;
    if (Mode >= 4u) return EFI_UNSUPPORTED;
    ++SetModeCalls;
    if (EFI_ERROR(SetModeStatusOverride)) return SetModeStatusOverride;
    GopMode.Mode = Mode;
    GopMode.Info = SetModeNullInfo ? NULL : &ModeInfo[Mode];
    GopMode.SizeOfInfo = SetModeInfoSizeOverride ?
        SetModeInfoSizeOverride : sizeof(ModeInfo[Mode]);
    GopMode.FrameBufferBase = SetModeZeroFramebufferBase ? 0 :
        (EFI_PHYSICAL_ADDRESS)(uintptr_t)Framebuffer;
    GopMode.FrameBufferSize = SetModeZeroFramebufferSize ? 0 :
        (FramebufferSizeOverride ? FramebufferSizeOverride : sizeof(Framebuffer));
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI TestLocateProtocol(
    EFI_GUID *Protocol, VOID *Registration, VOID **Interface)
{
    (void)Protocol; (void)Registration;
    if (!Interface) return EFI_INVALID_PARAMETER;
    *Interface = NULL;
    if (!GopAvailable) return EFI_NOT_FOUND;
    *Interface = &Gop;
    return EFI_SUCCESS;
}

static RC95LDR_STATUS TestTextWrite(void *Context, const char *Text)
{
    (void)Context; (void)Text;
    return Rc95LdrSuccess;
}

int main(void)
{
    RC95LDR_UEFI_IMAGE_CONTEXT Image;
    RC95LDR_UEFI_VIDEO Video;
    RC95LDR_FIRMWARE_MENU_OPS MenuOps;
    RC95LDR_TEXTUI TextUi;
    RC95LDR_TEXTUI_EXT TextExtension;
    RC95LDR_BGFX_PLATFORM Platform;
    RC95LDR_FIRMWARE_VIDEO_MODE Modes[4];
    RC95LDR_BGFX_FRAMEBUFFER FramebufferView;
    size_t ModeCount = 0;
    void *Mapping = NULL;

    memset(&BootServices, 0, sizeof(BootServices));
    memset(&SystemTable, 0, sizeof(SystemTable));
    memset(&ConsoleInput, 0, sizeof(ConsoleInput));
    memset(&Gop, 0, sizeof(Gop));
    memset(&GopMode, 0, sizeof(GopMode));
    memset(ModeInfo, 0, sizeof(ModeInfo));
    memset(&Image, 0, sizeof(Image));
    memset(&TextUi, 0, sizeof(TextUi));

    BootServices.Hdr.Signature = EFI_BOOT_SERVICES_SIGNATURE;
    BootServices.Hdr.HeaderSize = sizeof(BootServices);
    BootServices.LocateProtocol = TestLocateProtocol;
    BootServices.FreePool = TestFreePool;
    BootServices.WaitForEvent = TestWaitForEvent;

    SystemTable.Hdr.Signature = EFI_SYSTEM_TABLE_SIGNATURE;
    SystemTable.Hdr.HeaderSize = sizeof(SystemTable);
    SystemTable.BootServices = &BootServices;
    SystemTable.ConIn = &ConsoleInput;
    ConsoleInput.ReadKeyStroke = TestReadKey;
    ConsoleInput.WaitForKey = (EFI_EVENT)(UINTN)1;

    Image.StructureSize = sizeof(Image);
    Image.Version = RC95LDR_UEFI_ENTRY_VERSION;
    Image.SystemTable = &SystemTable;
    Image.BootServices = &BootServices;

    TextUi.StructureSize = sizeof(TextUi);
    TextUi.Version = RC95LDR_TEXTUI_VERSION;
    TextUi.Write = TestTextWrite;

    /* Firmware without GOP must still initialize the text-menu adapter. */
    GopAvailable = 0;
    assert(Rc95LdrUefiInitializeFirmwareMenuOps(&Image, &TextUi,
        &Video, &MenuOps, &TextExtension) == Rc95LdrSuccess);
    assert(Video.Gop == NULL);
    assert(MenuOps.EnumerateModes(MenuOps.Context, Modes, 4,
        &ModeCount) == Rc95LdrUnsupported);
    assert(MenuOps.SetMode(MenuOps.Context, 0) == Rc95LdrUnsupported);

    /* Mode 0: 1024x768 RGB565; mode 1: 800x600 RGBX8888;
       mode 2: BLT-only; mode 3: a nonstandard bitmask. */
    ModeInfo[0].HorizontalResolution = 1024;
    ModeInfo[0].VerticalResolution = 768;
    ModeInfo[0].PixelFormat = PixelBitMask;
    ModeInfo[0].PixelInformation.RedMask = 0xF800u;
    ModeInfo[0].PixelInformation.GreenMask = 0x07E0u;
    ModeInfo[0].PixelInformation.BlueMask = 0x001Fu;
    ModeInfo[0].PixelInformation.ReservedMask = 0;
    ModeInfo[0].PixelsPerScanLine = 1024;

    ModeInfo[1].HorizontalResolution = 800;
    ModeInfo[1].VerticalResolution = 600;
    ModeInfo[1].PixelFormat = PixelRedGreenBlueReserved8BitPerColor;
    ModeInfo[1].PixelsPerScanLine = 800;

    ModeInfo[2].HorizontalResolution = 640;
    ModeInfo[2].VerticalResolution = 480;
    ModeInfo[2].PixelFormat = PixelBltOnly;
    ModeInfo[2].PixelsPerScanLine = 640;

    ModeInfo[3] = ModeInfo[0];
    ModeInfo[3].PixelInformation.RedMask = 0x001Fu;
    ModeInfo[3].PixelInformation.BlueMask = 0xF800u;

    Gop.QueryMode = TestQueryMode;
    Gop.SetMode = TestSetMode;
    Gop.Mode = &GopMode;
    GopMode.MaxMode = 4;

    GopAvailable = 1;
    assert(Rc95LdrUefiInitializeFirmwareMenuOps(&Image, &TextUi,
        &Video, &MenuOps, &TextExtension) == Rc95LdrSuccess);
    assert(Video.Gop == &Gop);

    /* Reject incomplete firmware allocator context before calling QueryMode. */
    {
        const RC95LDR_UEFI_IMAGE_CONTEXT *SavedImage = Video.Image;
        EFI_BOOT_SERVICES *SavedBootServices = Image.BootServices;
        Video.Image = NULL;
        assert(MenuOps.EnumerateModes(MenuOps.Context, Modes, 4,
            &ModeCount) == Rc95LdrInvalidParameter);
        Video.Image = SavedImage;
        Image.BootServices = NULL;
        assert(MenuOps.EnumerateModes(MenuOps.Context, Modes, 4,
            &ModeCount) == Rc95LdrInvalidParameter);
        Image.BootServices = SavedBootServices;
        SavedBootServices->FreePool = NULL;
        assert(MenuOps.EnumerateModes(MenuOps.Context, Modes, 4,
            &ModeCount) == Rc95LdrInvalidParameter);
        SavedBootServices->FreePool = TestFreePool;
    }

    assert(MenuOps.EnumerateModes(MenuOps.Context, Modes, 4,
        &ModeCount) == Rc95LdrSuccess);
    assert(ModeCount == 2);
    assert(Modes[0].ModeId == 0);
    assert(Modes[0].PixelFormat == RC95LDR_BGFX_PIXEL_RGB565);
    assert(Modes[1].ModeId == 1);
    assert(Modes[1].PixelFormat == RC95LDR_BGFX_PIXEL_RGBX8888);

    /* A GOP record with a supported pixel format but zero dimensions is
       malformed and must not be offered to the firmware boot menu. */
    {
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION Saved = ModeInfo[2];
        ModeInfo[2] = ModeInfo[1];
        ModeInfo[2].HorizontalResolution = 0;
        assert(MenuOps.EnumerateModes(MenuOps.Context, Modes, 4,
            &ModeCount) == Rc95LdrSuccess);
        assert(ModeCount == 2);
        ModeInfo[2] = Saved;
    }

    /* QueryMode can report success but provide no information record.
       Such a mode is skipped; the null result is not passed to FreePool,
       while all other returned records are released exactly once. */
    {
        UINT32 FreePoolBefore = FreePoolCalls;
        QueryModeNullInfoMode = 0;
        assert(MenuOps.EnumerateModes(MenuOps.Context, Modes, 4,
            &ModeCount) == Rc95LdrSuccess);
        assert(ModeCount == 1);
        assert(Modes[0].ModeId == 1);
        assert(FreePoolCalls - FreePoolBefore == 3u);
        QueryModeNullInfoMode = -1;
    }

    /* QueryMode may return an error and still hand back a firmware pool
       allocation. Enumeration must release that record exactly once and
       continue examining subsequent modes. */
    {
        UINT32 FreePoolBefore = FreePoolCalls;
        QueryModeErrorMode = 0;
        assert(MenuOps.EnumerateModes(MenuOps.Context, Modes, 4,
            &ModeCount) == Rc95LdrSuccess);
        assert(ModeCount == 1);
        assert(Modes[0].ModeId == 1);
        assert(FreePoolCalls - FreePoolBefore == 4u);
        QueryModeErrorMode = -1;
    }

    /* A successful QueryMode call can still return a truncated information
       record. Enumeration must reject it without reading fields beyond the
       supplied size, release every returned buffer, and report no modes. */
    {
        UINT32 FreePoolBefore = FreePoolCalls;
        QueryModeInfoSizeOverride = sizeof(ModeInfo[0]) - 1u;
        assert(MenuOps.EnumerateModes(MenuOps.Context, Modes, 4,
            &ModeCount) == Rc95LdrNotFound);
        assert(ModeCount == 0);
        assert(FreePoolCalls - FreePoolBefore == 4u);
        QueryModeInfoSizeOverride = 0;
    }

    /* Capacity exhaustion is reported without leaking the first valid
       QueryMode record; ModeCount reports the required minimum capacity. */
    {
        UINT32 FreePoolBefore = FreePoolCalls;
        assert(MenuOps.EnumerateModes(MenuOps.Context, Modes, 0,
            &ModeCount) == Rc95LdrBufferTooSmall);
        assert(ModeCount == 1);
        assert(FreePoolCalls - FreePoolBefore == 1u);
    }

    /* Even if firmware switches to a nominally supported format, a
       zero-sized current mode is corrupt and must not publish a framebuffer. */
    {
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION Saved = ModeInfo[0];
        ModeInfo[0].VerticalResolution = 0;
        assert(MenuOps.SetMode(MenuOps.Context, 0) == Rc95LdrCorruptData);
        assert(MenuOps.MapFramebuffer(MenuOps.Context,
            (uint64_t)(uintptr_t)Framebuffer, sizeof(Framebuffer),
            &Mapping) == Rc95LdrNotFound);
        ModeInfo[0] = Saved;
    }
    assert(MenuOps.SetMode(MenuOps.Context, 0) == Rc95LdrSuccess);
    assert(SetModeCalls == 2);
    assert(MenuOps.MapFramebuffer(MenuOps.Context,
        (uint64_t)(uintptr_t)Framebuffer, sizeof(Framebuffer),
        &Mapping) == Rc95LdrSuccess);
    assert(Mapping == Framebuffer);
    assert(Rc95LdrUefiAcquireFramebuffer(&Video, &FramebufferView)
        == Rc95LdrSuccess);
    assert(FramebufferView.PixelFormat == RC95LDR_BGFX_PIXEL_RGB565);

    /* A failed firmware mode transition invalidates the previously valid
       framebuffer. Neither mapping nor acquisition may expose the stale
       descriptor after SetMode reports an error. */
    SetModeStatusOverride = EFI_DEVICE_ERROR;
    Mapping = (void *)(uintptr_t)1;
    assert(MenuOps.SetMode(MenuOps.Context, 0) == Rc95LdrIoError);
    assert(MenuOps.MapFramebuffer(MenuOps.Context,
        (uint64_t)(uintptr_t)Framebuffer, sizeof(Framebuffer),
        &Mapping) == Rc95LdrNotFound);
    assert(Mapping == NULL);
    assert(Rc95LdrUefiAcquireFramebuffer(&Video, &FramebufferView)
        == Rc95LdrNotFound);
    SetModeStatusOverride = EFI_SUCCESS;
    assert(MenuOps.SetMode(MenuOps.Context, 0) == Rc95LdrSuccess);

    /* Verify the GOP-specific EFI errors retain their intended RC95LDR
       mappings and each failed transition invalidates the prior descriptor. */
    {
        static const struct
        {
            EFI_STATUS EfiStatus;
            RC95LDR_STATUS ExpectedStatus;
        } Cases[] = {
            { EFI_UNSUPPORTED, Rc95LdrUnsupported },
            { EFI_NOT_FOUND, Rc95LdrNotFound },
            { EFI_OUT_OF_RESOURCES, Rc95LdrOutOfMemory }
        };
        size_t CaseIndex;
        for (CaseIndex = 0;
            CaseIndex < sizeof(Cases) / sizeof(Cases[0]); ++CaseIndex)
        {
            assert(MenuOps.SetMode(MenuOps.Context, 0) == Rc95LdrSuccess);
            SetModeStatusOverride = Cases[CaseIndex].EfiStatus;
            Mapping = (void *)(uintptr_t)1;
            assert(MenuOps.SetMode(MenuOps.Context, 0) ==
                Cases[CaseIndex].ExpectedStatus);
            assert(MenuOps.MapFramebuffer(MenuOps.Context,
                (uint64_t)(uintptr_t)Framebuffer, sizeof(Framebuffer),
                &Mapping) == Rc95LdrNotFound);
            assert(Mapping == NULL);
            assert(Rc95LdrUefiAcquireFramebuffer(&Video, &FramebufferView)
                == Rc95LdrNotFound);
        }
        SetModeStatusOverride = EFI_SUCCESS;
        assert(MenuOps.SetMode(MenuOps.Context, 0) == Rc95LdrSuccess);
    }

    /* A successful SetMode is not sufficient evidence that firmware
       returned a usable current-mode record or framebuffer aperture. */
    SetModeNullInfo = 1;
    assert(MenuOps.SetMode(MenuOps.Context, 0) == Rc95LdrCorruptData);
    assert(Rc95LdrUefiAcquireFramebuffer(&Video, &FramebufferView)
        == Rc95LdrNotFound);
    SetModeNullInfo = 0;

    SetModeInfoSizeOverride = sizeof(ModeInfo[0]) - 1u;
    assert(MenuOps.SetMode(MenuOps.Context, 0) == Rc95LdrCorruptData);
    assert(Rc95LdrUefiAcquireFramebuffer(&Video, &FramebufferView)
        == Rc95LdrNotFound);
    SetModeInfoSizeOverride = 0;

    SetModeZeroFramebufferBase = 1;
    assert(MenuOps.SetMode(MenuOps.Context, 0) == Rc95LdrCorruptData);
    assert(Rc95LdrUefiAcquireFramebuffer(&Video, &FramebufferView)
        == Rc95LdrNotFound);
    SetModeZeroFramebufferBase = 0;

    SetModeZeroFramebufferSize = 1;
    assert(MenuOps.SetMode(MenuOps.Context, 0) == Rc95LdrCorruptData);
    assert(Rc95LdrUefiAcquireFramebuffer(&Video, &FramebufferView)
        == Rc95LdrNotFound);
    SetModeZeroFramebufferSize = 0;

    /* The menu adapter rejects an impossible pitch before publishing a
       framebuffer, even when GOP reports SetMode success. */
    {
        UINT32 SavedPitch = ModeInfo[0].PixelsPerScanLine;
        ModeInfo[0].PixelsPerScanLine = 1023;
        assert(MenuOps.SetMode(MenuOps.Context, 0) == Rc95LdrUnsupported);
        assert(Rc95LdrUefiAcquireFramebuffer(&Video, &FramebufferView)
            == Rc95LdrNotFound);
        ModeInfo[0].PixelsPerScanLine = SavedPitch;
    }
    assert(MenuOps.SetMode(MenuOps.Context, 0) == Rc95LdrSuccess);

    /* Mapping is an independent trust boundary: corrupting the cached
       descriptor after acquisition must not expose the old GOP address. */
    {
        UINTN SavedSize = Video.Framebuffer.Size;
        Video.Framebuffer.Size = 1;
        Mapping = (void *)(uintptr_t)1;
        assert(MenuOps.MapFramebuffer(MenuOps.Context,
            (uint64_t)(uintptr_t)Framebuffer, SavedSize,
            &Mapping) == Rc95LdrOutOfBounds);
        assert(Mapping == NULL);
        assert(Rc95LdrUefiAcquireFramebuffer(&Video, &FramebufferView)
            == Rc95LdrNotFound);
        assert(MenuOps.SetMode(MenuOps.Context, 0) == Rc95LdrSuccess);
    }
    assert(FramebufferView.Width == 1024);
    assert(FramebufferView.Height == 768);

    /* A nonstandard bitmask must be rejected after SetMode, not silently
       treated as a supported 565 framebuffer. */
    assert(MenuOps.SetMode(MenuOps.Context, 3) == Rc95LdrUnsupported);
    assert(MenuOps.MapFramebuffer(MenuOps.Context,
        (uint64_t)(uintptr_t)Framebuffer, sizeof(Framebuffer),
        &Mapping) == Rc95LdrNotFound);
    assert(Rc95LdrUefiAcquireFramebuffer(&Video, &FramebufferView)
        == Rc95LdrNotFound);
    assert(MenuOps.SetMode(MenuOps.Context, 1) == Rc95LdrSuccess);
    assert(MenuOps.SetMode(MenuOps.Context, 2) == Rc95LdrUnsupported);
    assert(MenuOps.MapFramebuffer(MenuOps.Context,
        (uint64_t)(uintptr_t)Framebuffer, sizeof(Framebuffer),
        &Mapping) == Rc95LdrNotFound);

    /* Exercise the separate BGFX platform adapter's fixed 1024x768 path;
       it must use the same exact-mask RGB565 translation. */
    assert(Rc95LdrUefiInitializeVideo(&Image, &Video, &Platform)
        == Rc95LdrSuccess);
    assert(Platform.SetGraphicsMode(Platform.Context, 1024, 768)
        == Rc95LdrSuccess);
    assert(Platform.AcquireFramebuffer(Platform.Context, &FramebufferView)
        == Rc95LdrSuccess);
    assert(FramebufferView.PixelFormat == RC95LDR_BGFX_PIXEL_RGB565);
    assert(FramebufferView.Width == 1024);
    assert(FramebufferView.Height == 768);

    /* Exercise malformed current-mode records through the fixed-resolution
       BGFX adapter as well as the firmware-menu adapter above. */
    SetModeNullInfo = 1;
    assert(Platform.SetGraphicsMode(Platform.Context, 1024, 768)
        == Rc95LdrCorruptData);
    assert(Platform.AcquireFramebuffer(Platform.Context, &FramebufferView)
        == Rc95LdrNotFound);
    SetModeNullInfo = 0;

    SetModeInfoSizeOverride = sizeof(ModeInfo[0]) - 1u;
    assert(Platform.SetGraphicsMode(Platform.Context, 1024, 768)
        == Rc95LdrCorruptData);
    assert(Platform.AcquireFramebuffer(Platform.Context, &FramebufferView)
        == Rc95LdrNotFound);
    SetModeInfoSizeOverride = 0;

    SetModeZeroFramebufferBase = 1;
    assert(Platform.SetGraphicsMode(Platform.Context, 1024, 768)
        == Rc95LdrCorruptData);
    assert(Platform.AcquireFramebuffer(Platform.Context, &FramebufferView)
        == Rc95LdrNotFound);
    SetModeZeroFramebufferBase = 0;

    SetModeZeroFramebufferSize = 1;
    assert(Platform.SetGraphicsMode(Platform.Context, 1024, 768)
        == Rc95LdrCorruptData);
    assert(Platform.AcquireFramebuffer(Platform.Context, &FramebufferView)
        == Rc95LdrNotFound);
    SetModeZeroFramebufferSize = 0;

    {
        UINT32 SavedPitch = ModeInfo[0].PixelsPerScanLine;
        ModeInfo[0].PixelsPerScanLine = 1023;
        assert(Platform.SetGraphicsMode(Platform.Context, 1024, 768)
            == Rc95LdrUnsupported);
        assert(Platform.AcquireFramebuffer(Platform.Context, &FramebufferView)
            == Rc95LdrNotFound);
        ModeInfo[0].PixelsPerScanLine = SavedPitch;
    }
    assert(Platform.SetGraphicsMode(Platform.Context, 1024, 768)
        == Rc95LdrSuccess);

    /* The fixed BGFX adapter must also discard its descriptor when a
       successful GOP transition reports a pixel layout it cannot render. */
    {
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION Saved = ModeInfo[0];
        ModeInfo[0].PixelFormat = PixelBltOnly;
        assert(Platform.SetGraphicsMode(Platform.Context, 1024, 768)
            == Rc95LdrUnsupported);
        assert(Platform.AcquireFramebuffer(Platform.Context, &FramebufferView)
            == Rc95LdrNotFound);
        ModeInfo[0] = Saved;
    }

    /* A malformed post-transition framebuffer must not remain acquirable. */
    FramebufferSizeOverride = 1;
    assert(Platform.SetGraphicsMode(Platform.Context, 1024, 768)
        == Rc95LdrOutOfBounds);
    assert(Platform.AcquireFramebuffer(Platform.Context, &FramebufferView)
        == Rc95LdrNotFound);
    FramebufferSizeOverride = 0;

    /* Reject a mode whose scanline pitch cannot contain its visible pixels. */
    ModeInfo[1].PixelsPerScanLine = 799;
    assert(MenuOps.EnumerateModes(MenuOps.Context, Modes, 4,
        &ModeCount) == Rc95LdrSuccess);
    assert(ModeCount == 1);
    assert(Modes[0].ModeId == 0);
    assert(MenuOps.SetMode(MenuOps.Context, 1) == Rc95LdrUnsupported);
    assert(MenuOps.MapFramebuffer(MenuOps.Context,
        (uint64_t)(uintptr_t)Framebuffer, sizeof(Framebuffer),
        &Mapping) == Rc95LdrNotFound);

    /* A firmware-reported framebuffer smaller than the visible surface must
       not become an acquirable BGFX framebuffer. */
    FramebufferSizeOverride = 1;
    assert(MenuOps.SetMode(MenuOps.Context, 0) == Rc95LdrOutOfBounds);
    assert(Rc95LdrUefiAcquireFramebuffer(&Video, &FramebufferView)
        == Rc95LdrNotFound);
    assert(MenuOps.MapFramebuffer(MenuOps.Context,
        (uint64_t)(uintptr_t)Framebuffer, 1, &Mapping) == Rc95LdrNotFound);
    FramebufferSizeOverride = 0;
    return 0;
}
