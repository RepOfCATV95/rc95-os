#include "include/rc95ldrbiosmodule.h"
#include <string.h>

#define RC95LDR_MODULE_PAGE_SIZE 4096u

static int NativeArchitectureMatches(uint32_t Architecture)
{
#if defined(RC95LDR_BIOS_NATIVE_I386)
    return Architecture == Rc95LdrModuleI386;
#elif defined(RC95LDR_BIOS_NATIVE_AMD64)
    return Architecture == Rc95LdrModuleAmd64;
#else
    return Architecture == Rc95LdrModuleI386 ||
           Architecture == Rc95LdrModuleAmd64;
#endif
}



static RC95LDR_STATUS RC95LDR_MODULE_ABI
Rc95LdrBiosServiceOpenFile(void *Context, const uint16_t *Path,
                           RC95LDR_MODULE_FILE *ModuleFile)
{
    const RC95LDR_WINLOAD_PLATFORM *P =
        (const RC95LDR_WINLOAD_PLATFORM *)Context;
    RC95LDR_WINLOAD_FILE File;
    RC95LDR_STATUS Status;
    if (!P || !P->OpenFile || !ModuleFile)
        return Rc95LdrInvalidParameter;
    memset(&File, 0, sizeof(File));
    Status = P->OpenFile(P->Context, Path, &File);
    if (Status == Rc95LdrSuccess)
    {
        ModuleFile->FileContext = File.FileContext;
        ModuleFile->FileSize = File.FileSize;
    }
    return Status;
}

static RC95LDR_STATUS RC95LDR_MODULE_ABI
Rc95LdrBiosServiceReadFileAt(void *Context,
    const RC95LDR_MODULE_FILE *ModuleFile, uint64_t Offset,
    void *Buffer, size_t Length)
{
    const RC95LDR_WINLOAD_PLATFORM *P =
        (const RC95LDR_WINLOAD_PLATFORM *)Context;
    RC95LDR_WINLOAD_FILE File;
    if (!P || !P->ReadFileAt || !ModuleFile)
        return Rc95LdrInvalidParameter;
    File.FileContext = ModuleFile->FileContext;
    File.FileSize = ModuleFile->FileSize;
    return P->ReadFileAt(P->Context, &File, Offset, Buffer, Length);
}

static void RC95LDR_MODULE_ABI
Rc95LdrBiosServiceCloseFile(void *Context, RC95LDR_MODULE_FILE *ModuleFile)
{
    const RC95LDR_WINLOAD_PLATFORM *P =
        (const RC95LDR_WINLOAD_PLATFORM *)Context;
    RC95LDR_WINLOAD_FILE File;
    if (!P || !P->CloseFile || !ModuleFile) return;
    File.FileContext = ModuleFile->FileContext;
    File.FileSize = ModuleFile->FileSize;
    P->CloseFile(P->Context, &File);
    ModuleFile->FileContext = NULL;
    ModuleFile->FileSize = 0;
}

static RC95LDR_STATUS RC95LDR_MODULE_ABI
Rc95LdrBiosServiceAllocatePages(void *Context, uint64_t Size,
                                uint64_t Alignment, uint64_t *Base)
{
    const RC95LDR_WINLOAD_PLATFORM *P =
        (const RC95LDR_WINLOAD_PLATFORM *)Context;
    return (!P || !P->AllocatePages) ? Rc95LdrInvalidParameter :
        P->AllocatePages(P->Context, Size, Alignment, Base);
}

static void RC95LDR_MODULE_ABI
Rc95LdrBiosServiceFreePages(void *Context, uint64_t Base, uint64_t Size)
{
    const RC95LDR_WINLOAD_PLATFORM *P =
        (const RC95LDR_WINLOAD_PLATFORM *)Context;
    if (P && P->FreePages) P->FreePages(P->Context, Base, Size);
}

static RC95LDR_STATUS RC95LDR_MODULE_ABI
Rc95LdrBiosServiceValidateExtent(void *Context, uint64_t Physical,
    uint64_t Virtual, uint64_t Size, uint64_t Alignment)
{
    const RC95LDR_WINLOAD_PLATFORM *P =
        (const RC95LDR_WINLOAD_PLATFORM *)Context;
    return (!P || !P->ValidateAllocationExtent) ? Rc95LdrInvalidParameter :
        P->ValidateAllocationExtent(P->Context, Physical, Virtual,
                                    Size, Alignment);
}


static RC95LDR_STATUS RC95LDR_MODULE_ABI
Rc95LdrBiosServiceQuiesce(void *Context)
{
    return Context ? Rc95LdrSuccess : Rc95LdrInvalidParameter;
}

static int RoundToPages(uint64_t Size, uint64_t *Rounded)
{
    uint64_t Add = RC95LDR_MODULE_PAGE_SIZE - 1u;
    if (!Size || Size > UINT64_MAX - Add)
        return 0;
    *Rounded = (Size + Add) & ~(uint64_t)(RC95LDR_MODULE_PAGE_SIZE - 1u);
    return *Rounded != 0;
}

static void ReleaseAllocation(const RC95LDR_WINLOAD_PLATFORM *Platform,
                              uint64_t Base, uint64_t Size)
{
    if (Base && Size && Platform->FreePages)
        Platform->FreePages(Platform->Context, Base, Size);
}

RC95LDR_STATUS
Rc95LdrBiosLoadAndInvokeModule(
    const RC95LDR_WINLOAD_PLATFORM *Platform,
    const uint16_t *Path,
    uint32_t ModuleKind,
    uint32_t Architecture,
    const RC95LDR_MODULE_HANDOFF *ParentHandoff,
    size_t ParentHandoffSize,
    RC95LDR_MODULE_TRANSFER Transfer,
    void *TransferContext)
{
    RC95LDR_WINLOAD_FILE File;
    RC95LDR_PE_INFO PeInfo;
    RC95LDR_MODULE_HANDOFF Handoff;
    RC95LDR_MODULE_SERVICES Services;
    RC95LDR_STATUS Status;
    uint64_t FileAllocation = 0, FileAllocationSize = 0;
    uint64_t ImageAllocation = 0, ImageAllocationSize = 0;
    uint64_t FileSize, ExpectedMachine, EntryPoint, ImageAlignment;
    size_t FileBytes;
    void *FileBuffer = NULL;
    void *ImageBuffer = NULL;
    int FileOpened = 0;

    memset(&File, 0, sizeof(File));
    memset(&PeInfo, 0, sizeof(PeInfo));
    memset(&Handoff, 0, sizeof(Handoff));
    memset(&Services, 0, sizeof(Services));

    if (!Platform || !Path || !Transfer ||
        Platform->StructureSize < sizeof(*Platform) ||
        Platform->Version != RC95LDR_WINLOAD_ABI_VERSION ||
        !Platform->OpenFile || !Platform->ReadFileAt ||
        !Platform->CloseFile || !Platform->AllocatePages ||
        !Platform->FreePages || !Platform->ValidateAllocationExtent ||
        ModuleKind < Rc95LdrModuleRc95Ldr ||
        ModuleKind > Rc95LdrModuleWinload ||
        (Architecture != Rc95LdrModuleI386 &&
         Architecture != Rc95LdrModuleAmd64) ||
        !NativeArchitectureMatches(Architecture) ||
        ((ParentHandoff == NULL) != (ParentHandoffSize == 0)) ||
        ParentHandoffSize > 64u * 1024u ||
        (ParentHandoff && ParentHandoffSize < sizeof(*ParentHandoff)))
        return Rc95LdrInvalidParameter;

    if (ParentHandoff)
    {
        Status = Rc95LdrValidateModuleHandoff(
            ParentHandoff, ParentHandoffSize, ParentHandoff->ModuleKind,
            Rc95LdrModuleBios, Architecture);
        if (Status != Rc95LdrSuccess) return Status;
    }

    ExpectedMachine = Architecture == Rc95LdrModuleI386
        ? RC95LDR_PE_MACHINE_I386 : RC95LDR_PE_MACHINE_AMD64;

    Status = Platform->OpenFile(Platform->Context, Path, &File);
    if (Status != Rc95LdrSuccess)
        goto Cleanup;
    FileOpened = 1;

    FileSize = File.FileSize;
    if (!FileSize || FileSize > RC95LDR_MODULE_MAX_IMAGE_SIZE ||
        FileSize > (uint64_t)SIZE_MAX ||
        !RoundToPages(FileSize, &FileAllocationSize))
    {
        Status = Rc95LdrOutOfBounds;
        goto Cleanup;
    }

    Status = Platform->AllocatePages(Platform->Context, FileAllocationSize,
        RC95LDR_MODULE_PAGE_SIZE, &FileAllocation);
    if (Status != Rc95LdrSuccess)
        goto Cleanup;
    if (!FileAllocation || FileAllocation > (uint64_t)UINTPTR_MAX ||
        FileAllocationSize - 1u > (uint64_t)UINTPTR_MAX - FileAllocation)
    {
        Status = Rc95LdrOutOfBounds;
        goto Cleanup;
    }
    Status = Platform->ValidateAllocationExtent(Platform->Context,
        FileAllocation, FileAllocation, FileAllocationSize,
        RC95LDR_MODULE_PAGE_SIZE);
    if (Status != Rc95LdrSuccess)
        goto Cleanup;

    FileBuffer = (void *)(uintptr_t)FileAllocation;
    memset(FileBuffer, 0, (size_t)FileAllocationSize);
    FileBytes = (size_t)FileSize;
    Status = Platform->ReadFileAt(Platform->Context, &File, 0,
                                  FileBuffer, FileBytes);
    if (Status != Rc95LdrSuccess)
        goto Cleanup;

    Status = Rc95LdrPeValidate(FileBuffer, FileBytes,
        (uint16_t)ExpectedMachine, &PeInfo);
    if (Status != Rc95LdrSuccess)
        goto Cleanup;
    if (!(PeInfo.Characteristics & 0x0002u) ||
        (PeInfo.Characteristics & 0x2000u) ||
        !PeInfo.SizeOfImage || PeInfo.SizeOfImage > RC95LDR_MODULE_MAX_IMAGE_SIZE ||
        PeInfo.EntryPointRva >= PeInfo.SizeOfImage ||
        !RoundToPages(PeInfo.SizeOfImage, &ImageAllocationSize))
    {
        Status = Rc95LdrInvalidImage;
        goto Cleanup;
    }

    ImageAlignment = PeInfo.SectionAlignment > RC95LDR_MODULE_PAGE_SIZE
        ? PeInfo.SectionAlignment : RC95LDR_MODULE_PAGE_SIZE;
    Status = Platform->AllocatePages(Platform->Context, ImageAllocationSize,
        ImageAlignment, &ImageAllocation);
    if (Status != Rc95LdrSuccess)
        goto Cleanup;
    if (!ImageAllocation || ImageAllocation > (uint64_t)UINTPTR_MAX ||
        ImageAllocationSize - 1u > (uint64_t)UINTPTR_MAX - ImageAllocation)
    {
        Status = Rc95LdrOutOfBounds;
        goto Cleanup;
    }
    Status = Platform->ValidateAllocationExtent(Platform->Context,
        ImageAllocation, ImageAllocation, ImageAllocationSize,
        ImageAlignment);
    if (Status != Rc95LdrSuccess)
        goto Cleanup;

    ImageBuffer = (void *)(uintptr_t)ImageAllocation;
    memset(ImageBuffer, 0, (size_t)ImageAllocationSize);
    Status = Rc95LdrPeMap(FileBuffer, FileBytes, &PeInfo,
        ImageBuffer, (size_t)ImageAllocationSize, ImageAllocation);
    if (Status != Rc95LdrSuccess)
        goto Cleanup;

    if (PeInfo.EntryPointRva > UINT64_MAX - ImageAllocation)
    {
        Status = Rc95LdrIntegerOverflow;
        goto Cleanup;
    }
    EntryPoint = ImageAllocation + PeInfo.EntryPointRva;

    Handoff.Signature = RC95LDR_MODULE_SIGNATURE;
    Handoff.StructureSize = sizeof(Handoff);
    Handoff.Version = RC95LDR_MODULE_HANDOFF_VERSION;
    Handoff.ModuleKind = ModuleKind;
    Handoff.Firmware = Rc95LdrModuleBios;
    Handoff.Architecture = Architecture;
    Handoff.ImageBase = ImageAllocation;
    Handoff.ImageSize = PeInfo.SizeOfImage;
    Handoff.EntryPoint = EntryPoint;

    Services.Signature = RC95LDR_MODULE_SERVICES_SIGNATURE;
    Services.StructureSize = sizeof(Services);
    Services.Version = RC95LDR_MODULE_SERVICES_VERSION;
    Services.Firmware = Rc95LdrModuleBios;
    Services.Architecture = Architecture;
    Services.Context = (void *)Platform;
    Services.OpenFile = Rc95LdrBiosServiceOpenFile;
    Services.ReadFileAt = Rc95LdrBiosServiceReadFileAt;
    Services.CloseFile = Rc95LdrBiosServiceCloseFile;
    Services.AllocatePages = Rc95LdrBiosServiceAllocatePages;
    Services.FreePages = Rc95LdrBiosServiceFreePages;
    Services.ValidateAllocationExtent = Rc95LdrBiosServiceValidateExtent;
    Services.Quiesce = Rc95LdrBiosServiceQuiesce;
    Handoff.Services = (uint64_t)(uintptr_t)&Services;
    Handoff.ServicesSize = sizeof(Services);

    Handoff.ParentHandoff = (uint64_t)(uintptr_t)ParentHandoff;
    Handoff.ParentHandoffSize = ParentHandoffSize;

    Status = Rc95LdrValidateModuleServices(&Services, sizeof(Services),
        Rc95LdrModuleBios, Architecture);
    if (Status == Rc95LdrSuccess)
        Status = Rc95LdrInvokeModule(&Handoff, sizeof(Handoff), ModuleKind,
            Rc95LdrModuleBios, Architecture, Transfer, TransferContext);

Cleanup:
    if (FileOpened)
        Platform->CloseFile(Platform->Context, &File);
    ReleaseAllocation(Platform, FileAllocation, FileAllocationSize);
    ReleaseAllocation(Platform, ImageAllocation, ImageAllocationSize);
    return Status;
}
