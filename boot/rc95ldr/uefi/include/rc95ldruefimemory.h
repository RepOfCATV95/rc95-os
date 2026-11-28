#ifndef _RC95LDR_UEFI_MEMORY_H_
#define _RC95LDR_UEFI_MEMORY_H_
#include <ntdef.h>
#include <Uefi.h>
#include "rc95ldrmemorymap.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Call immediately after AllocatePages for the persistent final GetMemoryMap
 * buffer. PhysicalBase is the EFI physical address, not the host pointer. */
RC95LDR_STATUS Rc95LdrUefiTrackMemoryMapBuffer(
    RC95LDR_NATIVE_ALLOCATION_SET *Allocations,
    uint64_t PhysicalBase, size_t BufferSize);

/* Captures the final map into caller-preallocated storage and retries
 * ExitBootServices only for EFI_INVALID_PARAMETER (a stale map key). No pool
 * allocation or other boot-service call is made between map capture and exit.
 * On EFI_BUFFER_TOO_SMALL, the caller must grow the buffer and retry before
 * attempting the irreversible firmware transition. */
EFI_STATUS Rc95LdrUefiExitBootServices(
    const EFI_SYSTEM_TABLE *SystemTable,
    EFI_HANDLE ImageHandle,
    VOID *MemoryMapBuffer,
    UINTN MemoryMapCapacity,
    UINTN *MemoryMapSize,
    UINTN *DescriptorSize,
    UINT32 *DescriptorVersion,
    UINTN *MapKey);
#ifdef __cplusplus
}
#endif
#endif
