/*
 * RC95-OS BGK BMP decoder.
 * Supports uncompressed Windows BITMAPINFOHEADER-or-later 24/32-bpp BMPs.
 * Output is normalized to top-down 0x00RRGGBB pixels.
 */
#include <ntoskrnl.h>
#include "../include/bgfx_bitmap.h"

#define NDEBUG
#include <debug.h>

#define BGFX_BMP_SIGNATURE 0x4D42
#define BGFX_BI_RGB 0

static USHORT BgfxReadU16(const UCHAR *P)
{ return (USHORT)(P[0] | ((USHORT)P[1] << 8)); }
static ULONG BgfxReadU32(const UCHAR *P)
{ return (ULONG)P[0] | ((ULONG)P[1] << 8) | ((ULONG)P[2] << 16) | ((ULONG)P[3] << 24); }
static LONG BgfxReadS32(const UCHAR *P)
{ return (LONG)BgfxReadU32(P); }

NTSTATUS NTAPI
BgfxDecodeBmp(const UCHAR *Data, ULONG Size, PBGFX_BITMAP Bitmap)
{
    ULONG PixelOffset, DibSize, Compression, SourceStride;
    ULONG X, Y, Width, Height, BytesPerPixel;
    LONG SignedHeight, SignedWidth;
    ULONGLONG Required, PixelCount;
    PULONG Pixels;

    if (!Bitmap) return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(Bitmap, sizeof(*Bitmap));
    if (!Data || Size < 54 || BgfxReadU16(Data) != BGFX_BMP_SIGNATURE)
        return STATUS_INVALID_IMAGE_FORMAT;

    PixelOffset = BgfxReadU32(Data + 10);
    DibSize = BgfxReadU32(Data + 14);
    SignedWidth = BgfxReadS32(Data + 18);
    SignedHeight = BgfxReadS32(Data + 22);
    Compression = BgfxReadU32(Data + 30);

    if (DibSize < 40 || (ULONGLONG)14 + DibSize > Size ||
        SignedWidth <= 0 || SignedHeight == 0 || SignedHeight == MINLONG ||
        BgfxReadU16(Data + 26) != 1 || Compression != BGFX_BI_RGB)
        return STATUS_INVALID_IMAGE_FORMAT;
    if (BgfxReadU16(Data + 28) != 24 && BgfxReadU16(Data + 28) != 32)
        return STATUS_NOT_SUPPORTED;

    Width = (ULONG)SignedWidth;
    Height = (ULONG)(SignedHeight < 0 ? -SignedHeight : SignedHeight);
    BytesPerPixel = BgfxReadU16(Data + 28) / 8;
    if (PixelOffset < 14 + DibSize || PixelOffset > Size)
        return STATUS_INVALID_IMAGE_FORMAT;

    Required = (ULONGLONG)Width * BytesPerPixel;
    Required = (Required + 3) & ~3ULL;
    if (!Required || Required > MAXULONG)
        return STATUS_INVALID_IMAGE_FORMAT;
    SourceStride = (ULONG)Required;
    Required = (ULONGLONG)SourceStride * Height;
    PixelCount = (ULONGLONG)Width * Height;
    if (Required > Size - PixelOffset || PixelCount > MAXULONG / sizeof(ULONG))
        return STATUS_INVALID_IMAGE_FORMAT;

    Pixels = ExAllocatePoolWithTag(NonPagedPool,
                                   (SIZE_T)PixelCount * sizeof(ULONG), 'mBgR');
    if (!Pixels) return STATUS_INSUFFICIENT_RESOURCES;

    for (Y = 0; Y < Height; ++Y)
    {
        ULONG SourceY = SignedHeight < 0 ? Y : Height - 1 - Y;
        const UCHAR *Row = Data + PixelOffset + (SIZE_T)SourceY * SourceStride;
        for (X = 0; X < Width; ++X)
        {
            const UCHAR *Pixel = Row + (SIZE_T)X * BytesPerPixel;
            Pixels[(SIZE_T)Y * Width + X] =
                ((ULONG)Pixel[2] << 16) | ((ULONG)Pixel[1] << 8) | Pixel[0];
        }
    }
    Bitmap->Width = Width;
    Bitmap->Height = Height;
    Bitmap->Pixels = Pixels;
    return STATUS_SUCCESS;
}

VOID NTAPI
BgfxFreeBitmap(PBGFX_BITMAP Bitmap)
{
    if (!Bitmap) return;
    if (Bitmap->Pixels) ExFreePoolWithTag(Bitmap->Pixels, 'mBgR');
    RtlZeroMemory(Bitmap, sizeof(*Bitmap));
}
