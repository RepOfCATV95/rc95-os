#include "ntoskrnl.h"
#include "../include/bgfx_bitmap.h"
#include <assert.h>
#include <stdio.h>

static void W16(UCHAR *p, USHORT v) { p[0]=(UCHAR)v; p[1]=(UCHAR)(v>>8); }
static void W32(UCHAR *p, ULONG v)
{
    p[0]=(UCHAR)v; p[1]=(UCHAR)(v>>8); p[2]=(UCHAR)(v>>16); p[3]=(UCHAR)(v>>24);
}
static void MakeBmp(UCHAR *b, ULONG width, LONG height, USHORT bpp,
                    ULONG compression, ULONG pixelOffset, ULONG fileSize)
{
    memset(b, 0, fileSize);
    W16(b, 0x4D42); W32(b+2, fileSize); W32(b+10, pixelOffset);
    W32(b+14, 40); W32(b+18, width); W32(b+22, (ULONG)height);
    W16(b+26, 1); W16(b+28, bpp); W32(b+30, compression);
    W32(b+34, fileSize > pixelOffset ? fileSize-pixelOffset : 0);
}
static void Test24BitBottomUpAndPadding(void)
{
    UCHAR bmp[70];
    BGFX_BITMAP image;
    /* Two 24-bit pixels per row: each 6-byte row has 2 bytes of padding.
       BMP rows are stored bottom-up. */
    MakeBmp(bmp, 2, 2, 24, 0, 54, sizeof(bmp));
    bmp[54]=0x01; bmp[55]=0x02; bmp[56]=0x03;
    bmp[57]=0x04; bmp[58]=0x05; bmp[59]=0x06;
    bmp[62]=0x11; bmp[63]=0x12; bmp[64]=0x13;
    bmp[65]=0x14; bmp[66]=0x15; bmp[67]=0x16;
    assert(BgfxDecodeBmp(bmp,sizeof(bmp),&image)==STATUS_SUCCESS);
    assert(image.Width==2 && image.Height==2);
    assert(image.Pixels[0]==0x131211u && image.Pixels[1]==0x161514u);
    assert(image.Pixels[2]==0x030201u && image.Pixels[3]==0x060504u);
    BgfxFreeBitmap(&image);
    assert(!image.Pixels && image.Width==0 && image.Height==0);
}
static void Test32BitTopDown(void)
{
    UCHAR bmp[62];
    BGFX_BITMAP image;
    MakeBmp(bmp,1,-2,32,0,54,sizeof(bmp));
    bmp[54]=0x33; bmp[55]=0x22; bmp[56]=0x11; bmp[57]=0xFF;
    bmp[58]=0x66; bmp[59]=0x55; bmp[60]=0x44; bmp[61]=0x00;
    assert(BgfxDecodeBmp(bmp,sizeof(bmp),&image)==STATUS_SUCCESS);
    assert(image.Pixels[0]==0x112233u && image.Pixels[1]==0x445566u);
    BgfxFreeBitmap(&image);
}
static void TestRejectsMalformedInputs(void)
{
    UCHAR bmp[80];
    BGFX_BITMAP image;
    memset(&image,0xA5,sizeof(image));
    assert(BgfxDecodeBmp(NULL,0,&image)==STATUS_INVALID_IMAGE_FORMAT);
    assert(!image.Pixels && !image.Width && !image.Height);
    MakeBmp(bmp,2,2,24,0,54,sizeof(bmp));
    W32(bmp+10,53); /* pixel data overlaps the DIB header */
    assert(BgfxDecodeBmp(bmp,sizeof(bmp),&image)==STATUS_INVALID_IMAGE_FORMAT);
    MakeBmp(bmp,2,2,24,1,54,sizeof(bmp)); /* RLE/compressed */
    assert(BgfxDecodeBmp(bmp,sizeof(bmp),&image)==STATUS_INVALID_IMAGE_FORMAT);
    MakeBmp(bmp,2,2,16,0,54,sizeof(bmp));
    assert(BgfxDecodeBmp(bmp,sizeof(bmp),&image)==STATUS_NOT_SUPPORTED);
    MakeBmp(bmp,2,2,24,0,54,58); /* truncated scanlines */
    assert(BgfxDecodeBmp(bmp,58,&image)==STATUS_INVALID_IMAGE_FORMAT);
    MakeBmp(bmp,0,2,24,0,54,sizeof(bmp));
    assert(BgfxDecodeBmp(bmp,sizeof(bmp),&image)==STATUS_INVALID_IMAGE_FORMAT);
    MakeBmp(bmp,2,INT32_MIN,24,0,54,sizeof(bmp));
    assert(BgfxDecodeBmp(bmp,sizeof(bmp),&image)==STATUS_INVALID_IMAGE_FORMAT);
}
int main(void)
{
    Test24BitBottomUpAndPadding();
    Test32BitTopDown();
    TestRejectsMalformedInputs();
    puts("BGFX BMP decoder tests passed");
    return 0;
}
