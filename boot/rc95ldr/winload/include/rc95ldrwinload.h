#ifndef _RC95LDR_WINLOAD_H_
#define _RC95LDR_WINLOAD_H_
#include "rc95ldr.h"
#include "rc95ldrtextui.h"
#include "rc95ldrpe.h"
#ifdef __cplusplus
extern "C" {
#endif
#define RC95LDR_WINLOAD_ABI_VERSION 7u
#define RC95LDR_WINLOAD_MAX_SDI_IMAGE (64u * 1024u * 1024u)

struct _RC95LDR_NATIVE_PREPARE_REQUEST;
struct _RC95LDR_WINLOAD_REQUEST;
struct _RC95LDR_NATIVE_PREPARE_RESULT;
struct _RC95LDR_KERNEL_HANDOFF_CONTEXT;
struct _RC95LDR_NATIVE_ALLOCATION_SET;
struct _RC95LDR_LOADED_MODULE;
typedef RC95LDR_STATUS (*RC95LDR_BUILD_NATIVE_PREPARATION)(
    void *Context, const struct _RC95LDR_WINLOAD_REQUEST *WinloadRequest,
    uint64_t KernelVirtualBase, uint64_t KernelPhysicalBase,
    uint64_t KernelImageSize, uint64_t KernelAllocationSize,
    uint64_t KernelEntry, struct _RC95LDR_NATIVE_PREPARE_REQUEST *PrepareRequest);
typedef void (*RC95LDR_RELEASE_NATIVE_PREPARATION)(
    void *Context, struct _RC95LDR_NATIVE_PREPARE_REQUEST *PrepareRequest);
typedef enum _RC95LDR_BOOT_FIRMWARE { Rc95LdrFirmwareBios=1, Rc95LdrFirmwareUefi=2 } RC95LDR_BOOT_FIRMWARE;
/* Optional platform extension describing the exact filesystem provider bound
 * to the selected OS volume. Zero preserves older platform behavior. */
typedef enum _RC95LDR_WINLOAD_FILESYSTEM_KIND {
    Rc95LdrWinloadFsUnknown=0,
    Rc95LdrWinloadFsFat32=1,
    Rc95LdrWinloadFsNtfs=2,
    Rc95LdrWinloadFsBtrfs=3
} RC95LDR_WINLOAD_FILESYSTEM_KIND;
#define RC95LDR_WINLOAD_FS_CAP_READ_ONLY       0x00000001u
#define RC95LDR_WINLOAD_FS_CAP_BTRFS_NATIVE    0x00000002u
typedef enum _RC95LDR_OS_FAMILY {
    Rc95LdrOsWindowsNtLegacy=1, /* NT 4.0 through Server 2003 */
    Rc95LdrOsReactOS=2,
    Rc95LdrOsWindowsModern=3,   /* Vista through Windows 11 */
    Rc95LdrOsRc95=4,
    Rc95LdrOsLiberNt=5           /* CATV 95 LiberNT fork: unsupported */
} RC95LDR_OS_FAMILY;
typedef enum _RC95LDR_RESUME_POLICY {
    Rc95LdrResumeDisabled=0,
    Rc95LdrResumeViaWinresume=1, /* Windows-owned resume application, never RC95-OS */
    Rc95LdrResumeInsideWinload=2 /* RC95-OS only */
} RC95LDR_RESUME_POLICY;
struct _RC95LDR_WINLOAD_PLATFORM;
typedef struct _RC95LDR_WINLOAD_FILE { void *FileContext; uint64_t FileSize; } RC95LDR_WINLOAD_FILE;
/* Shared WINLOAD file boundary; uses the active scoped provider when present. */
RC95LDR_STATUS Rc95LdrWinloadFileOpen(
    const struct _RC95LDR_WINLOAD_PLATFORM *Platform,
    const uint16_t *Path, RC95LDR_WINLOAD_FILE *File);
RC95LDR_STATUS Rc95LdrWinloadFileReadAt(
    const struct _RC95LDR_WINLOAD_PLATFORM *Platform,
    const RC95LDR_WINLOAD_FILE *File, uint64_t Offset,
    void *Buffer, size_t Length);
void Rc95LdrWinloadFileClose(
    const struct _RC95LDR_WINLOAD_PLATFORM *Platform,
    RC95LDR_WINLOAD_FILE *File);
typedef struct _RC95LDR_WINLOAD_MEMORY_RANGE {
    uint64_t Base; uint64_t Length; uint32_t Type; uint32_t Attributes;
} RC95LDR_WINLOAD_MEMORY_RANGE;
typedef struct _RC95LDR_WINLOAD_PLATFORM {
    uint32_t StructureSize; uint32_t Version; void *Context;
    RC95LDR_STATUS (*OpenFile)(void *, const uint16_t *, RC95LDR_WINLOAD_FILE *);
    RC95LDR_STATUS (*ReadFileAt)(void *, const RC95LDR_WINLOAD_FILE *, uint64_t, void *, size_t);
    void (*CloseFile)(void *, RC95LDR_WINLOAD_FILE *);
    /* Returns a physical address that is identity-mapped and writable until
       handoff. Size is bytes; Alignment is a power-of-two byte alignment. */
    RC95LDR_STATUS (*AllocatePages)(void *, uint64_t, uint64_t, uint64_t *);
    void (*FreePages)(void *, uint64_t, uint64_t);
    /* Attests that a module extent belongs to a live allocation recorded by
       this platform allocator. Rejects pointer casts and fabricated extents. */
    RC95LDR_STATUS (*ValidateAllocationExtent)(void *, uint64_t, uint64_t,
        uint64_t, uint64_t);
    RC95LDR_STATUS (*GetMemoryMap)(void *, RC95LDR_WINLOAD_MEMORY_RANGE *, size_t, size_t *);
    /* Must not return on success. UEFI implementation exits boot services first. */
    RC95LDR_STATUS (*ExitFirmwareAndHandoff)(void *, uint64_t, uint64_t, uint64_t);
    /* Optional firmware text console. NULL keeps headless operation. Platform
       extensions are appended and guarded by StructureSize. ABI v3 cold boot
       requires the native preparation and firmware handoff callbacks below. */
    const RC95LDR_TEXTUI *TextUi;
    /* Required only for Rc95LdrResumeInsideWinload. Physical ranges must be
       checked against the firmware memory map and all loader-owned pages. */
    RC95LDR_STATUS (*ValidateResumeRange)(void *, uint64_t, uint64_t);
    RC95LDR_STATUS (*MapPhysicalRange)(void *, uint64_t, uint64_t, void **);
    void (*UnmapPhysicalRange)(void *, void *, uint64_t);
    /* Architecture backend validates the saved I386/AMD64 state. The
       following legacy slot is retained for ABI layout compatibility only;
       RC95LDR never dispatches processor restoration through a platform
       callback. The linked WINLOAD backend calls Rc95LdrRestoreProcessorState
       directly after memory restoration and resume-flag publication. */
    RC95LDR_STATUS (*ValidateResumeProcessorState)(void *, uint32_t,
        const void *, uint32_t, uint32_t, uint32_t, uint32_t);
    void *ReservedLegacyRestoreResumeProcessorState;
    /* Optional probe used by RC95-OS to choose resume versus cold boot.
       Returns nonzero only when the configured hiberfile is present. */
    int (*HiberFileExists)(void *, const uint16_t *);
    /* Cold-boot transaction adapter. BuildNativePreparation supplies the
       mapped HAL/base modules, resident SYSTEM hive, metadata, final memory
       map and final-address loader-block storage. Its BaseInput module list
       must contain exactly one kernel record matching the supplied image.
       HAL records and LoadBootDriver results must provide complete, page-aligned
       PhysicalBase/PhysicalSize extents copied from the allocator's physical
       return values, never derived by casting ImageBase. They must also provide
       PeSections/PeSectionCount/PeSizeOfHeaders from a successful Rc95LdrPeValidate
       call; the immutable section array must remain alive through baseline page
       table construction. Platform adapters should use Rc95LdrWinloadLoadPeModule
       to perform validation, mapping and durable metadata placement consistently.
       ImageBase is the loader virtual mapping; PhysicalBase is the allocator's
       physical address. The default allocator contract currently identity maps
       these pages until handoff.
       osload passes both kernel addresses and the page-rounded allocation size;
       HAL aliases cloned as boot-driver records retain the same physical extent.
       ReleaseNativePreparation must tolerate a zeroed or partially populated
       request because it is called after both successful and failed builds.
       Neither callback may transfer control. */
    RC95LDR_BUILD_NATIVE_PREPARATION BuildNativePreparation;
    RC95LDR_RELEASE_NATIVE_PREPARATION ReleaseNativePreparation;
    /* Optional final-stage import resolver. It must apply OS-specific module
       policy, load/validate any dependency before returning, and return only
       an address inside that dependency's mapped image. Missing resolver
       leaves import-bearing kernels unsupported. */
    RC95LDR_PE_IMPORT_RESOLVER ResolveKernelImport;
    /* Hibernation advanced-options input and explicit image deletion for cold boot. */
    const RC95LDR_TEXTUI_EXT *TextUiExtension;
    RC95LDR_STATUS (*DeleteHiberFile)(void *, const uint16_t *);
    /* Optional dedicated context lets filesystem deletion use a mounted
       volume without replacing the platform context used by other services. */
    void *DeleteHiberFileContext;
    /* Appended provider attestation. Btrfs is accepted only when the exact
       selected OS volume is served by the native read-only VENEER adapter.
       FAT32 and NTFS retain their existing platform-provider behavior. */
    uint32_t BootFileSystemKind;
    uint32_t BootFileSystemCapabilities;
    /*
     * Versioned native handoff extension. Receives the complete validated
     * preparation context, not just entry/loader-block addresses. UEFI
     * implementations must capture the final map and call ExitBootServices
     * before the AMD64 transfer; BIOS implementations must finish their
     * provider-owned transition before invoking the same validated transfer.
     * On success this callback never returns. Existing i386 providers retain
     * the legacy callback and ABI behavior.
     */
    RC95LDR_STATUS (*ExitFirmwareAndValidatedHandoff)(
        void *, struct _RC95LDR_KERNEL_HANDOFF_CONTEXT *);
    /* Dedicated context for the final firmware callback. Keeps UEFI image,
       memory-map storage and allocator ledger separate from the filesystem /
       allocation context shared by the other platform services. */
    void *ExitFirmwareContext;
    /* UEFI final-map pages are registered here so native preparation folds
     * them into the OS memory map and physical-allocation validation ledger. */
    const struct _RC95LDR_NATIVE_ALLOCATION_SET *NativeAllocationSet;
    /* Firmware-owned UEFI system/runtime service table addresses, copied
     * from the validated Loaded Image context by the native provider. */
    uint64_t FirmwareSystemTable;
    uint64_t FirmwareRuntimeServices;
    /* Optional independent context for native preparation/release callbacks.
     * Lets RC95's provider own its state without reusing a filesystem or
     * FreeLoader context. NULL preserves the original Platform->Context ABI. */
    void *NativePreparationContext;
} RC95LDR_WINLOAD_PLATFORM;
typedef struct _RC95LDR_WINLOAD_REQUEST {
    uint32_t StructureSize; uint32_t Version;
    RC95LDR_OS_FAMILY OsFamily;
    RC95LDR_BOOT_FIRMWARE Firmware;
    RC95LDR_RESUME_POLICY ResumePolicy;
    uint32_t Flags;
    const uint16_t *SystemRoot;
    const uint16_t *KernelPath;
    const uint16_t *HalPath;
    const uint16_t *BootDriverDirectory;
    const uint16_t *SystemHivePath;
    void *BootConfiguration;
    /* Explicit boot route and NT version prevent cross-family ABI guessing. */
    uint32_t BootEntryKind;
    uint32_t OsMajorVersion;
    uint32_t OsMinorVersion;
    uint32_t OsBuildNumber;
    /* Optional volume-root hiberfile path; defaults to \\hiberfil.sys.
       Appended to preserve the offsets of the existing v2 request fields. */
    const uint16_t *HiberFilePath;
    /* Selected BCD OS-loader element 0x26000046; effective only when policy is valid. */
    uint32_t GraphicsModeDisabled;
    /* Set only after the selected BCD object has been decoded. */
    uint32_t GraphicsModePolicyValid;
    const void *RamdiskSdiImage;
    uint64_t RamdiskSdiImageSize, RamdiskSdiDiskOffset, RamdiskSdiDiskLength;
    uint32_t RamdiskSdiMemoryType, RamdiskSdiFlags;
    /* Synchronous BuildNativePreparation input; points into the validated PE_INFO. */
    const RC95LDR_PE_SECTION_INFO *KernelPeSections;
    uint32_t KernelPeSectionCount, KernelPeSizeOfHeaders;
} RC95LDR_WINLOAD_REQUEST;
#define RC95LDR_HIBER_MACHINE_I386  0x014Cu
#define RC95LDR_HIBER_MACHINE_AMD64 0x8664u
#define RC95LDR_HIBER_FLAG_IMAGE_COMPLETE 0x00000002u
#define RC95LDR_HIBER_FLAG_HAS_CPU_STATE  0x00000004u
#define RC95LDR_HIBER_FLAG_UNIPROCESSOR   0x00000008u
#define RC95LDR_HIBER_FLAG_HAS_FX_STATE   0x00000010u
#define RC95LDR_HIBER_FLAG_HAS_FPU_STATE  0x00000020u

typedef struct _RC95LDR_WINLOAD_RESULT {
    uint32_t StructureSize; uint32_t Version;
    uint64_t KernelEntryPoint; uint64_t KernelImageBase;
    uint64_t KernelImageSize; uint64_t LoaderBlockAddress;
    uint64_t LoaderBlockSize; uint64_t ResumeEntryPoint;
    /* Distinct address domains: virtual mapping vs allocator-returned physical extent. */
    uint64_t KernelPhysicalBase, KernelAllocationSize;
    uint32_t ImageCount; uint32_t Flags;
} RC95LDR_WINLOAD_RESULT;
/*
 * Unified firmware-neutral OS preparation entry point. OS family selects the
 * loader-block/handoff profile; firmware supplies platform services.
 * NT legacy ({ntldr}) and ReactOS ({rosboot}) remain separate compatibility
 * dispatches, never implicit aliases for the Vista+ profile. ABI v2 requires
 * an explicit boot-entry kind and OS version identity, checked by profile.c.
 *
 * PE validation/mapping and initial kernel mapping are implemented. The
 * routine still returns Rc95LdrUnsupported after mapping until dependencies,
 * profile-specific loader blocks, and firmware handoff are complete.
 */
/* Restores a validated RC95 hiberfile. Successful processor restoration does
   not return; errors are returned to Rc95LdrLoadAndBootWindows. */
/* Native I386/AMD64 WINLOAD architecture callback. The final restore is
   non-returning; platform adapters may install this directly. */
RC95LDR_STATUS Rc95LdrRestoreProcessorState(
    void *Context, uint32_t Machine, const void *ProcessorState,
    uint32_t ProcessorStateSize, uint32_t Flags, uint32_t FxStateOffset,
    uint64_t ResumeFlagPhysicalAddress);
/* Adapter helper for Platform.DeleteHiberFile. Context is an RC95LDR_VOLUME
 * whose filesystem supplies DeleteFile; NULL path means the default hiberfile. */
RC95LDR_STATUS Rc95LdrWinloadDeleteHiberFile(void *VolumeContext,
    const uint16_t *Path);
/* Validates optional boot-volume provider attestation. Legacy FAT32/NTFS
 * providers remain unchanged; Btrfs requires the native read-only VENEER. */
RC95LDR_STATUS Rc95LdrWinloadValidateFilesystemProvider(
    const RC95LDR_WINLOAD_PLATFORM *Platform);
RC95LDR_STATUS Rc95LdrResumeFromHibernation(
    const RC95LDR_WINLOAD_PLATFORM *Platform,
    const RC95LDR_WINLOAD_REQUEST *Request);
/*
 * Common platform-adapter loader for HAL and boot-start driver PE images.
 * Allocates/maps the image and stores a private copy of validated section
 * metadata in the same allocator-owned extent. StableModulePath must outlive
 * native preparation. The returned module owns PhysicalBase/PhysicalSize;
 * release it exactly once with Rc95LdrWinloadReleasePeModule after native
 * preparation has finished. HAL boot-driver aliases must copy the record,
 * not load or free the image a second time.
 */
RC95LDR_STATUS Rc95LdrWinloadLoadPeModule(
    const RC95LDR_WINLOAD_PLATFORM *Platform,
    const uint16_t *FilePath,
    uint16_t ExpectedMachine,
    uint32_t ModuleFlags,
    const char *StableModulePath,
    RC95LDR_PE_IMPORT_RESOLVER ImportResolver,
    void *ImportResolverContext,
    struct _RC95LDR_LOADED_MODULE *Module);
void Rc95LdrWinloadReleasePeModule(
    const RC95LDR_WINLOAD_PLATFORM *Platform,
    struct _RC95LDR_LOADED_MODULE *Module);

RC95LDR_STATUS Rc95LdrLoadAndBootWindows(RC95LDR_BOOT_CONTEXT *BootContext,
    const RC95LDR_WINLOAD_PLATFORM *Platform,
    const RC95LDR_WINLOAD_REQUEST *Request,
    RC95LDR_WINLOAD_RESULT *Result);
#ifdef __cplusplus
}
#endif
#endif
