#include "rc95ldrwinload.h"
#include "rc95ldrpe.h"
#include "rc95ldrprofile.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define RC95LDR_PAGE_SIZE 4096u

static const RC95LDR_TEXTUI *
Rc95LdrGetTextUi(const RC95LDR_WINLOAD_PLATFORM *Platform)
{
    if (!Platform ||
        Platform->StructureSize < sizeof(*Platform) ||
        !Platform->TextUi ||
        Rc95LdrTextUiValidate(Platform->TextUi) != Rc95LdrSuccess)
        return NULL;
    return Platform->TextUi;
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
        !Platform->AllocatePages || !Platform->FreePages ||
        Request->StructureSize < sizeof(*Request) ||
        Request->Version != RC95LDR_WINLOAD_ABI_VERSION ||
        Result->StructureSize < sizeof(*Result) ||
        Result->Version != RC95LDR_WINLOAD_ABI_VERSION ||
        !Request->KernelPath)
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
    return 1;
}

static uint64_t
Rc95LdrPageCount(uint64_t Bytes)
{
    return (Bytes / RC95LDR_PAGE_SIZE) +
           ((Bytes % RC95LDR_PAGE_SIZE) != 0);
}

static RC95LDR_STATUS
Rc95LdrWinloadLoadKernel(RC95LDR_BOOT_CONTEXT *BootContext,
                         const RC95LDR_WINLOAD_PLATFORM *Platform,
                         const RC95LDR_WINLOAD_REQUEST *Request,
                         RC95LDR_WINLOAD_RESULT *Result)
{
    RC95LDR_WINLOAD_FILE File;
    RC95LDR_PE_INFO Pe;
    RC95LDR_STATUS Status;
    uint64_t FilePages=0, ImagePages=0, FileAddress=0, ImageAddress=0;
    uint64_t FileAllocationBytes, ImageAllocationBytes;
    uint16_t ExpectedMachine;
    void *FileBuffer;
    memset(&File,0,sizeof(File));

    Status=Platform->OpenFile(Platform->Context,Request->KernelPath,&File);
    if (Status!=Rc95LdrSuccess)
        return Rc95LdrFail(BootContext,Status,0x100u);
    if (!File.FileSize || File.FileSize>SIZE_MAX ||
        File.FileSize>UINT64_MAX-(RC95LDR_PAGE_SIZE-1u))
    {
        Status=Rc95LdrInvalidImage; goto CleanupFile;
    }
    FilePages=Rc95LdrPageCount(File.FileSize);
    FileAllocationBytes=FilePages*RC95LDR_PAGE_SIZE;
    if (FileAllocationBytes>SIZE_MAX) { Status=Rc95LdrOutOfBounds; goto CleanupFile; }
    Status=Platform->AllocatePages(Platform->Context,
        FileAllocationBytes,RC95LDR_PAGE_SIZE,&FileAddress);
    if (Status!=Rc95LdrSuccess) goto CleanupFile;
    if (!FileAddress || FileAddress>UINTPTR_MAX-File.FileSize) {
        Platform->FreePages(Platform->Context,FileAddress,FileAllocationBytes);
        FileAddress=0; Status=Rc95LdrOutOfBounds; goto CleanupFile;
    }
    FileBuffer=(void *)(uintptr_t)FileAddress;
    Status=Platform->ReadFileAt(Platform->Context,&File,0,
                                FileBuffer,(size_t)File.FileSize);
    if (Status!=Rc95LdrSuccess) goto CleanupBuffers;
#if defined(_M_AMD64) || defined(_M_X64) || defined(_AMD64_) || defined(__x86_64__) || defined(__amd64__)
    ExpectedMachine=RC95LDR_PE_MACHINE_AMD64;
#elif defined(_M_IX86) || defined(_X86_) || defined(__i386__) || defined(i386)
    ExpectedMachine=RC95LDR_PE_MACHINE_I386;
#else
    Status=Rc95LdrUnsupported; goto CleanupBuffers;
#endif
    /* Parse the PE machine first so an architecture mismatch can be reported
       as a useful text-screen diagnostic instead of a generic bad-image error. */
    Status=Rc95LdrPeValidate(FileBuffer,(size_t)File.FileSize,0,&Pe);
    if (Status!=Rc95LdrSuccess) goto CleanupBuffers;
    if (Pe.Machine!=ExpectedMachine) {
        Status=Rc95LdrFail(BootContext,Rc95LdrUnsupported,
                           (uint32_t)Pe.Machine);
        goto CleanupBuffers;
    }
    ImagePages=Rc95LdrPageCount(Pe.SizeOfImage);
    ImageAllocationBytes=ImagePages*RC95LDR_PAGE_SIZE;
    if (ImageAllocationBytes>SIZE_MAX) { Status=Rc95LdrOutOfBounds; goto CleanupBuffers; }
    Status=Platform->AllocatePages(Platform->Context,
        ImageAllocationBytes,Pe.SectionAlignment,&ImageAddress);
    if (Status!=Rc95LdrSuccess) goto CleanupBuffers;
    if (!ImageAddress || ImageAddress>UINT64_MAX-Pe.SizeOfImage ||
        ImageAddress>UINTPTR_MAX-Pe.SizeOfImage) {
        Platform->FreePages(Platform->Context,ImageAddress,ImageAllocationBytes);
        ImageAddress=0; Status=Rc95LdrOutOfBounds; goto CleanupBuffers;
    }
    Status=Rc95LdrPeMap(FileBuffer,(size_t)File.FileSize,&Pe,
        (void *)(uintptr_t)ImageAddress,(size_t)ImageAllocationBytes,
        ImageAddress);
    if (Status!=Rc95LdrSuccess)
    {
        Platform->FreePages(Platform->Context,ImageAddress,ImageAllocationBytes);
        ImageAddress=0;
        goto CleanupBuffers;
    }
    Result->KernelImageBase=ImageAddress;
    Result->KernelImageSize=Pe.SizeOfImage;
    Result->KernelEntryPoint=ImageAddress+Pe.EntryPointRva;
    Result->ImageCount=1;
CleanupBuffers:
    if (FileAddress)
        Platform->FreePages(Platform->Context,FileAddress,
                            FilePages*RC95LDR_PAGE_SIZE);
CleanupFile:
    Platform->CloseFile(Platform->Context,&File);
    return Status;
}

RC95LDR_STATUS
LoadAndBootWindows(RC95LDR_BOOT_CONTEXT *BootContext,
                   const RC95LDR_WINLOAD_PLATFORM *Platform,
                   const RC95LDR_WINLOAD_REQUEST *Request,
                   RC95LDR_WINLOAD_RESULT *Result)
{
    RC95LDR_STATUS Status;
    const RC95LDR_TEXTUI *TextUi;
    if (!BootContext) return Rc95LdrInvalidParameter;
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

    TextUi=Rc95LdrGetTextUi(Platform);
    if (TextUi)
        (void)Rc95LdrTextUiShowWelcome(TextUi,
            "Firmware-neutral Windows loader");
    if (TextUi)
        (void)Rc95LdrTextUiShowProgress(TextUi,
            "Validating and loading the Windows kernel image");

    Result->KernelEntryPoint=0;
    Result->KernelImageBase=0;
    Result->KernelImageSize=0;
    Result->LoaderBlockAddress=0;
    Result->LoaderBlockSize=0;
    Result->ResumeEntryPoint=0;
    Result->ImageCount=0;
    Result->Flags=0;

    Status=Rc95LdrWinloadLoadKernel(BootContext,Platform,Request,Result);
    if (Status!=Rc95LdrSuccess) {
        if (TextUi) {
            const char *Title="Kernel image load failed";
            const char *Message="RC95LDR could not validate, read, or map the selected kernel.";
            const char *Hint="Check the selected Windows installation and its architecture.";
            if (Status==Rc95LdrUnsupported &&
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

    /* Kernel PE mapping is integrated, and the profile-native preparation API
       now builds loader blocks from SYSTEM hive results. This orchestration
       entrypoint does not yet load the HAL, gather the final memory map or
       invoke that API with final-address allocations. Do not jump to the
       kernel until those steps and the handoff preflight are connected. */
    Rc95LdrSetStage(BootContext,Rc95LdrStageOsPrepare);
    Status=Rc95LdrFail(BootContext,Rc95LdrUnsupported,0x200u);
    if (TextUi)
        (void)Rc95LdrTextUiShowError(TextUi,"Boot preparation incomplete",
            "The kernel image was mapped, but loader state and final handoff are not ready.",
            Status,BootContext->LastError.Detail,
            "This RC95LDR build does not yet transfer control to the Windows kernel.");
    return Status;
}
