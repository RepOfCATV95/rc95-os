#include "rc95ldrwinload.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define RC95LDR_PROVIDER_PATH_MAX 1024u

static RC95LDR_STATUS
Rc95LdrWinloadConvertProviderPath(const uint16_t *Path,char *Narrow)
{
    size_t I=0,ComponentStart=1u;
    if (!Path||!Narrow||(Path[0]!='\\'&&Path[0]!='/'))
        return Rc95LdrInvalidImage;
    while (Path[I]) {
        uint16_t Ch=Path[I];
        if (I+1u>=RC95LDR_PROVIDER_PATH_MAX||Ch<0x20u||Ch>0x7eu||
            Ch==':'||Ch=='*'||Ch=='?'||Ch=='"'||Ch=='<'||
            Ch=='>'||Ch=='|')
            return Rc95LdrUnsupported;
        if (I!=0u&&(Ch=='\\'||Ch=='/')) {
            size_t N=I-ComponentStart;
            if (!N||(N==1u&&Path[ComponentStart]=='.')||
                (N==2u&&Path[ComponentStart]=='.'&&Path[ComponentStart+1u]=='.'))
                return Rc95LdrInvalidImage;
            ComponentStart=I+1u;
        }
        Narrow[I]=(Ch=='\\')?'/':(char)Ch;
        ++I;
    }
    if (I<=1u) return Rc95LdrInvalidImage;
    {
        size_t N=I-ComponentStart;
        if (!N||(N==1u&&Path[ComponentStart]=='.')||
            (N==2u&&Path[ComponentStart]=='.'&&Path[ComponentStart+1u]=='.'))
            return Rc95LdrInvalidImage;
    }
    Narrow[I]=0;
    return Rc95LdrSuccess;
}

/*
 * WINLOAD prefers the active loader-scoped provider. Platform callbacks are
 * retained only for non-scoped legacy boots.
 */
RC95LDR_STATUS
Rc95LdrWinloadFileOpen(const RC95LDR_WINLOAD_PLATFORM *Platform,
                       const uint16_t *Path, RC95LDR_WINLOAD_FILE *File)
{
    const RC95LDR_FILE_PROVIDER *Provider=Rc95LdrGetFileProvider();
    RC95LDR_STATUS Status;
    if (!Platform||!Path||!File) return Rc95LdrInvalidParameter;
    if (!Provider) {
        if (!Platform->OpenFile) return Rc95LdrUnsupported;
        return Platform->OpenFile(Platform->Context,Path,File);
    }
    if (Provider->StructureSize<sizeof(*Provider)||!Provider->Open||
        !Provider->GetFileInformation||!Provider->Read||!Provider->Seek||
        !Provider->Close) return Rc95LdrUnsupported;
    {
        char Narrow[RC95LDR_PROVIDER_PATH_MAX];
        void *Handle=NULL;
        RC95LDR_FILE_INFORMATION Information;
        Status=Rc95LdrWinloadConvertProviderPath(Path,Narrow);
        if (Status!=Rc95LdrSuccess) return Status;
        memset(File,0,sizeof(*File));
        Status=Provider->Open(Provider->VolumeContext,Narrow,&Handle);
        if (Status!=Rc95LdrSuccess) {
            if (Handle) Provider->Close(Provider->VolumeContext,Handle);
            return Status;
        }
        if (!Handle) return Rc95LdrInvalidImage;
        memset(&Information,0,sizeof(Information));
        Status=Provider->GetFileInformation(Provider->VolumeContext,
                                              Handle,&Information);
        if (Status!=Rc95LdrSuccess) {
            Provider->Close(Provider->VolumeContext,Handle);
            return Status;
        }
        File->FileContext=Handle;
        File->FileSize=Information.Size;
        return Rc95LdrSuccess;
    }
}

RC95LDR_STATUS
Rc95LdrWinloadFileReadAt(const RC95LDR_WINLOAD_PLATFORM *Platform,
                         const RC95LDR_WINLOAD_FILE *File,uint64_t Offset,
                         void *Buffer,size_t Length)
{
    const RC95LDR_FILE_PROVIDER *Provider=Rc95LdrGetFileProvider();
    if (!Platform||!File||(!Buffer&&Length)) return Rc95LdrInvalidParameter;
    if (!Provider) {
        if (!Platform->ReadFileAt) return Rc95LdrUnsupported;
        return Platform->ReadFileAt(Platform->Context,File,Offset,Buffer,Length);
    }
    if (!File||!File->FileContext||(!Buffer&&Length)||Offset>File->FileSize||
        (uint64_t)Length>File->FileSize-Offset)
        return Rc95LdrOutOfBounds;
    {
        RC95LDR_STATUS SeekStatus=Provider->Seek(
            Provider->VolumeContext,File->FileContext,Offset);
        if (SeekStatus!=Rc95LdrSuccess) return SeekStatus;
    }
    {
        size_t BytesRead=0;
        RC95LDR_STATUS Status=Provider->Read(Provider->VolumeContext,
            File->FileContext,Buffer,Length,&BytesRead);
        if (Status!=Rc95LdrSuccess) return Status;
        return BytesRead==Length?Rc95LdrSuccess:Rc95LdrIoError;
    }
}

void
Rc95LdrWinloadFileClose(const RC95LDR_WINLOAD_PLATFORM *Platform,
                        RC95LDR_WINLOAD_FILE *File)
{
    const RC95LDR_FILE_PROVIDER *Provider=Rc95LdrGetFileProvider();
    if (!File) return;
    if (!Provider) {
        if (Platform&&Platform->CloseFile)
            Platform->CloseFile(Platform->Context,File);
        return;
    }
    if (File->FileContext)
        Provider->Close(Provider->VolumeContext,File->FileContext);
    memset(File,0,sizeof(*File));
}

