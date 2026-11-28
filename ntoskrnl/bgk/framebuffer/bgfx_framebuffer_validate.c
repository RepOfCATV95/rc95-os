/*
 * Pure BGK framebuffer descriptor validation.
 *
 * Kept separate from MMIO discovery/mapping so the exact production checks
 * can be exercised by the host regression harness.
 */
#include <ntoskrnl.h>
#include "../include/bgfx_framebuffer.h"

static
BOOLEAN
BgfxIsEightBitMask(
    _In_ ULONG Mask)
{
    while (Mask && !(Mask & 1))
        Mask >>= 1;
    return Mask == 0xFF;
}

static
BOOLEAN
BgfxMasksAreValid(
    _In_ const CM_FRAMEBUF_DEVICE_DATA *VideoData)
{
    ULONG Combined;

    if (VideoData->BitsPerPixel != 32)
        return FALSE;

    if (VideoData->PixelMasks.RedMask == 0 ||
        VideoData->PixelMasks.GreenMask == 0 ||
        VideoData->PixelMasks.BlueMask == 0)
        return FALSE;

    Combined = VideoData->PixelMasks.RedMask |
               VideoData->PixelMasks.GreenMask |
               VideoData->PixelMasks.BlueMask |
               VideoData->PixelMasks.ReservedMask;

    if ((VideoData->PixelMasks.RedMask & VideoData->PixelMasks.GreenMask) ||
        (VideoData->PixelMasks.RedMask & VideoData->PixelMasks.BlueMask) ||
        (VideoData->PixelMasks.GreenMask & VideoData->PixelMasks.BlueMask) ||
        (VideoData->PixelMasks.ReservedMask &
         (VideoData->PixelMasks.RedMask |
          VideoData->PixelMasks.GreenMask |
          VideoData->PixelMasks.BlueMask)))
        return FALSE;

    /* The initial renderer packs 8-bit RGB channels into 32-bit pixels. */
    return Combined != 0 &&
           BgfxIsEightBitMask(VideoData->PixelMasks.RedMask) &&
           BgfxIsEightBitMask(VideoData->PixelMasks.GreenMask) &&
           BgfxIsEightBitMask(VideoData->PixelMasks.BlueMask);
}

NTSTATUS
NTAPI
BgfxValidateFramebuffer(
    _In_ const CM_FRAMEBUF_DEVICE_DATA *VideoData,
    _In_ ULONG VideoRamSize,
    _Out_ PBGFX_FRAMEBUFFER Validated)
{
    ULONGLONG BytesPerPixel, Pitch, FrameBufferBytes, EndOffset;

    if (Validated == NULL)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(Validated, sizeof(*Validated));

    if (VideoData == NULL || VideoRamSize == 0)
        return STATUS_INVALID_PARAMETER;

    if (VideoData->ScreenWidth == 0 ||
        VideoData->ScreenHeight == 0 ||
        VideoData->PixelsPerScanLine < VideoData->ScreenWidth)
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    if (!BgfxMasksAreValid(VideoData))
        return STATUS_NOT_SUPPORTED;

    BytesPerPixel = VideoData->BitsPerPixel / 8;
    Pitch = (ULONGLONG)VideoData->PixelsPerScanLine * BytesPerPixel;
    if (Pitch == 0 || Pitch > MAXULONG)
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    FrameBufferBytes = Pitch * VideoData->ScreenHeight;
    if (FrameBufferBytes == 0 || FrameBufferBytes > MAXULONG)
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    EndOffset = (ULONGLONG)VideoData->FrameBufferOffset + FrameBufferBytes;
    if (EndOffset > VideoRamSize)
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    Validated->Width = VideoData->ScreenWidth;
    Validated->Height = VideoData->ScreenHeight;
    Validated->PixelsPerScanLine = VideoData->PixelsPerScanLine;
    Validated->BitsPerPixel = VideoData->BitsPerPixel;
    Validated->BytesPerPixel = (ULONG)BytesPerPixel;
    Validated->Pitch = (ULONG)Pitch;
    Validated->FrameBufferOffset = VideoData->FrameBufferOffset;
    Validated->FrameBufferSize = (ULONG)FrameBufferBytes;
    Validated->RedMask = VideoData->PixelMasks.RedMask;
    Validated->GreenMask = VideoData->PixelMasks.GreenMask;
    Validated->BlueMask = VideoData->PixelMasks.BlueMask;
    Validated->ReservedMask = VideoData->PixelMasks.ReservedMask;

    return STATUS_SUCCESS;
}
