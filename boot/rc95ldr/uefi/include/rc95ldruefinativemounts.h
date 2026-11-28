#ifndef _RC95LDR_UEFI_NATIVE_MOUNTS_H_
#define _RC95LDR_UEFI_NATIVE_MOUNTS_H_

#include "rc95ldrdiscoveryuefi.h"
#include "rc95ldruedisk.h"
#include "rc95ldr_ntfs_provider.h"
#include "rc95ldr_btrfs.h"
#include <ntdef.h>
#include <Uefi.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef RC95LDR_STATUS (*RC95LDR_UEFI_ALLOCATE_POOL)(
    void *Context, size_t Bytes, void **Allocation);
typedef void (*RC95LDR_UEFI_FREE_POOL)(void *Context, void *Allocation);

typedef struct _RC95LDR_UEFI_POOL_CONTEXT {
    EFI_BOOT_SERVICES *BootServices;
} RC95LDR_UEFI_POOL_CONTEXT;

RC95LDR_STATUS Rc95LdrUefiAllocateLoaderPool(
    void *Context, size_t Bytes, void **Allocation);
void Rc95LdrUefiFreeLoaderPool(void *Context, void *Allocation);

/*
 * UEFI-owned mount lifecycle for the exact partition handles produced by
 * Rc95LdrUefiEnumeratePartitions. One ESP and one NTFS OS volume may be
 * mounted concurrently during BCD discovery. The NTFS volume, provider,
 * scratch buffers and block-I/O adapter remain owned here until UnmountVolume.
 */
typedef struct _RC95LDR_UEFI_NTFS_MOUNT_STATE {
    RC95LDR_UEFI_BLOCK_IO BlockIo;
    RC95LDR_BLOCK_DEVICE Device;
    RC95LDR_NTFS_VOLUME Ntfs;
    RC95LDR_NTFS_PROVIDER_CONTEXT Provider;
    void *MountScratch;
    size_t MountScratchLength;
    void *ProviderWorkspace;
    size_t ProviderWorkspaceLength;
    uint32_t Mounted;
} RC95LDR_UEFI_NTFS_MOUNT_STATE;

typedef struct _RC95LDR_UEFI_BTRFS_MOUNT_STATE {
    RC95LDR_UEFI_BLOCK_IO BlockIo;
    RC95LDR_BLOCK_DEVICE Device;
    RC95LDR_BTRFS_VOLUME Btrfs;
    RC95LDR_BTRFS_FILESYSTEM_CONTEXT FileSystemContext;
    RC95LDR_FILESYSTEM FileSystem;
    void *Scratch;
    size_t ScratchLength;
    void *ItemBuffer;
    void *InlineBuffer;
    void *StagingBuffer;
    size_t WorkspaceLength;
    uint32_t Mounted;
} RC95LDR_UEFI_BTRFS_MOUNT_STATE;

typedef struct _RC95LDR_UEFI_NATIVE_MOUNT_CONTEXT {
    RC95LDR_UEFI_FIRMWARE_OPS Firmware;
    RC95LDR_UEFI_FAT32_VOLUME Esp;
    RC95LDR_UEFI_NTFS_MOUNT_STATE Ntfs;
    RC95LDR_UEFI_BTRFS_MOUNT_STATE Btrfs;
    RC95LDR_UEFI_ALLOCATE_POOL AllocatePool;
    RC95LDR_UEFI_FREE_POOL FreePool;
    void *AllocatorContext;
    RC95LDR_UEFI_POOL_CONTEXT PoolContext;
    uint32_t Initialized;
} RC95LDR_UEFI_NATIVE_MOUNT_CONTEXT;

RC95LDR_STATUS Rc95LdrUefiInitializeNativeMountsFromBootServices(
    RC95LDR_UEFI_NATIVE_MOUNT_CONTEXT *Context,
    const RC95LDR_UEFI_FIRMWARE_OPS *Firmware,
    EFI_BOOT_SERVICES *BootServices);

RC95LDR_STATUS Rc95LdrUefiInitializeNativeMounts(
    RC95LDR_UEFI_NATIVE_MOUNT_CONTEXT *Context,
    const RC95LDR_UEFI_FIRMWARE_OPS *Firmware,
    RC95LDR_UEFI_ALLOCATE_POOL AllocatePool,
    RC95LDR_UEFI_FREE_POOL FreePool,
    void *AllocatorContext);

/* Functions can be assigned directly to RC95LDR_UEFI_DISCOVERY_OPS. */
RC95LDR_STATUS Rc95LdrUefiMountNativeVolume(
    void *Context, const RC95LDR_PARTITION_INFO *Partition,
    RC95LDR_FILESYSTEM_KIND FileSystem, RC95LDR_VOLUME *MountedVolume);
void Rc95LdrUefiUnmountNativeVolume(void *Context, RC95LDR_VOLUME *Volume);

/* Strict preflight for the BCD-selected UEFI Windows installation. Checks
 * the selected WINLOAD image, ntoskrnl.exe, hal.dll, and SYSTEM hive using
 * the exact mounted NTFS filesystem. */
RC95LDR_STATUS Rc95LdrUefiValidateNativeWindowsVolume(
    void *Context, const RC95LDR_VOLUME *Volume,
    const RC95LDR_WINDOWS_INSTALLATION *Installation);

/* Releases any still-mounted volumes and invalidates the lifecycle context. */
void Rc95LdrUefiDestroyNativeMounts(
    RC95LDR_UEFI_NATIVE_MOUNT_CONTEXT *Context);

#ifdef __cplusplus
}
#endif
#endif
