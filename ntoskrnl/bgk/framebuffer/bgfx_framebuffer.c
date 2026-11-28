/*
 * RC95-OS BGK framebuffer validation and surface binding.
 *
 * This module validates CM_FRAMEBUF_DEVICE_DATA and owns the ARC framebuffer
 * discovery, bus translation, and mapping needed by the BGK renderer.
 */

#include <ntoskrnl.h>
#include "../include/bgfx_framebuffer.h"

#define NDEBUG
#include <debug.h>

static BGFX_FRAMEBUFFER BgfxFrameBuffer;
static BOOLEAN BgfxFrameBufferReady;
static PVOID BgfxMappedFrameBuffer;
static ULONG BgfxMappedFrameBufferSize;

/* Implemented by the shared ARC boot-display discovery helper. */
NTSTATUS FindBootDisplay(PPHYSICAL_ADDRESS VideoRamAddress, PULONG VideoRamSize, PCM_FRAMEBUF_DEVICE_DATA VideoConfigData, PCM_MONITOR_DEVICE_DATA MonitorConfigData, PINTERFACE_TYPE Interface, PULONG BusNumber);
BOOLEAN NTAPI BootTranslateBusAddress(INTERFACE_TYPE InterfaceType, ULONG BusNumber, PHYSICAL_ADDRESS BusAddress, PULONG AddressSpace, PPHYSICAL_ADDRESS TranslatedAddress);

static
ULONG
BgfxMaskShift(
    _In_ ULONG Mask)
{
    ULONG Shift = 0;
    while (Mask && !(Mask & 1))
    {
        Mask >>= 1;
        ++Shift;
    }
    return Shift;
}

NTSTATUS
NTAPI
BgfxSetFramebuffer(
    _In_ PVOID MappedFrameBuffer,
    _In_ const BGFX_FRAMEBUFFER *Validated)
{
    if (MappedFrameBuffer == NULL || Validated == NULL ||
        Validated->Width == 0 || Validated->Height == 0 ||
        Validated->BitsPerPixel != 32 ||
        Validated->BytesPerPixel != 4 ||
        (ULONGLONG)Validated->Pitch < (ULONGLONG)Validated->Width * 4 ||
        Validated->FrameBufferSize == 0 ||
        (ULONGLONG)Validated->Pitch * Validated->Height >
            Validated->FrameBufferSize)
    {
        BgfxResetFramebuffer();
        return STATUS_INVALID_PARAMETER;
    }

    BgfxFrameBuffer = *Validated;
    BgfxFrameBuffer.Base =
        (PUCHAR)MappedFrameBuffer + Validated->FrameBufferOffset;
    BgfxFrameBufferReady = TRUE;
    return STATUS_SUCCESS;
}

VOID
NTAPI
BgfxResetFramebuffer(VOID)
{
    RtlZeroMemory(&BgfxFrameBuffer, sizeof(BgfxFrameBuffer));
    BgfxFrameBufferReady = FALSE;
    if (BgfxMappedFrameBuffer)
    {
        MmUnmapIoSpace(BgfxMappedFrameBuffer, BgfxMappedFrameBufferSize);
        BgfxMappedFrameBuffer = NULL;
        BgfxMappedFrameBufferSize = 0;
    }
}

BOOLEAN
NTAPI
BgfxFillRectangle(
    _In_ ULONG Left,
    _In_ ULONG Top,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _In_ ULONG RgbColor)
{
    ULONG Right, Bottom, X, Y;
    ULONG Pixel;

    if (!BgfxFrameBufferReady || Width == 0 || Height == 0)
        return FALSE;

    if (Left >= BgfxFrameBuffer.Width || Top >= BgfxFrameBuffer.Height ||
        Width > BgfxFrameBuffer.Width - Left ||
        Height > BgfxFrameBuffer.Height - Top)
    {
        return FALSE;
    }

    Right = Left + Width;
    Bottom = Top + Height;
    Pixel = (((RgbColor >> 16) & 0xFF) << BgfxMaskShift(BgfxFrameBuffer.RedMask)) |
            (((RgbColor >> 8) & 0xFF) << BgfxMaskShift(BgfxFrameBuffer.GreenMask)) |
            ((RgbColor & 0xFF) << BgfxMaskShift(BgfxFrameBuffer.BlueMask));

    for (Y = Top; Y < Bottom; ++Y)
    {
        PULONG ScanLine = (PULONG)(BgfxFrameBuffer.Base +
                                   (SIZE_T)Y * BgfxFrameBuffer.Pitch);
        for (X = Left; X < Right; ++X)
            ScanLine[X] = Pixel | BgfxFrameBuffer.ReservedMask;
    }

    return TRUE;
}


ULONG
NTAPI
BgfxGetFramebufferWidth(VOID)
{
    return BgfxFrameBufferReady ? BgfxFrameBuffer.Width : 0;
}

ULONG
NTAPI
BgfxGetFramebufferHeight(VOID)
{
    return BgfxFrameBufferReady ? BgfxFrameBuffer.Height : 0;
}

BOOLEAN
NTAPI
BgfxBlitBitmap(
    _In_ ULONG Left,
    _In_ ULONG Top,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _In_reads_(Width * Height) const ULONG *Pixels)
{
    ULONG X, Y;
    if (!BgfxFrameBufferReady || !Pixels || !Width || !Height ||
        Left >= BgfxFrameBuffer.Width || Top >= BgfxFrameBuffer.Height ||
        Width > BgfxFrameBuffer.Width - Left ||
        Height > BgfxFrameBuffer.Height - Top)
        return FALSE;

    for (Y = 0; Y < Height; ++Y)
    {
        PULONG Destination = (PULONG)(BgfxFrameBuffer.Base +
            (SIZE_T)(Top + Y) * BgfxFrameBuffer.Pitch) + Left;
        const ULONG *Source = Pixels + (SIZE_T)Y * Width;
        for (X = 0; X < Width; ++X)
        {
            ULONG Rgb = Source[X];
            Destination[X] =
                ((((Rgb >> 16) & 0xFF) << BgfxMaskShift(BgfxFrameBuffer.RedMask)) |
                 (((Rgb >> 8) & 0xFF) << BgfxMaskShift(BgfxFrameBuffer.GreenMask)) |
                 ((Rgb & 0xFF) << BgfxMaskShift(BgfxFrameBuffer.BlueMask)) |
                 BgfxFrameBuffer.ReservedMask);
        }
    }
    return TRUE;
}

/*
 * Discover the same ARC boot display data consumed by the generic framebuffer
 * BOOTVID driver, validate it, translate its bus address, and map video RAM.
 */
NTSTATUS
NTAPI
BgfxInitializeFramebuffer(VOID)
{
    PHYSICAL_ADDRESS VideoRamAddress, TranslatedAddress, AlignedAddress;
    CM_FRAMEBUF_DEVICE_DATA VideoData;
    INTERFACE_TYPE InterfaceType;
    ULONG VideoRamSize, BusNumber, AddressSpace, MapOffset, MapSize;
    ULONGLONG RequiredMapSize;
    PVOID Mapping;
    BGFX_FRAMEBUFFER Validated;
    NTSTATUS Status;

    BgfxResetFramebuffer();
    RtlZeroMemory(&VideoData, sizeof(VideoData));
    Status = FindBootDisplay(&VideoRamAddress,
                             &VideoRamSize,
                             &VideoData,
                             NULL,
                             &InterfaceType,
                             &BusNumber);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = BgfxValidateFramebuffer(&VideoData, VideoRamSize, &Validated);
    if (!NT_SUCCESS(Status))
        return Status;

    AddressSpace = 0; /* MMIO */
    if (!BootTranslateBusAddress(InterfaceType,
                                 BusNumber,
                                 VideoRamAddress,
                                 &AddressSpace,
                                 &TranslatedAddress) ||
        AddressSpace != 0)
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    AlignedAddress = TranslatedAddress;
    AlignedAddress.QuadPart &= ~((ULONGLONG)PAGE_SIZE - 1);
    MapOffset = (ULONG)(TranslatedAddress.QuadPart -
                        AlignedAddress.QuadPart);
    RequiredMapSize = (ULONGLONG)VideoRamSize + MapOffset;
    RequiredMapSize = (RequiredMapSize + PAGE_SIZE - 1) &
                      ~((ULONGLONG)PAGE_SIZE - 1);
    if (RequiredMapSize == 0 || RequiredMapSize > MAXULONG)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    MapSize = (ULONG)RequiredMapSize;
    if (AlignedAddress.QuadPart > (~(ULONGLONG)0) - RequiredMapSize)
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    /* Match the generic framebuffer BOOTVID cache policy where possible. */
    Mapping = MmMapIoSpace(AlignedAddress, MapSize, MmWriteCombined);
    if (!Mapping)
        Mapping = MmMapIoSpace(AlignedAddress, MapSize, MmNonCached);
    if (!Mapping)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = BgfxSetFramebuffer((PUCHAR)Mapping + MapOffset, &Validated);
    if (!NT_SUCCESS(Status))
    {
        MmUnmapIoSpace(Mapping, MapSize);
        return Status;
    }

    BgfxMappedFrameBuffer = Mapping;
    BgfxMappedFrameBufferSize = MapSize;
    return STATUS_SUCCESS;
}

/* Discover and map the same ARC framebuffer data used by generic BOOTVID. */
