#include "rc95ldr_volumeprovider.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct _FAKE_FS { const char *Data; size_t Length; unsigned Opens, Closes; int FailOpenAfterAllocation, FailRead; } FAKE_FS;
typedef struct _FAKE_FILE { size_t Length; } FAKE_FILE;
static RC95LDR_STATUS OpenRead(void *Opaque,const char *Path,void **Handle,uint64_t *Size)
{
    FAKE_FS *Fs=Opaque; FAKE_FILE *File;
    if(strcmp(Path,"bootmgr")!=0) return Rc95LdrNotFound;
    File=(FAKE_FILE*)malloc(sizeof(*File)); if(!File)return Rc95LdrOutOfMemory;
    File->Length=Fs->Length; *Handle=File; *Size=Fs->Length; Fs->Opens++;
    return Fs->FailOpenAfterAllocation?Rc95LdrIoError:Rc95LdrSuccess;
}
static RC95LDR_STATUS ReadAt(void *Opaque,void *Handle,uint64_t Offset,void *Buffer,size_t Length)
{
    FAKE_FS *Fs=Opaque; FAKE_FILE *File=Handle;
    if(Offset>File->Length||Length>File->Length-Offset)return Rc95LdrOutOfBounds;
    if(Fs->FailRead)return Rc95LdrIoError;
    memcpy(Buffer,Fs->Data+(size_t)Offset,Length); return Rc95LdrSuccess;
}
static void Close(void *Opaque,void *Handle)
{ FAKE_FS *Fs=Opaque; free(Handle); Fs->Closes++; }
int main(void)
{
    static const char Data[]="RC95LDR native provider";
    FAKE_FS Fake={Data,sizeof(Data)-1,0,0,0,0};
    RC95LDR_FILESYSTEM Fs={&Fake,OpenRead,ReadAt,Close};
    RC95LDR_VOLUME Volume={NULL,&Fs};
    RC95LDR_VOLUME_PROVIDER_CONTEXT Context;
    RC95LDR_FILE_PROVIDER Provider;
    RC95LDR_FILE_INFORMATION Info;
    void *Handle=NULL;
    char Buffer[32]={0};
    size_t Read=0;
    if(Rc95LdrInitializeVolumeFileProvider(&Volume,&Context,&Provider)!=Rc95LdrSuccess)return 1;
    /* OpenRead can fail after acquiring a filesystem handle. The volume
       provider must close that partial handle and keep its public handle null. */
    Fake.FailOpenAfterAllocation=1;
    Handle=(void*)1;
    if(Provider.Open(Provider.VolumeContext,"bootmgr",&Handle)!=Rc95LdrIoError||
       Handle!=NULL||Fake.Opens!=1||Fake.Closes!=1)return 19;
    Fake.FailOpenAfterAllocation=0;
    /* Keep the established success-path accounting assertions independent
       from the deliberate partial-open failure above. */
    Fake.Opens=Fake.Closes=0;
    if(Provider.Open(Provider.VolumeContext,"bootmgr",&Handle)!=Rc95LdrSuccess)return 2;
    if(Provider.GetFileInformation(Provider.VolumeContext,Handle,&Info)!=Rc95LdrSuccess||
       Info.Size!=sizeof(Data)-1)return 3;
    if(Provider.Read(Provider.VolumeContext,Handle,Buffer,7,&Read)!=Rc95LdrSuccess||
       Read!=7||memcmp(Buffer,"RC95LDR",7)!=0)return 4;
    /* A failed backing read must report zero bytes and leave the logical
       offset unchanged so the same range can be retried. */
    if(Provider.Seek(Provider.VolumeContext,Handle,0)!=Rc95LdrSuccess)return 20;
    memset(Buffer,0xA5,sizeof(Buffer));
    Read=(size_t)-1;
    Fake.FailRead=1;
    if(Provider.Read(Provider.VolumeContext,Handle,Buffer,7,&Read)!=Rc95LdrIoError||
       Read!=0||Buffer[0]!=(char)0xA5)return 21;
    Fake.FailRead=0;
    if(Provider.Read(Provider.VolumeContext,Handle,Buffer,7,&Read)!=Rc95LdrSuccess||
       Read!=7||memcmp(Buffer,"RC95LDR",7)!=0)return 22;
    if(Provider.Seek(Provider.VolumeContext,Handle,8)!=Rc95LdrSuccess)return 5;
    if(Provider.Read(Provider.VolumeContext,Handle,Buffer,6,&Read)!=Rc95LdrSuccess||
       Read!=6||memcmp(Buffer,"native",6)!=0)return 6;
    if(Provider.Seek(Provider.VolumeContext,Handle,Info.Size+1)!=Rc95LdrOutOfBounds)return 7;
    Provider.Close(Provider.VolumeContext,Handle);
    if(Fake.Opens!=1||Fake.Closes!=1)return 8;
    if(Provider.GetFileInformation(Provider.VolumeContext,Handle,&Info)!=Rc95LdrInvalidParameter)return 9;
    {
        void *NewHandle=NULL;
        if(Provider.Open(Provider.VolumeContext,"bootmgr",&NewHandle)!=Rc95LdrSuccess)return 10;
        if(NewHandle==Handle)return 11;
        if(Provider.GetFileInformation(Provider.VolumeContext,Handle,&Info)!=Rc95LdrInvalidParameter)return 12;
        /* A stale generation must not read, seek, or close the newly opened
           file that reused the same provider slot. */
        Read=0;
        if(Provider.Read(Provider.VolumeContext,Handle,Buffer,1,&Read)!=Rc95LdrInvalidParameter)return 23;
        if(Provider.Seek(Provider.VolumeContext,Handle,0)!=Rc95LdrInvalidParameter)return 24;
        Provider.Close(Provider.VolumeContext,Handle);
        if(Provider.GetFileInformation(Provider.VolumeContext,NewHandle,&Info)!=Rc95LdrSuccess||
           Info.Size!=sizeof(Data)-1)return 25;
        if(Provider.Read(Provider.VolumeContext,NewHandle,Buffer,7,&Read)!=Rc95LdrSuccess||
           Read!=7||memcmp(Buffer,"RC95LDR",7)!=0)return 26;
        Provider.Close(Provider.VolumeContext,NewHandle);
    }
    if(Fake.Opens!=2||Fake.Closes!=2)return 13;

    /* Empty files are valid filesystem objects and must open with size zero. */
    Fake.Length=0;
    if(Rc95LdrInitializeVolumeFileProvider(&Volume,&Context,&Provider)!=Rc95LdrSuccess)return 14;
    Handle=NULL;
    if(Provider.Open(Provider.VolumeContext,"bootmgr",&Handle)!=Rc95LdrSuccess)return 15;
    if(Provider.GetFileInformation(Provider.VolumeContext,Handle,&Info)!=Rc95LdrSuccess||
       Info.Size!=0)return 16;
    if(Provider.Read(Provider.VolumeContext,Handle,Buffer,0,&Read)!=Rc95LdrSuccess||
       Read!=0)return 17;
    Provider.Close(Provider.VolumeContext,Handle);
    if(Fake.Opens!=3||Fake.Closes!=3)return 18;
    puts("RC95LDR volume provider tests passed");
    return 0;
}
