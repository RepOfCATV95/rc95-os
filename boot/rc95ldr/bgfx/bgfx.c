#include "rc95ldrbgfx.h"
#include <string.h>
#define BG 0x000000u
#define ERROR_BG 0xA00000u
#define PANEL 0x202D43u
#define PANEL2 0x2A3A52u
#define WHITE 0xF4F7FBu
#define MUTED 0xAAB8C8u
#define ACCENT 0x3A83D5u
#define HILITE 0x4A9BE8u
typedef struct { char c; uint8_t r[7]; } GLYPH;
static const GLYPH Font[]={
{' ',{0,0,0,0,0,0,0}},{'!',{4,4,4,4,4,0,4}},{'.',{0,0,0,0,0,6,6}},{',',{0,0,0,0,0,6,4}},{':',{0,6,6,0,6,6,0}},{'-',{0,0,0,31,0,0,0}},{'/',{1,2,2,4,8,8,16}},{'?',{14,17,1,2,4,0,4}},
{'0',{14,17,19,21,25,17,14}},{'1',{4,12,4,4,4,4,14}},{'2',{14,17,1,2,4,8,31}},{'3',{30,1,1,14,1,1,30}},{'4',{2,6,10,18,31,2,2}},{'5',{31,16,16,30,1,1,30}},{'6',{14,16,16,30,17,17,14}},{'7',{31,1,2,4,8,8,8}},{'8',{14,17,17,14,17,17,14}},{'9',{14,17,17,15,1,1,14}},
{'A',{14,17,17,31,17,17,17}},{'B',{30,17,17,30,17,17,30}},{'C',{14,17,16,16,16,17,14}},{'D',{30,17,17,17,17,17,30}},{'E',{31,16,16,30,16,16,31}},{'F',{31,16,16,30,16,16,16}},{'G',{14,17,16,23,17,17,15}},{'H',{17,17,17,31,17,17,17}},{'I',{14,4,4,4,4,4,14}},{'J',{7,2,2,2,2,18,12}},{'K',{17,18,20,24,20,18,17}},{'L',{16,16,16,16,16,16,31}},{'M',{17,27,21,21,17,17,17}},{'N',{17,25,21,19,17,17,17}},{'O',{14,17,17,17,17,17,14}},{'P',{30,17,17,30,16,16,16}},{'Q',{14,17,17,17,21,18,13}},{'R',{30,17,17,30,20,18,17}},{'S',{15,16,16,14,1,1,30}},{'T',{31,4,4,4,4,4,4}},{'U',{17,17,17,17,17,17,14}},{'V',{17,17,17,17,17,10,4}},{'W',{17,17,17,21,21,21,10}},{'X',{17,17,10,4,10,17,17}},{'Y',{17,17,10,4,4,4,4}},{'Z',{31,1,2,4,8,16,31}},
{'a',{14,1,15,17,17,19,13}},{'b',{16,16,30,17,17,17,30}},{'c',{0,0,14,17,16,17,14}},{'d',{1,1,15,17,17,19,13}},{'e',{0,0,14,17,31,16,14}},{'f',{6,9,8,28,8,8,8}},{'g',{0,15,17,17,15,1,14}},{'h',{16,16,30,17,17,17,17}},{'i',{4,0,12,4,4,4,14}},{'j',{2,0,6,2,2,18,12}},{'k',{16,16,18,20,24,20,18}},{'l',{12,4,4,4,4,4,14}},{'m',{0,0,26,21,21,21,21}},{'n',{0,0,30,17,17,17,17}},{'o',{0,0,14,17,17,17,14}},{'p',{0,30,17,17,30,16,16}},{'q',{0,15,17,17,15,1,1}},{'r',{0,0,22,25,16,16,16}},{'s',{0,0,15,16,14,1,30}},{'t',{8,8,28,8,8,9,6}},{'u',{0,0,17,17,17,19,13}},{'v',{0,0,17,17,17,10,4}},{'w',{0,0,17,17,21,21,10}},{'x',{0,0,17,10,4,10,17}},{'y',{0,17,17,17,15,1,14}},{'z',{0,0,31,2,4,8,31}}
};
static uint32_t Pack(uint32_t c,uint32_t f){uint32_t r=(c>>16)&255,g=(c>>8)&255,b=c&255;if(f==1)return r|(g<<8)|(b<<16);if(f==2)return b|(g<<8)|(r<<16);if(f==3)return ((r>>3)<<11)|((g>>2)<<5)|(b>>3);return c;}
static void Pixel(const RC95LDR_BGFX_FRAMEBUFFER *f,uint32_t x,uint32_t y,uint32_t c){uint8_t *p=(uint8_t*)f->Base+((uint64_t)y*f->PixelsPerScanLine+x)*(f->PixelFormat==3?2:4);uint32_t v=Pack(c,f->PixelFormat);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);if(f->PixelFormat!=3){p[2]=(uint8_t)(v>>16);p[3]=0;}}
RC95LDR_STATUS Rc95LdrBgfxInitialize(const RC95LDR_BGFX_PLATFORM *p,const RC95LDR_BGFX_CONFIG *c,uint32_t firmware,RC95LDR_BGFX_FRAMEBUFFER *f)
{
    RC95LDR_STATUS s;
    if(!p||p->StructureSize<sizeof(*p)||p->Version!=RC95LDR_BGFX_VERSION||!c||
       c->StructureSize<sizeof(*c)||c->Version!=RC95LDR_BGFX_VERSION||!f||
       (firmware!=RC95LDR_BGFX_FIRMWARE_BIOS&&firmware!=RC95LDR_BGFX_FIRMWARE_UEFI))
        return Rc95LdrInvalidParameter;
    memset(f,0,sizeof(*f));
    /* BCD graphicsmodedisabled applies to both firmware paths. BIOS may
       need an explicit mode switch; UEFI text output remains firmware-owned. */
    if(c->GraphicsModeDisabled){
        if(firmware==RC95LDR_BGFX_FIRMWARE_BIOS&&p->SetTextMode){
            s=p->SetTextMode(p->Context,80u,50u);
            if(s!=Rc95LdrSuccess)return s;
        }
        return Rc95LdrUnsupported;
    }
    if(!p->SetGraphicsMode||!p->AcquireFramebuffer)return Rc95LdrUnsupported;
    s=p->SetGraphicsMode(p->Context,
        c->PreferredWidth?c->PreferredWidth:RC95LDR_BGFX_DEFAULT_WIDTH,
        c->PreferredHeight?c->PreferredHeight:RC95LDR_BGFX_DEFAULT_HEIGHT);
    if(s!=Rc95LdrSuccess)return s;
    f->StructureSize=sizeof(*f);f->Version=RC95LDR_BGFX_VERSION;
    s=p->AcquireFramebuffer(p->Context,f);
    if(s!=Rc95LdrSuccess)return s;
    return Rc95LdrBgfxValidateFramebuffer(f);
}
RC95LDR_STATUS Rc95LdrBgfxShowError(const RC95LDR_BGFX_FRAMEBUFFER *f,const char *title,const char *message)
{
    RC95LDR_STATUS s=Rc95LdrBgfxFill(f,ERROR_BG);
    uint32_t x,y,z;
    if(s!=Rc95LdrSuccess)return s;
    z=f->Width>=1024?2:1;x=f->Width/12;y=f->Height/4;
    s=Rc95LdrBgfxText(f,x,y,title?title:"BOOT ERROR",0xFFFFFFu,ERROR_BG,z);
    if(s==Rc95LdrSuccess)s=Rc95LdrBgfxText(f,x,y+48*z,message?message:"The system could not be started.",0xFFFFFFu,ERROR_BG,z);
    return s;
}
RC95LDR_STATUS Rc95LdrBgfxValidateFramebuffer(const RC95LDR_BGFX_FRAMEBUFFER *f){uint64_t b,n;if(!f||f->StructureSize<sizeof(*f)||f->Version!=RC95LDR_BGFX_VERSION||!f->Base||!f->Width||!f->Height||f->PixelsPerScanLine<f->Width||f->PixelFormat<1||f->PixelFormat>3)return Rc95LdrInvalidParameter;b=f->PixelFormat==3?2:4;if((uint64_t)f->PixelsPerScanLine>UINT64_MAX/b/f->Height)return Rc95LdrIntegerOverflow;n=(uint64_t)f->PixelsPerScanLine*f->Height*b;if(n>f->Size)return Rc95LdrOutOfBounds;/* Pixel() performs pointer arithmetic from Base. Reject a descriptor whose declared framebuffer extent would wrap the CPU address space, even if Size itself is large enough. */if(n>(uint64_t)(uintptr_t)-1||(uintptr_t)f->Base>(uintptr_t)-1-(uintptr_t)n)return Rc95LdrOutOfBounds;return Rc95LdrSuccess;}
RC95LDR_STATUS Rc95LdrBgfxFill(const RC95LDR_BGFX_FRAMEBUFFER *f,uint32_t c){uint32_t x,y;RC95LDR_STATUS s=Rc95LdrBgfxValidateFramebuffer(f);if(s)return s;for(y=0;y<f->Height;y++)for(x=0;x<f->Width;x++)Pixel(f,x,y,c);return Rc95LdrSuccess;}
RC95LDR_STATUS Rc95LdrBgfxRectangle(const RC95LDR_BGFX_FRAMEBUFFER *f,uint32_t x,uint32_t y,uint32_t w,uint32_t h,uint32_t c){uint32_t a,b;RC95LDR_STATUS s=Rc95LdrBgfxValidateFramebuffer(f);if(s)return s;if(x>f->Width||y>f->Height)return Rc95LdrOutOfBounds;if(w>f->Width-x)w=f->Width-x;if(h>f->Height-y)h=f->Height-y;for(b=y;b<y+h;b++)for(a=x;a<x+w;a++)Pixel(f,a,b,c);return Rc95LdrSuccess;}
RC95LDR_STATUS Rc95LdrBgfxText(const RC95LDR_BGFX_FRAMEBUFFER *f,uint32_t x,uint32_t y,const char *t,uint32_t fg,uint32_t bg,uint32_t z)
{
    size_t i,j,k;
    uint32_t a,b,q;
    const GLYPH *g;
    RC95LDR_STATUS s=Rc95LdrBgfxValidateFramebuffer(f);
    if(s!=Rc95LdrSuccess)return s;
    if(!t||!z||z>8)return Rc95LdrInvalidParameter;
    for(i=0;t[i];i++){
        uint64_t originX=(uint64_t)x+(uint64_t)i*6u*z;
        if(originX>=f->Width)break;
        g=NULL;
        for(j=0;j<sizeof(Font)/sizeof(Font[0]);j++)
            if(Font[j].c==t[i]){g=&Font[j];break;}
        for(b=0;b<7;b++)for(a=0;a<5;a++){
            uint32_t color=(g&&(g->r[b]&(1u<<(4-a))))?fg:bg;
            for(k=0;k<z;k++)for(q=0;q<z;q++){
                uint64_t px=originX+(uint64_t)a*z+q;
                uint64_t py=(uint64_t)y+(uint64_t)b*z+k;
                if(px<f->Width&&py<f->Height)
                    Pixel(f,(uint32_t)px,(uint32_t)py,color);
            }
        }
    }
    return Rc95LdrSuccess;
}
static RC95LDR_STATUS Draw(const RC95LDR_BGFX_FRAMEBUFFER *f,const char *title,const char *const *items,size_t n,size_t sel,int adv)
{
    size_t i,first,last,visible;
    uint32_t x,y,w,h,z,footerY,itemTop,itemStep,itemHeight;
    RC95LDR_STATUS s=Rc95LdrBgfxValidateFramebuffer(f);
    if(s!=Rc95LdrSuccess)return s;
    if(!items||!n||n>RC95LDR_TEXTUI_MAX_MENU_ITEMS||sel>=n)return Rc95LdrInvalidParameter;
    x=f->Width/12;y=f->Height/7;w=(f->Width/6)*5;h=(f->Height/7)*5;z=f->Width>=1024?2:1;
    footerY=(uint64_t)y+h>=35u*z?y+h-35u*z:y;
    itemTop=y+120u*z;
    itemStep=42u*z;
    itemHeight=34u*z;

    /* Keep the selected row inside the panel's usable viewport. Large BCD
       stores can contain more entries than fit on screen; render a moving
       window instead of drawing rows over the footer or beyond the panel. */
    if(footerY>itemTop+itemHeight)
        visible=(footerY-itemTop-itemHeight)/itemStep+1u;
    else
        visible=1u;
    if(visible>n)visible=n;
    first=sel>=visible?sel-visible+1u:0u;
    last=first+visible;
    if(last>n)last=n;
#define BGFX_DRAW(call) do{s=(call);if(s!=Rc95LdrSuccess)return s;}while(0)
    BGFX_DRAW(Rc95LdrBgfxFill(f,BG));
    BGFX_DRAW(Rc95LdrBgfxRectangle(f,x,y,w,h,PANEL));
    BGFX_DRAW(Rc95LdrBgfxRectangle(f,x,y,w,54*z,ACCENT));
    BGFX_DRAW(Rc95LdrBgfxText(f,x+28,y+18*z,adv?"RC95-OS ADVANCED STARTUP":"RC95-OS BOOT MANAGER",WHITE,ACCENT,z));
    BGFX_DRAW(Rc95LdrBgfxText(f,x+28,y+75*z,title?title:(adv?"Choose startup option":"Select operating system"),WHITE,PANEL,z));
    for(i=first;i<last;i++){
        uint32_t iy=itemTop+(uint32_t)(i-first)*itemStep;
        BGFX_DRAW(Rc95LdrBgfxRectangle(f,x+20,iy,w>40?w-40:0,itemHeight,i==sel?HILITE:PANEL2));
        BGFX_DRAW(Rc95LdrBgfxText(f,x+34,iy+10*z,items[i]?items[i]:"(unnamed)",WHITE,i==sel?HILITE:PANEL2,z));
    }
    BGFX_DRAW(Rc95LdrBgfxText(f,x+28,footerY,adv?"UP/DOWN SCROLL ENTER ESC BACK":"UP/DOWN SCROLL ENTER F8 ADVANCED",MUTED,PANEL,z));
#undef BGFX_DRAW
    return Rc95LdrSuccess;
}
RC95LDR_STATUS Rc95LdrBgfxShowBootMenu(const RC95LDR_BGFX_FRAMEBUFFER *f,const char *t,const char *const *i,size_t n,size_t s){return Draw(f,t,i,n,s,0);}
RC95LDR_STATUS Rc95LdrBgfxShowAdvancedOptions(const RC95LDR_BGFX_FRAMEBUFFER *f,const char *const *i,size_t n,size_t s){return Draw(f,"Choose a diagnostic startup mode",i,n,s,1);}
static RC95LDR_STATUS TextFallback(const RC95LDR_BGFX_PLATFORM *p,const RC95LDR_TEXTUI *tu,const RC95LDR_TEXTUI_EXT *te,const char *title,const char *const *items,size_t n,size_t initial,size_t *selected)
{
    RC95LDR_STATUS s;
    if(p && p->Firmware==RC95LDR_BGFX_FIRMWARE_BIOS && te &&
       te->StructureSize>=sizeof(*te) && te->Version==RC95LDR_TEXTUI_EXT_VERSION &&
       te->SetMode){
        s=Rc95LdrTextUiSetVideoMode(te,80u,50u);
        if(s!=Rc95LdrSuccess)return s;
    }
    return Rc95LdrTextUiRunBootMenu(tu,te,title,items,n,initial,selected);
}
RC95LDR_STATUS Rc95LdrBgfxRunBootMenu(const RC95LDR_BGFX_PLATFORM *p,const RC95LDR_BGFX_CONFIG *c,const RC95LDR_TEXTUI *tu,const RC95LDR_TEXTUI_EXT *te,const char *title,const char *const *items,size_t n,size_t initial,size_t *selected){RC95LDR_BGFX_FRAMEBUFFER f;uint32_t key;size_t sel=initial,a=0;RC95LDR_STATUS s;static const char *adv[]={"Safe Mode","Safe Mode with Networking","Disable automatic restart","Enable boot logging","Enable debugging","Return to boot menu"};if(!p||p->StructureSize<sizeof(*p)||p->Version!=RC95LDR_BGFX_VERSION||!p->ReadKey||!c||c->StructureSize<sizeof(*c)||c->Version!=RC95LDR_BGFX_VERSION||!selected||!items||!n||n>RC95LDR_TEXTUI_MAX_MENU_ITEMS||initial>=n)return Rc95LdrInvalidParameter;if(!p->AcquireFramebuffer)return TextFallback(p,tu,te,title,items,n,initial,selected);s=Rc95LdrBgfxInitialize(p,c,p->Firmware?p->Firmware:RC95LDR_BGFX_FIRMWARE_BIOS,&f);if(s!=Rc95LdrSuccess)return TextFallback(p,tu,te,title,items,n,initial,selected);for(;;){s=Rc95LdrBgfxShowBootMenu(&f,title,items,n,sel);if(s)return s;s=p->ReadKey(p->Context,&key);if(s)return s;if(key==RC95LDR_TEXTUI_KEY_UP)sel=sel?sel-1:n-1;else if(key==RC95LDR_TEXTUI_KEY_DOWN)sel=(sel+1)%n;else if(key==RC95LDR_TEXTUI_KEY_ENTER){*selected=sel;return Rc95LdrSuccess;}else if(key==RC95LDR_TEXTUI_KEY_ESCAPE)return Rc95LdrNotFound;else if(key==RC95LDR_TEXTUI_KEY_F8){for(;;){s=Rc95LdrBgfxShowAdvancedOptions(&f,adv,6,a);if(s)return s;s=p->ReadKey(p->Context,&key);if(s)return s;if(key==RC95LDR_TEXTUI_KEY_ESCAPE||(key==RC95LDR_TEXTUI_KEY_ENTER&&a==5))break;if(key==RC95LDR_TEXTUI_KEY_UP)a=a?a-1:5;else if(key==RC95LDR_TEXTUI_KEY_DOWN)a=(a+1)%6;else if(key==RC95LDR_TEXTUI_KEY_ENTER){if(!p->SelectAdvancedOption)return Rc95LdrUnsupported;s=p->SelectAdvancedOption(p->Context,a);if(s)return s;break;}}}}}
