#pragma once

typedef struct _BGFX_BITMAP
{
    ULONG Width;
    ULONG Height;
    PULONG Pixels; /* top-down 0x00RRGGBB pixels */
} BGFX_BITMAP, *PBGFX_BITMAP;

NTSTATUS NTAPI BgfxDecodeBmp(_In_reads_bytes_(Size) const UCHAR *Data,
                             _In_ ULONG Size,
                             _Out_ PBGFX_BITMAP Bitmap);
VOID NTAPI BgfxFreeBitmap(_Inout_ PBGFX_BITMAP Bitmap);
NTSTATUS NTAPI BgfxLoadBitmapResource(_In_ PCWSTR ResourceName,
                                      _Out_ PBGFX_BITMAP Bitmap);
NTSTATUS NTAPI BgfxDrawBitmapCentered(_In_ const BGFX_BITMAP *Bitmap);
NTSTATUS NTAPI BgfxDrawBitmapAt(_In_ const BGFX_BITMAP *Bitmap,
                                    _In_ ULONG Left, _In_ ULONG Top);
