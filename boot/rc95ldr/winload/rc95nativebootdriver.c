#include "include/rc95ldrrc95nativeprovider.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define RC95_NATIVE_DRIVER_PATH_CHARS 320u

static int
DriverAsciiEqualInsensitive(const char *A, const char *B, size_t Length)
{
    size_t I;
    for (I = 0; I < Length; ++I) {
        unsigned char X = (unsigned char)A[I];
        unsigned char Y = (unsigned char)B[I];
        if (X >= 'A' && X <= 'Z') X = (unsigned char)(X + ('a' - 'A'));
        if (Y >= 'A' && Y <= 'Z') Y = (unsigned char)(Y + ('a' - 'A'));
        if (X != Y) return 0;
    }
    return 1;
}

static int
DriverAppend(char *Destination, size_t Capacity, size_t *Length,
             const char *Source, size_t SourceLength)
{
    if (!Destination || !Length || (!Source && SourceLength) ||
        *Length >= Capacity || SourceLength >= Capacity - *Length)
        return 0;
    memcpy(Destination + *Length, Source, SourceLength);
    *Length += SourceLength;
    Destination[*Length] = '\0';
    return 1;
}

static int
DriverBuildPaths(const RC95LDR_WINDOWS_INSTALLATION *Installation,
                 const RC95LDR_HIVE_BOOT_DRIVER *Driver,
                 char *ProviderPath, size_t ProviderCapacity,
                 uint16_t *PePath, size_t PeCapacity,
                 char *StablePath, size_t StableCapacity)
{
    char Root[RC95LDR_MAX_SYSTEM_ROOT_CHARS + 1u];
    char Image[RC95LDR_HIVE_MAX_NAME_CHARS + 1u];
    size_t RootLength = 0, ImageLength = 0, I, OutLength = 0;
    size_t SuffixOffset = 0;
    static const char SystemRootPrefix[] = "\\SystemRoot\\";
    static const char PercentRootPrefix[] = "%SystemRoot%\\";
    static const char StablePrefix[] = "\\SystemRoot";

    if (!Installation || !Driver || !ProviderPath || !PePath ||
        !StablePath || !ProviderCapacity || !PeCapacity || !StableCapacity ||
        !Installation->SystemRootUtf16[0])
        return 0;

    while (RootLength < RC95LDR_MAX_SYSTEM_ROOT_CHARS &&
           Installation->SystemRootUtf16[RootLength]) {
        uint16_t Ch = Installation->SystemRootUtf16[RootLength];
        if (Ch > 0x7fu || Ch < 0x20u || Ch == ':' || Ch == '/' ||
            Ch == '%' || Ch == '*' || Ch == '?')
            return 0;
        Root[RootLength] = (char)Ch;
        ++RootLength;
    }
    if (!RootLength || RootLength >= RC95LDR_MAX_SYSTEM_ROOT_CHARS ||
        Root[0] != '\\')
        return 0;
    Root[RootLength] = '\0';
    while (RootLength > 1u && Root[RootLength - 1u] == '\\')
        Root[--RootLength] = '\0';

    while (ImageLength < RC95LDR_HIVE_MAX_NAME_CHARS &&
           Driver->ImagePath[ImageLength]) {
        unsigned char Ch = (unsigned char)Driver->ImagePath[ImageLength];
        if (Ch < 0x20u || Ch == 0x7fu || Ch == ':' || Ch == '/' || Ch == '*' || Ch == '?')
            return 0;
        Image[ImageLength] = (char)Ch;
        ++ImageLength;
    }
    if (!ImageLength || ImageLength >= RC95LDR_HIVE_MAX_NAME_CHARS)
        return 0;
    Image[ImageLength] = '\0';

    if (ImageLength >= sizeof(SystemRootPrefix) - 1u &&
        DriverAsciiEqualInsensitive(Image, SystemRootPrefix,
                                    sizeof(SystemRootPrefix) - 1u)) {
        SuffixOffset = sizeof(SystemRootPrefix) - 1u;
    } else if (ImageLength >= sizeof(PercentRootPrefix) - 1u &&
        DriverAsciiEqualInsensitive(Image, PercentRootPrefix,
                                    sizeof(PercentRootPrefix) - 1u)) {
        SuffixOffset = sizeof(PercentRootPrefix) - 1u;
    } else if (Image[0] == '\\') {
        if (ImageLength <= RootLength ||
            !DriverAsciiEqualInsensitive(Image, Root, RootLength) ||
            Image[RootLength] != '\\')
            return 0;
        SuffixOffset = RootLength + 1u;
    }

    ProviderPath[0] = '\0';
    if (!DriverAppend(ProviderPath, ProviderCapacity, &OutLength,
                      Root, RootLength))
        return 0;
    if (SuffixOffset) {
        if (SuffixOffset >= ImageLength)
            return 0;
        if (Image[SuffixOffset] == '\\') ++SuffixOffset;
        if (!DriverAppend(ProviderPath, ProviderCapacity, &OutLength,
                          "\\", 1u) ||
            !DriverAppend(ProviderPath, ProviderCapacity, &OutLength,
                          Image + SuffixOffset, ImageLength - SuffixOffset))
            return 0;
    } else {
        if (Image[0] == '\\') return 0;
        if (!DriverAppend(ProviderPath, ProviderCapacity, &OutLength,
                          "\\", 1u) ||
            !DriverAppend(ProviderPath, ProviderCapacity, &OutLength,
                          Image, ImageLength))
            return 0;
    }

    /* Reject traversal and empty path components before opening the file. */
    for (I = 0; I < OutLength; ) {
        size_t Start;
        while (I < OutLength && ProviderPath[I] == '\\') ++I;
        Start = I;
        while (I < OutLength && ProviderPath[I] != '\\') ++I;
        if (I - Start == 2u && ProviderPath[Start] == '.' &&
            ProviderPath[Start + 1u] == '.')
            return 0;
        if (I - Start == 1u && ProviderPath[Start] == '.')
            return 0;
    }

    if (OutLength + 1u > PeCapacity) return 0;
    for (I = 0; I <= OutLength; ++I)
        PePath[I] = (uint16_t)(unsigned char)ProviderPath[I];

    OutLength = 0;
    StablePath[0] = '\0';
    if (!DriverAppend(StablePath, StableCapacity, &OutLength,
                      StablePrefix, sizeof(StablePrefix) - 1u))
        return 0;
    if (ProviderPath[RootLength] &&
        !DriverAppend(StablePath, StableCapacity, &OutLength,
                      ProviderPath + RootLength,
                      strlen(ProviderPath + RootLength)))
        return 0;
    return 1;
}

static RC95LDR_STATUS
DriverOpenFile(void *Opaque, const uint16_t *Path, RC95LDR_WINLOAD_FILE *File)
{
    RC95LDR_RC95_NATIVE_PROVIDER_RESULT *Result =
        (RC95LDR_RC95_NATIVE_PROVIDER_RESULT *)Opaque;
    char Narrow[RC95_NATIVE_DRIVER_PATH_CHARS];
    size_t I = 0;
    void *Handle = NULL;
    RC95LDR_FILE_INFORMATION Information;
    RC95LDR_STATUS Status;

    if (!Result || !Result->BootFileProvider || !Path || !File ||
        !Result->BootFileProvider->Open ||
        !Result->BootFileProvider->GetFileInformation)
        return Rc95LdrInvalidParameter;
    while (I + 1u < sizeof(Narrow) && Path[I]) {
        if (Path[I] > 0x7fu || Path[I] < 0x20u)
            return Rc95LdrUnsupported;
        Narrow[I] = (char)Path[I];
        ++I;
    }
    if (!I || I + 1u >= sizeof(Narrow) || Path[I])
        return Rc95LdrOutOfBounds;
    Narrow[I] = '\0';
    Status = Result->BootFileProvider->Open(
        Result->BootFileProvider->VolumeContext, Narrow, &Handle);
    if (Status != Rc95LdrSuccess) return Status;
    if (!Handle) return Rc95LdrInvalidImage;
    memset(&Information, 0, sizeof(Information));
    Status = Result->BootFileProvider->GetFileInformation(
        Result->BootFileProvider->VolumeContext, Handle, &Information);
    if (Status != Rc95LdrSuccess || !Information.Size) {
        Result->BootFileProvider->Close(
            Result->BootFileProvider->VolumeContext, Handle);
        return Status == Rc95LdrSuccess ? Rc95LdrInvalidImage : Status;
    }
    File->FileContext = Handle;
    File->FileSize = Information.Size;
    return Rc95LdrSuccess;
}

static RC95LDR_STATUS
DriverReadFileAt(void *Opaque, const RC95LDR_WINLOAD_FILE *File,
                 uint64_t Offset, void *Buffer, size_t Length)
{
    RC95LDR_RC95_NATIVE_PROVIDER_RESULT *Result =
        (RC95LDR_RC95_NATIVE_PROVIDER_RESULT *)Opaque;
    size_t Remaining = Length;
    RC95LDR_STATUS Status;
    if (!Result || !Result->BootFileProvider || !File ||
        !File->FileContext || (!Buffer && Length) ||
        !Result->BootFileProvider->Seek || !Result->BootFileProvider->Read ||
        Offset > File->FileSize || (uint64_t)Length > File->FileSize - Offset)
        return Rc95LdrInvalidParameter;
    Status = Result->BootFileProvider->Seek(
        Result->BootFileProvider->VolumeContext, File->FileContext, Offset);
    if (Status != Rc95LdrSuccess) return Status;
    while (Remaining) {
        size_t BytesRead = 0;
        RC95LDR_STATUS Status = Result->BootFileProvider->Read(
            Result->BootFileProvider->VolumeContext, File->FileContext,
            (uint8_t *)Buffer + (Length - Remaining), Remaining, &BytesRead);
        if (Status != Rc95LdrSuccess) return Status;
        if (!BytesRead || BytesRead > Remaining)
            return Rc95LdrCorruptData;
        Remaining -= BytesRead;
    }
    return Rc95LdrSuccess;
}

static void
DriverCloseFile(void *Opaque, RC95LDR_WINLOAD_FILE *File)
{
    RC95LDR_RC95_NATIVE_PROVIDER_RESULT *Result =
        (RC95LDR_RC95_NATIVE_PROVIDER_RESULT *)Opaque;
    if (Result && Result->BootFileProvider && File && File->FileContext &&
        Result->BootFileProvider->Close)
        Result->BootFileProvider->Close(
            Result->BootFileProvider->VolumeContext, File->FileContext);
    if (File) memset(File, 0, sizeof(*File));
}

static RC95LDR_STATUS
DriverAllocatePages(void *Opaque, uint64_t Bytes, uint64_t Alignment,
                    uint64_t *PhysicalAddress)
{
    RC95LDR_RC95_NATIVE_PROVIDER_RESULT *Result = Opaque;
    const RC95LDR_WINLOAD_PLATFORM *Platform =
        Result ? Result->WinloadPlatform : NULL;
    if (!Platform || !Platform->AllocatePages) return Rc95LdrUnsupported;
    return Platform->AllocatePages(Platform->Context, Bytes, Alignment,
                                    PhysicalAddress);
}

static void
DriverFreePages(void *Opaque, uint64_t PhysicalAddress, uint64_t Bytes)
{
    RC95LDR_RC95_NATIVE_PROVIDER_RESULT *Result = Opaque;
    const RC95LDR_WINLOAD_PLATFORM *Platform =
        Result ? Result->WinloadPlatform : NULL;
    if (Platform && Platform->FreePages)
        Platform->FreePages(Platform->Context, PhysicalAddress, Bytes);
}

static RC95LDR_STATUS
DriverValidateAllocation(void *Opaque, uint64_t VirtualBase,
                         uint64_t PhysicalBase, uint64_t ImageSize,
                         uint64_t AllocationSize)
{
    RC95LDR_RC95_NATIVE_PROVIDER_RESULT *Result = Opaque;
    const RC95LDR_WINLOAD_PLATFORM *Platform =
        Result ? Result->WinloadPlatform : NULL;
    if (!Platform || !Platform->ValidateAllocationExtent)
        return Rc95LdrUnsupported;
    return Platform->ValidateAllocationExtent(Platform->Context, VirtualBase,
        PhysicalBase, ImageSize, AllocationSize);
}

static RC95LDR_STATUS
DriverResolveImport(void *Opaque, const char *ModuleName,
                    const char *SymbolName, uint16_t Ordinal,
                    int ByOrdinal, uint64_t *ResolvedAddress)
{
    RC95LDR_RC95_NATIVE_PROVIDER_RESULT *Result = Opaque;
    const RC95LDR_WINLOAD_PLATFORM *Platform =
        Result ? Result->WinloadPlatform : NULL;
    if (!Platform ||
        Platform->StructureSize <
            offsetof(RC95LDR_WINLOAD_PLATFORM, ResolveKernelImport) +
            sizeof(Platform->ResolveKernelImport) ||
        !Platform->ResolveKernelImport)
        return Rc95LdrUnsupported;
    return Platform->ResolveKernelImport(Platform->Context, ModuleName,
        SymbolName, Ordinal, ByOrdinal, ResolvedAddress);
}

static void
DriverBuildAdapterPlatform(RC95LDR_RC95_NATIVE_PROVIDER_RESULT *Result,
                           RC95LDR_WINLOAD_PLATFORM *Adapter)
{
    *Adapter = *Result->WinloadPlatform;
    Adapter->Context = Result;
    Adapter->OpenFile = DriverOpenFile;
    Adapter->ReadFileAt = DriverReadFileAt;
    Adapter->CloseFile = DriverCloseFile;
    Adapter->AllocatePages = DriverAllocatePages;
    Adapter->FreePages = DriverFreePages;
    Adapter->ValidateAllocationExtent = DriverValidateAllocation;
    if (Result->WinloadPlatform->StructureSize >=
            offsetof(RC95LDR_WINLOAD_PLATFORM, ResolveKernelImport) +
            sizeof(Result->WinloadPlatform->ResolveKernelImport) &&
        Result->WinloadPlatform->ResolveKernelImport)
        Adapter->ResolveKernelImport = DriverResolveImport;
    else
        Adapter->ResolveKernelImport = NULL;
}

RC95LDR_STATUS
Rc95LdrRc95NativeLoadBootDriver(
    void *Opaque, const RC95LDR_HIVE_BOOT_DRIVER *Driver,
    RC95LDR_LOADED_MODULE *LoadedModule)
{
    RC95LDR_RC95_NATIVE_PROVIDER_RESULT *Result = Opaque;
    RC95LDR_WINLOAD_PLATFORM Adapter;
    uint16_t Path[RC95_NATIVE_DRIVER_PATH_CHARS];
    char ProviderPath[RC95_NATIVE_DRIVER_PATH_CHARS];
    char *StablePath;
    uint16_t ExpectedMachine;
    RC95LDR_LOADED_MODULE *Module;
    RC95LDR_STATUS Status;
    size_t Slot;

    if (!Result || !Driver || !LoadedModule ||
        !Result->BootFileProvider || !Result->WinloadPlatform ||
        !Result->WinloadPlatform->AllocatePages ||
        !Result->WinloadPlatform->FreePages ||
        !Result->WinloadPlatform->ValidateAllocationExtent ||
        !Result->BootFileProvider->Open ||
        !Result->BootFileProvider->GetFileInformation ||
        !Result->BootFileProvider->Read ||
        !Result->BootFileProvider->Seek ||
        !Result->BootFileProvider->Close ||
        Result->BootDriverCount >= RC95LDR_MODULE_MAX)
        return Rc95LdrInvalidParameter;

    if (Result->BaseInput.Architecture == Rc95LdrNtArchI386) {
        ExpectedMachine = RC95LDR_PE_MACHINE_I386;
        if (Result->BaseInput.MachineType &&
            Result->BaseInput.MachineType != ExpectedMachine)
            return Rc95LdrInvalidImage;
    } else if (Result->BaseInput.Architecture == Rc95LdrNtArchAmd64) {
        ExpectedMachine = RC95LDR_PE_MACHINE_AMD64;
        if (Result->BaseInput.MachineType &&
            Result->BaseInput.MachineType != ExpectedMachine)
            return Rc95LdrInvalidImage;
    } else {
        return Rc95LdrUnsupported;
    }

    Slot = Result->BootDriverCount;
    StablePath = Result->BootDriverPaths[Slot];
    if (!DriverBuildPaths(Result->InputInstallation, Driver, ProviderPath,
            sizeof(ProviderPath), Path, sizeof(Path) / sizeof(Path[0]),
            StablePath, RC95_NATIVE_DRIVER_PATH_CHARS))
        return Rc95LdrInvalidImage;

    DriverBuildAdapterPlatform(Result, &Adapter);
    Module = &Result->BootDriverModules[Slot];
    memset(Module, 0, sizeof(*Module));
    Status = Rc95LdrWinloadLoadPeModule(&Adapter, Path, ExpectedMachine,
        RC95LDR_MODULE_BOOT_DRIVER, StablePath,
        Adapter.ResolveKernelImport ? DriverResolveImport : NULL,
        Result, Module);
    if (Status != Rc95LdrSuccess) {
        memset(StablePath, 0, RC95_NATIVE_DRIVER_PATH_CHARS);
        return Status;
    }
    ++Result->BootDriverCount;
    *LoadedModule = *Module;
    return Rc95LdrSuccess;
}

void
Rc95LdrRc95NativeReleaseBootDrivers(
    RC95LDR_RC95_NATIVE_PROVIDER_RESULT *Result)
{
    RC95LDR_WINLOAD_PLATFORM Adapter;
    uint32_t Index;
    if (!Result || !Result->WinloadPlatform) return;
    DriverBuildAdapterPlatform(Result, &Adapter);
    for (Index = Result->BootDriverCount; Index > 0; --Index)
        Rc95LdrWinloadReleasePeModule(&Adapter,
            &Result->BootDriverModules[Index - 1u]);
    Result->BootDriverCount = 0;
    memset(Result->BootDriverPaths, 0, sizeof(Result->BootDriverPaths));
}
