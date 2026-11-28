#ifndef _RC95LDR_RC95_NATIVE_PROVIDER_H_
#define _RC95LDR_RC95_NATIVE_PROVIDER_H_

#include "rc95ldrnativeprepare.h"
#include "rc95ldrwinload.h"
#include "rc95bcdwalk.h"
#include "rc95ldr_volumeprovider.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RC95LDR_RC95_PROVIDER_VERSION 8u
#define RC95LDR_RC95_PROVIDER_ALLOCATION_MAX 12u
#define RC95LDR_RC95_CPU_SIGNATURE 0x55504352u /* RCPU */
#define RC95LDR_RC95_SCHED_SIGNATURE 0x44435352u /* RSCD */
#define RC95LDR_RC95_ARCH_SIGNATURE 0x48414352u /* RCAH */
#define RC95LDR_RC95_PERCPU_SIGNATURE 0x43504352u /* RCPC */
#define RC95LDR_CPU_FEATURE_CPUID       0x00000001u
#define RC95LDR_CPU_FEATURE_FPU         0x00000002u
#define RC95LDR_CPU_FEATURE_TSC         0x00000004u
#define RC95LDR_CPU_FEATURE_MSR         0x00000008u
#define RC95LDR_CPU_FEATURE_PAE         0x00000010u
#define RC95LDR_CPU_FEATURE_CX8         0x00000020u /* CMPXCHG8B */
#define RC95LDR_CPU_FEATURE_APIC        0x00000040u
#define RC95LDR_CPU_FEATURE_SEP         0x00000080u
#define RC95LDR_CPU_FEATURE_MTRR        0x00000100u
#define RC95LDR_CPU_FEATURE_PGE         0x00000200u
#define RC95LDR_CPU_FEATURE_CMOV        0x00000400u
#define RC95LDR_CPU_FEATURE_MMX         0x00000800u
#define RC95LDR_CPU_FEATURE_FXSR        0x00001000u
#define RC95LDR_CPU_FEATURE_SSE         0x00002000u
#define RC95LDR_CPU_FEATURE_SSE2        0x00004000u
#define RC95LDR_CPU_FEATURE_SSE3        0x00008000u
#define RC95LDR_CPU_FEATURE_SSSE3       0x00010000u
#define RC95LDR_CPU_FEATURE_SSE41       0x00020000u
#define RC95LDR_CPU_FEATURE_SSE42       0x00040000u
#define RC95LDR_CPU_FEATURE_NX          0x00080000u
#define RC95LDR_CPU_FEATURE_LONG_MODE   0x00100000u
#define RC95LDR_CPU_FEATURE_PAGE1G      0x00200000u
#define RC95LDR_CPU_FEATURE_RDTSCP      0x00400000u
#define RC95LDR_CPU_FEATURE_AVX         0x00800000u
#define RC95LDR_CPU_FEATURE_AVX2        0x01000000u
#define RC95LDR_CPU_FEATURE_SMEP        0x02000000u
#define RC95LDR_CPU_FEATURE_SMAP        0x04000000u
#define RC95LDR_CPU_FEATURE_INVPCID     0x08000000u
#define RC95LDR_CPU_FEATURE_CX16        0x10000000u
#define RC95LDR_CPU_FEATURE_AES         0x20000000u
#define RC95LDR_CPU_FEATURE_XSAVE       0x40000000u
#define RC95LDR_CPU_FEATURE_RDRAND      0x80000000u
#define RC95LDR_RC95_CPU_INFO_VERSION 2u
#define RC95LDR_CPU_PAGING_I386_NONPAE 1u
#define RC95LDR_CPU_PAGING_I386_PAE   2u
#define RC95LDR_CPU_PAGING_AMD64      3u
#define RC95LDR_PAE_WINDOW_MAPPING_MAX 64u
#define RC95LDR_PAE_WINDOW_PHYSICAL_MIN 0x100000000ULL
#define RC95LDR_PAE_MAPPING_WRITABLE 0x00000001u
#define RC95LDR_PAE_MAPPING_CACHEABLE 0x00000002u

typedef struct _RC95LDR_PAE_VIRTUAL_WINDOW { uint64_t VirtualBase, Length; } RC95LDR_PAE_VIRTUAL_WINDOW;
typedef struct _RC95LDR_PAE_PHYSICAL_MAPPING { uint64_t PhysicalBase, VirtualBase, Length; uint32_t Flags, Reserved; } RC95LDR_PAE_PHYSICAL_MAPPING;

typedef struct _RC95LDR_RC95_CPU_INFO {
    uint32_t StructureSize, Version;
    uint32_t ProcessorId, HardwareId;
    uint32_t FeatureFlags, PhysicalAddressBits;
    uint32_t Vendor[3];
    uint32_t ProcessorFamily, ProcessorModel, ProcessorStepping;
    uint32_t MaxBasicLeaf, MaxExtendedLeaf, ProcessorSignature;
    uint32_t Leaf1Ecx, Leaf1Edx;
    uint32_t Leaf7Ebx, Leaf7Ecx, Leaf7Edx;
    uint32_t ExtendedLeaf1Ecx, ExtendedLeaf1Edx;
    uint32_t PagingMode, VirtualAddressBits;
} RC95LDR_RC95_CPU_INFO;

RC95LDR_STATUS Rc95LdrBuildI386PaeWindowMappings(const RC95LDR_RC95_CPU_INFO *CpuInformation, const RC95LDR_PAE_VIRTUAL_WINDOW *Window, const RC95LDR_LOADED_MODULE *Modules, size_t ModuleCount, const RC95LDR_NATIVE_ALLOCATION *Allocations, size_t AllocationCount, const RC95LDR_FIRMWARE_MEMORY_RANGE *FirmwareRanges, size_t FirmwareRangeCount, RC95LDR_PAE_PHYSICAL_MAPPING *Mappings, size_t MappingCapacity, size_t *MappingCount);
RC95LDR_STATUS Rc95LdrInstallI386PaeWindowMappings(
    const RC95LDR_RC95_CPU_INFO *CpuInformation,
    const RC95LDR_PAE_VIRTUAL_WINDOW *Window,
    const RC95LDR_PAE_PHYSICAL_MAPPING *Mappings, size_t MappingCount,
    const RC95LDR_PHYSMEM_ALLOCATION *PageTableStorage,
    uint64_t RootPhysicalAddress, size_t *PageTableStorageUsed);

RC95LDR_STATUS Rc95LdrQueryX86Cpu(
    void *Context, RC95LDR_NT_TARGET_ARCH Architecture,
    RC95LDR_RC95_CPU_INFO *Information);

typedef RC95LDR_STATUS (*RC95LDR_RC95_QUERY_CPU)(
    void *Context, RC95LDR_NT_TARGET_ARCH Architecture,
    RC95LDR_RC95_CPU_INFO *Information);

/* Optional platform override. NULL selects Rc95LdrBuildBaselinePageTables.
 * Build the baseline real page tables in the supplied storage. Preserve
 * identity mappings for loader-owned objects and low usable RAM. Map each
 * module at ImageBase -> PhysicalBase and each provider allocation at its
 * WritableMapping address. Return the physical root page and the page-aligned
 * end offset of all table pages consumed. The provider installs the bounded
 * I386 PAE high-memory mappings after this callback returns. */
typedef RC95LDR_STATUS (*RC95LDR_RC95_BUILD_PAGE_TABLES)(
    void *Context, RC95LDR_NT_TARGET_ARCH Architecture,
    const RC95LDR_RC95_CPU_INFO *CpuInformation,
    const RC95LDR_PAE_VIRTUAL_WINDOW *PaeWindow,
    const RC95LDR_PHYSMEM_ALLOCATION *PageTableStorage,
    const RC95LDR_LOADED_MODULE *Modules, uint32_t ModuleCount,
    const RC95LDR_NATIVE_ALLOCATION *Allocations, size_t AllocationCount,
    const RC95LDR_FIRMWARE_MEMORY_RANGE *FirmwareRanges,
    size_t FirmwareRangeCount, size_t *PageTableStorageUsed,
    uint64_t *RootPhysicalAddress);

/* Default baseline builder used when the platform supplies no override. It
 * creates real hardware page tables and returns the physical root plus exact
 * arena use. The PAE window is mandatory for I386 PAE and NULL otherwise. */
RC95LDR_STATUS Rc95LdrBuildBaselinePageTables(
    void *Context, RC95LDR_NT_TARGET_ARCH Architecture,
    const RC95LDR_RC95_CPU_INFO *CpuInformation,
    const RC95LDR_PAE_VIRTUAL_WINDOW *PaeWindow,
    const RC95LDR_PHYSMEM_ALLOCATION *PageTableStorage,
    const RC95LDR_LOADED_MODULE *Modules, uint32_t ModuleCount,
    const RC95LDR_NATIVE_ALLOCATION *Allocations, size_t AllocationCount,
    const RC95LDR_FIRMWARE_MEMORY_RANGE *FirmwareRanges,
    size_t FirmwareRangeCount, size_t *PageTableStorageUsed,
    uint64_t *RootPhysicalAddress);

typedef struct _RC95LDR_RC95_PERCPU_STATE {
    uint32_t Signature, Size, Version, ProcessorId;
    uint64_t HardwareId, SchedulerState, CpuState, ArchitectureState;
    uint64_t KernelStackBase, KernelStackSize;
    uint32_t FeatureFlags, Flags;
} RC95LDR_RC95_PERCPU_STATE;

typedef struct _RC95LDR_RC95_SCHEDULER_STATE {
    uint32_t Signature, Size, Version, ProcessorCount;
    uint64_t CurrentThread, IdleThread, ReadyQueueHead, ReadyQueueTail;
    uint64_t IdleStackBase, IdleStackSize, Quantum;
    uint32_t Flags, Reserved;
} RC95LDR_RC95_SCHEDULER_STATE;

typedef struct _RC95LDR_RC95_ARCH_STATE {
    uint32_t Signature, Size, Version, Architecture;
    uint64_t PageTableRoot, KernelEntry, LoaderBlock;
    uint64_t StackBase, StackSize, StackPointer;
    uint32_t PhysicalAddressBits, FeatureFlags;
    uint32_t PagingMode, VirtualAddressBits;
} RC95LDR_RC95_ARCH_STATE;

typedef struct _RC95LDR_RC95_CPU_STATE {
    uint32_t Signature, Size, Version, ProcessorId;
    uint32_t FeatureFlags, PhysicalAddressBits;
    uint32_t Vendor[3], Reserved;
    uint32_t PagingMode, VirtualAddressBits;
    uint32_t Leaf1Ecx, Leaf1Edx;
    uint32_t Leaf7Ebx, Leaf7Ecx, Leaf7Edx;
    uint32_t ExtendedLeaf1Ecx, ExtendedLeaf1Edx;
    uint32_t ProcessorFamily, ProcessorModel, ProcessorStepping;
    uint32_t MaxBasicLeaf, MaxExtendedLeaf, ProcessorSignature;
    uint64_t InitialStackPointer, InitialInstructionPointer;
} RC95LDR_RC95_CPU_STATE;

typedef struct _RC95LDR_RC95_NATIVE_PROVIDER_INPUT {
    uint32_t StructureSize, Version;
    const RC95LDR_OS_PROFILE *Profile;
    const RC95LDR_NATIVE_LOADER_INPUT *BaseInput;
    /* Transaction-scoped BIOS discovery bindings. Valid only while the
       selected NTFS volume and its file-provider scope remain active. */
    const RC95LDR_WINDOWS_INSTALLATION *SelectedInstallation;
    const RC95LDR_FILE_PROVIDER *BootFileProvider;
    /* Original WINLOAD platform supplies the allocator and allocation ledger. */
    const RC95LDR_WINLOAD_PLATFORM *WinloadPlatform;
    uint64_t KernelVirtualBase, KernelPhysicalBase;
    uint64_t KernelImageSize, KernelAllocationSize, KernelEntry;
    const RC95LDR_PE_SECTION_INFO *KernelPeSections;
    uint32_t KernelPeSectionCount, KernelPeSizeOfHeaders;
    RC95LDR_NATIVE_ALLOCATION_SET *SharedNativeAllocationSet;
    const void *SystemHive;
    size_t SystemHiveSize;
    uint64_t SystemHivePhysicalBase, SystemHiveAllocationSize;
    RC95LDR_LOAD_HIVE_BOOT_DRIVER LoadBootDriver;
    void *DriverLoadContext;
    RC95LDR_VALIDATE_MODULE_ALLOCATION ValidateModuleAllocation;
    void *AllocationValidationContext;
    const RC95LDR_LOADED_MODULE *BootResourceModule;
    const RC95LDR_PHYSMEM_OPS *PhysicalMemory;
    RC95LDR_RC95_QUERY_CPU QueryCpu;
    void *CpuContext;
    RC95LDR_RC95_BUILD_PAGE_TABLES BuildPageTables;
    void *PageTableContext;
    RC95LDR_PAE_VIRTUAL_WINDOW PaeWindow; /* Required only for I386 PAE. */
    const RC95LDR_FIRMWARE_MEMORY_RANGE *FirmwareRanges;
    size_t FirmwareRangeCount;
    RC95LDR_METADATA_INPUT Metadata;
    RC95LDR_PLATFORM_INVENTORY_INPUT PlatformInventory;
    uint64_t FirmwareSystemTable, FirmwareRuntimeServices;
    uint64_t FirmwareMapAddress;
    uint32_t FirmwareMapSize, FirmwareMapDescriptorSize;
    uint64_t UefiMapPhysicalBase, UefiMapAllocationSize;
    const void *BiosFirmwareState;
    uint32_t BiosFirmwareStateSize, BiosFirmwareStateVersion;
    uint64_t BiosFirmwareStatePhysicalBase, BiosFirmwareStateAllocationSize;
    const char *BootDevice, *HalDevice, *SystemRoot, *LoadOptions;
} RC95LDR_RC95_NATIVE_PROVIDER_INPUT;

typedef struct _RC95LDR_RC95_NATIVE_PROVIDER_RESULT {
    uint32_t StructureSize, Version, AllocationCount, Reserved;
    RC95LDR_PHYSMEM_OPS PhysicalMemory;
    RC95LDR_OS_PROFILE Profile;
    RC95LDR_NATIVE_LOADER_INPUT BaseInput;
    RC95LDR_LOADED_MODULE Modules[RC95LDR_MODULE_MAX];
    RC95LDR_PHYSMEM_ALLOCATION Allocations[RC95LDR_RC95_PROVIDER_ALLOCATION_MAX];
    RC95LDR_NATIVE_ALLOCATION NativeAllocations[RC95LDR_MEMORYMAP_MAX_RESERVATIONS];
    size_t NativeAllocationCount;
    RC95LDR_NATIVE_ALLOCATION_SET *SharedNativeAllocationSet;
    RC95LDR_NATIVE_ALLOCATION_SET SharedNativeAllocationSetBefore;
    uint32_t SharedAllocationSetMerged;
    RC95LDR_MEMORY_RESERVATION_SET MemoryReservations;
    RC95LDR_RC95_CPU_INFO CpuInformation;
    RC95LDR_PAE_PHYSICAL_MAPPING PaeMappings[RC95LDR_PAE_WINDOW_MAPPING_MAX];
    size_t PaeMappingCount;
    RC95LDR_PAE_VIRTUAL_WINDOW PaeWindow;
    RC95LDR_RC95_PERCPU_STATE *PerCpu;
    RC95LDR_RC95_SCHEDULER_STATE *Scheduler;
    RC95LDR_RC95_ARCH_STATE *ArchitectureState;
    RC95LDR_RC95_CPU_STATE *CpuState;
    RC95LDR_ARCH_HANDOFF_INPUT ArchitectureHandoff;
    RC95LDR_METADATA_INPUT MetadataInput;
    RC95LDR_RC95_LOADERSTATE_INPUT Rc95LoaderStateInput;
    RC95LDR_NATIVE_PREPARE_REQUEST NativeRequest;
    /* Transaction-owned driver images and stable paths loaded through NTFS. */
    const RC95LDR_FILE_PROVIDER *BootFileProvider;
    const RC95LDR_WINDOWS_INSTALLATION *InputInstallation;
    const RC95LDR_WINLOAD_PLATFORM *WinloadPlatform;
    RC95LDR_LOADED_MODULE BootDriverModules[RC95LDR_MODULE_MAX];
    char BootDriverPaths[RC95LDR_MODULE_MAX][320];
    uint32_t BootDriverCount;
    uint64_t StackBase, StackSize, StackPointer, PageTableRoot;
    uint64_t LoaderBlockPhysicalBase, MetadataPhysicalBase;
    uint64_t BiosFirmwareStateAddress;
    uint32_t BiosFirmwareStateSize, BiosFirmwareStateVersion;
} RC95LDR_RC95_NATIVE_PROVIDER_RESULT;

/* This provider has no FreeLoader dependency. The caller supplies the
 * firmware-native physical-memory and hardware access operations. */
RC95LDR_STATUS Rc95LdrRc95NativeLoadBootDriver(
    void *Context, const RC95LDR_HIVE_BOOT_DRIVER *Driver,
    RC95LDR_LOADED_MODULE *LoadedModule);
RC95LDR_STATUS Rc95LdrPrepareRc95NativeInput(
    const RC95LDR_RC95_NATIVE_PROVIDER_INPUT *Input,
    RC95LDR_RC95_NATIVE_PROVIDER_RESULT *Result);
void Rc95LdrRc95NativeReleaseBootDrivers(
    RC95LDR_RC95_NATIVE_PROVIDER_RESULT *Result);
void Rc95LdrReleaseRc95NativeInput(
    RC95LDR_RC95_NATIVE_PROVIDER_RESULT *Result);

typedef struct _RC95LDR_RC95_NATIVE_PLATFORM_CONTEXT {
    const RC95LDR_RC95_NATIVE_PROVIDER_INPUT *Input;
    RC95LDR_RC95_NATIVE_PROVIDER_RESULT *Result;
    RC95LDR_OS_PROFILE Profile;
} RC95LDR_RC95_NATIVE_PLATFORM_CONTEXT;

/* Direct adapters for RC95LDR_WINLOAD_PLATFORM.BuildNativePreparation and
 * ReleaseNativePreparation. Store this context separately from filesystem
 * and firmware contexts in NativePreparationContext. */
RC95LDR_STATUS Rc95LdrRc95NativeBuildPreparation(
    void *Context, const RC95LDR_WINLOAD_REQUEST *WinloadRequest,
    uint64_t KernelVirtualBase, uint64_t KernelPhysicalBase,
    uint64_t KernelImageSize, uint64_t KernelAllocationSize,
    uint64_t KernelEntry, RC95LDR_NATIVE_PREPARE_REQUEST *PrepareRequest);
void Rc95LdrRc95NativeReleasePreparation(
    void *Context, RC95LDR_NATIVE_PREPARE_REQUEST *PrepareRequest);
RC95LDR_STATUS Rc95LdrAttachRc95NativeProvider(
    RC95LDR_WINLOAD_PLATFORM *Platform,
    RC95LDR_RC95_NATIVE_PLATFORM_CONTEXT *Context);

#ifdef __cplusplus
}
#endif
#endif
