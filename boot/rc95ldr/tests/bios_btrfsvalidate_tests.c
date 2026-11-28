#include "rc95ldrbiosdisk.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

typedef struct _FAKE_FILE_HANDLE {
    unsigned Kind;
} FAKE_FILE_HANDLE;

typedef struct _FAKE_FILESYSTEM {
    FAKE_FILE_HANDLE Handle;
    int CorruptKernel;
    unsigned Opens;
    unsigned Closes;
} FAKE_FILESYSTEM;

static RC95LDR_STATUS
FakeOpen(void *Opaque, const char *Path, void **Handle, uint64_t *Size)
{
    FAKE_FILESYSTEM *Fake = (FAKE_FILESYSTEM *)Opaque;
    unsigned Kind;
    if (!strcmp(Path, "/Windows/System32/winload.efi"))
        Kind = 1u;
    else if (!strcmp(Path, "/Windows/System32/ntoskrnl.exe"))
        Kind = 2u;
    else if (!strcmp(Path, "/Windows/System32/config/SYSTEM"))
        Kind = 3u;
    else
        return Rc95LdrNotFound;
    Fake->Handle.Kind = Kind;
    ++Fake->Opens;
    *Handle = &Fake->Handle;
    *Size = (Kind == 3u) ? 8192u : 16384u;
    return Rc95LdrSuccess;
}

static RC95LDR_STATUS
FakeReadAt(void *Opaque, void *Handle, uint64_t Offset,
           void *Buffer, size_t Length)
{
    FAKE_FILESYSTEM *Fake = (FAKE_FILESYSTEM *)Opaque;
    FAKE_FILE_HANDLE *File = (FAKE_FILE_HANDLE *)Handle;
    uint8_t *Bytes = (uint8_t *)Buffer;
    (void)Opaque;
    if (!File || Offset != 0 || Length > 4096u)
        return Rc95LdrInvalidParameter;
    memset(Bytes, 0, Length);
    if (File->Kind == 3u) {
        if (Length >= 4u) memcpy(Bytes, "regf", 4u);
    } else if (Length >= 0x84u) {
        Bytes[0] = 'M';
        Bytes[1] = 'Z';
        Bytes[0x3cu] = 0x80u;
        if (!(Fake->CorruptKernel && File->Kind == 2u))
            memcpy(Bytes + 0x80u, "PE\0\0", 4u);
    }
    return Rc95LdrSuccess;
}

static void
FakeClose(void *Opaque, void *Handle)
{
    FAKE_FILESYSTEM *Fake = (FAKE_FILESYSTEM *)Opaque;
    assert(Handle == &Fake->Handle);
    ++Fake->Closes;
}

int main(void)
{
    FAKE_FILESYSTEM Fake;
    RC95LDR_FILESYSTEM FileSystem;
    RC95LDR_VOLUME Volume;
    RC95LDR_BIOS_BTRFS_VALIDATION_CONTEXT Validation;
    RC95LDR_WINDOWS_INSTALLATION Installation;
    uint8_t Probe[4096];

    memset(&Fake, 0, sizeof(Fake));
    memset(&FileSystem, 0, sizeof(FileSystem));
    memset(&Volume, 0, sizeof(Volume));
    memset(&Validation, 0, sizeof(Validation));
    memset(&Installation, 0, sizeof(Installation));

    FileSystem.FileSystemContext = &Fake;
    FileSystem.OpenRead = FakeOpen;
    FileSystem.ReadAt = FakeReadAt;
    FileSystem.Close = FakeClose;
    Volume.FileSystem = &FileSystem;
    Validation.ProbeBuffer = Probe;
    Validation.ProbeCapacity = sizeof(Probe);

    {
        static const uint16_t Loader[] =
            {'\\','W','i','n','d','o','w','s','\\','S','y','s','t','e','m','3','2','\\',
             'w','i','n','l','o','a','d','.','e','f','i'};
        static const uint16_t Root[] =
            {'\\','W','i','n','d','o','w','s'};
        memcpy(Installation.LoaderPathUtf16, Loader, sizeof(Loader));
        Installation.LoaderPathCharacters = sizeof(Loader) / sizeof(Loader[0]);
        memcpy(Installation.SystemRootUtf16, Root, sizeof(Root));
        Installation.SystemRootCharacters = sizeof(Root) / sizeof(Root[0]);
    }

    assert(Rc95LdrBiosValidateBtrfsWindowsVolume(
        &Validation, &Volume, &Installation) == Rc95LdrSuccess);
    assert(Fake.Opens == 3u && Fake.Closes == 3u);

    Fake.CorruptKernel = 1;
    assert(Rc95LdrBiosValidateBtrfsWindowsVolume(
        &Validation, &Volume, &Installation) == Rc95LdrInvalidImage);
    assert(Fake.Opens == 5u && Fake.Closes == 5u);

    puts("RC95LDR BIOS Btrfs Windows validator tests passed");
    return 0;
}
