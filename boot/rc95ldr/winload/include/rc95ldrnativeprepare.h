#ifndef _RC95LDR_NATIVE_PREPARE_H_
#define _RC95LDR_NATIVE_PREPARE_H_
#include "rc95ldrloaderblock.h"
#include "rc95ldrsystemhive.h"
#include "rc95ldrhandoff.h"
#include "rc95ldrloaderstate.h"
#include "rc95ldrrc95loaderstate.h"
#include "rc95ldrarchprepare.h"
#include "rc95ldrplatforminventory.h"
#include "rc95ldrmetadata.h"
#include "rc95ldrmemorymap.h"
#ifdef __cplusplus
extern "C" {
#endif
#define RC95LDR_NATIVE_PREPARE_VERSION 16u
typedef struct _RC95LDR_NATIVE_PREPARE_REQUEST RC95LDR_NATIVE_PREPARE_REQUEST;
typedef RC95LDR_STATUS (*RC95LDR_LAUNCH_ROSLOAD)(void *Context, const RC95LDR_NATIVE_PREPARE_REQUEST *Request);
typedef void (*RC95LDR_REPORT_BOOT_ERROR)(void *Context, const char *Message);
/* Platform provider performs CPUID-based AMD64 long-mode detection. */
typedef RC95LDR_STATUS (*RC95LDR_CHECK_AMD64_CPU_SUPPORT)(void *Context);
typedef RC95LDR_STATUS (*RC95LDR_LOAD_HIVE_BOOT_DRIVER)(
    void *Context, const RC95LDR_HIVE_BOOT_DRIVER *Driver,
    RC95LDR_LOADED_MODULE *LoadedModule);
typedef RC95LDR_STATUS (*RC95LDR_VALIDATE_MODULE_ALLOCATION)(
    void *Context, uint64_t VirtualBase, uint64_t PhysicalBase,
    uint64_t ImageSize, uint64_t AllocationSize);
/* Validates the optional resource-only bootres.dll module before loader-list insertion. */
RC95LDR_STATUS Rc95LdrValidateBootResourceModule(
    const RC95LDR_LOADED_MODULE *Module,
    RC95LDR_VALIDATE_MODULE_ALLOCATION ValidateAllocation,
    void *ValidationContext);
/* Optional RC95 bootres handoff policy. A NULL module represents
 * graphicsmodedisabled and leaves the load-order module array unchanged.
 * Invalid/unattested optional resources are omitted without failing boot. */
RC95LDR_STATUS Rc95LdrAppendBootResourceModule(
    const RC95LDR_LOADED_MODULE *Module,
    RC95LDR_VALIDATE_MODULE_ALLOCATION ValidateAllocation,
    void *ValidationContext,
    RC95LDR_LOADED_MODULE *Modules,
    uint32_t *ModuleCount,
    uint32_t ModuleCapacity,
    uint32_t *Added);
struct _RC95LDR_NATIVE_PREPARE_REQUEST {
    uint32_t StructureSize, Version;
    const RC95LDR_OS_PROFILE *Profile;
    const uint16_t *RosLoadPath;
    const RC95LDR_NATIVE_LOADER_INPUT *BaseInput;
    const void *SystemHiveBytes;
    size_t SystemHiveSize;
    void *LoaderBlockBuffer;
    size_t LoaderBlockCapacity;
    RC95LDR_LOAD_HIVE_BOOT_DRIVER LoadBootDriver;
    void *DriverLoadContext;
    RC95LDR_VALIDATE_MODULE_ALLOCATION ValidateModuleAllocation;
    void *AllocationValidationContext;
    RC95LDR_LAUNCH_ROSLOAD LaunchRosLoad;
    void *RosLoadContext;
    uint64_t FirmwareSystemTable;
    /* BIOS snapshot virtual handoff address plus its physical reservation. */
    uint64_t BiosFirmwareStateAddress;
    uint32_t BiosFirmwareStateSize, BiosFirmwareStateVersion;
    uint64_t BiosFirmwareStatePhysicalBase, BiosFirmwareStateAllocationSize;
    const RC95LDR_LOADERSTATE_INPUT *LoaderStateInput; /* NT profiles only */
    const RC95LDR_RC95_LOADERSTATE_INPUT *Rc95LoaderStateInput; /* RC95 profile only */
    const RC95LDR_ARCH_HANDOFF_INPUT *ArchitectureHandoffInput;
    const RC95LDR_PLATFORM_INVENTORY_INPUT *PlatformInventoryInput;
    const RC95LDR_METADATA_INPUT *MetadataInput;
    /* Optional synchronous fatal-error display callback; message is ASCII. */
    RC95LDR_REPORT_BOOT_ERROR ReportBootError;
    void *BootErrorContext;
    /* Required only for AMD64 profiles; never queried for I386. */
    RC95LDR_CHECK_AMD64_CPU_SUPPORT CheckAmd64CpuSupport;
    void *CpuFeatureContext;
    /* Physical extents for registry/NLS/heap/stack/page tables/loader block/UEFI map. */
    const RC95LDR_NATIVE_ALLOCATION *NativeAllocations;
    size_t NativeAllocationCount;
    RC95LDR_MEMORY_RESERVATION_SET *MemoryReservations;
    const RC95LDR_FIRMWARE_MEMORY_RANGE *FirmwareMemoryRanges;
    size_t FirmwareMemoryRangeCount;
    /* Exact physical allocation returned for the kernel image by AllocatePages. */
    uint64_t KernelPhysicalBase, KernelPhysicalSize; /* allocator-returned physical extent */
    uint64_t RamdiskPhysicalBase, RamdiskAllocationSize;
    uint64_t RamdiskImageOffset, RamdiskImageLength;
    const RC95LDR_LOADED_MODULE *BootResourceModule;
};
typedef struct _RC95LDR_NATIVE_PREPARE_RESULT {
    uint32_t StructureSize, Version;
    void *LoaderBlock;
    size_t LoaderBlockSize;
    uint32_t LoadedBootDriverCount;
    uint32_t HandoffReady;
    char ErrorMessage[256];
    RC95LDR_LOADERSTATE_RESULT LoaderState; /* Windows NT profiles only */
    RC95LDR_RC95_LOADERSTATE_RESULT Rc95LoaderState; /* RC95-OS profile only */
    RC95LDR_PLATFORM_INVENTORY_RESULT PlatformInventory;
    RC95LDR_METADATA_RESULT Metadata;
    RC95LDR_KERNEL_HANDOFF_CONTEXT Handoff;
    RC95LDR_MEMORYMAP_RESULT FinalMemoryMap;
    /* Per-result storage keeps module RegistryPath pointers valid across
     * independent preparation transactions without global mutable state. */
    char DriverRegistryPaths[RC95LDR_MODULE_MAX][320];
    uint32_t LoadedBootResource;
} RC95LDR_NATIVE_PREPARE_RESULT;
/* ReactOS delegates to rosload.exe through LaunchRosLoad. Other profiles
 * parse SYSTEM, resolve Select\\Current, load Start=0/1 kernel/filesystem
 * services through the caller's image loader, and build the selected ABI.
 * The result includes a preflight context; this function never jumps. */
RC95LDR_STATUS Rc95LdrPrepareNativeLoaderBlock(
    const RC95LDR_NATIVE_PREPARE_REQUEST *Request,
    RC95LDR_NATIVE_PREPARE_RESULT *Result);
/* Releases owned loader-state mappings before returning their physical pages.
 * Safe after a failed or partially completed preparation. */
void Rc95LdrReleaseNativePrepareResult(RC95LDR_NATIVE_PREPARE_RESULT *Result);
#ifdef __cplusplus
}
#endif
#endif
