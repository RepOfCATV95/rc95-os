#ifndef _RC95LDR_PHYSMEM_H_
#define _RC95LDR_PHYSMEM_H_
#include <stddef.h>
#include <stdint.h>
#include "rc95ldrntabi.h"
#ifdef __cplusplus
extern "C" {
#endif

#define RC95LDR_PHYSMEM_ABI_VERSION 1u
#define RC95LDR_PHYSMEM_PAGE_SIZE 4096u
#define RC95LDR_PHYSMEM_ALLOC_CONTIGUOUS 0x00000001u
#define RC95LDR_PHYSMEM_ALLOC_BELOW_4GB   0x00000002u
#define RC95LDR_PHYSMEM_ALLOC_ZERO        0x00000004u
#define RC95LDR_PHYSMEM_MAP_WRITABLE      0x00000001u
#define RC95LDR_PHYSMEM_MAP_CACHED        0x00000002u

typedef struct _RC95LDR_PHYSMEM_ALLOCATION {
    uint64_t PhysicalBase;
    uint64_t PageCount;
    void *AllocationToken;
    void *WritableMapping;
    size_t MappingLength;
    uint32_t MappingFlags;
    uint32_t State;
} RC95LDR_PHYSMEM_ALLOCATION;

typedef RC95LDR_STATUS (*RC95LDR_PHYSMEM_ALLOCATE_PAGES)(
    void *Context, uint64_t PageCount, uint64_t AlignmentPages,
    uint32_t AllocationFlags, void **AllocationToken, uint64_t *PhysicalBase);
typedef RC95LDR_STATUS (*RC95LDR_PHYSMEM_MAP_WRITABLE)(
    void *Context, void *AllocationToken, uint64_t PhysicalBase,
    uint64_t PageCount, uint32_t MappingFlags,
    void **WritableMapping, size_t *MappingLength);
typedef void (*RC95LDR_PHYSMEM_UNMAP)(
    void *Context, void *AllocationToken, void *WritableMapping,
    size_t MappingLength);
typedef void (*RC95LDR_PHYSMEM_RELEASE_PAGES)(
    void *Context, void *AllocationToken, uint64_t PhysicalBase,
    uint64_t PageCount);

typedef struct _RC95LDR_PHYSMEM_OPS {
    uint32_t StructureSize;
    uint32_t AbiVersion;
    void *Context;
    RC95LDR_PHYSMEM_ALLOCATE_PAGES AllocatePages;
    RC95LDR_PHYSMEM_MAP_WRITABLE MapWritable;
    RC95LDR_PHYSMEM_UNMAP Unmap;
    RC95LDR_PHYSMEM_RELEASE_PAGES ReleasePages;
} RC95LDR_PHYSMEM_OPS;

RC95LDR_STATUS Rc95LdrPhysMemAllocate(
    const RC95LDR_PHYSMEM_OPS *Ops, uint64_t PageCount,
    uint64_t AlignmentPages, uint32_t AllocationFlags,
    uint32_t MappingFlags, RC95LDR_PHYSMEM_ALLOCATION *Allocation);
void Rc95LdrPhysMemRelease(
    const RC95LDR_PHYSMEM_OPS *Ops,
    RC95LDR_PHYSMEM_ALLOCATION *Allocation);
RC95LDR_STATUS Rc95LdrPhysMemValidate(
    const RC95LDR_PHYSMEM_ALLOCATION *Allocation);

#ifdef __cplusplus
}
#endif
#endif
