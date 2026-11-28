#ifndef _RC95LDR_BIOS_MEMORY_H_
#define _RC95LDR_BIOS_MEMORY_H_

#include "rc95ldrbiosstartup.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RC95LDR_BIOS_MEMORY_ALLOCATIONS 128u
#define RC95LDR_BIOS_PAGE_SIZE 4096u

typedef struct _RC95LDR_BIOS_MEMORY_ALLOCATION {
    uint64_t Base;
    uint64_t Length;
    uint8_t InUse;
    uint8_t Reserved[7];
} RC95LDR_BIOS_MEMORY_ALLOCATION;

/*
 * First-fit physical allocator for the no-paging i386 BIOS environment.
 * It allocates only E820 type-1 memory above 1 MiB, returns identity-mapped
 * physical addresses, and excludes firmware reservations and live extents.
 * The object must remain alive until all platform allocations are released.
 */
typedef struct _RC95LDR_BIOS_MEMORY_ALLOCATOR {
    uint32_t StructureSize;
    uint32_t Version;
    RC95LDR_BIOS_STARTUP *Startup;
    RC95LDR_BIOS_MEMORY_ALLOCATION Allocations[RC95LDR_BIOS_MEMORY_ALLOCATIONS];
} RC95LDR_BIOS_MEMORY_ALLOCATOR;

#define RC95LDR_BIOS_MEMORY_ALLOCATOR_VERSION 1u

RC95LDR_STATUS Rc95LdrBiosInitializeMemoryAllocator(
    RC95LDR_BIOS_MEMORY_ALLOCATOR *Allocator,
    RC95LDR_BIOS_STARTUP *Startup);
RC95LDR_STATUS Rc95LdrBiosAllocatePages(
    void *Context, uint64_t Size, uint64_t Alignment, uint64_t *PhysicalAddress);
void Rc95LdrBiosFreePages(void *Context, uint64_t PhysicalAddress, uint64_t Size);
RC95LDR_STATUS Rc95LdrBiosValidateAllocationExtent(
    void *Context, uint64_t VirtualBase, uint64_t PhysicalBase,
    uint64_t ImageSize, uint64_t AllocationSize);
RC95LDR_STATUS Rc95LdrBiosGetMemoryMap(
    void *Context, RC95LDR_WINLOAD_MEMORY_RANGE *Ranges,
    size_t Capacity, size_t *Count);
RC95LDR_STATUS Rc95LdrBiosInstallMemoryCallbacks(
    RC95LDR_BIOS_MEMORY_ALLOCATOR *Allocator,
    RC95LDR_WINLOAD_PLATFORM *Platform);

#ifdef __cplusplus
}
#endif
#endif
