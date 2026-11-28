#ifndef _RC95LDR_NTFS_PROVIDER_H_
#define _RC95LDR_NTFS_PROVIDER_H_

#include "rc95ldr_ntfs_stream.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RC95LDR_NTFS_PROVIDER_MAX_OPEN_FILES 8u
#define RC95LDR_NTFS_PROVIDER_MAX_PATH 512u

typedef struct _RC95LDR_NTFS_FILE_SLOT {
    struct _RC95LDR_NTFS_PROVIDER_CONTEXT *Owner;
    RC95LDR_NTFS_STREAM Stream;
    uint8_t *Scratch;
    size_t ScratchLength;
    const uint8_t *ResidentData;
    uint64_t FileSize;
    uint32_t InUse;
    uint32_t Resident;
} RC95LDR_NTFS_FILE_SLOT;

typedef struct _RC95LDR_NTFS_PROVIDER_CONTEXT {
    const RC95LDR_NTFS_VOLUME *Volume;
    RC95LDR_FILESYSTEM Interface;
    RC95LDR_NTFS_FILE_SLOT Slots[RC95LDR_NTFS_PROVIDER_MAX_OPEN_FILES];
    void *Workspace;
    size_t WorkspaceLength;
    size_t SlotWorkspaceLength;
    uint32_t Initialized;
} RC95LDR_NTFS_PROVIDER_CONTEXT;

/*
 * Returns the required caller-owned workspace for all concurrent file slots.
 * Workspace per slot: two FILE records, one INDEX buffer, and two sectors.
 */
RC95LDR_STATUS
Rc95LdrNtfsFileSystemWorkspaceSize(
    const RC95LDR_NTFS_VOLUME *Volume,
    size_t *WorkspaceLength);

/*
 * Bind a mounted NTFS volume to RC95LDR_FILESYSTEM. The volume and workspace
 * must outlive every open handle. The adapter is strictly read-only.
 */
RC95LDR_STATUS
Rc95LdrNtfsInitializeFileSystem(
    const RC95LDR_NTFS_VOLUME *Volume,
    RC95LDR_NTFS_PROVIDER_CONTEXT *Context,
    void *Workspace,
    size_t WorkspaceLength);

RC95LDR_STATUS
Rc95LdrNtfsBindVolume(
    const RC95LDR_NTFS_VOLUME *NtfsVolume,
    RC95LDR_NTFS_PROVIDER_CONTEXT *Context,
    void *Workspace, size_t WorkspaceLength,
    RC95LDR_VOLUME *Volume);

void
Rc95LdrNtfsDestroyFileSystem(RC95LDR_NTFS_PROVIDER_CONTEXT *Context);

#ifdef __cplusplus
}
#endif
#endif
