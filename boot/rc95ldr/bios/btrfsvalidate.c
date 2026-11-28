#include "include/rc95ldrbiosdisk.h"
#include <string.h>

#define BIOS_BTRFS_VALIDATE_PATH_CHARS 260u
#define BIOS_BTRFS_VALIDATE_PROBE_BYTES 4096u

static RC95LDR_STATUS
BiosBtrfsUtf16Path(const uint16_t *Source, size_t Characters,
                   char Destination[BIOS_BTRFS_VALIDATE_PATH_CHARS],
                   size_t *DestinationLength)
{
    size_t I, Out = 0;
    if (!Source || !Characters || !Destination || !DestinationLength ||
        Characters >= BIOS_BTRFS_VALIDATE_PATH_CHARS)
        return Rc95LdrInvalidParameter;

    for (I = 0; I < Characters; ++I) {
        uint16_t Ch = Source[I];
        if (!Ch || Ch < 0x20u || Ch > 0x7eu)
            return Rc95LdrUnsupported;
        Destination[Out++] = (Ch == '\\') ? '/' : (char)Ch;
    }
    Destination[Out] = '\0';
    *DestinationLength = Out;
    return Rc95LdrSuccess;
}

static RC95LDR_STATUS
BiosBtrfsBuildSystemPath(const uint16_t *Root, size_t RootCharacters,
                         const char *Suffix,
                         char Path[BIOS_BTRFS_VALIDATE_PATH_CHARS])
{
    size_t RootLength, SuffixLength, Offset;
    RC95LDR_STATUS Status = BiosBtrfsUtf16Path(
        Root, RootCharacters, Path, &RootLength);
    if (Status != Rc95LdrSuccess)
        return Status;
    SuffixLength = strlen(Suffix);
    Offset = RootLength;
    if (Offset && Path[Offset - 1u] != '/')
        Path[Offset++] = '/';
    if (SuffixLength >= BIOS_BTRFS_VALIDATE_PATH_CHARS - Offset)
        return Rc95LdrBufferTooSmall;
    memcpy(Path + Offset, Suffix, SuffixLength);
    Path[Offset + SuffixLength] = '\0';
    return Rc95LdrSuccess;
}

static RC95LDR_STATUS
BiosBtrfsReadPrefix(const RC95LDR_VOLUME *Volume,
                    RC95LDR_BIOS_BTRFS_VALIDATION_CONTEXT *Context,
                    const char *Path, size_t *ReadBytes, uint64_t *FileSize)
{
    const RC95LDR_FILESYSTEM *FileSystem;
    void *Handle = NULL;
    size_t Length;
    RC95LDR_STATUS Status;

    if (!Volume || !Volume->FileSystem || !Context || !Context->ProbeBuffer ||
        Context->ProbeCapacity < BIOS_BTRFS_VALIDATE_PROBE_BYTES ||
        !Path || !ReadBytes || !FileSize)
        return Rc95LdrInvalidParameter;
    FileSystem = Volume->FileSystem;
    if (!FileSystem->OpenRead || !FileSystem->ReadAt || !FileSystem->Close)
        return Rc95LdrUnsupported;

    *ReadBytes = 0;
    *FileSize = 0;
    Status = FileSystem->OpenRead(FileSystem->FileSystemContext, Path,
                                  &Handle, FileSize);
    if (Status != Rc95LdrSuccess)
        return Status;
    if (!Handle)
        return Rc95LdrCorruptData;
    Length = (*FileSize < Context->ProbeCapacity) ?
        (size_t)*FileSize : Context->ProbeCapacity;
    if (Length) {
        Status = FileSystem->ReadAt(FileSystem->FileSystemContext, Handle,
                                    0, Context->ProbeBuffer, Length);
        if (Status != Rc95LdrSuccess)
            goto Cleanup;
    }
    *ReadBytes = Length;
Cleanup:
    FileSystem->Close(FileSystem->FileSystemContext, Handle);
    return Status;
}

static RC95LDR_STATUS
BiosBtrfsValidatePe(const RC95LDR_VOLUME *Volume,
                    RC95LDR_BIOS_BTRFS_VALIDATION_CONTEXT *Context,
                    const char *Path)
{
    const uint8_t *Bytes;
    size_t ReadBytes;
    uint64_t FileSize;
    uint32_t PeOffset;
    RC95LDR_STATUS Status = BiosBtrfsReadPrefix(
        Volume, Context, Path, &ReadBytes, &FileSize);
    if (Status != Rc95LdrSuccess)
        return Status;
    Bytes = (const uint8_t *)Context->ProbeBuffer;
    if (FileSize < 64u || ReadBytes < 64u ||
        Bytes[0] != 'M' || Bytes[1] != 'Z')
        return Rc95LdrInvalidImage;
    PeOffset = Rc95LdrReadU32(Bytes + 0x3cu);
    if (PeOffset < 64u || PeOffset > ReadBytes ||
        ReadBytes - PeOffset < 4u ||
        Bytes[PeOffset] != 'P' || Bytes[PeOffset + 1u] != 'E' ||
        Bytes[PeOffset + 2u] != 0 || Bytes[PeOffset + 3u] != 0)
        return Rc95LdrInvalidImage;
    return Rc95LdrSuccess;
}

RC95LDR_STATUS
Rc95LdrBiosValidateBtrfsWindowsVolume(
    void *Opaque, const RC95LDR_VOLUME *Volume,
    const RC95LDR_WINDOWS_INSTALLATION *Installation)
{
    static const char KernelSuffix[] = "System32/ntoskrnl.exe";
    static const char HiveSuffix[] = "System32/config/SYSTEM";
    RC95LDR_BIOS_BTRFS_VALIDATION_CONTEXT *Context =
        (RC95LDR_BIOS_BTRFS_VALIDATION_CONTEXT *)Opaque;
    char Path[BIOS_BTRFS_VALIDATE_PATH_CHARS];
    size_t ReadBytes, LoaderLength;
    uint64_t FileSize;
    const uint8_t *Bytes;
    RC95LDR_STATUS Status;

    if (!Context || !Volume || !Volume->FileSystem || !Installation ||
        !Installation->LoaderPathCharacters ||
        Installation->LoaderPathCharacters >= BIOS_BTRFS_VALIDATE_PATH_CHARS ||
        !Installation->SystemRootCharacters ||
        Installation->SystemRootCharacters >= BIOS_BTRFS_VALIDATE_PATH_CHARS)
        return Rc95LdrInvalidParameter;

    Status = BiosBtrfsUtf16Path(Installation->LoaderPathUtf16,
        Installation->LoaderPathCharacters, Path, &LoaderLength);
    if (Status != Rc95LdrSuccess)
        return Status;
    if (!LoaderLength)
        return Rc95LdrInvalidImage;
    Status = BiosBtrfsValidatePe(Volume, Context, Path);
    if (Status != Rc95LdrSuccess)
        return Status;

    Status = BiosBtrfsBuildSystemPath(Installation->SystemRootUtf16,
        Installation->SystemRootCharacters, KernelSuffix, Path);
    if (Status != Rc95LdrSuccess)
        return Status;
    Status = BiosBtrfsValidatePe(Volume, Context, Path);
    if (Status != Rc95LdrSuccess)
        return Status;

    Status = BiosBtrfsBuildSystemPath(Installation->SystemRootUtf16,
        Installation->SystemRootCharacters, HiveSuffix, Path);
    if (Status != Rc95LdrSuccess)
        return Status;
    Status = BiosBtrfsReadPrefix(Volume, Context, Path, &ReadBytes, &FileSize);
    if (Status != Rc95LdrSuccess)
        return Status;
    Bytes = (const uint8_t *)Context->ProbeBuffer;
    if (FileSize < 4096u || ReadBytes < 4u ||
        Bytes[0] != 'r' || Bytes[1] != 'e' ||
        Bytes[2] != 'g' || Bytes[3] != 'f')
        return Rc95LdrInvalidImage;
    return Rc95LdrSuccess;
}
