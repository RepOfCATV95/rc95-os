#include "rc95ldrwinload.h"
#include <string.h>

RC95LDR_STATUS
Rc95LdrWinloadDeleteHiberFile(void *VolumeContext,const uint16_t *Path)
{
    static const uint16_t DefaultPath[]={
        '\\','h','i','b','e','r','f','i','l','.','s','y','s',0
    };
    const RC95LDR_VOLUME *Volume=(const RC95LDR_VOLUME*)VolumeContext;
    const uint16_t *Input=Path?Path:DefaultPath;
    char AsciiPath[260];
    size_t I=0,ComponentStart=0;
    if(!Volume||!Volume->FileSystem||
       !Volume->FileSystem->DeleteFile)return Rc95LdrUnsupported;
    if(Input[0]!='\\'&&Input[0]!='/')return Rc95LdrInvalidParameter;
    while(Input[I]){
        uint16_t C=Input[I];
        if(I>=sizeof(AsciiPath)-1u||C<0x20u||C>0x7eu||
           C==':'||C=='*'||C=='?')
            return Rc95LdrInvalidParameter;
        AsciiPath[I]=(char)C;
        if(C=='\\'||C=='/'){
            size_t N=I-ComponentStart;
            if((N==1u&&AsciiPath[ComponentStart]=='.')||
               (N==2u&&AsciiPath[ComponentStart]=='.'&&
                         AsciiPath[ComponentStart+1u]=='.'))
                return Rc95LdrInvalidParameter;
            ComponentStart=I+1u;
        }
        ++I;
    }
    if(!I||AsciiPath[I-1u]=='\\'||AsciiPath[I-1u]=='/')
        return Rc95LdrInvalidParameter;
    {
        size_t N=I-ComponentStart;
        if((N==1u&&AsciiPath[ComponentStart]=='.')||
           (N==2u&&AsciiPath[ComponentStart]=='.'&&
                     AsciiPath[ComponentStart+1u]=='.'))
            return Rc95LdrInvalidParameter;
    }
    AsciiPath[I]=0;
    return Volume->FileSystem->DeleteFile(
        Volume->FileSystem->FileSystemContext,AsciiPath);
}
