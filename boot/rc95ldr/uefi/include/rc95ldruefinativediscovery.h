#ifndef _RC95LDR_UEFI_NATIVE_DISCOVERY_H_
#define _RC95LDR_UEFI_NATIVE_DISCOVERY_H_

#include "rc95ldruefinativemounts.h"
#include "rc95ldruefiservices.h"
#include "rc95ldrmodule.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Complete UEFI discovery backend. FirmwareServices owns the validated
 * Block I/O handle inventory and cached GPT/MBR partitions; Mounts owns the
 * active FAT32 ESP and native NTFS OS-volume lifetimes.
 */
typedef struct _RC95LDR_UEFI_NATIVE_DISCOVERY_CONTEXT {
    RC95LDR_UEFI_SERVICE_CONTEXT FirmwareServices;
    RC95LDR_MODULE_SERVICES_V2 ModuleServices;
    RC95LDR_UEFI_FIRMWARE_OPS FirmwareOps;
    RC95LDR_UEFI_NATIVE_MOUNT_CONTEXT Mounts;
    uint32_t Initialized;
} RC95LDR_UEFI_NATIVE_DISCOVERY_CONTEXT;

/* Initializes UEFI Block I/O/device-path services, partition discovery,
 * native FAT32/NTFS mount callbacks and the strict Windows file preflight. */
RC95LDR_STATUS Rc95LdrUefiInitializeNativeDiscovery(
    RC95LDR_UEFI_NATIVE_DISCOVERY_CONTEXT *Context,
    const RC95LDR_UEFI_IMAGE_CONTEXT *Image,
    uint32_t Architecture);

/* Returns a ready-to-use discovery table. The caller may set TextUi after. */
RC95LDR_STATUS Rc95LdrUefiGetNativeDiscoveryOps(
    RC95LDR_UEFI_NATIVE_DISCOVERY_CONTEXT *Context,
    RC95LDR_UEFI_DISCOVERY_OPS *Operations);

/* Destroys mounts first, then releases the firmware service inventory. */
void Rc95LdrUefiDestroyNativeDiscovery(
    RC95LDR_UEFI_NATIVE_DISCOVERY_CONTEXT *Context);

#ifdef __cplusplus
}
#endif
#endif
