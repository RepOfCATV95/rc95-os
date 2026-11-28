#include "rc95ldrtextui.h"
#include <stdio.h>
#include <string.h>

typedef struct _CAPTURE {
    char Buffer[8192];
    size_t Used;
    char History[16384];
    size_t HistoryUsed;
    unsigned Clears;
    uint32_t Keys[8];
    size_t KeyCount, KeyIndex;
} CAPTURE;

static RC95LDR_STATUS CaptureWrite(void *Context,const char *Text)
{
    CAPTURE *C=(CAPTURE*)Context;
    size_t N=strlen(Text);
    if(N>sizeof(C->Buffer)-C->Used-1 ||
       N>sizeof(C->History)-C->HistoryUsed-1)
        return Rc95LdrBufferTooSmall;
    memcpy(C->Buffer+C->Used,Text,N);
    C->Used+=N;C->Buffer[C->Used]=0;
    memcpy(C->History+C->HistoryUsed,Text,N);
    C->HistoryUsed+=N;C->History[C->HistoryUsed]=0;
    return Rc95LdrSuccess;
}
static RC95LDR_STATUS CaptureClear(void *Context)
{
    CAPTURE *C=(CAPTURE*)Context;
    C->Used=0;C->Buffer[0]=0;C->Clears++;
    return Rc95LdrSuccess;
}
static RC95LDR_STATUS CaptureReadKey(void *Context,uint32_t *Key)
{
    CAPTURE *C=(CAPTURE*)Context;
    if(!Key || C->KeyIndex>=C->KeyCount)return Rc95LdrIoError;
    *Key=C->Keys[C->KeyIndex++];
    return Rc95LdrSuccess;
}
int main(void)
{
    CAPTURE C;
    RC95LDR_TEXTUI Ui;
    RC95LDR_TEXTUI_EXT Ext;
    size_t Action;
    const char *Items[]={"Windows 11","Windows 2000"};
    memset(&C,0,sizeof(C));memset(&Ui,0,sizeof(Ui));memset(&Ext,0,sizeof(Ext));
    Ui.StructureSize=sizeof(Ui);Ui.Version=RC95LDR_TEXTUI_VERSION;
    Ui.Context=&C;Ui.Write=CaptureWrite;Ui.Clear=CaptureClear;
    if(Rc95LdrTextUiClear(&Ui)!=Rc95LdrSuccess||C.Clears!=1)return 1;
    Ui.Clear=NULL;
    if(Rc95LdrTextUiClear(&Ui)!=Rc95LdrSuccess||C.Clears!=1)return 1;
    Ui.Clear=CaptureClear;
    if(Rc95LdrTextUiShowWelcome(&Ui,"Firmware text console")!=Rc95LdrSuccess||
       !strstr(C.Buffer,"Republic of CATV 95 OS Loader")||
       !strstr(C.Buffer,"Firmware text console"))return 1;
    if(Rc95LdrTextUiShowBootMenu(&Ui,"Select an operating system",
       Items,2,1,"Enter=boot")!=Rc95LdrSuccess||
       !strstr(C.Buffer,"> Windows 2000")||
       !strstr(C.Buffer,"Windows 11"))return 2;
    if(Rc95LdrTextUiShowProgress(&Ui,"Loading kernel")!=Rc95LdrSuccess||
       !strstr(C.Buffer,"Loading kernel")||
       !strstr(C.Buffer,"0%"))return 3;
    if(Rc95LdrTextUiUpdateProgress(&Ui,"Loading kernel",50u)!=Rc95LdrSuccess||
       !strstr(C.Buffer,"#########################-------------------------")||
       !strstr(C.Buffer,"50%"))return 3;
    if(Rc95LdrTextUiUpdateProgress(&Ui,"Loading kernel",100u)!=Rc95LdrSuccess||
       !strstr(C.Buffer,"##################################################")||
       !strstr(C.Buffer,"100%"))return 3;
    if(Rc95LdrTextUiUpdateProgress(&Ui,"Loading kernel",UINT32_MAX)!=Rc95LdrSuccess||
       !strstr(C.Buffer,"##################################################")||
       !strstr(C.Buffer,"100%"))return 3;
    if(Rc95LdrTextUiShowError(&Ui,"Architecture mismatch",
       "This loader cannot start the selected OS.",Rc95LdrUnsupported,
       0x1234u,"Choose a matching loader.")!=Rc95LdrSuccess||
       !strstr(C.Buffer,"Architecture mismatch")||
       !strstr(C.Buffer,"This loader cannot start the selected OS.")||
       !strstr(C.Buffer,"Required feature or platform operation is unsupported")||
       !strstr(C.Buffer,"Status code: 0x00000003")||
       !strstr(C.Buffer,"0x00001234"))return 4;
    if(Rc95LdrTextUiShowBcdError(&Ui,Rc95LdrNotFound,0xBCD00001u)!=Rc95LdrSuccess ||
       !strstr(C.Buffer,"Boot Configuration Data is missing") ||
       !strstr(C.Buffer,"\\EFI\\Microsoft\\Boot\\BCD"))return 5;
    if(Rc95LdrTextUiShowBcdError(&Ui,Rc95LdrCorruptData,0xBCD00002u)!=Rc95LdrSuccess ||
       !strstr(C.Buffer,"Boot Configuration Data is corrupt") ||
       !strstr(C.Buffer,"truncated, or internally inconsistent"))return 5;
    if(Rc95LdrTextUiShowBootMenu(&Ui,"Bad",Items,2,2,NULL)!=
       Rc95LdrInvalidParameter)return 5;
    Ext.StructureSize=sizeof(Ext);Ext.Version=RC95LDR_TEXTUI_EXT_VERSION;
    Ext.Context=&C;Ext.ReadKey=CaptureReadKey;
    C.Keys[0]=RC95LDR_TEXTUI_KEY_F8;
    C.Keys[1]=RC95LDR_TEXTUI_KEY_DOWN;
    C.Keys[2]=RC95LDR_TEXTUI_KEY_ENTER;
    C.KeyCount=3;C.KeyIndex=0;
    if(Rc95LdrTextUiRunHiberOptions(&Ui,&Ext,&Action)!=Rc95LdrSuccess ||
       Action!=RC95LDR_HIBER_ACTION_NORMAL_BOOT ||
       !strstr(C.History,"Resuming from hibernation") ||
       !strstr(C.History,"Start with normal boot (delete hibernation image)") ||
       !strstr(C.History,"Proceed with hibernation resume"))return 6;
    C.Keys[0]=RC95LDR_TEXTUI_KEY_SPACE;
    C.Keys[1]=RC95LDR_TEXTUI_KEY_ENTER;
    C.KeyCount=2;C.KeyIndex=0;
    if(Rc95LdrTextUiRunHiberOptions(&Ui,&Ext,&Action)!=Rc95LdrSuccess ||
       Action!=RC95LDR_HIBER_ACTION_RESUME)return 7;
    /* Escape from advanced options is a safe, explicit resume choice. */
    C.Keys[0]=RC95LDR_TEXTUI_KEY_F8;
    C.Keys[1]=RC95LDR_TEXTUI_KEY_ESCAPE;
    C.KeyCount=2;C.KeyIndex=0;
    if(Rc95LdrTextUiRunHiberOptions(&Ui,&Ext,&Action)!=Rc95LdrSuccess ||
       Action!=RC95LDR_HIBER_ACTION_RESUME)return 8;
    /* Enter on the initial screen proceeds directly with resume. */
    C.Keys[0]=RC95LDR_TEXTUI_KEY_ENTER;
    C.KeyCount=1;C.KeyIndex=0;
    if(Rc95LdrTextUiRunHiberOptions(&Ui,&Ext,&Action)!=Rc95LdrSuccess ||
       Action!=RC95LDR_HIBER_ACTION_RESUME)return 9;
    /* Input-provider failures propagate; no action is silently selected. */
    C.KeyCount=0;C.KeyIndex=0;Action=99u;
    if(Rc95LdrTextUiRunHiberOptions(&Ui,&Ext,&Action)!=Rc95LdrIoError ||
       Action!=99u)return 10;
    if(C.Clears!=19)return 11;
    puts("RC95LDR text UI tests passed");
    return 0;
}
