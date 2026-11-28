#ifndef _RC95LDR_BGFX_H_
#define _RC95LDR_BGFX_H_
#include "rc95ldrtextui.h"
#ifdef __cplusplus
extern "C" {
#endif
#define RC95LDR_BGFX_VERSION 2u
#define RC95LDR_BGFX_PIXEL_RGBX8888 1u
#define RC95LDR_BGFX_PIXEL_BGRX8888 2u
#define RC95LDR_BGFX_PIXEL_RGB565 3u
#define RC95LDR_BGFX_FIRMWARE_BIOS 1u
#define RC95LDR_BGFX_FIRMWARE_UEFI 2u
#define RC95LDR_BGFX_DEFAULT_WIDTH 1024u
#define RC95LDR_BGFX_DEFAULT_HEIGHT 768u
typedef struct _RC95LDR_BGFX_FRAMEBUFFER { uint32_t StructureSize,Version; void *Base; uint64_t Size; uint32_t Width,Height,PixelsPerScanLine,PixelFormat; } RC95LDR_BGFX_FRAMEBUFFER;
typedef struct _RC95LDR_BGFX_PLATFORM { uint32_t StructureSize,Version,Firmware; void *Context; RC95LDR_STATUS (*AcquireFramebuffer)(void *,RC95LDR_BGFX_FRAMEBUFFER *); RC95LDR_STATUS (*SetGraphicsMode)(void *,uint32_t,uint32_t); RC95LDR_STATUS (*SetTextMode)(void *,uint32_t,uint32_t); RC95LDR_TEXTUI_READ_KEY ReadKey; RC95LDR_TEXTUI_ADVANCED_OPTION SelectAdvancedOption; } RC95LDR_BGFX_PLATFORM;
typedef struct _RC95LDR_BGFX_CONFIG { uint32_t StructureSize,Version; uint32_t GraphicsModeDisabled; uint32_t PreferredWidth,PreferredHeight; } RC95LDR_BGFX_CONFIG;
RC95LDR_STATUS Rc95LdrBgfxInitialize(const RC95LDR_BGFX_PLATFORM *,const RC95LDR_BGFX_CONFIG *,uint32_t,RC95LDR_BGFX_FRAMEBUFFER *);
RC95LDR_STATUS Rc95LdrBgfxShowError(const RC95LDR_BGFX_FRAMEBUFFER *,const char *,const char *);
RC95LDR_STATUS Rc95LdrBgfxValidateFramebuffer(const RC95LDR_BGFX_FRAMEBUFFER *);
RC95LDR_STATUS Rc95LdrBgfxFill(const RC95LDR_BGFX_FRAMEBUFFER *,uint32_t);
RC95LDR_STATUS Rc95LdrBgfxRectangle(const RC95LDR_BGFX_FRAMEBUFFER *,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t);
RC95LDR_STATUS Rc95LdrBgfxText(const RC95LDR_BGFX_FRAMEBUFFER *,uint32_t,uint32_t,const char *,uint32_t,uint32_t,uint32_t);
RC95LDR_STATUS Rc95LdrBgfxShowBootMenu(const RC95LDR_BGFX_FRAMEBUFFER *,const char *,const char *const *,size_t,size_t);
RC95LDR_STATUS Rc95LdrBgfxShowAdvancedOptions(const RC95LDR_BGFX_FRAMEBUFFER *,const char *const *,size_t,size_t);
RC95LDR_STATUS Rc95LdrBgfxRunBootMenu(const RC95LDR_BGFX_PLATFORM *,const RC95LDR_BGFX_CONFIG *,const RC95LDR_TEXTUI *,const RC95LDR_TEXTUI_EXT *,const char *,const char *const *,size_t,size_t,size_t *);
#ifdef __cplusplus
}
#endif
#endif
