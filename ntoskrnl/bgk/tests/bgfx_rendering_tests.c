#include "ntoskrnl.h"
#include "../include/bgfx_framebuffer.h"
#include "../include/bgfx_bitmap.h"
#include <assert.h>
#include <stdio.h>

static UCHAR FakeMapping[0x6000];
static ULONG FakeMapCalls, FakeUnmapCalls, FakeMappedSize;
static PHYSICAL_ADDRESS FakeMappedAddress;
static PVOID FakeUnmappedBase;

NTSTATUS
FindBootDisplay(
    PPHYSICAL_ADDRESS VideoRamAddress,
    PULONG VideoRamSize,
    PCM_FRAMEBUF_DEVICE_DATA VideoConfigData,
    PCM_MONITOR_DEVICE_DATA MonitorConfigData,
    PINTERFACE_TYPE Interface,
    PULONG BusNumber)
{
    (void)MonitorConfigData;
    VideoRamAddress->QuadPart=0x1003;
    *VideoRamSize=0x4000;
    memset(VideoConfigData,0,sizeof(*VideoConfigData));
    VideoConfigData->FrameBufferOffset=16;
    VideoConfigData->ScreenWidth=2;
    VideoConfigData->ScreenHeight=2;
    VideoConfigData->PixelsPerScanLine=2;
    VideoConfigData->BitsPerPixel=32;
    VideoConfigData->PixelMasks.RedMask=0x00FF0000u;
    VideoConfigData->PixelMasks.GreenMask=0x0000FF00u;
    VideoConfigData->PixelMasks.BlueMask=0x000000FFu;
    VideoConfigData->PixelMasks.ReservedMask=0xFF000000u;
    *Interface=0;
    *BusNumber=0;
    return STATUS_SUCCESS;
}
BOOLEAN
BootTranslateBusAddress(
    INTERFACE_TYPE InterfaceType,
    ULONG BusNumber,
    PHYSICAL_ADDRESS BusAddress,
    PULONG AddressSpace,
    PPHYSICAL_ADDRESS TranslatedAddress)
{
    (void)InterfaceType; (void)BusNumber;
    *AddressSpace=0;
    *TranslatedAddress=BusAddress;
    return TRUE;
}
PVOID
MmMapIoSpace(PHYSICAL_ADDRESS Address,SIZE_T NumberOfBytes,int CacheType)
{
    (void)CacheType;
    ++FakeMapCalls;
    FakeMappedAddress=Address;
    FakeMappedSize=(ULONG)NumberOfBytes;
    return FakeMapping;
}
VOID
MmUnmapIoSpace(PVOID BaseAddress,SIZE_T NumberOfBytes)
{
    ++FakeUnmapCalls;
    FakeUnmappedBase=BaseAddress;
    assert(NumberOfBytes==FakeMappedSize);
}
static void TestSurfaceDrawingAndBounds(void)
{
    CM_FRAMEBUF_DEVICE_DATA config;
    BGFX_FRAMEBUFFER fb;
    BGFX_BITMAP bitmap;
    ULONG pixels[12];
    ULONG source[2]={0x00112233u,0x00445566u};
    memset(&config,0,sizeof(config));
    config.ScreenWidth=4; config.ScreenHeight=3;
    config.PixelsPerScanLine=4; config.BitsPerPixel=32;
    config.PixelMasks.RedMask=0x00FF0000u;
    config.PixelMasks.GreenMask=0x0000FF00u;
    config.PixelMasks.BlueMask=0x000000FFu;
    config.PixelMasks.ReservedMask=0xFF000000u;
    assert(BgfxValidateFramebuffer(&config,sizeof(pixels),&fb)==STATUS_SUCCESS);
    memset(pixels,0,sizeof(pixels));
    assert(BgfxSetFramebuffer(pixels,&fb)==STATUS_SUCCESS);
    assert(BgfxGetFramebufferWidth()==4 && BgfxGetFramebufferHeight()==3);
    assert(BgfxFillRectangle(1,1,2,1,0x112233u));
    assert(pixels[5]==0xFF112233u && pixels[6]==0xFF112233u);
    assert(!BgfxFillRectangle(3,2,2,1,0xFFFFFFu));
    assert(!BgfxFillRectangle(0,0,0,1,0xFFFFFFu));

    bitmap.Width=2; bitmap.Height=1; bitmap.Pixels=source;
    assert(BgfxDrawBitmapCentered(&bitmap)==STATUS_SUCCESS);
    assert(pixels[5]==0xFF112233u && pixels[6]==0xFF445566u);
    assert(BgfxDrawBitmapAt(&bitmap,0,0)==STATUS_SUCCESS);
    assert(pixels[0]==0xFF112233u && pixels[1]==0xFF445566u);
    assert(BgfxBlitBitmap(0,2,2,1,source));
    assert(pixels[8]==0xFF112233u && pixels[9]==0xFF445566u);
    assert(!BgfxBlitBitmap(3,2,2,1,source));
    assert(!BgfxBlitBitmap(0,0,2,1,NULL));

    BgfxResetFramebuffer();
    assert(BgfxGetFramebufferWidth()==0 && BgfxGetFramebufferHeight()==0);
}
static void TestMappedFramebufferOwnership(void)
{
    FakeMapCalls=FakeUnmapCalls=FakeMappedSize=0;
    FakeMappedAddress.QuadPart=0;
    FakeUnmappedBase=NULL;
    memset(FakeMapping,0,sizeof(FakeMapping));
    assert(BgfxInitializeFramebuffer()==STATUS_SUCCESS);
    assert(FakeMapCalls==1 && FakeUnmapCalls==0);
    assert(FakeMappedAddress.QuadPart==0x1000);
    assert(FakeMappedSize==0x5000);
    assert(BgfxGetFramebufferWidth()==2 && BgfxGetFramebufferHeight()==2);
    assert(BgfxFillRectangle(0,0,1,1,0xABCDEFu));
    {
        ULONG pixel;
        memcpy(&pixel,FakeMapping+3+16,sizeof(pixel));
        assert(pixel==0xFFABCDEFu);
    }

    {
        BGFX_FRAMEBUFFER invalid;
        memset(&invalid,0,sizeof(invalid));
        assert(BgfxSetFramebuffer(FakeMapping,&invalid)==STATUS_INVALID_PARAMETER);
    }
    /* Invalid replacement tears down the old mapping; a second reset is safe. */
    assert(FakeUnmapCalls==1 && FakeUnmappedBase==FakeMapping);
    assert(BgfxGetFramebufferWidth()==0 && BgfxGetFramebufferHeight()==0);
    BgfxResetFramebuffer();
    assert(FakeUnmapCalls==1);
}
int main(void)
{
    TestSurfaceDrawingAndBounds();
    TestMappedFramebufferOwnership();
    puts("BGFX rendering and framebuffer ownership tests passed");
    return 0;
}
