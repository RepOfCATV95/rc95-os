#include "rc95ldrwinload.h"
#include "rc95ldrpe.h"
#include "rc95ldrprofile.h"
#include "rc95ldrnativeprepare.h"
#include "rc95ldrloaderblock.h"
#include "rc95ldrpolicy.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define RC95LDR_PAGE_SIZE 4096u
/* Stable diagnostic detail values for the kernel image transaction. */
#define RC95LDR_LOAD_OPEN_FAILED       0x100u
#define RC95LDR_LOAD_BAD_FILE_SIZE     0x101u
#define RC95LDR_LOAD_FILE_BUFFER_SIZE  0x102u
#define RC95LDR_LOAD_FILE_ALLOC        0x103u
#define RC95LDR_LOAD_FILE_ADDRESS      0x104u
#define RC95LDR_LOAD_FILE_READ         0x105u
#define RC95LDR_LOAD_ARCH_UNSUPPORTED  0x106u
#define RC95LDR_LOAD_PE_INVALID        0x107u
#define RC95LDR_LOAD_IMAGE_ALLOC       0x108u
#define RC95LDR_LOAD_IMAGE_ADDRESS     0x109u
#define RC95LDR_LOAD_PE_MAP            0x10Au

static const RC95LDR_TEXTUI *
Rc95LdrGetTextUi(const RC95LDR_WINLOAD_PLATFORM *Platform)
{
    if (!Platform ||
        Platform->StructureSize < offsetof(RC95LDR_WINLOAD_PLATFORM, TextUi) +
                                  sizeof(Platform->TextUi) ||
        !Platform->TextUi ||
        Rc95LdrTextUiValidate(Platform->TextUi) != Rc95LdrSuccess)
        return NULL;
    return Platform->TextUi;
}

RC95LDR_STATUS
Rc95LdrWinloadValidateFilesystemProvider(
    const RC95LDR_WINLOAD_PLATFORM *Platform)
{
    uint32_t Required;
    if (!Platform) return Rc95LdrInvalidParameter;
    if (Platform->StructureSize <
        offsetof(RC95LDR_WINLOAD_PLATFORM, BootFileSystemKind) +
        sizeof(Platform->BootFileSystemKind))
        return Rc95LdrSuccess; /* legacy platform ABI */

    if (Platform->BootFileSystemKind > Rc95LdrWinloadFsBtrfs)
        return Rc95LdrInvalidParameter;
    if (Platform->BootFileSystemKind != Rc95LdrWinloadFsBtrfs)
        return Rc95LdrSuccess;

    if (Platform->StructureSize <
        offsetof(RC95LDR_WINLOAD_PLATFORM, BootFileSystemCapabilities) +
        sizeof(Platform->BootFileSystemCapabilities))
        return Rc95LdrUnsupported;

    Required = RC95LDR_WINLOAD_FS_CAP_READ_ONLY |
               RC95LDR_WINLOAD_FS_CAP_BTRFS_NATIVE;
    if ((Platform->BootFileSystemCapabilities & Required) != Required ||
        (Platform->BootFileSystemCapabilities & ~Required) != 0)
        return Rc95LdrUnsupported;

    /* A capability bit alone must never make an unbound Btrfs volume
       bootable. Require the exact-volume file callbacks and their context
       before accepting the platform's native-VENEER attestation. */
    if (!Platform->Context || !Platform->OpenFile ||
        !Platform->ReadFileAt || !Platform->CloseFile)
        return Rc95LdrUnsupported;
    /* A native Btrfs attestation is valid only while the exact-volume
       provider scope is active; platform callbacks alone are not evidence. */
    {
        const RC95LDR_FILE_PROVIDER *Provider=Rc95LdrGetFileProvider();
        if (!Provider||Provider->StructureSize<sizeof(*Provider)||
            !Provider->VolumeContext||!Provider->Open||
            !Provider->GetFileInformation||!Provider->Read||
            !Provider->Seek||!Provider->Close)
            return Rc95LdrUnsupported;
    }
    return Rc95LdrSuccess;
}

static int
Rc95LdrWinloadRequestValid(const RC95LDR_WINLOAD_PLATFORM *Platform,
                           const RC95LDR_WINLOAD_REQUEST *Request,
                           const RC95LDR_WINLOAD_RESULT *Result)
{
    if (!Platform || !Request || !Result ||
        Platform->StructureSize < offsetof(RC95LDR_WINLOAD_PLATFORM, TextUi) ||
        Platform->Version != RC95LDR_WINLOAD_ABI_VERSION ||
        !Platform->OpenFile || !Platform->ReadFileAt || !Platform->CloseFile ||
        Request->StructureSize < offsetof(RC95LDR_WINLOAD_REQUEST, HiberFilePath) ||
        Request->Version != RC95LDR_WINLOAD_ABI_VERSION ||
        Result->StructureSize < sizeof(*Result) ||
        Result->Version != RC95LDR_WINLOAD_ABI_VERSION ||
        (Request->ResumePolicy != Rc95LdrResumeInsideWinload &&
         (!Request->KernelPath || !Platform->AllocatePages ||
          !Platform->FreePages)) ||
        (Request->ResumePolicy == Rc95LdrResumeInsideWinload &&
         (Request->OsFamily != Rc95LdrOsRc95 ||
          Platform->StructureSize < offsetof(RC95LDR_WINLOAD_PLATFORM, HiberFileExists) +
                                    sizeof(Platform->HiberFileExists) ||
          !Platform->HiberFileExists ||
          !Platform->ValidateResumeRange ||
          !Platform->MapPhysicalRange ||
          !Platform->UnmapPhysicalRange ||
          !Platform->ValidateResumeProcessorState)))
        return 0;
    {
        RC95LDR_OS_IDENTITY Identity;
        RC95LDR_OS_PROFILE Profile;
        RC95LDR_STATUS ProfileStatus;
        memset(&Identity,0,sizeof(Identity));
        Identity.StructureSize=sizeof(Identity);
        Identity.Version=RC95LDR_OS_PROFILE_VERSION;
        Identity.OsFamily=Request->OsFamily;
        Identity.Firmware=Request->Firmware;
        Identity.BootEntry=(RC95LDR_BOOT_ENTRY_KIND)Request->BootEntryKind;
        Identity.OsMajorVersion=Request->OsMajorVersion;
        Identity.OsMinorVersion=Request->OsMinorVersion;
        Identity.OsBuildNumber=Request->OsBuildNumber;
        ProfileStatus=Rc95LdrSelectOsProfile(&Identity,&Profile);
        if (ProfileStatus!=Rc95LdrSuccess) return 0;

        switch (Request->ResumePolicy)
        {
            case Rc95LdrResumeDisabled:
                break;
            case Rc95LdrResumeViaWinresume:
                if (Request->OsFamily!=Rc95LdrOsWindowsModern)
                    return 0;
                break;
            case Rc95LdrResumeInsideWinload:
                if (Request->OsFamily!=Rc95LdrOsRc95)
                    return 0;
                break;
            default:
                return 0;
        }
    }
    if (Request->StructureSize >=
        offsetof(RC95LDR_WINLOAD_REQUEST, GraphicsModeDisabled) +
        sizeof(Request->GraphicsModeDisabled) &&
        Request->GraphicsModeDisabled > 1u)
        return 0;
    if (Request->StructureSize >=
        offsetof(RC95LDR_WINLOAD_REQUEST, GraphicsModePolicyValid) +
        sizeof(Request->GraphicsModePolicyValid) &&
        Request->GraphicsModePolicyValid > 1u)
        return 0;
    if (Request->StructureSize >=
        offsetof(RC95LDR_WINLOAD_REQUEST, RamdiskSdiFlags) +
        sizeof(Request->RamdiskSdiFlags)) {
        if (Request->RamdiskSdiFlags & ~1u) return 0;
        if (Request->RamdiskSdiFlags) {
            if ((Request->OsFamily!=Rc95LdrOsWindowsNtLegacy &&
                 Request->OsFamily!=Rc95LdrOsWindowsModern) ||
                !Request->RamdiskSdiImage || !Request->RamdiskSdiImageSize ||
                Request->RamdiskSdiImageSize>RC95LDR_WINLOAD_MAX_SDI_IMAGE ||
                !Request->RamdiskSdiDiskLength ||
                Request->RamdiskSdiDiskOffset>Request->RamdiskSdiImageSize ||
                Request->RamdiskSdiDiskLength>
                    Request->RamdiskSdiImageSize-Request->RamdiskSdiDiskOffset ||
                (Request->RamdiskSdiMemoryType!=1u &&
                 Request->RamdiskSdiMemoryType!=2u))
                return 0;
        } else if (Request->RamdiskSdiImage || Request->RamdiskSdiImageSize ||
                   Request->RamdiskSdiDiskOffset || Request->RamdiskSdiDiskLength)
            return 0;
    }
    return 1;
}

static uint64_t
Rc95LdrPageCount(uint64_t Bytes)
{
    return (Bytes / RC95LDR_PAGE_SIZE) +
           ((Bytes % RC95LDR_PAGE_SIZE) != 0);
}

/*
 * Shared loader for platform HAL and boot-start driver adapters. PE section
 * metadata is stored after the page-rounded image in the same allocator-owned
 * extent, so it remains valid through native page-table construction. The
 * image itself is still mapped at ImageBase; the metadata tail is not part of
 * SizeOfImage and is never published as executable image content.
 */
RC95LDR_STATUS
Rc95LdrWinloadLoadPeModule(
    const RC95LDR_WINLOAD_PLATFORM *Platform,
    const uint16_t *FilePath,
    uint16_t ExpectedMachine,
    uint32_t ModuleFlags,
    const char *StableModulePath,
    RC95LDR_PE_IMPORT_RESOLVER ImportResolver,
    void *ImportResolverContext,
    RC95LDR_LOADED_MODULE *Module)
{
    RC95LDR_WINLOAD_FILE File;
    RC95LDR_PE_INFO Pe;
    RC95LDR_STATUS Status;
    uint64_t FileAllocation=0, FileAllocationBytes=0;
    uint64_t ImageAllocation=0, ImageBytes=0, MetadataBytes=0;
    uint64_t TotalBytes=0, Alignment;
    size_t MetadataSize=0;
    void *FileBuffer;
    uint8_t *Image;
    RC95LDR_PE_SECTION_INFO *Sections;
    int FileOpened=0;

    if (!Platform || !FilePath || !StableModulePath || !StableModulePath[0] ||
        !Module ||
        (ExpectedMachine!=RC95LDR_PE_MACHINE_I386 &&
         ExpectedMachine!=RC95LDR_PE_MACHINE_AMD64) ||
        !Platform->OpenFile || !Platform->ReadFileAt || !Platform->CloseFile ||
        !Platform->AllocatePages || !Platform->FreePages ||
        !Platform->ValidateAllocationExtent ||
        (ModuleFlags != RC95LDR_MODULE_HAL &&
         ModuleFlags != RC95LDR_MODULE_BOOT_DRIVER))
        return Rc95LdrInvalidParameter;

    memset(Module,0,sizeof(*Module));
    memset(&File,0,sizeof(File));
    Status=Rc95LdrWinloadFileOpen(Platform,FilePath,&File);
    if (Status!=Rc95LdrSuccess) return Status;
    FileOpened=1;
    if (!File.FileSize || File.FileSize>UINT32_MAX ||
        File.FileSize>SIZE_MAX ||
        File.FileSize>UINT64_MAX-(RC95LDR_PAGE_SIZE-1u)) {
        Status=Rc95LdrInvalidImage;
        goto Cleanup;
    }
    FileAllocationBytes=(File.FileSize+RC95LDR_PAGE_SIZE-1u) &
        ~(uint64_t)(RC95LDR_PAGE_SIZE-1u);
    Status=Platform->AllocatePages(Platform->Context,FileAllocationBytes,
        RC95LDR_PAGE_SIZE,&FileAllocation);
    if (Status!=Rc95LdrSuccess) goto Cleanup;
    if (!FileAllocation || (FileAllocation&(RC95LDR_PAGE_SIZE-1u)) ||
        FileAllocation>UINTPTR_MAX) {
        Status=Rc95LdrOutOfBounds;
        goto Cleanup;
    }
    FileBuffer=(void *)(uintptr_t)FileAllocation;
    Status=Rc95LdrWinloadFileReadAt(Platform,&File,0,FileBuffer,
        (size_t)File.FileSize);
    if (Status!=Rc95LdrSuccess) goto Cleanup;

    Status=Rc95LdrPeValidate(FileBuffer,(size_t)File.FileSize,
        ExpectedMachine,&Pe);
    if (Status!=Rc95LdrSuccess) goto Cleanup;
    if (!Pe.SectionCount || Pe.SectionCount>RC95LDR_PE_MAX_SECTIONS ||
        Pe.SizeOfImage>UINT64_MAX-(RC95LDR_PAGE_SIZE-1u)) {
        Status=Rc95LdrInvalidImage;
        goto Cleanup;
    }
    ImageBytes=((uint64_t)Pe.SizeOfImage+RC95LDR_PAGE_SIZE-1u) &
        ~(uint64_t)(RC95LDR_PAGE_SIZE-1u);
    if (!Rc95LdrMultiplySize(Pe.SectionCount,sizeof(Pe.Sections[0]),
            &MetadataSize)) {
        Status=Rc95LdrIntegerOverflow;
        goto Cleanup;
    }
    MetadataBytes=(uint64_t)MetadataSize;
    if (MetadataBytes>UINT64_MAX-ImageBytes ||
        ImageBytes+MetadataBytes>UINT64_MAX-(RC95LDR_PAGE_SIZE-1u)) {
        Status=Rc95LdrIntegerOverflow;
        goto Cleanup;
    }
    TotalBytes=(ImageBytes+MetadataBytes+RC95LDR_PAGE_SIZE-1u) &
        ~(uint64_t)(RC95LDR_PAGE_SIZE-1u);
    if (!TotalBytes || TotalBytes>SIZE_MAX) {
        Status=Rc95LdrOutOfBounds;
        goto Cleanup;
    }
    Alignment=Pe.SectionAlignment>RC95LDR_PAGE_SIZE ?
        Pe.SectionAlignment : RC95LDR_PAGE_SIZE;
    if ((Alignment&(Alignment-1u))!=0) {
        Status=Rc95LdrInvalidImage;
        goto Cleanup;
    }
    Status=Platform->AllocatePages(Platform->Context,TotalBytes,
        Alignment,&ImageAllocation);
    if (Status!=Rc95LdrSuccess) goto Cleanup;
    if (!ImageAllocation || (ImageAllocation&(Alignment-1u)) ||
        ImageAllocation>UINTPTR_MAX ||
        ImageAllocation>UINT64_MAX-TotalBytes) {
        Status=Rc95LdrOutOfBounds;
        goto Cleanup;
    }
    Image=(uint8_t *)(uintptr_t)ImageAllocation;
    memset(Image,0,(size_t)TotalBytes);
    Status=Rc95LdrPeMapEx(FileBuffer,(size_t)File.FileSize,&Pe,Image,
        (size_t)ImageBytes,ImageAllocation,ImportResolver,
        ImportResolverContext);
    if (Status!=Rc95LdrSuccess) goto Cleanup;
    Sections=(RC95LDR_PE_SECTION_INFO *)(void *)(Image+ImageBytes);
    memcpy(Sections,Pe.Sections,Pe.SectionCount*sizeof(Pe.Sections[0]));
    if (ImageAllocation>UINT64_MAX-Pe.EntryPointRva) {
        Status=Rc95LdrOutOfBounds;
        goto Cleanup;
    }
    Status=Platform->ValidateAllocationExtent(Platform->Context,
        ImageAllocation,ImageAllocation,Pe.SizeOfImage,TotalBytes);
    if (Status!=Rc95LdrSuccess) goto Cleanup;

    Module->ImageBase=ImageAllocation;
    Module->ImageSize=Pe.SizeOfImage;
    Module->EntryPoint=ImageAllocation+Pe.EntryPointRva;
    Module->PhysicalBase=ImageAllocation;
    Module->PhysicalSize=TotalBytes;
    Module->Flags=ModuleFlags;
    Module->Path=StableModulePath;
    Module->PeSections=Sections;
    Module->PeSectionCount=Pe.SectionCount;
    Module->PeSizeOfHeaders=Pe.SizeOfHeaders;
    ImageAllocation=0; /* Ownership transfers to the returned module. */
    Status=Rc95LdrSuccess;

Cleanup:
    if (ImageAllocation)
        Platform->FreePages(Platform->Context,ImageAllocation,TotalBytes);
    if (FileAllocation)
        Platform->FreePages(Platform->Context,FileAllocation,FileAllocationBytes);
    if (FileOpened)
        Rc95LdrWinloadFileClose(Platform,&File);
    if (Status!=Rc95LdrSuccess)
        memset(Module,0,sizeof(*Module));
    return Status;
}

void
Rc95LdrWinloadReleasePeModule(
    const RC95LDR_WINLOAD_PLATFORM *Platform,
    RC95LDR_LOADED_MODULE *Module)
{
    if (!Platform || !Module) return;
    if (Module->PhysicalBase && Module->PhysicalSize && Platform->FreePages)
        Platform->FreePages(Platform->Context,Module->PhysicalBase,
            Module->PhysicalSize);
    memset(Module,0,sizeof(*Module));
}

#define RC95LDR_BOOTRES_PATH "\\SystemRoot\\System32\\bootres.dll"
#define RC95LDR_BOOTRES_MAX_IMAGE_SIZE (64u * 1024u * 1024u)
#define RC95LDR_BOOTRES_SYSTEM_ROOT_MAX 260u

static int
Rc95LdrBuildBootResourcePath(const uint16_t *SystemRoot,
                             uint16_t *Path, size_t Capacity)
{
    static const uint16_t Suffix[] = {
        '\\','S','y','s','t','e','m','3','2','\\',
        'b','o','o','t','r','e','s','.','d','l','l',0
    };
    size_t Length=0, SuffixLength=0, i;
    if (!SystemRoot || !Path || Capacity < sizeof(Suffix)/sizeof(Suffix[0]) + 1u ||
        SystemRoot[0] != '\\')
        return 0;
    while (Length < RC95LDR_BOOTRES_SYSTEM_ROOT_MAX &&
           SystemRoot[Length] && Length < Capacity - 1u)
        ++Length;
    if (!Length || Length == RC95LDR_BOOTRES_SYSTEM_ROOT_MAX ||
        SystemRoot[Length] != 0)
        return 0;
    if (Length && SystemRoot[Length-1] == '\\')
        --Length;
    while (Suffix[SuffixLength]) ++SuffixLength;
    if (Length > Capacity - SuffixLength - 1u)
        return 0;
    for (i=0; i<Length; ++i) Path[i]=SystemRoot[i];
    for (i=0; i<SuffixLength; ++i) Path[Length+i]=Suffix[i];
    Path[Length+SuffixLength]=0;
    return 1;
}

/*
 * Load the RC95-OS resource-only DLL as a mapped PE image. The file buffer is
 * temporary; only the mapped image is returned for the kernel loader list.
 * The resource DLL has no entry point or imports and therefore is never
 * executed by RC95LDR.
 */
static RC95LDR_STATUS
Rc95LdrLoadBootResource(const RC95LDR_WINLOAD_PLATFORM *Platform,
                        const RC95LDR_WINLOAD_REQUEST *Request,
                        RC95LDR_LOADED_MODULE *Module,
                        uint64_t *ImageAllocationSize)
{
    RC95LDR_WINLOAD_FILE File;
    RC95LDR_PE_INFO Pe;
    RC95LDR_STATUS Status;
    uint16_t Path[512];
    uint64_t FileAllocation=0, FileAllocationSize=0;
    uint64_t ImageAllocation=0, ImageBytes=0, TotalAllocationBytes=0, Alignment;
    uint16_t ExpectedMachine;
    void *FileBuffer;
    memset(&File,0,sizeof(File));
    memset(&Pe,0,sizeof(Pe));
    if (!Platform || !Request || !Module || !ImageAllocationSize ||
        !Platform->OpenFile || !Platform->ReadFileAt || !Platform->CloseFile ||
        !Platform->AllocatePages || !Platform->FreePages ||
        !Platform->ValidateAllocationExtent ||
        !Rc95LdrBuildBootResourcePath(Request->SystemRoot,Path,
                                      sizeof(Path)/sizeof(Path[0])))
        return Rc95LdrInvalidParameter;
#if defined(_M_AMD64) || defined(_M_X64) || defined(_AMD64_) || defined(__x86_64__) || defined(__amd64__)
    ExpectedMachine=RC95LDR_PE_MACHINE_AMD64;
#elif defined(_M_IX86) || defined(_X86_) || defined(__i386__) || defined(i386)
    ExpectedMachine=RC95LDR_PE_MACHINE_I386;
#else
    return Rc95LdrUnsupported;
#endif
    Status=Rc95LdrWinloadFileOpen(Platform,Path,&File);
    if (Status!=Rc95LdrSuccess) return Status;
    if (!File.FileSize || File.FileSize>RC95LDR_BOOTRES_MAX_IMAGE_SIZE ||
        File.FileSize>SIZE_MAX || File.FileSize>UINT64_MAX-(RC95LDR_PAGE_SIZE-1u))
    {
        Status=Rc95LdrInvalidImage;
        goto CleanupFile;
    }
    FileAllocationSize=Rc95LdrPageCount(File.FileSize)*RC95LDR_PAGE_SIZE;
    Status=Platform->AllocatePages(Platform->Context,FileAllocationSize,
        RC95LDR_PAGE_SIZE,&FileAllocation);
    if (Status!=Rc95LdrSuccess) goto CleanupFile;
    if (!FileAllocation || (FileAllocation & (RC95LDR_PAGE_SIZE-1u)) ||
        FileAllocation>UINTPTR_MAX ||
        FileAllocationSize-1u>UINTPTR_MAX-FileAllocation ||
        FileAllocation>UINT64_MAX-FileAllocationSize ||
        Platform->ValidateAllocationExtent(Platform->Context,FileAllocation,
            FileAllocation,FileAllocationSize,FileAllocationSize)!=Rc95LdrSuccess)
    {
        Status=Rc95LdrOutOfBounds;
        goto Cleanup;
    }
    FileBuffer=(void *)(uintptr_t)FileAllocation;
    memset(FileBuffer,0,(size_t)FileAllocationSize);
    Status=Rc95LdrWinloadFileReadAt(Platform,&File,0,FileBuffer,
                                (size_t)File.FileSize);
    if (Status!=Rc95LdrSuccess) goto Cleanup;
    Status=Rc95LdrPeValidateResource(FileBuffer,(size_t)File.FileSize,
                                     ExpectedMachine,&Pe);
    if (Status!=Rc95LdrSuccess) goto Cleanup;
    if (!Pe.SizeOfImage || Pe.SizeOfImage>RC95LDR_BOOTRES_MAX_IMAGE_SIZE)
    {
        Status=Rc95LdrInvalidImage;
        goto Cleanup;
    }
    ImageBytes=Rc95LdrPageCount(Pe.SizeOfImage)*RC95LDR_PAGE_SIZE;
    /* Keep the validated section descriptor array in the same owned,
       identity-mapped allocation as this resource-only image. */
    TotalAllocationBytes=ImageBytes+RC95LDR_PAGE_SIZE;
    if (TotalAllocationBytes<ImageBytes) {
        Status=Rc95LdrOutOfBounds;
        goto Cleanup;
    }
    Alignment=Pe.SectionAlignment>RC95LDR_PAGE_SIZE ?
        Pe.SectionAlignment : RC95LDR_PAGE_SIZE;
    Status=Platform->AllocatePages(Platform->Context,TotalAllocationBytes,Alignment,
                                   &ImageAllocation);
    if (Status!=Rc95LdrSuccess) goto Cleanup;
    if (!ImageAllocation || (ImageAllocation & (Alignment-1u)) ||
        (ImageAllocation & (RC95LDR_PAGE_SIZE-1u)) ||
        ImageAllocation>UINTPTR_MAX ||
        TotalAllocationBytes-1u>UINTPTR_MAX-ImageAllocation ||
        ImageAllocation>UINT64_MAX-TotalAllocationBytes ||
        Platform->ValidateAllocationExtent(Platform->Context,ImageAllocation,
            ImageAllocation,TotalAllocationBytes,TotalAllocationBytes)!=Rc95LdrSuccess)
    {
        Status=Rc95LdrOutOfBounds;
        goto Cleanup;
    }
    Status=Rc95LdrPeMapResource(FileBuffer,(size_t)File.FileSize,&Pe,
        (void *)(uintptr_t)ImageAllocation,(size_t)ImageBytes,ImageAllocation);
    if (Status!=Rc95LdrSuccess) goto Cleanup;

    memset(Module,0,sizeof(*Module));
    Module->ImageBase=ImageAllocation;
    Module->ImageSize=Pe.SizeOfImage;
    Module->EntryPoint=0;
    Module->PhysicalBase=ImageAllocation;
    Module->PhysicalSize=TotalAllocationBytes;
    memcpy((uint8_t *)(uintptr_t)(ImageAllocation+ImageBytes),
           Pe.Sections,Pe.SectionCount*sizeof(Pe.Sections[0]));
    Module->PeSections=(const RC95LDR_PE_SECTION_INFO *)(uintptr_t)
        (ImageAllocation+ImageBytes);
    Module->PeSectionCount=Pe.SectionCount;
    Module->PeSizeOfHeaders=Pe.SizeOfHeaders;
    Module->Flags=RC95LDR_MODULE_RESOURCE;
    Module->Path=RC95LDR_BOOTRES_PATH;
    *ImageAllocationSize=TotalAllocationBytes;
    ImageAllocation=0; /* ownership transfers to the caller through the module */
    Status=Rc95LdrSuccess;
Cleanup:
    if (ImageAllocation)
        Platform->FreePages(Platform->Context,ImageAllocation,TotalAllocationBytes);
    if (FileAllocation)
        Platform->FreePages(Platform->Context,FileAllocation,FileAllocationSize);
CleanupFile:
    Rc95LdrWinloadFileClose(Platform,&File);
    return Status;
}

static int
Rc95LdrAsciiEqualInsensitive(const char *Left, const char *Right)
{
    while (*Left && *Right) {
        unsigned char a=(unsigned char)*Left++;
        unsigned char b=(unsigned char)*Right++;
        if (a>='A' && a<='Z') a=(unsigned char)(a-'A'+'a');
        if (b>='A' && b<='Z') b=(unsigned char)(b-'A'+'a');
        if (a!=b) return 0;
    }
    return *Left==0 && *Right==0;
}

static int
Rc95LdrHasBootResource(const RC95LDR_LOADED_MODULE *Modules, uint32_t Count)
{
    uint32_t i;
    if (Count && !Modules) return 0;
    for (i=0; i<Count; ++i) {
        const char *p=Modules[i].Path, *base=p;
        if (!p) continue;
        for (; *p; ++p) if (*p=='\\' || *p=='/') base=p+1;
        if (Rc95LdrAsciiEqualInsensitive(base,"bootres.dll"))
            return 1;
    }
    return 0;
}

static RC95LDR_STATUS
Rc95LdrWinloadLoadKernel(RC95LDR_BOOT_CONTEXT *BootContext,
                         const RC95LDR_WINLOAD_PLATFORM *Platform,
                         const RC95LDR_WINLOAD_REQUEST *Request,
                         RC95LDR_WINLOAD_RESULT *Result,
                         RC95LDR_PE_INFO *ValidatedPeInfo)
{
    RC95LDR_WINLOAD_FILE File;
    RC95LDR_PE_INFO Pe;
    RC95LDR_STATUS Status;
    uint64_t FilePages=0, ImagePages=0, FileAddress=0, ImageAddress=0;
    uint64_t FileAllocationBytes, ImageAllocationBytes;
    uint16_t ExpectedMachine;
    void *FileBuffer;
    if (!ValidatedPeInfo) return Rc95LdrInvalidParameter;
    memset(ValidatedPeInfo,0,sizeof(*ValidatedPeInfo));
    memset(&File,0,sizeof(File));

    Status=Rc95LdrWinloadFileOpen(Platform,Request->KernelPath,&File);
    if (Status!=Rc95LdrSuccess)
        return Rc95LdrFail(BootContext,Status,RC95LDR_LOAD_OPEN_FAILED);
    /* The PE parser uses 32-bit file offsets. Reject oversized files before
       allocating memory or asking a filesystem provider to stream them. */
    if (!File.FileSize || File.FileSize>SIZE_MAX ||
        File.FileSize>UINT32_MAX ||
        File.FileSize>UINT64_MAX-(RC95LDR_PAGE_SIZE-1u))
    {
        Status=Rc95LdrFail(BootContext,Rc95LdrInvalidImage,RC95LDR_LOAD_BAD_FILE_SIZE); goto CleanupFile;
    }
    FilePages=Rc95LdrPageCount(File.FileSize);
    if (!FilePages || FilePages>UINT64_MAX/RC95LDR_PAGE_SIZE) {
        Status=Rc95LdrFail(BootContext,Rc95LdrOutOfBounds,RC95LDR_LOAD_FILE_BUFFER_SIZE); goto CleanupFile;
    }
    FileAllocationBytes=FilePages*RC95LDR_PAGE_SIZE;
    if (FileAllocationBytes>SIZE_MAX) { Status=Rc95LdrFail(BootContext,Rc95LdrOutOfBounds,RC95LDR_LOAD_FILE_BUFFER_SIZE); goto CleanupFile; }
    Status=Platform->AllocatePages(Platform->Context,
        FileAllocationBytes,RC95LDR_PAGE_SIZE,&FileAddress);
    if (Status!=Rc95LdrSuccess) { Status=Rc95LdrFail(BootContext,Status,RC95LDR_LOAD_FILE_ALLOC); goto CleanupFile; }
    if (!FileAddress || (FileAddress&(RC95LDR_PAGE_SIZE-1u)) ||
        FileAddress>UINTPTR_MAX ||
        FileAllocationBytes-1u>UINTPTR_MAX-FileAddress ||
        FileAddress>UINT64_MAX-FileAllocationBytes ||
        (Platform->ValidateAllocationExtent &&
         Platform->ValidateAllocationExtent(Platform->Context,FileAddress,
             FileAddress,FileAllocationBytes,FileAllocationBytes)!=Rc95LdrSuccess)) {
        Platform->FreePages(Platform->Context,FileAddress,FileAllocationBytes);
        FileAddress=0; Status=Rc95LdrFail(BootContext,Rc95LdrOutOfBounds,RC95LDR_LOAD_FILE_ADDRESS); goto CleanupFile;
    }
    FileBuffer=(void *)(uintptr_t)FileAddress;
    memset(FileBuffer,0,(size_t)FileAllocationBytes);
    Status=Rc95LdrWinloadFileReadAt(Platform,&File,0,
                                FileBuffer,(size_t)File.FileSize);
    if (Status!=Rc95LdrSuccess) { Status=Rc95LdrFail(BootContext,Status,RC95LDR_LOAD_FILE_READ); goto CleanupBuffers; }
#if defined(_M_AMD64) || defined(_M_X64) || defined(_AMD64_) || defined(__x86_64__) || defined(__amd64__)
    ExpectedMachine=RC95LDR_PE_MACHINE_AMD64;
#elif defined(_M_IX86) || defined(_X86_) || defined(__i386__) || defined(i386)
    ExpectedMachine=RC95LDR_PE_MACHINE_I386;
#else
    Status=Rc95LdrFail(BootContext,Rc95LdrUnsupported,RC95LDR_LOAD_ARCH_UNSUPPORTED); goto CleanupBuffers;
#endif
    /* Parse the PE machine first so an architecture mismatch can be reported
       as a useful text-screen diagnostic instead of a generic bad-image error. */
    Status=Rc95LdrPeValidate(FileBuffer,(size_t)File.FileSize,0,&Pe);
    if (Status!=Rc95LdrSuccess) { Status=Rc95LdrFail(BootContext,Status,RC95LDR_LOAD_PE_INVALID); goto CleanupBuffers; }
    if (Pe.Machine!=ExpectedMachine) {
        Status=Rc95LdrFail(BootContext,Rc95LdrUnsupported,
                           (uint32_t)Pe.Machine);
        goto CleanupBuffers;
    }
    ImagePages=Rc95LdrPageCount(Pe.SizeOfImage);
    ImageAllocationBytes=ImagePages*RC95LDR_PAGE_SIZE;
    if (ImageAllocationBytes>SIZE_MAX) { Status=Rc95LdrFail(BootContext,Rc95LdrOutOfBounds,RC95LDR_LOAD_IMAGE_ALLOC); goto CleanupBuffers; }
    Status=Platform->AllocatePages(Platform->Context,
        ImageAllocationBytes,Pe.SectionAlignment,&ImageAddress);
    if (Status!=Rc95LdrSuccess) { Status=Rc95LdrFail(BootContext,Status,RC95LDR_LOAD_IMAGE_ALLOC); goto CleanupBuffers; }
    if (!ImageAddress || (ImageAddress&(Pe.SectionAlignment-1u)) ||
        ImageAddress>UINTPTR_MAX ||
        ImageAllocationBytes-1u>UINTPTR_MAX-ImageAddress ||
        ImageAddress>UINT64_MAX-ImageAllocationBytes ||
        (Platform->ValidateAllocationExtent &&
         Platform->ValidateAllocationExtent(Platform->Context,ImageAddress,
             ImageAddress,ImageAllocationBytes,ImageAllocationBytes)!=Rc95LdrSuccess)) {
        Platform->FreePages(Platform->Context,ImageAddress,ImageAllocationBytes);
        ImageAddress=0; Status=Rc95LdrFail(BootContext,Rc95LdrOutOfBounds,RC95LDR_LOAD_IMAGE_ADDRESS); goto CleanupBuffers;
    }
    {
        RC95LDR_PE_IMPORT_RESOLVER importResolver = NULL;
        if (Platform->StructureSize >=
            offsetof(RC95LDR_WINLOAD_PLATFORM, ResolveKernelImport) +
            sizeof(Platform->ResolveKernelImport))
            importResolver = Platform->ResolveKernelImport;
        Status=Rc95LdrPeMapEx(FileBuffer,(size_t)File.FileSize,&Pe,
            (void *)(uintptr_t)ImageAddress,(size_t)ImageAllocationBytes,
            ImageAddress,importResolver,Platform->Context);
    }
    if (Status!=Rc95LdrSuccess)
    {
        Platform->FreePages(Platform->Context,ImageAddress,ImageAllocationBytes);
        ImageAddress=0;
        Status=Rc95LdrFail(BootContext,Status,RC95LDR_LOAD_PE_MAP);
        goto CleanupBuffers;
    }
    Result->KernelImageBase=(uint64_t)(uintptr_t)(void *)(uintptr_t)ImageAddress;
    Result->KernelPhysicalBase=ImageAddress;
    Result->KernelAllocationSize=ImageAllocationBytes;
    Result->KernelImageSize=Pe.SizeOfImage;
    Result->KernelEntryPoint=ImageAddress+Pe.EntryPointRva;
    Result->ImageCount=1;
    *ValidatedPeInfo=Pe;
CleanupBuffers:
    if (FileAddress)
        Platform->FreePages(Platform->Context,FileAddress,
                            FilePages*RC95LDR_PAGE_SIZE);
CleanupFile:
    Rc95LdrWinloadFileClose(Platform,&File);
    return Status;
}

RC95LDR_STATUS
Rc95LdrLoadAndBootWindows(RC95LDR_BOOT_CONTEXT *BootContext,
                   const RC95LDR_WINLOAD_PLATFORM *Platform,
                   const RC95LDR_WINLOAD_REQUEST *Request,
                   RC95LDR_WINLOAD_RESULT *Result)
{
    RC95LDR_STATUS Status;
    const RC95LDR_TEXTUI *TextUi;
    const uint16_t *HiberFilePath = NULL;
    RC95LDR_PE_INFO KernelPeInfo;
    if (!BootContext) return Rc95LdrInvalidParameter;
    memset(&KernelPeInfo,0,sizeof(KernelPeInfo));
    Rc95LdrSetStage(BootContext,Rc95LdrStageImageLoad);

    /* Reject the CATV 95 LiberNT fork before opening winload.exe/winload.efi
       or mapping any image. This policy is identical for BIOS and UEFI. */
    if (Request && Request->StructureSize >= sizeof(*Request) &&
        Request->OsFamily == Rc95LdrOsLiberNt &&
        (Request->Firmware == Rc95LdrFirmwareBios ||
         Request->Firmware == Rc95LdrFirmwareUefi))
    {
        TextUi = Rc95LdrGetTextUi(Platform);
        Status = Rc95LdrFail(BootContext, Rc95LdrUnsupported, 0x4C4E0001u);
        if (TextUi)
            (void)Rc95LdrTextUiShowError(TextUi,
                "LiberNT is not supported",
                "RC95LDR cannot boot LiberNT: its winload.exe (BIOS) and winload.efi (UEFI) implementations are incompatible with RC95LDR. The CATV 95 fork of LiberNT was discontinued after November 11, 2026.",
                Status, BootContext->LastError.Detail,
                "Select a supported RC95-OS, Windows, or ReactOS boot entry.");
        return Status;
    }

    if (!Rc95LdrWinloadRequestValid(Platform,Request,Result))
        return Rc95LdrFail(BootContext,Rc95LdrInvalidParameter,1u);

    /* Never expose stale output addresses from a previous attempt when this
       invocation fails partway through validation, allocation, or handoff. */
    Result->KernelEntryPoint=0;
    Result->KernelImageBase=0;
    Result->KernelImageSize=0;
    Result->LoaderBlockAddress=0;
    Result->LoaderBlockSize=0;
    Result->ResumeEntryPoint=0;
    Result->KernelPhysicalBase=0;
    Result->KernelAllocationSize=0;
    Result->ImageCount=0;
    Result->Flags=0;

    /* Btrfs must be the exact BCD-selected OS-volume provider. Do not let a
       generic platform callback silently fall back to NTFS/FAT or another
       discovered partition. This extension is optional for legacy providers;
       when it identifies Btrfs, both read-only and native-provider attestations
       are mandatory before WINLOAD opens any image. */
    Status = Rc95LdrWinloadValidateFilesystemProvider(Platform);
    if (Status != Rc95LdrSuccess) {
        uint32_t Detail = Status == Rc95LdrUnsupported ?
            RC95LDR_DETAIL_BTRFS_PROVIDER_UNAVAILABLE : RC95LDR_DETAIL_INVALID_FILESYSTEM_PROVIDER;
        Status = Rc95LdrFail(BootContext,Status,Detail);
        TextUi = Rc95LdrGetTextUi(Platform);
        if (TextUi && Status == Rc95LdrUnsupported)
            (void)Rc95LdrTextUiShowError(TextUi,
                "Btrfs file provider is unavailable",
                "The selected Windows volume is marked as Btrfs, but the platform has not supplied a validated native, read-only VENEER file provider. WINLOAD has stopped before opening winload, the SYSTEM hive, boot drivers or the kernel.",
                Status,Detail,
                "Enable Btrfs boot only after the VENEER provider implements bounded chunk mapping, B-tree traversal, subvolume selection and file-extent reads. Otherwise select a supported NTFS Windows volume.");
        else if (TextUi)
            (void)Rc95LdrTextUiShowError(TextUi,
                "Invalid boot filesystem provider",
                "The platform supplied an invalid filesystem-provider identity or capability set. RC95LDR will not continue with an unverified volume provider.",
                Status,Detail,
                "Check the BIOS/UEFI volume adapter and its WINLOAD provider registration.");
        return Status;
    }

    /* Cold boot requires the complete native preparation and transfer
       adapter. Resume has its own processor-restore contract above. */
    if (Request->ResumePolicy != Rc95LdrResumeInsideWinload &&
        (Platform->StructureSize <
            offsetof(RC95LDR_WINLOAD_PLATFORM, ReleaseNativePreparation) +
            sizeof(Platform->ReleaseNativePreparation) ||
         !Platform->BuildNativePreparation ||
         !Platform->ReleaseNativePreparation ||
         !Platform->GetMemoryMap ||
         (!Platform->ExitFirmwareAndHandoff &&
          (Platform->StructureSize <
              offsetof(RC95LDR_WINLOAD_PLATFORM, ExitFirmwareAndValidatedHandoff) +
              sizeof(Platform->ExitFirmwareAndValidatedHandoff) ||
           !Platform->ExitFirmwareAndValidatedHandoff))))
        return Rc95LdrFail(BootContext,Rc95LdrInvalidParameter,0x201u);

    TextUi=Rc95LdrGetTextUi(Platform);
    if (TextUi)
        (void)Rc95LdrTextUiShowWelcome(TextUi,
            "Firmware-neutral Windows loader");
    if (TextUi)
        (void)Rc95LdrTextUiShowProgress(TextUi,
            Request->ResumePolicy == Rc95LdrResumeInsideWinload ?
            "Checking the RC95-OS hibernation image" :
            (Request->OsFamily == Rc95LdrOsRc95 ?
             "Validating and loading the RC95-OS kernel image" :
             "Validating and loading the Windows kernel image"));

    Result->KernelEntryPoint=0;
    Result->KernelImageBase=0;
    Result->KernelImageSize=0;
    Result->LoaderBlockAddress=0;
    Result->LoaderBlockSize=0;
    Result->ResumeEntryPoint=0;
    Result->ImageCount=0;
    Result->Flags=0;

    if (Request->StructureSize >= offsetof(RC95LDR_WINLOAD_REQUEST, HiberFilePath) +
                                 sizeof(Request->HiberFilePath))
        HiberFilePath = Request->HiberFilePath;

    /* RC95-OS owns resume detection and restoration inside winload. Probe the
       selected volume before allocating or loading anything: a present image
       takes the native resume path, while an absent image follows cold boot.
       The probe never searches for or executes winresume.exe/winresume.efi. */
    if (Request->OsFamily == Rc95LdrOsRc95 &&
        Request->ResumePolicy == Rc95LdrResumeInsideWinload &&
        Platform->HiberFileExists(Platform->Context, HiberFilePath))
    {
        size_t ResumeAction=RC95LDR_HIBER_ACTION_RESUME;
        const RC95LDR_TEXTUI_EXT *UiExtension=NULL;
        if (Platform->StructureSize >=
            offsetof(RC95LDR_WINLOAD_PLATFORM, TextUiExtension) +
            sizeof(Platform->TextUiExtension))
            UiExtension=Platform->TextUiExtension;
        /* The screen is optional for headless firmware, but when a keyboard
           extension is supplied Space/F8 exposes the two explicit choices. */
        if (TextUi && UiExtension) {
            Status=Rc95LdrTextUiRunHiberOptions(TextUi,UiExtension,&ResumeAction);
            if (Status!=Rc95LdrSuccess)
                return Rc95LdrFail(BootContext,Status,0x52534D4Fu);
        }
        if (ResumeAction==RC95LDR_HIBER_ACTION_NORMAL_BOOT) {
            if (Platform->StructureSize <
                    offsetof(RC95LDR_WINLOAD_PLATFORM, DeleteHiberFile) +
                    sizeof(Platform->DeleteHiberFile) ||
                !Platform->DeleteHiberFile) {
                Status=Rc95LdrFail(BootContext,Rc95LdrUnsupported,0x48494244u);
                if (TextUi)
                    (void)Rc95LdrTextUiShowError(TextUi,
                        "Normal boot is unavailable",
                        "The platform does not provide a safe hibernation-file deletion service. RC95LDR will not start a cold boot while the hibernation image may still be present.",
                        Status,BootContext->LastError.Detail,
                        "Use a platform adapter with DeleteHiberFile support, or choose hibernation resume.");
                return Status;
            }
            {
                void *DeleteContext=Platform->Context;
                if(Platform->StructureSize>=
                    offsetof(RC95LDR_WINLOAD_PLATFORM,DeleteHiberFileContext)+
                    sizeof(Platform->DeleteHiberFileContext) &&
                   Platform->DeleteHiberFileContext)
                    DeleteContext=Platform->DeleteHiberFileContext;
                Status=Platform->DeleteHiberFile(DeleteContext,HiberFilePath);
            }
            if (Status!=Rc95LdrSuccess) {
                (void)Rc95LdrFail(BootContext,Status,0x48494244u);
                if (TextUi)
                    (void)Rc95LdrTextUiShowError(TextUi,
                        "Could not discard the hibernation image",
                        "The hibernation file was not deleted. RC95LDR will not start a normal boot while a possibly valid resume image remains.",
                        Status,BootContext->LastError.Detail,
                        "Check the boot volume's write permissions and filesystem health.");
                return Status;
            }
            /* Continue below through the normal kernel-load transaction. */
        } else {
            Status=Rc95LdrResumeFromHibernation(Platform,Request);
            (void)Rc95LdrFail(BootContext,Status,0x52534D45u);
            if (TextUi)
                (void)Rc95LdrTextUiShowError(TextUi,
                    "RC95-OS hibernation resume failed",
                    "The native hiberfile could not be validated or restored, or the processor-state trampoline returned unexpectedly.",
                    Status,BootContext->LastError.Detail,
                    "Check hiberfil.sys and the firmware memory-map and architecture restore services. RC95LDR will not launch Windows winresume.exe or winresume.efi.");
            return Status;
        }
    }
    if (!Request->KernelPath)
        return Rc95LdrFail(BootContext,Rc95LdrInvalidParameter,0x48494230u);

    Status=Rc95LdrWinloadLoadKernel(BootContext,Platform,Request,Result,
                                    &KernelPeInfo);
    if (Status!=Rc95LdrSuccess) {
        /* Platform allocation/open callbacks may return OutOfMemory directly
           without updating the shared boot error record. Normalize it here so
           the displayed detail and the recorded status describe this failure. */
        if (Status==Rc95LdrOutOfMemory)
            (void)Rc95LdrFail(BootContext,Rc95LdrOutOfMemory,0u);
        if (TextUi) {
            const char *Title="Kernel image load failed";
            const char *Message="RC95LDR could not validate, read, or map the selected kernel.";
            const char *Hint="Check the selected Windows installation and its architecture.";
            if (BootContext->LastError.Detail==RC95LDR_LOAD_OPEN_FAILED) {
                Title="Kernel image could not be opened";
                Message="The selected kernel path could not be opened by the firmware file provider. The file may be missing, inaccessible, or on an unavailable volume.";
                Hint="Verify the selected installation's System32\\ntoskrnl.exe path, volume mapping, and filesystem-provider status.";
            } else if (BootContext->LastError.Detail==RC95LDR_LOAD_BAD_FILE_SIZE) {
                Title="Kernel image has an invalid file size";
                Message="The kernel file is empty, too large for this loader build, or its size cannot be represented safely.";
                Hint="Restore the kernel from matching installation media and verify the filesystem reports its correct file size.";
            } else if (BootContext->LastError.Detail==RC95LDR_LOAD_FILE_READ) {
                Title="Kernel image could not be read";
                Message="The file opened successfully, but the firmware file provider failed while reading its contents into the temporary buffer.";
                Hint="Check the boot volume for I/O errors and verify the file-provider read implementation.";
            } else if (BootContext->LastError.Detail==RC95LDR_LOAD_PE_INVALID) {
                Title="Kernel executable is malformed";
                Message="The kernel does not contain a structurally valid PE image. Its headers, section table, alignment, entry point, or data-directory bounds failed validation.";
                Hint="Use the kernel that belongs to the selected Windows installation and architecture; do not rename an unrelated executable to ntoskrnl.exe.";
            } else if (BootContext->LastError.Detail==RC95LDR_LOAD_PE_MAP &&
                       Status==Rc95LdrUnsupported) {
                Title="Kernel imports are not yet supported";
                Message="The PE image declares imported modules, but RC95LDR does not yet have a completed kernel import resolver and dependency policy. The image was deliberately not executed.";
                Hint="Use a build with the native import resolver completed; this loader intentionally fails closed rather than leaving unresolved import addresses.";
            } else if (BootContext->LastError.Detail==RC95LDR_LOAD_PE_MAP) {
                Title="Kernel image could not be mapped";
                Message="The validated PE image could not be mapped or relocated at the allocated destination address.";
                Hint="Check the kernel's relocation data and the platform page allocator; unsupported relocation types are rejected.";
            } else if (BootContext->LastError.Detail==RC95LDR_LOAD_FILE_ALLOC ||
                       BootContext->LastError.Detail==RC95LDR_LOAD_IMAGE_ALLOC ||
                       BootContext->LastError.Detail==RC95LDR_LOAD_FILE_BUFFER_SIZE) {
                Title="Kernel image allocation failed";
                Message="RC95LDR could not reserve a correctly sized temporary file buffer or kernel image allocation.";
                Hint="Verify the firmware memory map, available contiguous pages, and page-allocation callback.";
            } else if (Status==Rc95LdrOutOfMemory) {
                Title="Not enough memory to load the operating system";
                Message="RC95LDR could not allocate the memory required for the kernel image or its temporary file buffer.";
                Hint="Close or remove unnecessary memory reservations, verify the firmware memory map, or use a system with more available RAM.";
            } else if (Status==Rc95LdrUnsupported &&
                BootContext->LastError.Detail==RC95LDR_PE_MACHINE_I386) {
#if defined(_M_AMD64) || defined(_M_X64) || defined(_AMD64_) || defined(__x86_64__) || defined(__amd64__)
                Title="32-bit Windows is not supported by this loader";
                Message="This 64-bit RC95LDR build cannot boot a 32-bit Windows installation.";
                Hint="Use the 32-bit RC95LDR loader for this Windows installation.";
#endif
            } else if (Status==Rc95LdrUnsupported &&
                       BootContext->LastError.Detail==RC95LDR_PE_MACHINE_AMD64) {
#if defined(_M_IX86) || defined(_X86_) || defined(__i386__) || defined(i386)
                Title="64-bit Windows is not supported by this loader";
                Message="This 32-bit RC95LDR build cannot boot 64-bit Windows (XP x64 through Windows 11).";
                Hint="Use the 64-bit RC95LDR loader for this Windows installation.";
#endif
            }
            (void)Rc95LdrTextUiShowError(TextUi,Title,Message,Status,
                BootContext->LastError.Detail,Hint);
        }
        return Status;
    }

    /*
     * Execute the cold-boot preparation as one transaction. The platform
     * adapter maps HAL/boot images and supplies the resident hive, metadata,
     * firmware memory map and final-address loader-block buffer. The native
     * builder owns loader-state mappings through NativePrepareResult.
     */
    {
        RC95LDR_NATIVE_PREPARE_REQUEST prepareRequest;
        RC95LDR_WINLOAD_REQUEST PrepareWinloadRequest;
        RC95LDR_NATIVE_LOADER_INPUT uefiBaseInput;
        void *nativePreparationContext = Platform->Context;
        RC95LDR_NATIVE_PREPARE_RESULT *prepareResult = NULL;
        RC95LDR_LOADED_MODULE BootResource;
        uint64_t BootResAllocation=0;
        int BootResMapped=0;
        char NativePrepareError[256];
        uint64_t resultAddress = 0, resultAllocation = 0;
        uint64_t RamdiskAddress = 0, RamdiskAllocation = 0;
        RC95LDR_NATIVE_ALLOCATION_SET RamdiskAllocationSet;
        uint64_t resultPages = Rc95LdrPageCount(sizeof(*prepareResult));
        uint32_t moduleIndex, kernelMatches = 0;
        const RC95LDR_LOADED_MODULE *kernelModule = NULL;

        Rc95LdrSetStage(BootContext,Rc95LdrStageOsPrepare);
        memset(&prepareRequest,0,sizeof(prepareRequest));
        memset(NativePrepareError,0,sizeof(NativePrepareError));
        resultAllocation = resultPages * RC95LDR_PAGE_SIZE;
        Status = Platform->AllocatePages(Platform->Context,resultAllocation,
                                         RC95LDR_PAGE_SIZE,&resultAddress);
        if (Status != Rc95LdrSuccess || !resultAddress ||
            resultAddress > UINTPTR_MAX - sizeof(*prepareResult)) {
            if (resultAddress)
                Platform->FreePages(Platform->Context,resultAddress,resultAllocation);
            Status = (Status == Rc95LdrSuccess) ? Rc95LdrOutOfBounds : Status;
            goto NativePrepareFailed;
        }
        prepareResult = (RC95LDR_NATIVE_PREPARE_RESULT *)(uintptr_t)resultAddress;
        memset(prepareResult,0,sizeof(*prepareResult));
        prepareResult->StructureSize=sizeof(*prepareResult);
        prepareResult->Version=RC95LDR_NATIVE_PREPARE_VERSION;

        if (!Platform->ValidateAllocationExtent) {
            (void)Rc95LdrFail(BootContext,Rc95LdrUnsupported,0x205u);
            Status = Rc95LdrUnsupported;
            goto NativePrepareCleanup;
        }
        if (Platform->ValidateAllocationExtent(Platform->Context,
                Result->KernelImageBase,Result->KernelPhysicalBase,
                Result->KernelImageSize,Result->KernelAllocationSize) != Rc95LdrSuccess) {
            (void)Rc95LdrFail(BootContext,Rc95LdrInvalidImage,0x206u);
            Status = Rc95LdrInvalidImage;
            goto NativePrepareCleanup;
        }
        if (Platform->StructureSize >=
                offsetof(RC95LDR_WINLOAD_PLATFORM, NativePreparationContext) +
                sizeof(Platform->NativePreparationContext) &&
            Platform->NativePreparationContext)
            nativePreparationContext = Platform->NativePreparationContext;
        memset(&PrepareWinloadRequest,0,sizeof(PrepareWinloadRequest));
        memcpy(&PrepareWinloadRequest,Request,
            Request->StructureSize < sizeof(PrepareWinloadRequest) ?
                Request->StructureSize : sizeof(PrepareWinloadRequest));
        PrepareWinloadRequest.StructureSize = sizeof(PrepareWinloadRequest);
        PrepareWinloadRequest.KernelPeSections = KernelPeInfo.Sections;
        PrepareWinloadRequest.KernelPeSectionCount = KernelPeInfo.SectionCount;
        PrepareWinloadRequest.KernelPeSizeOfHeaders = KernelPeInfo.SizeOfHeaders;
        Status = Platform->BuildNativePreparation(nativePreparationContext,&PrepareWinloadRequest,
            Result->KernelImageBase,Result->KernelPhysicalBase,
            Result->KernelImageSize,Result->KernelAllocationSize,
            Result->KernelEntryPoint,&prepareRequest);
        if (Status != Rc95LdrSuccess)
            goto NativePrepareCleanup;

        /*
         * UEFI AMD64's final GetMemoryMap buffer must be part of the same
         * provider-wide allocation ledger consumed by native preparation.
         * Publish that complete ledger here, after the provider has built
         * its request but before the loader-block/memory-map transaction.
         * This prevents the buffer being advertised as free RAM or omitted
         * from the target page-table mapping.
         */
        if (Request->Firmware == Rc95LdrFirmwareUefi &&
            prepareRequest.BaseInput &&
            prepareRequest.BaseInput->Architecture == Rc95LdrNtArchAmd64) {
            size_t allocationIndex;
            int haveUefiMap = 0;
            if (Platform->StructureSize <
                    offsetof(RC95LDR_WINLOAD_PLATFORM, FirmwareRuntimeServices) +
                    sizeof(Platform->FirmwareRuntimeServices) ||
                !Platform->NativeAllocationSet ||
                !Platform->FirmwareSystemTable ||
                !Platform->FirmwareRuntimeServices ||
                prepareRequest.BaseInput->StructureSize < sizeof(uefiBaseInput) ||
                Platform->NativeAllocationSet->Count >
                    RC95LDR_MEMORYMAP_MAX_RESERVATIONS) {
                Status = Rc95LdrInvalidParameter;
                goto NativePrepareCleanup;
            }

            /* The UEFI entry owns these pointers; do not trust a request
             * builder to substitute stale or caller-fabricated table values. */
            uefiBaseInput = *prepareRequest.BaseInput;
            uefiBaseInput.FirmwareIsUefi = 1u;
            uefiBaseInput.FirmwareSystemTable = Platform->FirmwareSystemTable;
            uefiBaseInput.VirtualEfiRuntimeServices =
                Platform->FirmwareRuntimeServices;
            prepareRequest.BaseInput = &uefiBaseInput;
            prepareRequest.FirmwareSystemTable = Platform->FirmwareSystemTable;

            for (allocationIndex = 0;
                 allocationIndex < Platform->NativeAllocationSet->Count;
                 ++allocationIndex) {
                if (Platform->NativeAllocationSet->Entries[allocationIndex].Kind ==
                    RC95LDR_ALLOC_UEFI_MAP) {
                    haveUefiMap = 1;
                    break;
                }
            }
            if (!haveUefiMap) {
                Status = Rc95LdrInvalidParameter;
                goto NativePrepareCleanup;
            }
            prepareRequest.NativeAllocations =
                Platform->NativeAllocationSet->Entries;
            prepareRequest.NativeAllocationCount =
                Platform->NativeAllocationSet->Count;
        }

        if (Request->StructureSize >=
                offsetof(RC95LDR_WINLOAD_REQUEST, RamdiskSdiFlags) +
                sizeof(Request->RamdiskSdiFlags) &&
            Request->RamdiskSdiFlags) {
            uint64_t Pages;
            size_t AllocationIndex;
            const RC95LDR_NATIVE_ALLOCATION *SourceAllocations =
                prepareRequest.NativeAllocations;
            size_t SourceAllocationCount = prepareRequest.NativeAllocationCount;
            if ((Request->OsFamily != Rc95LdrOsWindowsNtLegacy &&
                 Request->OsFamily != Rc95LdrOsWindowsModern) ||
                !Request->RamdiskSdiImage || !Request->RamdiskSdiImageSize ||
                Request->RamdiskSdiImageSize>RC95LDR_WINLOAD_MAX_SDI_IMAGE ||
                Request->RamdiskSdiImageSize > SIZE_MAX ||
                Request->RamdiskSdiImageSize > UINT64_MAX-(RC95LDR_PAGE_SIZE-1u) ||
                !Request->RamdiskSdiDiskLength ||
                Request->RamdiskSdiDiskOffset > Request->RamdiskSdiImageSize ||
                Request->RamdiskSdiDiskLength >
                    Request->RamdiskSdiImageSize-Request->RamdiskSdiDiskOffset ||
                !Platform->ValidateAllocationExtent) {
                Status=Rc95LdrInvalidImage;
                goto NativePrepareCleanup;
            }
            Pages=Rc95LdrPageCount(Request->RamdiskSdiImageSize);
            if (!Pages || Pages>UINT64_MAX/RC95LDR_PAGE_SIZE) {
                Status=Rc95LdrOutOfBounds;
                goto NativePrepareCleanup;
            }
            RamdiskAllocation=Pages*RC95LDR_PAGE_SIZE;
            Status=Platform->AllocatePages(Platform->Context,RamdiskAllocation,
                RC95LDR_PAGE_SIZE,&RamdiskAddress);
            if (Status!=Rc95LdrSuccess) goto NativePrepareCleanup;
            if (!RamdiskAddress || (RamdiskAddress&(RC95LDR_PAGE_SIZE-1u)) ||
                RamdiskAddress>UINTPTR_MAX ||
                RamdiskAllocation>UINTPTR_MAX-RamdiskAddress ||
                RamdiskAddress>UINT64_MAX-RamdiskAllocation ||
                Platform->ValidateAllocationExtent(Platform->Context,
                    RamdiskAddress,RamdiskAddress,RamdiskAllocation,
                    RamdiskAllocation)!=Rc95LdrSuccess) {
                Status=Rc95LdrOutOfBounds;
                goto NativePrepareCleanup;
            }
            memset((void *)(uintptr_t)RamdiskAddress,0,(size_t)RamdiskAllocation);
            memcpy((void *)(uintptr_t)RamdiskAddress,Request->RamdiskSdiImage,
                   (size_t)Request->RamdiskSdiImageSize);

            /* Use the provider's complete ledger where available; otherwise
               extend the allocation records already returned by its builder. */
            if (SourceAllocationCount>RC95LDR_MEMORYMAP_MAX_RESERVATIONS ||
                (SourceAllocationCount && !SourceAllocations)) {
                Status=Rc95LdrInvalidParameter;
                goto NativePrepareCleanup;
            }
            memset(&RamdiskAllocationSet,0,sizeof(RamdiskAllocationSet));
            RamdiskAllocationSet.Count=SourceAllocationCount;
            for (AllocationIndex=0;AllocationIndex<SourceAllocationCount;
                 ++AllocationIndex)
                RamdiskAllocationSet.Entries[AllocationIndex]=
                    SourceAllocations[AllocationIndex];
            Status=Rc95LdrTrackNativeAllocation(&RamdiskAllocationSet,
                RC95LDR_ALLOC_RAMDISK,RamdiskAddress,RamdiskAllocation);
            if (Status!=Rc95LdrSuccess) goto NativePrepareCleanup;
            prepareRequest.NativeAllocations=RamdiskAllocationSet.Entries;
            prepareRequest.NativeAllocationCount=RamdiskAllocationSet.Count;
            prepareRequest.RamdiskPhysicalBase=RamdiskAddress;
            prepareRequest.RamdiskAllocationSize=RamdiskAllocation;
            prepareRequest.RamdiskImageOffset=Request->RamdiskSdiDiskOffset;
            prepareRequest.RamdiskImageLength=Request->RamdiskSdiDiskLength;
        }

        /* PhysicalBase is the exact allocator return; ImageBase is only the
           CPU mapping used to access the image. They may currently be equal
           on identity-mapped firmware, but are never interchangeable. */
        if (!Result->KernelImageBase || !Result->KernelPhysicalBase ||
            !Result->KernelAllocationSize ||
            Result->KernelAllocationSize < Result->KernelImageSize ||
            (Result->KernelPhysicalBase & (RC95LDR_PAGE_SIZE - 1u)) ||
            (Result->KernelAllocationSize & (RC95LDR_PAGE_SIZE - 1u))) {
            Status = Rc95LdrOutOfBounds;
            goto NativePrepareCleanup;
        }
        prepareRequest.KernelPhysicalBase = Result->KernelPhysicalBase;
        prepareRequest.KernelPhysicalSize = Result->KernelAllocationSize;
        if (!prepareRequest.BaseInput ||
            prepareRequest.BaseInput->StructureSize < sizeof(*prepareRequest.BaseInput) ||
            prepareRequest.BaseInput->Version != RC95LDR_NATIVE_LOADER_INPUT_VERSION ||
            !prepareRequest.BaseInput->Modules) {
            Status=Rc95LdrInvalidParameter;
            goto NativePrepareCleanup;
        }
        for (moduleIndex=0;moduleIndex<prepareRequest.BaseInput->ModuleCount;
             ++moduleIndex) {
            const RC95LDR_LOADED_MODULE *candidate =
                &prepareRequest.BaseInput->Modules[moduleIndex];
            if (candidate->Flags & RC95LDR_MODULE_KERNEL) {
                ++kernelMatches;
                kernelModule=candidate;
            }
        }
        if (kernelMatches != 1 || !kernelModule ||
            kernelModule->ImageBase != Result->KernelImageBase ||
            kernelModule->ImageSize != Result->KernelImageSize ||
            kernelModule->EntryPoint != Result->KernelEntryPoint) {
            Status=Rc95LdrInvalidImage;
            goto NativePrepareCleanup;
        }

        /*
         * RC95-OS only: load the selected installation's bootres.dll as a
         * resource-only image. graphicsmodedisabled suppresses the load
         * entirely. Missing or invalid optional graphics resources leave the
         * kernel's INBV fallback intact and do not block OS startup.
         */
        if (Request->StructureSize >=
                offsetof(RC95LDR_WINLOAD_REQUEST, GraphicsModePolicyValid) +
                sizeof(Request->GraphicsModePolicyValid) &&
            Rc95LdrShouldLoadBootResource(Request->OsFamily,
                Request->GraphicsModePolicyValid,
                Request->GraphicsModeDisabled) &&
            !Rc95LdrHasBootResource(prepareRequest.BaseInput->Modules,
                                    prepareRequest.BaseInput->ModuleCount) &&
            prepareRequest.BaseInput->ModuleCount < RC95LDR_MODULE_MAX)
        {
            memset(&BootResource,0,sizeof(BootResource));
            Status=Rc95LdrLoadBootResource(Platform,Request,&BootResource,
                                            &BootResAllocation);
            if (Status == Rc95LdrSuccess)
            {
                prepareRequest.BootResourceModule=&BootResource;
                BootResMapped=1;
            }
            else
            {
                /* Optional resource failure is deliberately non-fatal. */
                BootResAllocation=0;
            }
        }

        Status=Rc95LdrPrepareNativeLoaderBlock(&prepareRequest,prepareResult);
        if (Status != Rc95LdrSuccess)
            goto NativePrepareCleanup;
        if (BootResMapped && !prepareResult->LoadedBootResource &&
            BootResAllocation)
        {
            Platform->FreePages(Platform->Context,
                BootResource.PhysicalBase,BootResAllocation);
            BootResMapped=0;
            BootResAllocation=0;
        }
        if (!prepareResult->LoaderBlock || !prepareResult->LoaderBlockSize ||
            !prepareResult->HandoffReady ||
            prepareResult->Handoff.KernelEntry != Result->KernelEntryPoint ||
            prepareResult->Handoff.LoaderBlockAddress !=
                (uint64_t)(uintptr_t)prepareResult->LoaderBlock ||
            prepareResult->Handoff.LoaderBlockSize != prepareResult->LoaderBlockSize) {
            Status=Rc95LdrFail(BootContext,Rc95LdrUnsupported,0x202u);
            if (TextUi)
                (void)Rc95LdrTextUiShowError(TextUi,"Kernel handoff prerequisites are incomplete",
                    prepareResult->ErrorMessage[0] ? prepareResult->ErrorMessage :
                    "Native loader preparation completed only partially. Required loader-extension, firmware, memory-map or architecture state is still missing.",
                    Status,BootContext->LastError.Detail,
                    "Check the platform preparation adapter and target-specific loader ABI.");
            goto NativePrepareCleanup;
        }

        Result->LoaderBlockAddress=(uint64_t)(uintptr_t)prepareResult->LoaderBlock;
        Result->LoaderBlockSize=prepareResult->LoaderBlockSize;
        Result->ImageCount=prepareRequest.BaseInput->ModuleCount +
            prepareResult->LoadedBootDriverCount +
            (prepareResult->LoadedBootResource ? 1u : 0u);
        Result->Flags=1u; /* Native loader block and handoff preflight are ready. */

        /*
         * The native callback receives the complete OS-profile-specific
         * handoff. AMD64 must never fall back to the legacy address-only
         * callback: that path cannot prove the processor state, page tables,
         * PCR/PRCB, final firmware map or loader-block ABI at transfer time.
         * i386 keeps its established provider callback unchanged.
         */
        if (Platform->StructureSize >=
                offsetof(RC95LDR_WINLOAD_PLATFORM, ExitFirmwareAndValidatedHandoff) +
                sizeof(Platform->ExitFirmwareAndValidatedHandoff) &&
            Platform->ExitFirmwareAndValidatedHandoff) {
            Status=Rc95LdrValidateKernelHandoff(&prepareResult->Handoff);
            if (Status != Rc95LdrSuccess)
                goto NativePrepareCleanup;
            Status=Platform->ExitFirmwareAndValidatedHandoff(
                (Platform->StructureSize >=
                    offsetof(RC95LDR_WINLOAD_PLATFORM, ExitFirmwareContext) +
                    sizeof(Platform->ExitFirmwareContext) &&
                 Platform->ExitFirmwareContext) ?
                    Platform->ExitFirmwareContext : Platform->Context,
                &prepareResult->Handoff);
        } else if (prepareResult->Handoff.OsFamily == Rc95LdrOsRc95 ||
                   prepareResult->Handoff.Architecture == Rc95LdrNtArchAmd64) {
            /* RC95-OS owns a distinct native loader-block ABI. It must cross
             * the firmware boundary with the complete typed handoff; the
             * legacy entry/block callback cannot carry or validate RC95 CPU,
             * scheduler, stack, page-table and register-frame state. */
            Status=Rc95LdrUnsupported;
            goto NativePrepareCleanup;
        } else {
            Status=Platform->ExitFirmwareAndHandoff(Platform->Context,
                Result->KernelEntryPoint,Result->LoaderBlockAddress,
                Result->LoaderBlockSize);
        }
        if (Status == Rc95LdrSuccess) {
            /* Returning after a successful non-returning handoff is fatal;
               do not touch firmware allocations after this point. */
            return Rc95LdrFail(BootContext,Rc95LdrUnsupported,0x203u);
        }
NativePrepareCleanup:
        if (prepareResult && prepareResult->ErrorMessage[0]) {
            memcpy(NativePrepareError,prepareResult->ErrorMessage,
                   sizeof(NativePrepareError));
            NativePrepareError[sizeof(NativePrepareError)-1]=0;
        }
        Rc95LdrReleaseNativePrepareResult(prepareResult);
        Platform->ReleaseNativePreparation(nativePreparationContext,&prepareRequest);
        if (RamdiskAddress) {
            Platform->FreePages(Platform->Context,RamdiskAddress,RamdiskAllocation);
            RamdiskAddress=0;
        }
        if (BootResMapped && BootResAllocation)
        {
            Platform->FreePages(Platform->Context,
                BootResource.PhysicalBase,BootResAllocation);
            BootResMapped=0;
            BootResAllocation=0;
        }
        Platform->FreePages(Platform->Context,resultAddress,resultAllocation);
NativePrepareFailed:
        if (Result->KernelPhysicalBase) {
            uint64_t kernelAllocation=Result->KernelAllocationSize;
            if (kernelAllocation && !(kernelAllocation & (RC95LDR_PAGE_SIZE-1u)))
                Platform->FreePages(Platform->Context,Result->KernelPhysicalBase,
                                    kernelAllocation);
        }
        Result->KernelEntryPoint=0;
        Result->KernelImageBase=0;
        Result->KernelPhysicalBase=0;
        Result->KernelAllocationSize=0;
        Result->KernelImageSize=0;
        Result->LoaderBlockAddress=0;
        Result->LoaderBlockSize=0;
        Result->ResumeEntryPoint=0;
        Result->ImageCount=0;
        Result->Flags=0;
        if (Status != Rc95LdrUnsupported || BootContext->LastError.Status == Rc95LdrSuccess)
            (void)Rc95LdrFail(BootContext,Status,0x204u);
        if (TextUi && BootContext->LastError.Detail != 0x202u)
            (void)Rc95LdrTextUiShowError(TextUi,"Native OS preparation failed",
                NativePrepareError[0] ? NativePrepareError :
                "RC95LDR could not assemble the kernel, HAL, SYSTEM hive, boot-driver set, loader metadata and final handoff state.",
                Status,BootContext->LastError.Detail,
                "Inspect the platform adapter diagnostics and firmware memory-map services.");
        return Status;
    }
}
