#include "rc95ldrfirmwaremenu.h"
#include <string.h>
#include <stdio.h>
typedef struct _FAKE {
    uint32_t Keys[16],KeyCount,KeyIndex,SetCalls,MapCalls,NoModes,RestartCalls,TextModeCalls,FallbackOnly,AdvancedCalls,AdvancedIndex;
    unsigned char Pixels[1024*768*4];
} FAKE;
static RC95LDR_STATUS Modes(void *C,RC95LDR_FIRMWARE_VIDEO_MODE *M,size_t Cap,size_t *N)
{FAKE*f=C;if(f->NoModes)return Rc95LdrNotFound;if(Cap<1)return Rc95LdrBufferTooSmall;memset(M,0,sizeof(*M));M[0].ModeId=f->FallbackOnly?0x115:0x118;M[0].Width=f->FallbackOnly?800:1024;M[0].Height=f->FallbackOnly?600:768;M[0].PixelsPerScanLine=M[0].Width;M[0].PixelFormat=RC95LDR_BGFX_PIXEL_RGBX8888;M[0].FramebufferBase=0x100000;M[0].FramebufferSize=f->FallbackOnly?800u*600u*4u:sizeof(f->Pixels);*N=1;return Rc95LdrSuccess;}
static RC95LDR_STATUS Set(void*C,uint32_t Id){FAKE*f=C;if(Id!=(f->FallbackOnly?0x115u:0x118u))return Rc95LdrInvalidParameter;f->SetCalls++;return Rc95LdrSuccess;}
static RC95LDR_STATUS Map(void*C,uint64_t P,uint64_t S,void **B){FAKE*f=C;if(P!=0x100000||S>sizeof(f->Pixels))return Rc95LdrOutOfBounds;f->MapCalls++;*B=f->Pixels;return Rc95LdrSuccess;}
static RC95LDR_STATUS Key(void*C,uint32_t*K){FAKE*f=C;if(f->KeyIndex>=f->KeyCount)return Rc95LdrIoError;*K=f->Keys[f->KeyIndex++];return Rc95LdrSuccess;}
static RC95LDR_STATUS Write(void*C,const char*T){(void)C;(void)T;return Rc95LdrSuccess;}
static RC95LDR_STATUS TextSetMode(void*C,uint32_t Columns,uint32_t Rows){FAKE*f=C;if(Columns!=80u||Rows!=50u)return Rc95LdrInvalidParameter;f->TextModeCalls++;return Rc95LdrSuccess;}
static RC95LDR_STATUS Restart(void*C){((FAKE*)C)->RestartCalls++;return Rc95LdrIoError;}
int main(void)
{
    static FAKE F;RC95LDR_FIRMWARE_MENU_OPS O;RC95LDR_TEXTUI U;RC95LDR_TEXTUI_EXT E;
    RC95LDR_BCD_BOOT_CANDIDATE Candidates[2];
    const RC95LDR_BCD_BOOT_CANDIDATE *SelectedCandidate=NULL;
    uint32_t GraphicsModeDisabled=0;
    const char *Items[]={"RC95-OS","Windows"};size_t Selected=99;uint32_t x,y;
    memset(&F,0,sizeof(F));memset(&O,0,sizeof(O));memset(&U,0,sizeof(U));memset(&E,0,sizeof(E));
    O.StructureSize=sizeof(O);O.Version=RC95LDR_FIRMWARE_MENU_VERSION;O.Context=&F;
    O.EnumerateModes=Modes;O.SetMode=Set;O.MapFramebuffer=Map;O.ReadKey=Key;O.SelectAdvancedOption=Advanced;
    U.StructureSize=sizeof(U);U.Version=RC95LDR_TEXTUI_VERSION;U.Write=Write;
    E.StructureSize=sizeof(E);E.Version=RC95LDR_TEXTUI_EXT_VERSION;E.Context=&F;E.SetMode=TextSetMode;E.ReadKey=Key;
    O.TextUi=&U;O.TextUiExtension=&E;
    O.GraphicsModeDisabledOutput=&GraphicsModeDisabled;
    O.RestartContext=&F;O.RestartSystem=Restart;
    F.Keys[0]=RC95LDR_TEXTUI_KEY_DOWN;F.Keys[1]=RC95LDR_TEXTUI_KEY_ENTER;F.KeyCount=2;
    if(Rc95LdrRunBiosBootMenu(&O,NULL,0,"Select",Items,2,0,&Selected)!=Rc95LdrSuccess||Selected!=1||F.SetCalls!=1||F.MapCalls!=1)return 1;
    for(y=0;y<768;y++)for(x=0;x<1024*4;x++)if(F.Pixels[y*1024*4+x])goto rendered;
    return 2;
rendered:
    F.KeyIndex=0;F.KeyCount=2;F.Keys[0]=RC95LDR_TEXTUI_KEY_ENTER;F.Keys[1]=0;
    Selected=99;
    if(Rc95LdrRunUefiBootMenu(&O,NULL,0,"Select",Items,2,0,&Selected)!=Rc95LdrSuccess||Selected!=0)return 3;
    /* Navigation wraps at both ends of the candidate list. */
    F.KeyIndex=0;F.KeyCount=2;
    F.Keys[0]=RC95LDR_TEXTUI_KEY_UP;F.Keys[1]=RC95LDR_TEXTUI_KEY_ENTER;
    Selected=99;
    if(Rc95LdrRunUefiBootMenu(&O,NULL,0,"Wrap upward",Items,2,0,
        &Selected)!=Rc95LdrSuccess||Selected!=1)return 13;
    F.KeyIndex=0;F.KeyCount=2;
    F.Keys[0]=RC95LDR_TEXTUI_KEY_DOWN;F.Keys[1]=RC95LDR_TEXTUI_KEY_ENTER;
    Selected=99;
    if(Rc95LdrRunUefiBootMenu(&O,NULL,0,"Wrap downward",Items,2,1,
        &Selected)!=Rc95LdrSuccess||Selected!=0)return 14;
    /* Escape cancels the menu without publishing a selection. */
    F.KeyIndex=0;F.KeyCount=1;F.Keys[0]=RC95LDR_TEXTUI_KEY_ESCAPE;
    Selected=99;
    if(Rc95LdrRunUefiBootMenu(&O,NULL,0,"Cancel",Items,2,0,
        &Selected)!=Rc95LdrNotFound||Selected!=99)return 15;
    /* F8 opens advanced startup; the chosen option is dispatched and the
       main menu remains active until Enter chooses a boot candidate. */
    F.KeyIndex=0;F.KeyCount=4;
    F.Keys[0]=RC95LDR_TEXTUI_KEY_F8;
    F.Keys[1]=RC95LDR_TEXTUI_KEY_DOWN;
    F.Keys[2]=RC95LDR_TEXTUI_KEY_ENTER;
    F.Keys[3]=RC95LDR_TEXTUI_KEY_ENTER;
    F.AdvancedCalls=0;F.AdvancedIndex=UINT32_MAX;Selected=99;
    if(Rc95LdrRunUefiBootMenu(&O,NULL,0,"Advanced",Items,2,0,
        &Selected)!=Rc95LdrSuccess||Selected!=0||F.AdvancedCalls!=1||
        F.AdvancedIndex!=1)return 16;
    /* Escape from the advanced page returns to the boot menu without
       dispatching an advanced action. */
    F.KeyIndex=0;F.KeyCount=3;
    F.Keys[0]=RC95LDR_TEXTUI_KEY_F8;
    F.Keys[1]=RC95LDR_TEXTUI_KEY_ESCAPE;
    F.Keys[2]=RC95LDR_TEXTUI_KEY_ENTER;
    F.AdvancedCalls=0;Selected=99;
    if(Rc95LdrRunUefiBootMenu(&O,NULL,0,"Advanced cancel",Items,2,0,
        &Selected)!=Rc95LdrSuccess||Selected!=0||F.AdvancedCalls!=0)return 17;
    /* A machine without 1024x768 must use its best supported GOP/VBE mode. */
    F.FallbackOnly=1;F.KeyIndex=0;F.KeyCount=1;F.Keys[0]=RC95LDR_TEXTUI_KEY_ENTER;
    Selected=99;
    {
        RC95LDR_STATUS FallbackStatus=Rc95LdrRunUefiBootMenu(&O,NULL,0,
            "Native resolution",Items,2,0,&Selected);
        if(FallbackStatus!=Rc95LdrSuccess||Selected!=0||
           F.SetCalls!=3||F.MapCalls!=3){
            fprintf(stderr,"native-resolution fallback: status=%u selected=%u set=%u map=%u\\n",
                (unsigned)FallbackStatus,(unsigned)Selected,F.SetCalls,F.MapCalls);
            return 12;
        }
    }
    F.FallbackOnly=0;
    F.NoModes=1;F.KeyIndex=0;F.KeyCount=1;F.Keys[0]=RC95LDR_TEXTUI_KEY_ENTER;Selected=99;
    if(Rc95LdrRunBiosBootMenu(&O,NULL,0,"Text fallback",Items,2,0,&Selected)!=Rc95LdrSuccess||Selected!=0||F.MapCalls!=3)return 4;

    /* UEFI candidate menu must start on the BCD default, and return the
       exact selected candidate record rather than reselecting array[0]. */
    memset(Candidates,0,sizeof(Candidates));
    Candidates[0].ObjectCell=0x100;Candidates[1].ObjectCell=0x200;
    Candidates[0].GraphicsModeDisabled=0;Candidates[1].GraphicsModeDisabled=1;
    F.NoModes=1;F.KeyIndex=0;F.KeyCount=1;F.Keys[0]=RC95LDR_TEXTUI_KEY_ENTER;
    SelectedCandidate=NULL;
    if(Rc95LdrRunUefiCandidateMenu(&O,NULL,Candidates,2,"BCD default",Items,1,
        &SelectedCandidate)!=Rc95LdrSuccess||SelectedCandidate!=&Candidates[1]||
        GraphicsModeDisabled!=1u)return 5;
    /* Initial candidate policy must affect the menu itself even without a
       hive handle; disabled graphics must not set a GOP mode or map pixels. */
    F.NoModes=0;F.KeyIndex=0;F.KeyCount=1;F.Keys[0]=RC95LDR_TEXTUI_KEY_ENTER;
    {
        uint32_t SetCalls=F.SetCalls, MapCalls=F.MapCalls;
        RC95LDR_STATUS MenuStatus;
        SelectedCandidate=NULL;
        MenuStatus=Rc95LdrRunUefiCandidateMenu(&O,NULL,Candidates,2,
            "Graphics disabled",Items,1,&SelectedCandidate);
        if(MenuStatus!=Rc95LdrSuccess||SelectedCandidate!=&Candidates[1]||
           GraphicsModeDisabled!=1u||F.SetCalls!=SetCalls||F.MapCalls!=MapCalls){
            fprintf(stderr,"graphics-disabled candidate: status=%u selected=%d policy=%u set=%u/%u map=%u/%u\n",
                (unsigned)MenuStatus,SelectedCandidate==&Candidates[1],
                GraphicsModeDisabled,F.SetCalls,SetCalls,F.MapCalls,MapCalls);
            fprintf(stderr,"firmware_menu_tests failure checkpoint 9\\n");return 9;
        }
    }
    /* Explicit graphics-disable policy must work even when no graphics
       protocols or BGFX key callback are available. */
    {
        RC95LDR_STATUS (*SavedReadKey)(void *,uint32_t *)=O.ReadKey;
        RC95LDR_STATUS (*SavedEnumerate)(void *,RC95LDR_FIRMWARE_VIDEO_MODE *,size_t,size_t *)=O.EnumerateModes;
        RC95LDR_STATUS (*SavedSetMode)(void *,uint32_t)=O.SetMode;
        RC95LDR_STATUS (*SavedMap)(void *,uint64_t,uint64_t,void **)=O.MapFramebuffer;
        uint32_t SetCalls=F.SetCalls, MapCalls=F.MapCalls;
        O.ReadKey=NULL;O.EnumerateModes=NULL;O.SetMode=NULL;O.MapFramebuffer=NULL;
        F.KeyIndex=0;F.KeyCount=1;F.Keys[0]=RC95LDR_TEXTUI_KEY_ENTER;
        SelectedCandidate=NULL;
        if(Rc95LdrRunUefiCandidateMenu(&O,NULL,Candidates,2,
            "UEFI text-only policy",Items,1,&SelectedCandidate)!=Rc95LdrSuccess||
            SelectedCandidate!=&Candidates[1]||GraphicsModeDisabled!=1u||
            F.SetCalls!=SetCalls||F.MapCalls!=MapCalls)return 10;
        F.KeyIndex=0;F.KeyCount=1;F.Keys[0]=RC95LDR_TEXTUI_KEY_ENTER;
        SelectedCandidate=NULL;
        if(Rc95LdrRunBiosCandidateMenu(&O,NULL,Candidates,2,
            "BIOS text-only policy",Items,&SelectedCandidate)!=Rc95LdrSuccess||
            SelectedCandidate!=&Candidates[0]||GraphicsModeDisabled!=0u||
            F.TextModeCalls==0)return 11;
        O.ReadKey=SavedReadKey;O.EnumerateModes=SavedEnumerate;
        O.SetMode=SavedSetMode;O.MapFramebuffer=SavedMap;
    }
    F.KeyIndex=0;F.KeyCount=3;F.Keys[0]=RC95LDR_TEXTUI_KEY_DOWN;
    F.Keys[1]=RC95LDR_TEXTUI_KEY_DOWN;F.Keys[2]=RC95LDR_TEXTUI_KEY_ENTER;
    SelectedCandidate=NULL;
    if(Rc95LdrRunUefiCandidateMenu(&O,NULL,Candidates,2,"BCD override",Items,1,
        &SelectedCandidate)!=Rc95LdrSuccess||SelectedCandidate!=&Candidates[0]||
        GraphicsModeDisabled!=0u)return 6;
    if(Rc95LdrRunUefiCandidateMenu(&O,NULL,Candidates,2,"Invalid default",Items,2,
        &SelectedCandidate)!=Rc95LdrInvalidParameter)return 7;
    /* Restart is a synthetic action, not an OS-loader/BCD candidate. */
    F.KeyIndex=0;F.KeyCount=3;F.Keys[0]=RC95LDR_TEXTUI_KEY_DOWN;
    F.Keys[1]=RC95LDR_TEXTUI_KEY_DOWN;F.Keys[2]=RC95LDR_TEXTUI_KEY_ENTER;
    SelectedCandidate=NULL;
    if(Rc95LdrRunUefiCandidateMenu(&O,NULL,Candidates,2,"Restart",Items,0,
        &SelectedCandidate)!=Rc95LdrIoError||SelectedCandidate!=NULL||
        F.RestartCalls!=1)return 8;
    puts("RC95LDR firmware menu tests passed");return 0;
}
