#include "rc95ldrfirmwaremenu.h"
#include <string.h>
typedef struct _MENU_CONTEXT {
    const RC95LDR_FIRMWARE_MENU_OPS *Ops;
    RC95LDR_FIRMWARE_VIDEO_MODE Mode;
    void *Framebuffer;
} MENU_CONTEXT;
static RC95LDR_STATUS RunTextFallback(RC95LDR_FIRMWARE_KIND Kind,
    const RC95LDR_FIRMWARE_MENU_OPS *Ops,const char *Title,
    const char *const *Items,size_t Count,size_t Initial,size_t *Selected)
{
    RC95LDR_STATUS Status;
    if(Kind==Rc95LdrFirmwareMenuBios && Ops->TextUiExtension &&
       Ops->TextUiExtension->SetMode){
        Status=Rc95LdrTextUiSetVideoMode(Ops->TextUiExtension,80u,50u);
        if(Status!=Rc95LdrSuccess)return Status;
    }
    return Rc95LdrTextUiRunBootMenu(Ops->TextUi,Ops->TextUiExtension,
        Title,Items,Count,Initial,Selected);
}
static RC95LDR_STATUS SelectMode(const RC95LDR_FIRMWARE_MENU_OPS *Ops,
                                  RC95LDR_FIRMWARE_VIDEO_MODE *Selected)
{
    RC95LDR_FIRMWARE_VIDEO_MODE Modes[RC95LDR_FIRMWARE_MAX_VIDEO_MODES];
    size_t Count=0, i, Best=(size_t)-1;
    uint64_t BestArea=0;
    RC95LDR_STATUS Status=Ops->EnumerateModes(Ops->Context,Modes,
        RC95LDR_ARRAY_COUNT(Modes),&Count);
    if(Status!=Rc95LdrSuccess)return Status;
    if(!Count||Count>RC95LDR_ARRAY_COUNT(Modes))return Rc95LdrNotFound;
    /* Prefer the established 1024x768 layout when available. Otherwise pick
       the largest sane desktop mode so the menu can still use GOP/VBE
       framebuffers on machines with different native panel resolutions. */
    for(i=0;i<Count;i++){
        uint64_t Area;
        if(Modes[i].Width<640u||Modes[i].Height<400u||
           Modes[i].Width>4096u||Modes[i].Height>2160u||
           Modes[i].PixelsPerScanLine<Modes[i].Width||
           Modes[i].PixelFormat<RC95LDR_BGFX_PIXEL_RGBX8888||
           Modes[i].PixelFormat>RC95LDR_BGFX_PIXEL_RGB565||
           !Modes[i].PixelsPerScanLine)continue;
        if(Modes[i].Width==RC95LDR_BGFX_DEFAULT_WIDTH&&
           Modes[i].Height==RC95LDR_BGFX_DEFAULT_HEIGHT){
            Best=i;
            break;
        }
        /* Reject unusually narrow/tall modes that technically meet the
           minimum dimensions but are unsuitable for a boot menu. */
        if((uint64_t)Modes[i].Width*5u<(uint64_t)Modes[i].Height*6u||
           (uint64_t)Modes[i].Width*5u>(uint64_t)Modes[i].Height*11u)
            continue;
        Area=(uint64_t)Modes[i].Width*Modes[i].Height;
        if(Best==(size_t)-1||Area>BestArea){Best=i;BestArea=Area;}
    }
    if(Best==(size_t)-1)return Rc95LdrNotFound;
    *Selected=Modes[Best];
    return Rc95LdrSuccess;
}
static RC95LDR_STATUS MapFb(void *Context,RC95LDR_BGFX_FRAMEBUFFER *Fb)
{
    MENU_CONTEXT *M=(MENU_CONTEXT*)Context;
    RC95LDR_FIRMWARE_VIDEO_MODE Modes[RC95LDR_FIRMWARE_MAX_VIDEO_MODES];
    size_t Count=0,i;
    RC95LDR_STATUS S=M->Ops->EnumerateModes(M->Ops->Context,Modes,
        RC95LDR_ARRAY_COUNT(Modes),&Count);
    if(S!=Rc95LdrSuccess)return S;
    for(i=0;i<Count;i++)if(Modes[i].ModeId==M->Mode.ModeId){M->Mode=Modes[i];break;}
    if(i==Count||!M->Mode.FramebufferBase||!M->Mode.FramebufferSize)return Rc95LdrNotFound;
    S=M->Ops->MapFramebuffer(M->Ops->Context,M->Mode.FramebufferBase,
        M->Mode.FramebufferSize,&M->Framebuffer);
    if(S!=Rc95LdrSuccess)return S;
    Fb->Base=M->Framebuffer;Fb->Size=M->Mode.FramebufferSize;
    Fb->Width=M->Mode.Width;Fb->Height=M->Mode.Height;
    Fb->PixelsPerScanLine=M->Mode.PixelsPerScanLine;
    Fb->PixelFormat=M->Mode.PixelFormat;
    return Rc95LdrSuccess;
}
static RC95LDR_STATUS ReadKey(void *Context,uint32_t *Key)
{
    MENU_CONTEXT *M=(MENU_CONTEXT*)Context;
    return M->Ops->ReadKey(M->Ops->Context,Key);
}
static RC95LDR_STATUS SetMode(void *Context,uint32_t Width,uint32_t Height)
{
    MENU_CONTEXT *M=(MENU_CONTEXT*)Context;
    if(Width!=M->Mode.Width||Height!=M->Mode.Height)
        return Rc95LdrInvalidParameter;
    return M->Ops->SetMode(M->Ops->Context,M->Mode.ModeId);
}
static RC95LDR_STATUS SelectAdvanced(void *Context,size_t OptionIndex)
{
    MENU_CONTEXT *M=(MENU_CONTEXT*)Context;
    if(!M->Ops->SelectAdvancedOption)return Rc95LdrUnsupported;
    return M->Ops->SelectAdvancedOption(M->Ops->Context,OptionIndex);
}
RC95LDR_STATUS Rc95BcdReadGraphicsModeDisabled(const RC95BCD_HIVE *Hive,
    uint32_t ObjectCell,uint32_t *Disabled)
{
    RC95BCD_VALUE Value;
    RC95BCD_ELEMENT_VIEW Element;
    RC95LDR_STATUS Status;
    if(!Hive||!Disabled)return Rc95LdrInvalidParameter;
    *Disabled=0;
    Status=Rc95BcdFindElement(Hive,ObjectCell,
        RC95BCD_OSLOADER_GRAPHICS_MODE_DISABLED,&Value);
    if(Status==Rc95LdrNotFound)return Rc95LdrSuccess;
    if(Status!=Rc95LdrSuccess)return Status;
    Status=Rc95BcdDecodeElement(RC95BCD_OSLOADER_GRAPHICS_MODE_DISABLED,
        &Value,&Element);
    if(Status!=Rc95LdrSuccess)return Status;
    if(Element.Kind!=Rc95BcdElementBoolean)return Rc95LdrCorruptData;
    *Disabled=Element.BooleanValue;
    return Rc95LdrSuccess;
}
static RC95LDR_STATUS RunFirmwareBootMenuInternal(RC95LDR_FIRMWARE_KIND Kind,
    const RC95LDR_FIRMWARE_MENU_OPS *Ops,const RC95BCD_HIVE *Hive,
    uint32_t OsLoaderObjectCell,uint32_t DefaultGraphicsModeDisabled,
    const char *Title,const char *const *Items,
    size_t Count,size_t Initial,size_t *Selected)
{
    MENU_CONTEXT M;
    RC95LDR_BGFX_PLATFORM Platform;
    RC95LDR_BGFX_CONFIG Config;
    RC95LDR_STATUS Status;
    uint32_t Disabled=DefaultGraphicsModeDisabled;
    if((Kind!=Rc95LdrFirmwareMenuBios&&Kind!=Rc95LdrFirmwareMenuUefi)||
       !Ops||Ops->StructureSize<sizeof(*Ops)||Ops->Version!=RC95LDR_FIRMWARE_MENU_VERSION||
       !Ops->TextUi||!Ops->TextUiExtension||
       !Items||!Count||Count>RC95LDR_TEXTUI_MAX_MENU_ITEMS||Initial>=Count||!Selected)
        return Rc95LdrInvalidParameter;
    if(Ops->GraphicsModeDisabledOutput)
        *Ops->GraphicsModeDisabledOutput=0;
    if(Hive){
        Status=Rc95BcdReadGraphicsModeDisabled(Hive,OsLoaderObjectCell,&Disabled);
        if(Status!=Rc95LdrSuccess)return Status;
    }
    if(Ops->GraphicsModeDisabledOutput)
        *Ops->GraphicsModeDisabledOutput=Disabled;

    /* A loader explicitly configured with graphicsmodedisabled must not
       depend on VBE/GOP discovery, mode setting, framebuffer mapping, or
       graphics keyboard callbacks. Keep the text-console path available
       even on firmware where no usable graphics protocol is installed. */
    if(Disabled)
        return RunTextFallback(Kind,Ops,Title,Items,Count,Initial,Selected);

    if(!Ops->ReadKey||!Ops->EnumerateModes||!Ops->SetMode||
       !Ops->MapFramebuffer)
        return RunTextFallback(Kind,Ops,Title,Items,Count,Initial,Selected);

    memset(&M,0,sizeof(M));M.Ops=Ops;
    memset(&Platform,0,sizeof(Platform));
    Platform.StructureSize=sizeof(Platform);Platform.Version=RC95LDR_BGFX_VERSION;
    Platform.Firmware=Kind==Rc95LdrFirmwareMenuBios ?
        RC95LDR_BGFX_FIRMWARE_BIOS : RC95LDR_BGFX_FIRMWARE_UEFI;
    Platform.Context=&M;Platform.AcquireFramebuffer=MapFb;
    Platform.SetGraphicsMode=SetMode;Platform.ReadKey=ReadKey;
    Platform.SelectAdvancedOption=Ops->SelectAdvancedOption?SelectAdvanced:NULL;
    memset(&Config,0,sizeof(Config));Config.StructureSize=sizeof(Config);
    Config.Version=RC95LDR_BGFX_VERSION;Config.GraphicsModeDisabled=Disabled;
    if(Disabled && Kind==Rc95LdrFirmwareMenuBios){
        Status=Rc95LdrTextUiSetVideoMode(Ops->TextUiExtension,80u,50u);
        if(Status!=Rc95LdrSuccess)return Status;
        return Rc95LdrTextUiRunBootMenu(Ops->TextUi,
            Ops->TextUiExtension,Title,Items,Count,Initial,Selected);
    }
    Status=SelectMode(Ops,&M.Mode);
    if(Status!=Rc95LdrSuccess)return RunTextFallback(Kind,Ops,Title,Items,Count,Initial,Selected);
    Config.PreferredWidth=M.Mode.Width;
    Config.PreferredHeight=M.Mode.Height;
    Status=Rc95LdrBgfxRunBootMenu(&Platform,&Config,Ops->TextUi,
        Ops->TextUiExtension,Title,Items,Count,Initial,Selected);
    if(Status==Rc95LdrUnsupported||Status==Rc95LdrNotFound)
        return RunTextFallback(Kind,Ops,Title,Items,Count,Initial,Selected);
    return Status;
}


RC95LDR_STATUS Rc95LdrRunFirmwareBootMenu(RC95LDR_FIRMWARE_KIND Kind,
    const RC95LDR_FIRMWARE_MENU_OPS *Ops,const RC95BCD_HIVE *Hive,
    uint32_t OsLoaderObjectCell,const char *Title,const char *const *Items,
    size_t Count,size_t Initial,size_t *Selected)
{
    return RunFirmwareBootMenuInternal(Kind,Ops,Hive,OsLoaderObjectCell,0,
        Title,Items,Count,Initial,Selected);
}

RC95LDR_STATUS Rc95LdrRunFirmwareCandidateMenu(
    RC95LDR_FIRMWARE_KIND Kind,const RC95LDR_FIRMWARE_MENU_OPS *Ops,
    const RC95BCD_HIVE *Hive,const RC95LDR_BCD_BOOT_CANDIDATE *Candidates,
    size_t CandidateCount,const char *Title,const char *const *Items,
    size_t Initial,const RC95LDR_BCD_BOOT_CANDIDATE **Selected)
{
    size_t Index;
    size_t MenuCount;
    const char *MenuItems[RC95LDR_TEXTUI_MAX_MENU_ITEMS];
    RC95LDR_STATUS Status;
    if(!Candidates||!CandidateCount||!Items||!Selected||
       Initial>=CandidateCount || !Ops ||
       CandidateCount > RC95LDR_TEXTUI_MAX_MENU_ITEMS ||
       (Ops->RestartSystem &&
        CandidateCount >= RC95LDR_TEXTUI_MAX_MENU_ITEMS))
        return Rc95LdrInvalidParameter;
    *Selected=NULL;
    for(Index=0;Index<CandidateCount;Index++) MenuItems[Index]=Items[Index];
    MenuCount=CandidateCount;
    if(Ops->RestartSystem) {
        MenuItems[CandidateCount]="Restart computer";
        MenuCount=CandidateCount+1u;
    }
    Status=RunFirmwareBootMenuInternal(Kind,Ops,Hive,
        Candidates[Initial].ObjectCell,Candidates[Initial].GraphicsModeDisabled,
        Title,MenuItems,MenuCount,Initial,&Index);
    if(Status!=Rc95LdrSuccess)return Status;
    if(Ops->RestartSystem && Index==CandidateCount){
        Status=Ops->RestartSystem(Ops->RestartContext);
        /* A successful reset does not return. Treat any callback return as
           failure so callers cannot mistake it for a completed boot choice. */
        return Status==Rc95LdrSuccess ? Rc95LdrIoError : Status;
    }
    if(Index>=CandidateCount)return Rc95LdrOutOfBounds;
    if(Ops && Ops->GraphicsModeDisabledOutput){
        uint32_t Disabled=Candidates[Index].GraphicsModeDisabled;
        if(Hive){
            Status=Rc95BcdReadGraphicsModeDisabled(Hive,
                Candidates[Index].ObjectCell,&Disabled);
            if(Status!=Rc95LdrSuccess)return Status;
        }
        *Ops->GraphicsModeDisabledOutput=Disabled;
    }
    *Selected=&Candidates[Index];
    return Rc95LdrSuccess;
}
RC95LDR_STATUS Rc95LdrRunBiosCandidateMenu(
    const RC95LDR_FIRMWARE_MENU_OPS *Ops,const RC95BCD_HIVE *Hive,
    const RC95LDR_BCD_BOOT_CANDIDATE *Candidates,size_t Count,
    const char *Title,const char *const *Items,
    const RC95LDR_BCD_BOOT_CANDIDATE **Selected)
{
    return Rc95LdrRunFirmwareCandidateMenu(Rc95LdrFirmwareMenuBios,Ops,Hive,
        Candidates,Count,Title,Items,0,Selected);
}
RC95LDR_STATUS Rc95LdrRunUefiCandidateMenu(
    const RC95LDR_FIRMWARE_MENU_OPS *Ops,const RC95BCD_HIVE *Hive,
    const RC95LDR_BCD_BOOT_CANDIDATE *Candidates,size_t Count,
    const char *Title,const char *const *Items,size_t Initial,
    const RC95LDR_BCD_BOOT_CANDIDATE **Selected)
{
    return Rc95LdrRunFirmwareCandidateMenu(Rc95LdrFirmwareMenuUefi,Ops,Hive,
        Candidates,Count,Title,Items,Initial,Selected);
}
RC95LDR_STATUS Rc95LdrRunBiosBootMenu(const RC95LDR_FIRMWARE_MENU_OPS *Ops,
    const RC95BCD_HIVE *Hive,uint32_t Cell,const char *Title,
    const char *const *Items,size_t Count,size_t Initial,size_t *Selected)
{return Rc95LdrRunFirmwareBootMenu(Rc95LdrFirmwareMenuBios,Ops,Hive,Cell,
    Title,Items,Count,Initial,Selected);}
RC95LDR_STATUS Rc95LdrRunUefiBootMenu(const RC95LDR_FIRMWARE_MENU_OPS *Ops,
    const RC95BCD_HIVE *Hive,uint32_t Cell,const char *Title,
    const char *const *Items,size_t Count,size_t Initial,size_t *Selected)
{return Rc95LdrRunFirmwareBootMenu(Rc95LdrFirmwareMenuUefi,Ops,Hive,Cell,
    Title,Items,Count,Initial,Selected);}
