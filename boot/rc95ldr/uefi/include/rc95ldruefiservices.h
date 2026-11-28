#ifndef _RC95LDR_UEFI_SERVICES_H_
#define _RC95LDR_UEFI_SERVICES_H_

#include "rc95ldruefientry.h"
#include <GraphicsOutput.h>
#include <BlockIo.h>
#include "rc95ldrdiscover.h"
#include "rc95ldruedisk.h"
#include "rc95ldrbgfx.h"
#include "rc95ldrfirmwaremenu.h"

#define RC95LDR_UEFI_MAX_OPEN_FILES 16u
#define RC95LDR_UEFI_MAX_ALLOCATIONS 64u
#define RC95LDR_UEFI_FILE_PATH_CHARS 512u
#define RC95LDR_UEFI_PAGE_SIZE 4096u
#define RC95LDR_UEFI_MAX_BLOCK_DEVICES 32u
#define RC95LDR_UEFI_MAX_PARTITIONS 256u

typedef struct _RC95LDR_UEFI_BLOCK_DEVICE_SLOT {
    EFI_HANDLE Handle;
    EFI_BLOCK_IO_PROTOCOL *Protocol;
    const UINT8 *DevicePath;
    UINTN DevicePathBytes;
    UINT32 InUse;
} RC95LDR_UEFI_BLOCK_DEVICE_SLOT;


/* Minimal UEFI 2.x Simple File System / File Protocol declarations. These
 * match the firmware ABI and avoid inventing SDK-owned public header names. */
typedef struct _RC95LDR_EFI_FILE_PROTOCOL RC95LDR_EFI_FILE_PROTOCOL;
typedef RC95LDR_EFI_FILE_PROTOCOL *RC95LDR_EFI_FILE_HANDLE;
typedef EFI_STATUS (EFIAPI *RC95LDR_EFI_FILE_OPEN)(
    RC95LDR_EFI_FILE_PROTOCOL *, RC95LDR_EFI_FILE_HANDLE *,
    CHAR16 *, UINT64, UINT64);
typedef EFI_STATUS (EFIAPI *RC95LDR_EFI_FILE_CLOSE)(RC95LDR_EFI_FILE_PROTOCOL *);
typedef EFI_STATUS (EFIAPI *RC95LDR_EFI_FILE_READ)(
    RC95LDR_EFI_FILE_PROTOCOL *, UINTN *, VOID *);
typedef EFI_STATUS (EFIAPI *RC95LDR_EFI_FILE_SET_POSITION)(
    RC95LDR_EFI_FILE_PROTOCOL *, UINT64);
typedef EFI_STATUS (EFIAPI *RC95LDR_EFI_FILE_GET_INFO)(
    RC95LDR_EFI_FILE_PROTOCOL *, EFI_GUID *, UINTN *, VOID *);

struct _RC95LDR_EFI_FILE_PROTOCOL {
    UINT64 Revision;
    RC95LDR_EFI_FILE_OPEN Open;
    RC95LDR_EFI_FILE_CLOSE Close;
    VOID *Delete;
    RC95LDR_EFI_FILE_READ Read;
    VOID *Write;
    VOID *GetPosition;
    RC95LDR_EFI_FILE_SET_POSITION SetPosition;
    RC95LDR_EFI_FILE_GET_INFO GetInfo;
    VOID *SetInfo;
    VOID *Flush;
};

typedef struct _RC95LDR_EFI_SIMPLE_FILE_SYSTEM_PROTOCOL {
    UINT64 Revision;
    EFI_STATUS (EFIAPI *OpenVolume)(
        struct _RC95LDR_EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *,
        RC95LDR_EFI_FILE_HANDLE *);
} RC95LDR_EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;

typedef struct _RC95LDR_UEFI_FILE_SLOT {
    RC95LDR_EFI_FILE_HANDLE Handle;
    UINT64 Size;
    UINT32 InUse;
} RC95LDR_UEFI_FILE_SLOT;

typedef struct _RC95LDR_UEFI_ALLOCATION_SLOT {
    /* Base/Size describe the aligned range exposed to the child. RawBase and
       RawSize retain the complete EFI allocation so cleanup is exact. */
    UINT64 Base;
    UINT64 Size;
    UINT64 RequestedSize;
    UINT64 RawBase;
    UINT64 RawSize;
    UINT32 InUse;
} RC95LDR_UEFI_ALLOCATION_SLOT;

typedef struct _RC95LDR_UEFI_SERVICE_CONTEXT {
    UINT32 StructureSize;
    UINT32 Version;
    UINT32 Shutdown;
    EFI_HANDLE ImageHandle;
    EFI_HANDLE DeviceHandle;
    EFI_BOOT_SERVICES *BootServices;
    RC95LDR_EFI_FILE_HANDLE Root;
    RC95LDR_EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FileSystem;
    EFI_GUID SimpleFileSystemGuid;
    EFI_GUID FileInfoGuid;
    RC95LDR_UEFI_FILE_SLOT Files[RC95LDR_UEFI_MAX_OPEN_FILES];
    RC95LDR_UEFI_ALLOCATION_SLOT Allocations[RC95LDR_UEFI_MAX_ALLOCATIONS];
    EFI_HANDLE *BlockHandles;
    UINTN BlockHandleCount;
    RC95LDR_UEFI_BLOCK_DEVICE_SLOT BlockDevices[RC95LDR_UEFI_MAX_BLOCK_DEVICES];
    RC95LDR_PARTITION_INFO Partitions[RC95LDR_UEFI_MAX_PARTITIONS];
    UINTN PartitionCount;
    UINT32 DiscoveryInitialized;
    EFI_PHYSICAL_ADDRESS DiscoveryScratchBase;
    UINTN DiscoveryScratchPages;
} RC95LDR_UEFI_SERVICE_CONTEXT;

typedef struct _RC95LDR_UEFI_VIDEO {
    UINT32 StructureSize;
    UINT32 Version;
    const RC95LDR_UEFI_IMAGE_CONTEXT *Image;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop;
    RC95LDR_BGFX_FRAMEBUFFER Framebuffer;
} RC95LDR_UEFI_VIDEO;

/* Bind the candidate-menu restart action to this image's EFI runtime services. */
RC95LDR_STATUS Rc95LdrUefiBindFirmwareMenuOps(
    const RC95LDR_UEFI_IMAGE_CONTEXT *, RC95LDR_FIRMWARE_MENU_OPS *);

RC95LDR_STATUS Rc95LdrUefiInitializeFirmwareMenuOps(
    const RC95LDR_UEFI_IMAGE_CONTEXT *, const RC95LDR_TEXTUI *,
    RC95LDR_UEFI_VIDEO *, RC95LDR_FIRMWARE_MENU_OPS *,
    RC95LDR_TEXTUI_EXT *);
RC95LDR_STATUS Rc95LdrUefiInitializeVideo(
    const RC95LDR_UEFI_IMAGE_CONTEXT *, RC95LDR_UEFI_VIDEO *,
    RC95LDR_BGFX_PLATFORM *);
RC95LDR_STATUS Rc95LdrUefiSetGraphicsMode(void *, UINT32, UINT32);
RC95LDR_STATUS Rc95LdrUefiAcquireFramebuffer(void *,
    RC95LDR_BGFX_FRAMEBUFFER *);

/* Returns the validated Block I/O/device-path adapter owned by Provider. */
RC95LDR_STATUS Rc95LdrUefiGetFirmwareOps(
    RC95LDR_UEFI_SERVICE_CONTEXT *Provider,
    RC95LDR_UEFI_FIRMWARE_OPS *Operations);

/* Initializes a synchronous, provider-owned service table for the volume
 * from which the child image was loaded. ImageHandle remains the parent
 * protocol agent; LoadedImage supplies the child device handle. The context
 * and table must outlive the child StartImage call. */
RC95LDR_STATUS Rc95LdrUefiInitializeModuleServices(
    const RC95LDR_UEFI_IMAGE_CONTEXT *Image,
    UINT32 Architecture,
    RC95LDR_UEFI_SERVICE_CONTEXT *Provider,
    RC95LDR_MODULE_SERVICES *Services);

/* V2 initializer. V1 remains the exact prefix and the legacy initializer
 * above remains available for V1-only consumers. */
RC95LDR_STATUS Rc95LdrUefiInitializeModuleServicesV2(
    const RC95LDR_UEFI_IMAGE_CONTEXT *Image,
    UINT32 Architecture,
    RC95LDR_UEFI_SERVICE_CONTEXT *Provider,
    RC95LDR_MODULE_SERVICES_V2 *Services);

/* Closes firmware handles before ExitBootServices; tracked pages remain owned.
 * The operation is idempotent and retries any handles that failed to close. */
RC95LDR_STATUS Rc95LdrUefiQuiesceModuleServices(
    RC95LDR_UEFI_SERVICE_CONTEXT *Provider);

/* Closes remaining handles and releases tracked page allocations on return. */
void Rc95LdrUefiDestroyModuleServices(RC95LDR_UEFI_SERVICE_CONTEXT *Provider);

#endif
