#include "rc95ldrbgfx.h"
#include <stdio.h>
#include <string.h>

typedef struct _FAKE_VIDEO {
    unsigned GraphicsCalls, TextCalls;
    uint32_t Width, Height, TextColumns, TextRows;
    RC95LDR_BGFX_FRAMEBUFFER Framebuffer;
} FAKE_VIDEO;

static unsigned char Pixels[1024u * 768u * 4u];

static RC95LDR_STATUS SetGraphics(void *Context,uint32_t Width,uint32_t Height)
{
    FAKE_VIDEO *V=(FAKE_VIDEO*)Context;
    if(Width!=1024u||Height!=768u)return Rc95LdrInvalidParameter;
    V->GraphicsCalls++;V->Width=Width;V->Height=Height;
    V->Framebuffer.StructureSize=sizeof(V->Framebuffer);
    V->Framebuffer.Version=RC95LDR_BGFX_VERSION;
    V->Framebuffer.Base=Pixels;
    V->Framebuffer.Size=sizeof(Pixels);
    V->Framebuffer.Width=Width;
    V->Framebuffer.Height=Height;
    V->Framebuffer.PixelsPerScanLine=Width;
    V->Framebuffer.PixelFormat=RC95LDR_BGFX_PIXEL_RGBX8888;
    return Rc95LdrSuccess;
}
static RC95LDR_STATUS SetText(void *Context,uint32_t Columns,uint32_t Rows)
{
    FAKE_VIDEO *V=(FAKE_VIDEO*)Context;
    V->TextCalls++;V->TextColumns=Columns;V->TextRows=Rows;
    return Rc95LdrSuccess;
}
static RC95LDR_STATUS Acquire(void *Context,RC95LDR_BGFX_FRAMEBUFFER *Framebuffer)
{
    FAKE_VIDEO *V=(FAKE_VIDEO*)Context;
    *Framebuffer=V->Framebuffer;
    return Rc95LdrSuccess;
}
int main(void)
{
    FAKE_VIDEO Fake;
    RC95LDR_BGFX_PLATFORM Platform;
    RC95LDR_BGFX_CONFIG Config;
    RC95LDR_BGFX_FRAMEBUFFER Framebuffer;
    memset(&Fake,0,sizeof(Fake));memset(&Platform,0,sizeof(Platform));
    memset(&Config,0,sizeof(Config));
    Platform.StructureSize=sizeof(Platform);
    Platform.Version=RC95LDR_BGFX_VERSION;
    Platform.Context=&Fake;
    Platform.AcquireFramebuffer=Acquire;
    Platform.SetGraphicsMode=SetGraphics;
    Platform.SetTextMode=SetText;
    Config.StructureSize=sizeof(Config);
    Config.Version=RC95LDR_BGFX_VERSION;
    Config.GraphicsModeDisabled=1;
    if(Rc95LdrBgfxInitialize(&Platform,&Config,
       RC95LDR_BGFX_FIRMWARE_BIOS,&Framebuffer)!=Rc95LdrUnsupported ||
       Fake.TextCalls!=1 || Fake.TextColumns!=80 || Fake.TextRows!=50 ||
       Fake.GraphicsCalls!=0)return 1;
    /* BCD graphicsmodedisabled also disables GOP on UEFI. */
    Platform.Firmware=RC95LDR_BGFX_FIRMWARE_UEFI;
    if(Rc95LdrBgfxInitialize(&Platform,&Config,
       RC95LDR_BGFX_FIRMWARE_UEFI,&Framebuffer)!=Rc95LdrUnsupported ||
       Fake.GraphicsCalls!=0)return 2;
    Config.GraphicsModeDisabled=0;
    if(Rc95LdrBgfxInitialize(&Platform,&Config,
       RC95LDR_BGFX_FIRMWARE_UEFI,&Framebuffer)!=Rc95LdrSuccess ||
       Fake.GraphicsCalls!=1 || Fake.Width!=1024 || Fake.Height!=768 ||
       Framebuffer.Width!=1024 || Framebuffer.Height!=768)return 3;
    if(Rc95LdrBgfxShowError(&Framebuffer,"LiberNT is unsupported",
       "This operating system is not supported.")!=Rc95LdrSuccess ||
       Pixels[0]!=0xA0 || Pixels[1]!=0x00 || Pixels[2]!=0x00)return 4;

    /* Verify RGBX/BGRX byte order and each RGB565 color channel. */
    Framebuffer.PixelFormat=RC95LDR_BGFX_PIXEL_RGBX8888;
    if(Rc95LdrBgfxFill(&Framebuffer,0x123456u)!=Rc95LdrSuccess ||
       Pixels[0]!=0x12 || Pixels[1]!=0x34 || Pixels[2]!=0x56 ||
       Pixels[3]!=0x00)return 5;
    Framebuffer.PixelFormat=RC95LDR_BGFX_PIXEL_BGRX8888;
    if(Rc95LdrBgfxFill(&Framebuffer,0x123456u)!=Rc95LdrSuccess ||
       Pixels[0]!=0x56 || Pixels[1]!=0x34 || Pixels[2]!=0x12 ||
       Pixels[3]!=0x00)return 6;
    Framebuffer.PixelFormat=RC95LDR_BGFX_PIXEL_RGB565;
    if(Rc95LdrBgfxFill(&Framebuffer,0xFF0000u)!=Rc95LdrSuccess ||
       Pixels[0]!=0x00 || Pixels[1]!=0xF8)return 7;
    if(Rc95LdrBgfxFill(&Framebuffer,0x00FF00u)!=Rc95LdrSuccess ||
       Pixels[0]!=0xE0 || Pixels[1]!=0x07)return 8;
    if(Rc95LdrBgfxFill(&Framebuffer,0x0000FFu)!=Rc95LdrSuccess ||
       Pixels[0]!=0x1F || Pixels[1]!=0x00)return 9;
    if(Rc95LdrBgfxFill(&Framebuffer,0xFFFFFFu)!=Rc95LdrSuccess ||
       Pixels[0]!=0xFF || Pixels[1]!=0xFF)return 10;

    /* Exercise drawing on every supported pixel format with padded
       scanlines. Rectangle and glyph writes must preserve row padding. */
    {
        unsigned char Surfaces[3][5u*8u*4u];
        const uint32_t Formats[3]={
            RC95LDR_BGFX_PIXEL_RGBX8888,
            RC95LDR_BGFX_PIXEL_BGRX8888,
            RC95LDR_BGFX_PIXEL_RGB565
        };
        unsigned FormatIndex;
        for(FormatIndex=0;FormatIndex<3u;FormatIndex++){
            unsigned BytesPerPixel=FormatIndex==2u?2u:4u;
            unsigned Row;
            unsigned char *Surface=Surfaces[FormatIndex];
            uint32_t ExpectedRectangle=Formats[FormatIndex]==
                RC95LDR_BGFX_PIXEL_RGBX8888?0xABu:
                Formats[FormatIndex]==RC95LDR_BGFX_PIXEL_BGRX8888?0xEFu:0x7Du;
            memset(Surface,0xCC,sizeof(Surfaces[FormatIndex]));
            Framebuffer.Base=Surface;
            Framebuffer.Size=5u*8u*BytesPerPixel;
            Framebuffer.Width=3u;
            Framebuffer.Height=8u;
            Framebuffer.PixelsPerScanLine=5u;
            Framebuffer.PixelFormat=Formats[FormatIndex];
            if(Rc95LdrBgfxFill(&Framebuffer,0x123456u)!=Rc95LdrSuccess)
                return 29;
            if(Rc95LdrBgfxRectangle(&Framebuffer,1u,1u,1u,1u,
                0xABCDEFu)!=Rc95LdrSuccess)
                return 30;
            if(Surface[(5u+1u)*BytesPerPixel]!=ExpectedRectangle)
                return 31;
            if(Rc95LdrBgfxText(&Framebuffer,0u,7u,"A",0xFFFFFFu,
                0x000000u,1u)!=Rc95LdrSuccess)
                return 32;
            /* The top of A has an on-pixel at x=1; row 7 is the
               final visible row, so the rest of the glyph is clipped. */
            if(Surface[(7u*5u+1u)*BytesPerPixel]!=
                (FormatIndex==2u?0xFFu:0xFFu))
                return 33;
            for(Row=0;Row<8u;Row++){
                unsigned Padding;
                for(Padding=3u*BytesPerPixel;Padding<5u*BytesPerPixel;
                    Padding++)
                    if(Surface[Row*5u*BytesPerPixel+Padding]!=0xCCu)
                        return 34;
            }
        }
    }

    /* Text beginning near the lower/right edges must clip safely. */
    {
        unsigned char Small[20u*8u*4u];
        memset(Small,0xCC,sizeof(Small));
        Framebuffer.Base=Small;Framebuffer.Size=sizeof(Small);
        Framebuffer.Width=16;Framebuffer.Height=8;
        Framebuffer.PixelsPerScanLine=20; /* Include four padding pixels. */
        Framebuffer.PixelFormat=RC95LDR_BGFX_PIXEL_RGBX8888;
        if(Rc95LdrBgfxFill(&Framebuffer,0x123456u)!=Rc95LdrSuccess)
            return 11;
        /* Visible pixels use pitch; row padding must remain untouched. */
        if(Small[0]!=0x12 || Small[1]!=0x34 || Small[2]!=0x56 ||
           Small[16u*4u]!=0xCC || Small[20u*4u]!=0x12)
            return 12;
        if(Rc95LdrBgfxText(&Framebuffer,14,6,"A",0xFFFFFFu,0,1)!=Rc95LdrSuccess)
            return 13;
        if(Rc95LdrBgfxText(&Framebuffer,16,0,"A",0xFFFFFFu,0,1)!=Rc95LdrSuccess)
            return 14;
        /* Rectangles clip by subtraction, including dimensions that
           would overflow if x + width or y + height were evaluated. */
        Framebuffer.Size=sizeof(Small);
        if(Rc95LdrBgfxFill(&Framebuffer,0u)!=Rc95LdrSuccess)
            return 23;
        if(Rc95LdrBgfxRectangle(&Framebuffer,14u,6u,UINT32_MAX,
            UINT32_MAX,0xABCDEFu)!=Rc95LdrSuccess)
            return 24;
        if(Small[((size_t)6u*20u+14u)*4u]!=0xABu ||
           Small[((size_t)7u*20u+15u)*4u]!=0xABu ||
           Small[((size_t)5u*20u+14u)*4u]!=0x00u ||
           Small[((size_t)6u*20u+16u)*4u]!=0x00u)
            return 25;
        /* Empty rectangles at the right/bottom edge are valid no-ops. */
        if(Rc95LdrBgfxRectangle(&Framebuffer,16u,8u,0u,0u,
            0xFFFFFFu)!=Rc95LdrSuccess ||
           Rc95LdrBgfxRectangle(&Framebuffer,4u,4u,0u,3u,
            0xFFFFFFu)!=Rc95LdrSuccess)
            return 26;
        if(Rc95LdrBgfxRectangle(&Framebuffer,17u,0u,0u,0u,
            0xFFFFFFu)!=Rc95LdrOutOfBounds ||
           Rc95LdrBgfxRectangle(&Framebuffer,0u,9u,0u,0u,
            0xFFFFFFu)!=Rc95LdrOutOfBounds)
            return 27;
        /* Extreme text origins must clip without wrapping into the image. */
        if(Rc95LdrBgfxText(&Framebuffer,UINT32_MAX,0u,"A",
            0xFFFFFFu,0u,1u)!=Rc95LdrSuccess ||
           Rc95LdrBgfxText(&Framebuffer,0u,UINT32_MAX,"A",
            0xFFFFFFu,0u,1u)!=Rc95LdrSuccess)
            return 28;
        Framebuffer.Size--;
        if(Rc95LdrBgfxValidateFramebuffer(&Framebuffer)!=Rc95LdrOutOfBounds)
            return 15;
    }
    /* Multiplication of pitch * height * bytes-per-pixel must be
       checked before it is used for the framebuffer extent. */
    {
        Framebuffer.Base=Pixels;
        Framebuffer.Size=UINT64_MAX;
        Framebuffer.Width=1u;
        Framebuffer.Height=UINT32_MAX;
        Framebuffer.PixelsPerScanLine=UINT32_MAX;
        Framebuffer.PixelFormat=RC95LDR_BGFX_PIXEL_RGBX8888;
        if(Rc95LdrBgfxValidateFramebuffer(&Framebuffer)!=Rc95LdrIntegerOverflow)
            return 21;
        /* A large but representable extent must still be bounded by Size. */
        Framebuffer.Height=2u;
        Framebuffer.PixelsPerScanLine=UINT32_MAX;
        Framebuffer.Size=UINT64_MAX;
        if(Rc95LdrBgfxValidateFramebuffer(&Framebuffer)!=Rc95LdrOutOfBounds)
            return 22;
    }
    /* A framebuffer extent must not wrap the native address space. This
       descriptor is never dereferenced; validation alone must reject it. */
    {
        Framebuffer.Base=(void *)(uintptr_t)(~(uintptr_t)0-7u);
        Framebuffer.Size=64u;
        Framebuffer.Width=2u;Framebuffer.Height=2u;
        Framebuffer.PixelsPerScanLine=2u;
        Framebuffer.PixelFormat=RC95LDR_BGFX_PIXEL_RGBX8888;
        if(Rc95LdrBgfxValidateFramebuffer(&Framebuffer)!=Rc95LdrOutOfBounds)
            return 20;
    }
    /* Menus larger than the visible framebuffer viewport scroll the
       selected row into view without writing outside the mapped surface. */
    {
        static unsigned char Guarded[16u+640u*400u*4u+16u];
        const char *LargeItems[RC95LDR_TEXTUI_MAX_MENU_ITEMS];
        size_t Item;
        memset(Guarded,0xA5,sizeof(Guarded));
        for(Item=0;Item<RC95LDR_TEXTUI_MAX_MENU_ITEMS;Item++)
            LargeItems[Item]="RC95-OS ENTRY";
        Framebuffer.Base=Guarded+16u;
        Framebuffer.Size=640u*400u*4u;
        Framebuffer.Width=640u;
        Framebuffer.Height=400u;
        Framebuffer.PixelsPerScanLine=640u;
        Framebuffer.PixelFormat=RC95LDR_BGFX_PIXEL_RGBX8888;
        if(Rc95LdrBgfxShowBootMenu(&Framebuffer,"Many boot entries",
            LargeItems,RC95LDR_TEXTUI_MAX_MENU_ITEMS,
            RC95LDR_TEXTUI_MAX_MENU_ITEMS-1u)!=Rc95LdrSuccess)
            return 16;
        /* At 640x400 the last boot entry must be visible in the third row;
           no later row may spill below the panel into the black margin. */
        if(Guarded[16u+(270u*640u+80u)*4u]!=0x4Au ||
           Guarded[16u+(390u*640u+100u)*4u]!=0x00u)
            return 19;
        if(Rc95LdrBgfxShowAdvancedOptions(&Framebuffer,LargeItems,6u,5u)
            !=Rc95LdrSuccess)
            return 17;
        for(Item=0;Item<16u;Item++)
            if(Guarded[Item]!=0xA5u ||
               Guarded[sizeof(Guarded)-16u+Item]!=0xA5u)
                return 18;
    }
    Config.PreferredWidth=800;Config.PreferredHeight=600;
    if(Rc95LdrBgfxInitialize(&Platform,&Config,
       RC95LDR_BGFX_FIRMWARE_BIOS,&Framebuffer)!=Rc95LdrInvalidParameter)
       return 10;
    puts("RC95LDR native video initialization tests passed");
    return 0;
}
