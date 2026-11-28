#include "rc95ldr.h"
#include <string.h>

typedef struct _TEST_FILE
{
    uint64_t Position;
    unsigned CloseCount;
} TEST_FILE;

static const uint8_t FileBytes[] = { 0x52u, 0x43u, 0x39u, 0x35u };
static TEST_FILE FileState;

static RC95LDR_STATUS TestOpen(void *Context, const char *Path, void **Handle)
{
    if (Context == NULL || Path == NULL || strcmp(Path, "\\boot\\winload.exe") != 0 || Handle == NULL)
        return Rc95LdrInvalidParameter;
    FileState.Position = 0;
    *Handle = &FileState;
    return Rc95LdrSuccess;
}
static RC95LDR_STATUS TestInformation(void *Context, void *Handle, RC95LDR_FILE_INFORMATION *Info)
{
    if (Context == NULL || Handle != &FileState || Info == NULL)
        return Rc95LdrInvalidParameter;
    Info->Size = sizeof(FileBytes);
    Info->Attributes = 1u;
    return Rc95LdrSuccess;
}
static RC95LDR_STATUS TestRead(void *Context, void *Handle, void *Buffer, size_t Length, size_t *Read)
{
    size_t Available;
    if (Context == NULL || Handle != &FileState || Buffer == NULL || Read == NULL)
        return Rc95LdrInvalidParameter;
    if (FileState.Position > sizeof(FileBytes))
        return Rc95LdrOutOfBounds;
    Available = sizeof(FileBytes) - (size_t)FileState.Position;
    if (Length > Available) Length = Available;
    memcpy(Buffer, FileBytes + (size_t)FileState.Position, Length);
    FileState.Position += Length;
    *Read = Length;
    return Rc95LdrSuccess;
}
static RC95LDR_STATUS TestSeek(void *Context, void *Handle, uint64_t Offset)
{
    if (Context == NULL || Handle != &FileState || Offset > sizeof(FileBytes))
        return Rc95LdrInvalidParameter;
    FileState.Position = Offset;
    return Rc95LdrSuccess;
}
static void TestClose(void *Context, void *Handle)
{
    if (Context != NULL && Handle == &FileState)
        ++FileState.CloseCount;
}

int main(void)
{
    int Context = 1;
    uint16_t DriverName[] = { 'B', 'T', 'R', 'F', 'S', 0 };
    RC95LDR_FILE_PROVIDER Provider;
    RC95LDR_FILE_PROVIDER InvalidProvider;
    RC95LDR_FILE_PROVIDER_SCOPE Scope;
    RC95LDR_FILE_PROVIDER_SCOPE InvalidScope;
    RC95LDR_FILE_INFORMATION Info;
    void *Handle = NULL;
    uint8_t Buffer[sizeof(FileBytes)];
    size_t Read = 0;

    memset(&Provider, 0, sizeof(Provider));
    Provider.StructureSize = sizeof(Provider);
    Provider.VolumeContext = &Context;
    Provider.Open = TestOpen;
    Provider.GetFileInformation = TestInformation;
    Provider.Read = TestRead;
    Provider.Seek = TestSeek;
    Provider.Close = TestClose;
    memset(&Scope, 0, sizeof(Scope));
    memset(&InvalidScope, 0, sizeof(InvalidScope));

    if (Rc95LdrBeginFileProviderScope(&Provider, &Scope) != Rc95LdrSuccess) return 1;
    if (Rc95LdrGetFileProvider() != &Provider) return 2;
    if (Rc95LdrBeginFileProviderScope(&Provider, &InvalidScope) != Rc95LdrUnsupported) return 3;
    if (Rc95LdrSetFileSystemDriverName(&Provider, DriverName) != Rc95LdrSuccess) return 4;
    if (Rc95LdrGetFileSystemDriverName() != DriverName) return 5;
    if (Rc95LdrSetFileSystemDriverName(&Provider, DriverName) != Rc95LdrInvalidParameter) return 6;

    if (Provider.Open(Provider.VolumeContext, "\\boot\\winload.exe", &Handle) != Rc95LdrSuccess) return 7;
    if (Provider.GetFileInformation(Provider.VolumeContext, Handle, &Info) != Rc95LdrSuccess ||
        Info.Size != sizeof(FileBytes)) return 8;
    if (Provider.Read(Provider.VolumeContext, Handle, Buffer, sizeof(Buffer), &Read) != Rc95LdrSuccess ||
        Read != sizeof(FileBytes) || memcmp(Buffer, FileBytes, sizeof(Buffer)) != 0) return 9;
    if (Provider.Seek(Provider.VolumeContext, Handle, 0) != Rc95LdrSuccess) return 10;
    Provider.Close(Provider.VolumeContext, Handle);
    if (FileState.CloseCount != 1u) return 11;

    if (Rc95LdrEndFileProviderScope(&Scope) != Rc95LdrSuccess) return 12;
    if (Rc95LdrGetFileProvider() != NULL || Rc95LdrGetFileSystemDriverName() != NULL) return 13;
    if (Rc95LdrEndFileProviderScope(&Scope) != Rc95LdrInvalidParameter) return 14;

    /* The loader-facing provider vtable is read-only by construction: it has
       no write, truncate, unlink, or metadata mutation entry point. */
    InvalidProvider = Provider;
    InvalidProvider.Open = NULL;
    if (Rc95LdrBeginFileProviderScope(&InvalidProvider, &InvalidScope) != Rc95LdrInvalidParameter) return 15;
    return 0;
}
