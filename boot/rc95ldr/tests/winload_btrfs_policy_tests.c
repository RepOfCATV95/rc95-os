#include "rc95ldrwinload.h"
#include <stdio.h>
#include <string.h>

static RC95LDR_STATUS TestOpenFile(void *Context, const uint16_t *Path,
                                  RC95LDR_WINLOAD_FILE *File)
{
    (void)Context; (void)Path; (void)File;
    return Rc95LdrUnsupported;
}

static RC95LDR_STATUS TestReadFileAt(void *Context,
    const RC95LDR_WINLOAD_FILE *File, uint64_t Offset, void *Buffer, size_t Length)
{
    (void)Context; (void)File; (void)Offset; (void)Buffer; (void)Length;
    return Rc95LdrUnsupported;
}

static void TestCloseFile(void *Context, RC95LDR_WINLOAD_FILE *File)
{
    (void)Context; (void)File;
}
typedef struct _TEST_PROVIDER_CONTEXT {
    char Path[128];
    const char *Data;
    size_t Length;
    size_t Offset;
    unsigned Opens,Closes;
    int FailOpenWithHandle,FailInformation,FailSeek,ShortRead;
} TEST_PROVIDER_CONTEXT;
static RC95LDR_STATUS ProviderOpen(void *Context,const char *Path,void **Handle)
{
    TEST_PROVIDER_CONTEXT *T=Context;
    size_t I=0;
    while(Path[I]&&I+1u<sizeof(T->Path)){T->Path[I]=Path[I];++I;}
    if(Path[I])return Rc95LdrOutOfBounds;
    T->Path[I]=0;T->Opens++;T->Offset=0;*Handle=T;
    return T->FailOpenWithHandle?Rc95LdrIoError:Rc95LdrSuccess;
}
static RC95LDR_STATUS ProviderInfo(void *Context,void *Handle,
                                   RC95LDR_FILE_INFORMATION *Info)
{
    TEST_PROVIDER_CONTEXT *T=Context;
    if(Handle!=T||T->FailInformation)return Rc95LdrIoError;
    Info->Size=T->Length;Info->Attributes=0;return Rc95LdrSuccess;
}
static RC95LDR_STATUS ProviderRead(void *Context,void *Handle,void *Buffer,
                                   size_t Length,size_t *Read)
{
    TEST_PROVIDER_CONTEXT *T=Context;
    size_t N=Length;
    if(Handle!=T||!Read||(!Buffer&&Length))return Rc95LdrInvalidParameter;
    if(T->ShortRead&&N)N--;
    if(T->Offset>T->Length||N>T->Length-T->Offset)return Rc95LdrOutOfBounds;
    if(N)memcpy(Buffer,T->Data+T->Offset,N);
    T->Offset+=N;*Read=N;return Rc95LdrSuccess;
}
static RC95LDR_STATUS ProviderSeek(void *Context,void *Handle,uint64_t Offset)
{
    TEST_PROVIDER_CONTEXT *T=Context;
    if(Handle!=T)return Rc95LdrInvalidParameter;
    if(T->FailSeek)return Rc95LdrIoError;
    if(Offset>T->Length)return Rc95LdrOutOfBounds;
    T->Offset=(size_t)Offset;return Rc95LdrSuccess;
}
static void ProviderClose(void *Context,void *Handle)
{
    TEST_PROVIDER_CONTEXT *T=Context;
    if(Handle==T)T->Closes++;
}
int main(void)
{
    RC95LDR_WINLOAD_PLATFORM Platform;
    RC95LDR_FILE_PROVIDER Provider;
    RC95LDR_FILE_PROVIDER_SCOPE Scope;
    RC95LDR_STATUS Status;
    TEST_PROVIDER_CONTEXT TestProvider;

    memset(&Platform, 0, sizeof(Platform));
    Platform.StructureSize = sizeof(Platform);
    Platform.Version = RC95LDR_WINLOAD_ABI_VERSION;

    Platform.BootFileSystemKind = Rc95LdrWinloadFsFat32;
    if (Rc95LdrWinloadValidateFilesystemProvider(&Platform) != Rc95LdrSuccess)
        return 1;
    Platform.BootFileSystemKind = Rc95LdrWinloadFsNtfs;
    if (Rc95LdrWinloadValidateFilesystemProvider(&Platform) != Rc95LdrSuccess)
        return 2;

    Platform.BootFileSystemKind = Rc95LdrWinloadFsBtrfs;
    Platform.BootFileSystemCapabilities = 0;
    Status = Rc95LdrWinloadValidateFilesystemProvider(&Platform);
    if (Status != Rc95LdrUnsupported) return 3;

    Platform.BootFileSystemCapabilities = RC95LDR_WINLOAD_FS_CAP_READ_ONLY;
    if (Rc95LdrWinloadValidateFilesystemProvider(&Platform) != Rc95LdrUnsupported)
        return 4;

    Platform.BootFileSystemCapabilities =
        RC95LDR_WINLOAD_FS_CAP_READ_ONLY |
        RC95LDR_WINLOAD_FS_CAP_BTRFS_NATIVE;
    /* Native/read-only flags without an actual bound file provider are not
       sufficient to authorize WINLOAD to open Btrfs boot files. */
    if (Rc95LdrWinloadValidateFilesystemProvider(&Platform) != Rc95LdrUnsupported)
        return 5;
    Platform.Context = &Platform;
    Platform.OpenFile = TestOpenFile;
    Platform.ReadFileAt = TestReadFileAt;
    Platform.CloseFile = TestCloseFile;
    if (Rc95LdrWinloadValidateFilesystemProvider(&Platform) != Rc95LdrUnsupported)
        return 10; /* Platform callbacks without the scoped volume must fail. */
    memset(&TestProvider,0,sizeof(TestProvider));
    TestProvider.Data="abcdef";TestProvider.Length=6;
    memset(&Provider,0,sizeof(Provider));
    Provider.StructureSize=sizeof(Provider); Provider.VolumeContext=&TestProvider;
    Provider.Open=ProviderOpen; Provider.GetFileInformation=ProviderInfo;
    Provider.Read=ProviderRead; Provider.Seek=ProviderSeek; Provider.Close=ProviderClose;
    memset(&Scope,0,sizeof(Scope));
    if (Rc95LdrBeginFileProviderScope(&Provider,&Scope)!=Rc95LdrSuccess)
        return 11;
    if (Rc95LdrWinloadValidateFilesystemProvider(&Platform)!=Rc95LdrSuccess)
        return 12;
    {
        static const uint16_t GoodPath[]={'\\','W','i','n','d','o','w','s','\\',
            'n','t','o','s','k','r','n','l','.','e','x','e',0};
        static const uint16_t BadPath[]={'\\','W','i','n','d','o','w','s',':',
            'x',0};
        static const uint16_t TraversalPath[]={'\\','W','i','n','d','o','w','s','\\',
            '.','.','\\','b','o','o','t',0};
        static const uint16_t RepeatedSeparatorPath[]={'\\','W','i','n','d','o','w','s','\\',
            '\\','n','t','o','s','k','r','n','l',0};
        static const uint16_t TrailingSeparatorPath[]={'\\','W','i','n','d','o','w','s','\\',0};
        RC95LDR_WINLOAD_FILE File;
        char Data[8]={0};
        memset(&File,0,sizeof(File));
        if(Rc95LdrWinloadFileOpen(&Platform,GoodPath,&File)!=Rc95LdrSuccess||
           strcmp(TestProvider.Path,"/Windows/ntoskrnl.exe")||File.FileSize!=6)
            return 15;
        if(Rc95LdrWinloadFileReadAt(&Platform,&File,2,Data,3)!=Rc95LdrSuccess||
           memcmp(Data,"cde",3))return 16;
        if(Rc95LdrWinloadFileReadAt(&Platform,&File,5,Data,2)!=Rc95LdrOutOfBounds)
            return 17;
        TestProvider.FailSeek=1;
        if(Rc95LdrWinloadFileReadAt(&Platform,&File,0,Data,1)!=Rc95LdrIoError)
            return 18;
        TestProvider.FailSeek=0;TestProvider.ShortRead=1;
        if(Rc95LdrWinloadFileReadAt(&Platform,&File,0,Data,2)!=Rc95LdrIoError)
            return 19;
        TestProvider.ShortRead=0;
        Rc95LdrWinloadFileClose(&Platform,&File);
        if(File.FileContext||TestProvider.Opens!=1||TestProvider.Closes!=1)
            return 20;
        if(Rc95LdrWinloadFileOpen(&Platform,BadPath,&File)!=Rc95LdrUnsupported)
            return 21;
        if(Rc95LdrWinloadFileOpen(&Platform,TraversalPath,&File)!=Rc95LdrInvalidImage||
           Rc95LdrWinloadFileOpen(&Platform,RepeatedSeparatorPath,&File)!=Rc95LdrInvalidImage||
           Rc95LdrWinloadFileOpen(&Platform,TrailingSeparatorPath,&File)!=Rc95LdrInvalidImage||
           TestProvider.Opens!=1)return 23;
        TestProvider.FailOpenWithHandle=1;
        if(Rc95LdrWinloadFileOpen(&Platform,GoodPath,&File)!=Rc95LdrIoError||
           TestProvider.Closes!=2)return 24;
        TestProvider.FailOpenWithHandle=0;
        TestProvider.FailInformation=1;
        if(Rc95LdrWinloadFileOpen(&Platform,GoodPath,&File)!=Rc95LdrIoError||
           TestProvider.Closes!=3)return 22;
        TestProvider.FailInformation=0;
    }
    if (Rc95LdrEndFileProviderScope(&Scope)!=Rc95LdrSuccess)
        return 13;
    if (Rc95LdrWinloadValidateFilesystemProvider(&Platform)!=Rc95LdrUnsupported)
        return 14;
    Platform.BootFileSystemCapabilities |= 0x80000000u;
    if (Rc95LdrWinloadValidateFilesystemProvider(&Platform) != Rc95LdrUnsupported)
        return 6;
    Platform.BootFileSystemCapabilities =
        RC95LDR_WINLOAD_FS_CAP_READ_ONLY |
        RC95LDR_WINLOAD_FS_CAP_BTRFS_NATIVE;

    Platform.BootFileSystemKind = 0xffffffffu;
    if (Rc95LdrWinloadValidateFilesystemProvider(&Platform) != Rc95LdrInvalidParameter)
        return 7;

    Platform.BootFileSystemKind = Rc95LdrWinloadFsBtrfs;
    Platform.StructureSize =
        (uint32_t)offsetof(RC95LDR_WINLOAD_PLATFORM, BootFileSystemCapabilities);
    if (Rc95LdrWinloadValidateFilesystemProvider(&Platform) != Rc95LdrUnsupported)
        return 8;

    Platform.StructureSize =
        (uint32_t)offsetof(RC95LDR_WINLOAD_PLATFORM, BootFileSystemKind);
    if (Rc95LdrWinloadValidateFilesystemProvider(&Platform) != Rc95LdrSuccess)
        return 9;

    puts("RC95LDR WINLOAD Btrfs provider policy tests passed");
    return 0;
}
