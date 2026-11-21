#ifndef _RC95LDR_NATIVE_PREPARE_H_
#define _RC95LDR_NATIVE_PREPARE_H_
#include "rc95ldrloaderblock.h"
#include "rc95ldrsystemhive.h"
#include "rc95ldrhandoff.h"
#include "rc95ldrloaderstate.h"
#include "rc95ldrarchprepare.h"
#include "rc95ldrplatforminventory.h"
#include "rc95ldrmetadata.h"
#ifdef __cplusplus
extern "C" {
#endif
#define RC95LDR_NATIVE_PREPARE_VERSION 4u
typedef struct _RC95LDR_NATIVE_PREPARE_REQUEST RC95LDR_NATIVE_PREPARE_REQUEST;
typedef RC95LDR_STATUS (*RC95LDR_LAUNCH_ROSLOAD)(void *Context, const RC95LDR_NATIVE_PREPARE_REQUEST *Request);
typedef RC95LDR_STATUS (*RC95LDR_LOAD_HIVE_BOOT_DRIVER)(
    void *Context, const RC95LDR_HIVE_BOOT_DRIVER *Driver,
    RC95LDR_LOADED_MODULE *LoadedModule);
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
    RC95LDR_LAUNCH_ROSLOAD LaunchRosLoad;
    void *RosLoadContext;
    uint64_t FirmwareSystemTable;
    const RC95LDR_LOADERSTATE_INPUT *LoaderStateInput;
    const RC95LDR_ARCH_HANDOFF_INPUT *ArchitectureHandoffInput;
    const RC95LDR_PLATFORM_INVENTORY_INPUT *PlatformInventoryInput;
    const RC95LDR_METADATA_INPUT *MetadataInput;
};
typedef struct _RC95LDR_NATIVE_PREPARE_RESULT {
    uint32_t StructureSize, Version;
    void *LoaderBlock;
    size_t LoaderBlockSize;
    uint32_t LoadedBootDriverCount;
    uint32_t HandoffReady;
    RC95LDR_LOADERSTATE_RESULT LoaderState;
    RC95LDR_PLATFORM_INVENTORY_RESULT PlatformInventory;
    RC95LDR_METADATA_RESULT Metadata;
    RC95LDR_KERNEL_HANDOFF_CONTEXT Handoff;
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
