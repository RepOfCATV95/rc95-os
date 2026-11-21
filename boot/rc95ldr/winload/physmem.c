#include "rc95ldrphysmem.h"
#include <limits.h>
#include <string.h>

#define RC95LDR_PHYSMEM_STATE_ALLOCATED 0x1u
#define RC95LDR_PHYSMEM_STATE_MAPPED    0x2u
#define RC95LDR_PHYSMEM_KNOWN_ALLOC_FLAGS (RC95LDR_PHYSMEM_ALLOC_CONTIGUOUS | RC95LDR_PHYSMEM_ALLOC_BELOW_4GB | RC95LDR_PHYSMEM_ALLOC_ZERO)
#define RC95LDR_PHYSMEM_KNOWN_MAP_FLAGS (RC95LDR_PHYSMEM_MAP_WRITABLE | RC95LDR_PHYSMEM_MAP_CACHED)

static int Rc95LdrPhysMemOpsValid(const RC95LDR_PHYSMEM_OPS *o)
{
    return o && o->StructureSize >= sizeof(*o) &&
        o->AbiVersion == RC95LDR_PHYSMEM_ABI_VERSION &&
        o->AllocatePages && o->MapWritable && o->Unmap && o->ReleasePages;
}

RC95LDR_STATUS Rc95LdrPhysMemValidate(const RC95LDR_PHYSMEM_ALLOCATION *a)
{
    uint64_t bytes;
    if (!a || a->State != (RC95LDR_PHYSMEM_STATE_ALLOCATED | RC95LDR_PHYSMEM_STATE_MAPPED) ||
        !a->AllocationToken || !a->WritableMapping || !a->PageCount ||
        (a->PhysicalBase & (RC95LDR_PHYSMEM_PAGE_SIZE - 1u)) ||
        a->PageCount > UINT64_MAX / RC95LDR_PHYSMEM_PAGE_SIZE)
        return Rc95LdrInvalidParameter;
    bytes = a->PageCount * RC95LDR_PHYSMEM_PAGE_SIZE;
    if (a->PhysicalBase > UINT64_MAX - bytes ||
        bytes > SIZE_MAX || a->MappingLength != (size_t)bytes)
        return Rc95LdrOutOfBounds;
    if (!(a->MappingFlags & RC95LDR_PHYSMEM_MAP_WRITABLE))
        return Rc95LdrInvalidParameter;
    return Rc95LdrSuccess;
}

RC95LDR_STATUS Rc95LdrPhysMemAllocate(
    const RC95LDR_PHYSMEM_OPS *o, uint64_t pages, uint64_t alignPages,
    uint32_t allocFlags, uint32_t mapFlags, RC95LDR_PHYSMEM_ALLOCATION *a)
{
    RC95LDR_STATUS st;
    uint64_t bytes, physical = 0;
    void *token = NULL, *mapping = NULL;
    size_t mappingLength = 0;
    if (!Rc95LdrPhysMemOpsValid(o) || !a || !pages ||
        !alignPages || (alignPages & (alignPages - 1u)) ||
        alignPages > UINT64_MAX / RC95LDR_PHYSMEM_PAGE_SIZE ||
        (allocFlags & ~RC95LDR_PHYSMEM_KNOWN_ALLOC_FLAGS) ||
        (mapFlags & ~RC95LDR_PHYSMEM_KNOWN_MAP_FLAGS) ||
        !(mapFlags & RC95LDR_PHYSMEM_MAP_WRITABLE) ||
        pages > UINT64_MAX / RC95LDR_PHYSMEM_PAGE_SIZE)
        return Rc95LdrInvalidParameter;
    bytes = pages * RC95LDR_PHYSMEM_PAGE_SIZE;
    if (bytes > SIZE_MAX) return Rc95LdrOutOfBounds;
    memset(a, 0, sizeof(*a));
    st = o->AllocatePages(o->Context, pages, alignPages, allocFlags, &token, &physical);
    if (st != Rc95LdrSuccess) return st;
    if (!token || (physical & ((alignPages * RC95LDR_PHYSMEM_PAGE_SIZE) - 1u)) ||
        physical > UINT64_MAX - bytes ||
        ((allocFlags & RC95LDR_PHYSMEM_ALLOC_BELOW_4GB) &&
         physical + bytes > 0x100000000ULL)) {
        o->ReleasePages(o->Context, token, physical, pages);
        return Rc95LdrOutOfBounds;
    }
    st = o->MapWritable(o->Context, token, physical, pages, mapFlags,
                        &mapping, &mappingLength);
    if (st != Rc95LdrSuccess || !mapping || mappingLength != (size_t)bytes) {
        if (mapping) o->Unmap(o->Context, token, mapping, mappingLength);
        o->ReleasePages(o->Context, token, physical, pages);
        return st == Rc95LdrSuccess ? Rc95LdrOutOfBounds : st;
    }
    a->PhysicalBase = physical;
    a->PageCount = pages;
    a->AllocationToken = token;
    a->WritableMapping = mapping;
    a->MappingLength = mappingLength;
    a->MappingFlags = mapFlags;
    a->State = RC95LDR_PHYSMEM_STATE_ALLOCATED | RC95LDR_PHYSMEM_STATE_MAPPED;
    if (allocFlags & RC95LDR_PHYSMEM_ALLOC_ZERO)
        memset(mapping, 0, mappingLength);
    return Rc95LdrPhysMemValidate(a);
}

void Rc95LdrPhysMemRelease(
    const RC95LDR_PHYSMEM_OPS *o, RC95LDR_PHYSMEM_ALLOCATION *a)
{
    if (!o || !a || !a->AllocationToken || !o->Unmap || !o->ReleasePages) return;
    /* Tear down the virtual mapping before returning its physical pages. */
    if ((a->State & RC95LDR_PHYSMEM_STATE_MAPPED) && a->WritableMapping)
        o->Unmap(o->Context, a->AllocationToken, a->WritableMapping, a->MappingLength);
    if (a->State & RC95LDR_PHYSMEM_STATE_ALLOCATED)
        o->ReleasePages(o->Context, a->AllocationToken, a->PhysicalBase, a->PageCount);
    memset(a, 0, sizeof(*a));
}
