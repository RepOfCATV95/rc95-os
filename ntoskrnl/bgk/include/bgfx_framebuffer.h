#pragma once

#include <drivers/bootvid/framebuf.h>

/*
 * BGK framebuffer contract.
 *
 * The caller owns framebuffer discovery and mapping. BGK accepts only an
 * already-mapped virtual address paired with validated boot display metadata;
 * it never assumes a GOP/FreeLoader-specific handoff layout.
 */
typedef struct _BGFX_FRAMEBUFFER
{
    PUCHAR Base;
    ULONG Width;
    ULONG Height;
    ULONG PixelsPerScanLine;
    ULONG BitsPerPixel;
    ULONG BytesPerPixel;
    ULONG Pitch;
    ULONG FrameBufferOffset;
    ULONG FrameBufferSize;
    ULONG RedMask;
    ULONG GreenMask;
    ULONG BlueMask;
    ULONG ReservedMask;
} BGFX_FRAMEBUFFER, *PBGFX_FRAMEBUFFER;

NTSTATUS
NTAPI
BgfxValidateFramebuffer(
    _In_ const CM_FRAMEBUF_DEVICE_DATA *VideoData,
    _In_ ULONG VideoRamSize,
    _Out_ PBGFX_FRAMEBUFFER Validated);

NTSTATUS
NTAPI
BgfxSetFramebuffer(
    _In_ PVOID MappedFrameBuffer,
    _In_ const BGFX_FRAMEBUFFER *Validated);

VOID
NTAPI
BgfxResetFramebuffer(VOID);

BOOLEAN
NTAPI
BgfxFillRectangle(
    _In_ ULONG Left,
    _In_ ULONG Top,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _In_ ULONG RgbColor);

ULONG NTAPI BgfxGetFramebufferWidth(VOID);
ULONG NTAPI BgfxGetFramebufferHeight(VOID);
BOOLEAN NTAPI BgfxBlitBitmap(_In_ ULONG Left, _In_ ULONG Top,
                              _In_ ULONG Width, _In_ ULONG Height,
                              _In_reads_(Width * Height) const ULONG *Pixels);

NTSTATUS NTAPI BgfxInitializeFramebuffer(VOID);
