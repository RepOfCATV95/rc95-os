#pragma once
#include <ntoskrnl.h>

/* Host-test shape of the framebuffer-specific ARC configuration payload. */
typedef struct _CM_FRAMEBUF_DEVICE_DATA
{
    ULONG FrameBufferOffset;
    ULONG ScreenWidth;
    ULONG ScreenHeight;
    ULONG PixelsPerScanLine;
    ULONG BitsPerPixel;
    struct
    {
        ULONG RedMask;
        ULONG GreenMask;
        ULONG BlueMask;
        ULONG ReservedMask;
    } PixelMasks;
} CM_FRAMEBUF_DEVICE_DATA, *PCM_FRAMEBUF_DEVICE_DATA;
