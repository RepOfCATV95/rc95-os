#ifndef _RC95LDR_UEFI_ENTRY_H_
#define _RC95LDR_UEFI_ENTRY_H_

/* The checked-in EDK2 headers in this tree layer on NT base types. */
#include <ntdef.h>
#include <Uefi.h>
#include <LoadedImage.h>
#include "rc95ldrmodule.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RC95LDR_UEFI_ENTRY_VERSION 1u

typedef struct _RC95LDR_UEFI_IMAGE_CONTEXT {
    UINT32 StructureSize;
    UINT32 Version;
    EFI_HANDLE ImageHandle;
    EFI_SYSTEM_TABLE *SystemTable;
    EFI_BOOT_SERVICES *BootServices;
    EFI_LOADED_IMAGE_PROTOCOL *LoadedImage;
} RC95LDR_UEFI_IMAGE_CONTEXT;

/* Validates firmware table signatures, revisions, extents and mandatory
 * service pointers, then captures the Loaded Image protocol for this image. */
RC95LDR_STATUS Rc95LdrUefiInitializeImage(
    EFI_HANDLE ImageHandle,
    EFI_SYSTEM_TABLE *SystemTable,
    RC95LDR_UEFI_IMAGE_CONTEXT *Context);

/* Loads a child from a firmware device path, attaches a bounded load-options
 * block, starts it, and unloads it if control returns. The child path and
 * options remain caller-owned for the duration of this call. */
EFI_STATUS Rc95LdrUefiLoadAndStartImage(
    const RC95LDR_UEFI_IMAGE_CONTEXT *Context,
    EFI_DEVICE_PATH_PROTOCOL *DevicePath,
    UINTN DevicePathBytes,
    const VOID *LoadOptions,
    UINTN LoadOptionsBytes,
    EFI_HANDLE *ChildImageHandle);

/* Loads a child PE/COFF image through firmware, constructs the versioned
 * R95M handoff from the firmware-populated Loaded Image protocol, exposes it
 * as the child's LoadOptions for the entire synchronous StartImage call, and
 * unloads the child if it returns. The firmware, not this loader, owns the
 * actual EFI entry-point transfer. */
EFI_STATUS Rc95LdrUefiLoadAndStartModule(
    const RC95LDR_UEFI_IMAGE_CONTEXT *Context,
    EFI_DEVICE_PATH_PROTOCOL *DevicePath,
    UINTN DevicePathBytes,
    UINT32 ModuleKind,
    UINT32 Architecture,
    const RC95LDR_MODULE_HANDOFF *ParentHandoff,
    UINTN ParentHandoffSize,
    EFI_HANDLE *ChildImageHandle);

/* Retrieves and validates the handoff passed by the parent module. */
RC95LDR_STATUS Rc95LdrUefiGetModuleHandoff(
    const RC95LDR_UEFI_IMAGE_CONTEXT *Context,
    UINT32 ExpectedKind,
    UINT32 ExpectedArchitecture,
    const RC95LDR_MODULE_HANDOFF **Handoff);

/* Native PE/COFF application entry point shared by RC95LDR, VENEER and
 * WINLOAD. Module-specific orchestration is installed by the image target. */
EFI_STATUS EFIAPI Rc95LdrUefiImageEntry(
    EFI_HANDLE ImageHandle,
    EFI_SYSTEM_TABLE *SystemTable);

#ifdef __cplusplus
}
#endif
#endif
