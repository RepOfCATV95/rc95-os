#include "ntoskrnl.h"
#include "../include/bgfx_framebuffer.h"
#include <assert.h>
#include <stdio.h>

static void InitValid(CM_FRAMEBUF_DEVICE_DATA *data)
{
    memset(data,0,sizeof(*data));
    data->ScreenWidth=640;
    data->ScreenHeight=480;
    data->PixelsPerScanLine=640;
    data->BitsPerPixel=32;
    data->PixelMasks.RedMask=0x00FF0000u;
    data->PixelMasks.GreenMask=0x0000FF00u;
    data->PixelMasks.BlueMask=0x000000FFu;
    data->PixelMasks.ReservedMask=0xFF000000u;
}
static void TestValidSurfaceAndPadding(void)
{
    CM_FRAMEBUF_DEVICE_DATA data;
    BGFX_FRAMEBUFFER fb;
    InitValid(&data);
    assert(BgfxValidateFramebuffer(&data,640u*480u*4u,&fb)==STATUS_SUCCESS);
    assert(fb.Width==640 && fb.Height==480);
    assert(fb.BytesPerPixel==4 && fb.Pitch==2560);
    assert(fb.FrameBufferSize==640u*480u*4u && fb.FrameBufferOffset==0);

    data.PixelsPerScanLine=672;
    assert(BgfxValidateFramebuffer(&data,672u*480u*4u,&fb)==STATUS_SUCCESS);
    assert(fb.Pitch==2688 && fb.FrameBufferSize==2688u*480u);
}
static void TestUnsupportedPixelFormats(void)
{
    CM_FRAMEBUF_DEVICE_DATA data;
    BGFX_FRAMEBUFFER fb;
    InitValid(&data);
    data.BitsPerPixel=24;
    assert(BgfxValidateFramebuffer(&data,640u*480u*4u,&fb)==STATUS_NOT_SUPPORTED);
    InitValid(&data);
    data.PixelMasks.RedMask=0x0000F800u;
    data.PixelMasks.GreenMask=0x000007E0u;
    data.PixelMasks.BlueMask=0x0000001Fu;
    data.PixelMasks.ReservedMask=0;
    assert(BgfxValidateFramebuffer(&data,640u*480u*4u,&fb)==STATUS_NOT_SUPPORTED);
    InitValid(&data);
    data.PixelMasks.RedMask=0x00FF0000u;
    data.PixelMasks.GreenMask=0x00FF0000u; /* overlapping channels */
    assert(BgfxValidateFramebuffer(&data,640u*480u*4u,&fb)==STATUS_NOT_SUPPORTED);
    InitValid(&data);
    data.PixelMasks.RedMask=0x00F70000u; /* non-contiguous 8-bit mask */
    assert(BgfxValidateFramebuffer(&data,640u*480u*4u,&fb)==STATUS_NOT_SUPPORTED);
    InitValid(&data);
    data.PixelMasks.ReservedMask=0x000000FFu; /* overlaps blue */
    assert(BgfxValidateFramebuffer(&data,640u*480u*4u,&fb)==STATUS_NOT_SUPPORTED);
}
static void TestGeometryAndOverflow(void)
{
    CM_FRAMEBUF_DEVICE_DATA data;
    BGFX_FRAMEBUFFER fb;
    InitValid(&data);
    data.ScreenWidth=0;
    assert(BgfxValidateFramebuffer(&data,1,&fb)==STATUS_DEVICE_CONFIGURATION_ERROR);
    InitValid(&data);
    data.PixelsPerScanLine=639;
    assert(BgfxValidateFramebuffer(&data,640u*480u*4u,&fb)==STATUS_DEVICE_CONFIGURATION_ERROR);
    InitValid(&data);
    data.FrameBufferOffset=4096;
    assert(BgfxValidateFramebuffer(&data,640u*480u*4u,&fb)==STATUS_DEVICE_CONFIGURATION_ERROR);
    InitValid(&data);
    data.ScreenWidth=0xFFFFFFFFu;
    data.ScreenHeight=1;
    data.PixelsPerScanLine=0xFFFFFFFFu;
    assert(BgfxValidateFramebuffer(&data,0xFFFFFFFFu,&fb)==STATUS_DEVICE_CONFIGURATION_ERROR);
    InitValid(&data);
    assert(BgfxValidateFramebuffer(&data,0,&fb)==STATUS_INVALID_PARAMETER);
    assert(BgfxValidateFramebuffer(&data,1,NULL)==STATUS_INVALID_PARAMETER);
}
int main(void)
{
    TestValidSurfaceAndPadding();
    TestUnsupportedPixelFormats();
    TestGeometryAndOverflow();
    puts("BGFX framebuffer validation tests passed");
    return 0;
}
