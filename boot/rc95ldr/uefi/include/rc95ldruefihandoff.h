#ifndef _RC95LDR_UEFI_HANDOFF_H_
#define _RC95LDR_UEFI_HANDOFF_H_

#include "rc95ldruefientry.h"
#include "rc95ldrhandoff.h"

#include "rc95ldrmemorymap.h"

struct _RC95LDR_WINLOAD_STARTUP;

#ifdef __cplusplus
extern "C" {
#endif

#define RC95LDR_UEFI_AMD64_HANDOFF_CONTEXT_VERSION 1u
typedef RC95LDR_STATUS (*RC95LDR_UEFI_HANDOFF_QUIESCE)(void *Context);

typedef struct _RC95LDR_UEFI_AMD64_HANDOFF_CONTEXT {
    UINT32 StructureSize;
    UINT32 Version;
    const RC95LDR_UEFI_IMAGE_CONTEXT *Image;
    VOID *MemoryMapBuffer;
    UINTN MemoryMapCapacity;
    UINTN MemoryMapPages;
    RC95LDR_NATIVE_ALLOCATION_SET *AllocationSet;
    RC95LDR_UEFI_HANDOFF_QUIESCE Quiesce;
    void *QuiesceContext;
} RC95LDR_UEFI_AMD64_HANDOFF_CONTEXT;

#define RC95LDR_UEFI_AMD64_MEMORY_MAP_CAPACITY (256u * 1024u)

/* Allocate and ledger-track persistent EFI_LOADER_DATA pages before native
 * preparation starts. UEFI x64 keeps these physical addresses identity-mapped
 * until ExitBootServices; the final page tables must preserve this mapping. */
RC95LDR_STATUS Rc95LdrUefiInitializeAmd64HandoffContext(
    const RC95LDR_UEFI_IMAGE_CONTEXT *Image,
    RC95LDR_NATIVE_ALLOCATION_SET *AllocationSet,
    RC95LDR_UEFI_AMD64_HANDOFF_CONTEXT *Context);
/* Convenience provider wiring: allocate/track the final map and attach
 * Context to an already constructed UEFI WINLOAD startup record. */
RC95LDR_STATUS Rc95LdrUefiAttachAmd64HandoffContext(
    const RC95LDR_UEFI_IMAGE_CONTEXT *Image,
    RC95LDR_NATIVE_ALLOCATION_SET *AllocationSet,
    struct _RC95LDR_WINLOAD_STARTUP *Startup,
    RC95LDR_UEFI_AMD64_HANDOFF_CONTEXT *Context);

/* Call only if WINLOAD returned before successful ExitBootServices. */
void Rc95LdrUefiReleaseAmd64HandoffContext(
    RC95LDR_UEFI_AMD64_HANDOFF_CONTEXT *Context);
/* Optional idempotent provider cleanup. Runs before the final GetMemoryMap
 * / ExitBootServices pair so no handles or protocols remain active. */
RC95LDR_STATUS Rc95LdrUefiSetAmd64HandoffQuiesce(
    RC95LDR_UEFI_AMD64_HANDOFF_CONTEXT *Context,
    RC95LDR_UEFI_HANDOFF_QUIESCE Quiesce,
    void *QuiesceContext);

/* Installs the final callback and its dedicated context into a WINLOAD
 * platform. The platform's ordinary Context remains untouched. */
RC95LDR_STATUS Rc95LdrUefiBindAmd64Handoff(
    RC95LDR_WINLOAD_PLATFORM *Platform,
    RC95LDR_UEFI_AMD64_HANDOFF_CONTEXT *Context);

/* Drop-in WINLOAD final callback. Rc95LdrUefiBindAmd64Handoff installs
 * this function and its dedicated context without replacing Platform.Context. */
RC95LDR_STATUS Rc95LdrUefiAmd64ValidatedHandoff(
    void *Context, RC95LDR_KERNEL_HANDOFF_CONTEXT *Handoff);

/*
 * AMD64 final firmware callback. MemoryMapBuffer must be preallocated,
 * identity-mapped, and represented in the handoff's reserved allocations.
 * This routine captures the final map, calls ExitBootServices, revalidates
 * the now-final handoff, and transfers without returning.
 */
EFI_STATUS Rc95LdrUefiAmd64ExitBootServicesAndTransfer(
    const RC95LDR_UEFI_IMAGE_CONTEXT *Image,
    RC95LDR_KERNEL_HANDOFF_CONTEXT *Handoff,
    VOID *MemoryMapBuffer,
    UINTN MemoryMapCapacity);

#ifdef __cplusplus
}
#endif
#endif
