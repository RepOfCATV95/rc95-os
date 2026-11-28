#include "rc95ldrwinload.h"
#include <string.h>
#include <stdio.h>

static char LastPath[260];
static unsigned Calls;
static RC95LDR_STATUS DeleteResult=Rc95LdrSuccess;
static RC95LDR_STATUS Delete(void *Context,const char *Path)
{
    (void)Context;
    ++Calls;
    strncpy(LastPath,Path,sizeof(LastPath)-1u);
    LastPath[sizeof(LastPath)-1u]=0;
    return DeleteResult;
}
int main(void)
{
    RC95LDR_FILESYSTEM Fs;
    RC95LDR_VOLUME Volume;
    static const uint16_t Custom[]={'\\','W','i','n','d','o','w','s','\\',
        'h','i','b','e','r','f','i','l','.','s','y','s',0};
    static const uint16_t Traversal[]={'\\','W','i','n','d','o','w','s',
        '\\','.','.','\\','x',0};
    static const uint16_t Trailing[]={'\\','h','i','b','e','r','f','i','l',
        '.','s','y','s','\\',0};
    static const uint16_t Wildcard[]={'\\','h','i','b','e','r','*','.','s','y','s',0};
    memset(&Fs,0,sizeof(Fs));memset(&Volume,0,sizeof(Volume));
    Fs.DeleteFile=Delete;Volume.FileSystem=&Fs;
    if(Rc95LdrWinloadDeleteHiberFile(&Volume,NULL)!=Rc95LdrSuccess||
       strcmp(LastPath,"\\hiberfil.sys")!=0||Calls!=1)return 1;
    if(Rc95LdrWinloadDeleteHiberFile(&Volume,Custom)!=Rc95LdrSuccess||
       strcmp(LastPath,"\\Windows\\hiberfil.sys")!=0||Calls!=2)return 1;
    DeleteResult=Rc95LdrIoError;
    if(Rc95LdrWinloadDeleteHiberFile(&Volume,Custom)!=Rc95LdrIoError||
       Calls!=3)return 1;
    DeleteResult=Rc95LdrSuccess;
    if(Rc95LdrWinloadDeleteHiberFile(&Volume,Traversal)!=Rc95LdrInvalidParameter||
       Rc95LdrWinloadDeleteHiberFile(&Volume,Trailing)!=Rc95LdrInvalidParameter||
       Rc95LdrWinloadDeleteHiberFile(&Volume,Wildcard)!=Rc95LdrInvalidParameter||
       Calls!=3)return 1;
    Fs.DeleteFile=NULL;
    if(Rc95LdrWinloadDeleteHiberFile(&Volume,NULL)!=Rc95LdrUnsupported||
       Calls!=3)return 1;
    puts("RC95LDR WINLOAD hibernation deletion adapter tests passed");
    return 0;
}
