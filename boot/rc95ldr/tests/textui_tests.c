#include "rc95ldrtextui.h"
#include <stdio.h>
#include <string.h>

typedef struct _CAPTURE {
    char Buffer[8192];
    size_t Used;
    unsigned Clears;
} CAPTURE;

static RC95LDR_STATUS CaptureWrite(void *Context,const char *Text)
{
    CAPTURE *C=(CAPTURE*)Context;
    size_t N=strlen(Text);
    if(N>sizeof(C->Buffer)-C->Used-1)return Rc95LdrBufferTooSmall;
    memcpy(C->Buffer+C->Used,Text,N);
    C->Used+=N;C->Buffer[C->Used]=0;
    return Rc95LdrSuccess;
}
static RC95LDR_STATUS CaptureClear(void *Context)
{
    CAPTURE *C=(CAPTURE*)Context;
    C->Used=0;C->Buffer[0]=0;C->Clears++;
    return Rc95LdrSuccess;
}
int main(void)
{
    CAPTURE C;
    RC95LDR_TEXTUI Ui;
    const char *Items[]={"Windows 11","Windows 2000"};
    memset(&C,0,sizeof(C));memset(&Ui,0,sizeof(Ui));
    Ui.StructureSize=sizeof(Ui);Ui.Version=RC95LDR_TEXTUI_VERSION;
    Ui.Context=&C;Ui.Write=CaptureWrite;Ui.Clear=CaptureClear;
    if(Rc95LdrTextUiClear(&Ui)!=Rc95LdrSuccess||C.Clears!=1)return 1;
    Ui.Clear=NULL;
    if(Rc95LdrTextUiClear(&Ui)!=Rc95LdrSuccess||C.Clears!=1)return 1;
    Ui.Clear=CaptureClear;
    if(Rc95LdrTextUiShowWelcome(&Ui,"Firmware text console")!=Rc95LdrSuccess||
       !strstr(C.Buffer,"RC95LDR BOOT MANAGER")||
       !strstr(C.Buffer,"Firmware text console"))return 1;
    if(Rc95LdrTextUiShowBootMenu(&Ui,"Select an operating system",
       Items,2,1,"Enter=boot")!=Rc95LdrSuccess||
       !strstr(C.Buffer,"> Windows 2000")||
       !strstr(C.Buffer,"Windows 11"))return 2;
    if(Rc95LdrTextUiShowProgress(&Ui,"Loading kernel")!=Rc95LdrSuccess||
       !strstr(C.Buffer,"Loading kernel"))return 3;
    if(Rc95LdrTextUiShowError(&Ui,"Architecture mismatch",
       "This loader cannot start the selected OS.",Rc95LdrUnsupported,
       0x1234u,"Choose a matching loader.")!=Rc95LdrSuccess||
       !strstr(C.Buffer,"Architecture mismatch")||
       !strstr(C.Buffer,"This loader cannot start the selected OS.")||
       !strstr(C.Buffer,"0x00000003")||
       !strstr(C.Buffer,"0x00001234"))return 4;
    if(Rc95LdrTextUiShowBootMenu(&Ui,"Bad",Items,2,2,NULL)!=
       Rc95LdrInvalidParameter)return 5;
    if(C.Clears!=5)return 6;
    puts("RC95LDR text UI tests passed");
    return 0;
}
