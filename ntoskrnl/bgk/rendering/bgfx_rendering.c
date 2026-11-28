/*
 * RC95-OS BGK bitmap presentation helpers.
 */
#include <ntoskrnl.h>
#include "../include/bgfx_bitmap.h"
#include "../include/bgfx_framebuffer.h"

NTSTATUS NTAPI
BgfxDrawBitmapCentered(const BGFX_BITMAP *Bitmap)
{
    ULONG Left, Top, ScreenWidth, ScreenHeight;
    if (!Bitmap || !Bitmap->Pixels || !Bitmap->Width || !Bitmap->Height)
        return STATUS_INVALID_PARAMETER;
    ScreenWidth = BgfxGetFramebufferWidth();
    ScreenHeight = BgfxGetFramebufferHeight();
    if (!ScreenWidth || !ScreenHeight ||
        Bitmap->Width > ScreenWidth || Bitmap->Height > ScreenHeight)
        return STATUS_BUFFER_TOO_SMALL;
    Left = (ScreenWidth - Bitmap->Width) / 2;
    Top = (ScreenHeight - Bitmap->Height) / 2;
    return BgfxBlitBitmap(Left, Top, Bitmap->Width, Bitmap->Height,
                          Bitmap->Pixels) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

NTSTATUS NTAPI
BgfxDrawBitmapAt(const BGFX_BITMAP *Bitmap, ULONG Left, ULONG Top)
{
    if (!Bitmap || !Bitmap->Pixels || !Bitmap->Width || !Bitmap->Height)
        return STATUS_INVALID_PARAMETER;
    if (Left > BgfxGetFramebufferWidth() ||
        Top > BgfxGetFramebufferHeight() ||
        Bitmap->Width > BgfxGetFramebufferWidth() - Left ||
        Bitmap->Height > BgfxGetFramebufferHeight() - Top)
        return STATUS_BUFFER_TOO_SMALL;
    return BgfxBlitBitmap(Left, Top, Bitmap->Width, Bitmap->Height,
                          Bitmap->Pixels) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}
