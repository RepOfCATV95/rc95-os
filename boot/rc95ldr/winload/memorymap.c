#include "rc95ldrmemorymap.h"
#include <string.h>
#include <limits.h>

#define PAGE_SHIFT 12u
#define PAGE_MASK (RC95LDR_MEMORYMAP_PAGE_SIZE - 1u)
#define EFI_RESERVED_MEMORY_TYPE 0u
#define EFI_LOADER_CODE 1u
#define EFI_LOADER_DATA 2u
#define EFI_BOOT_SERVICES_CODE 3u
#define EFI_BOOT_SERVICES_DATA 4u
#define EFI_RUNTIME_SERVICES_CODE 5u
#define EFI_RUNTIME_SERVICES_DATA 6u
#define EFI_UNUSABLE_MEMORY 8u
#define EFI_ACPI_RECLAIM_MEMORY 9u
#define EFI_ACPI_MEMORY_NVS 10u
#define EFI_MEMORY_MAPPED_IO 11u
#define EFI_MEMORY_MAPPED_IO_PORT_SPACE 12u
#define EFI_PAL_CODE 13u
#define EFI_PERSISTENT_MEMORY 14u
#define EFI_UNACCEPTED_MEMORY 15u

static int Add64(uint64_t a, uint64_t b, uint64_t *v)
{ if (b > UINT64_MAX - a) return 0; *v = a + b; return 1; }

static TYPE_OF_MEMORY FirmwareType(uint32_t kind)
{
    switch (kind) {
    case Rc95LdrFirmwareMemoryAvailable: return LoaderFree;
    case Rc95LdrFirmwareMemoryUnusable: return LoaderBad;
    case Rc95LdrFirmwareMemoryAcpiReclaim:
    case Rc95LdrFirmwareMemoryLoaderTemporary: return LoaderFirmwareTemporary;
    case Rc95LdrFirmwareMemoryAcpiNvs: return LoaderSpecialMemory;
    case Rc95LdrFirmwareMemoryRuntime:
    case Rc95LdrFirmwareMemoryReserved:
    default: return LoaderFirmwarePermanent;
    }
}

RC95LDR_STATUS
Rc95LdrReserveMemoryRange(RC95LDR_MEMORY_RESERVATION_SET *set,
    uint64_t base, uint64_t length, TYPE_OF_MEMORY type)
{
    uint64_t end, pageBase, pageEnd;
    size_t i;
    if (!set || type >= LoaderMaximum || type == LoaderFree ||
        !length || !Add64(base, length, &end))
        return Rc95LdrInvalidParameter;
    if (end & PAGE_MASK) {
        if (end > UINT64_MAX - PAGE_MASK) return Rc95LdrIntegerOverflow;
        pageEnd = (end + PAGE_MASK) & ~(uint64_t)PAGE_MASK;
    } else pageEnd = end;
    pageBase = base & ~(uint64_t)PAGE_MASK;
    if (set->Count > RC95LDR_MEMORYMAP_MAX_RESERVATIONS)
        return Rc95LdrCorruptData;
    for (i = 0; i < set->Count; ++i) {
        uint64_t existingEnd, existingPageEnd;
        const RC95LDR_MEMORY_RESERVATION *r = &set->Entries[i];
        if (!r->Length || !Add64(r->Base, r->Length, &existingEnd))
            return Rc95LdrCorruptData;
        if (r->Base == base && r->Length == length && r->MemoryType == type)
            return Rc95LdrSuccess;
        if (existingEnd & PAGE_MASK) {
            if (existingEnd > UINT64_MAX - PAGE_MASK) return Rc95LdrIntegerOverflow;
            existingPageEnd = (existingEnd + PAGE_MASK) & ~(uint64_t)PAGE_MASK;
        } else existingPageEnd = existingEnd;
        /* Distinct ownerships may not share even a partially used page. */
        if (pageBase < existingPageEnd &&
            (r->Base & ~(uint64_t)PAGE_MASK) < pageEnd)
            return Rc95LdrCorruptData;
    }
    if (set->Count == RC95LDR_MEMORYMAP_MAX_RESERVATIONS)
        return Rc95LdrBufferTooSmall;
    set->Entries[set->Count].Base = base;
    set->Entries[set->Count].Length = length;
    set->Entries[set->Count].MemoryType = type;
    ++set->Count;
    return Rc95LdrSuccess;
}



RC95LDR_STATUS
Rc95LdrTrackNativeAllocation(RC95LDR_NATIVE_ALLOCATION_SET *set,
    uint32_t kind, uint64_t physicalBase, uint64_t length)
{
    TYPE_OF_MEMORY type;
    uint64_t end, pageBase, pageEnd;
    size_t i;
    if (!set || !physicalBase || !length ||
        (physicalBase & PAGE_MASK) || (length & PAGE_MASK) ||
        physicalBase > UINT64_MAX - length ||
        set->Count > RC95LDR_MEMORYMAP_MAX_RESERVATIONS)
        return Rc95LdrInvalidParameter;
    switch (kind) {
    case RC95LDR_ALLOC_REGISTRY: type=LoaderRegistryData; break;
    case RC95LDR_ALLOC_NLS:
    case RC95LDR_ALLOC_METADATA: type=LoaderNlsData; break;
    case RC95LDR_ALLOC_LOADER_HEAP: type=LoaderOsloaderHeap; break;
    case RC95LDR_ALLOC_LOADER_STACK: type=LoaderOsloaderStack; break;
    case RC95LDR_ALLOC_PAGE_TABLES:
    case RC95LDR_ALLOC_UEFI_MAP:
    case RC95LDR_ALLOC_LOADER_BLOCK: type=LoaderMemoryData; break;
    case RC95LDR_ALLOC_RAMDISK: type=LoaderXIPRom; break;
    case RC95LDR_ALLOC_BIOS_FIRMWARE_STATE: type=LoaderMemoryData; break;
    default: return Rc95LdrInvalidParameter;
    }
    end=physicalBase+length;
    pageBase=physicalBase & ~(uint64_t)PAGE_MASK;
    pageEnd=end;
    if (pageEnd & PAGE_MASK) {
        if (pageEnd > UINT64_MAX-PAGE_MASK) return Rc95LdrIntegerOverflow;
        pageEnd=(pageEnd+PAGE_MASK) & ~(uint64_t)PAGE_MASK;
    }
    for (i=0;i<set->Count;i++) {
        const RC95LDR_NATIVE_ALLOCATION *a=&set->Entries[i];
        uint64_t otherEnd, otherPageEnd;
        if (!a->Length || a->PhysicalBase > UINT64_MAX-a->Length)
            return Rc95LdrCorruptData;
        otherEnd=a->PhysicalBase+a->Length;
        if (physicalBase==a->PhysicalBase && length==a->Length && kind==a->Kind)
            return Rc95LdrSuccess;
        otherPageEnd=otherEnd;
        if (otherPageEnd & PAGE_MASK) {
            if (otherPageEnd > UINT64_MAX-PAGE_MASK) return Rc95LdrIntegerOverflow;
            otherPageEnd=(otherPageEnd+PAGE_MASK) & ~(uint64_t)PAGE_MASK;
        }
        if (pageBase<otherPageEnd &&
            (a->PhysicalBase & ~(uint64_t)PAGE_MASK)<pageEnd)
            return Rc95LdrCorruptData;
    }
    if (set->Count==RC95LDR_MEMORYMAP_MAX_RESERVATIONS)
        return Rc95LdrBufferTooSmall;
    set->Entries[set->Count].Kind=kind;
    set->Entries[set->Count].MemoryType=type;
    set->Entries[set->Count].PhysicalBase=physicalBase;
    set->Entries[set->Count].Length=length;
    ++set->Count;
    return Rc95LdrSuccess;
}

RC95LDR_STATUS
Rc95LdrRegisterNativeAllocations(const RC95LDR_LOADED_MODULE *modules,
    size_t moduleCount, const RC95LDR_NATIVE_ALLOCATION *allocations,
    size_t allocationCount, int firmwareIsUefi,
    RC95LDR_MEMORY_RESERVATION_SET *reservations)
{
    uint32_t seen = 0;
    size_t i, j;
    RC95LDR_STATUS status;
    if (!reservations || (moduleCount && !modules) ||
        (allocationCount && !allocations) ||
        moduleCount > RC95LDR_MODULE_MAX ||
        allocationCount > RC95LDR_MEMORYMAP_MAX_RESERVATIONS)
        return Rc95LdrInvalidParameter;
    for (i = 0; i < moduleCount; ++i) {
        const RC95LDR_LOADED_MODULE *m = &modules[i];
        TYPE_OF_MEMORY type;
        if (!(m->Flags & (RC95LDR_MODULE_KERNEL | RC95LDR_MODULE_HAL |
                          RC95LDR_MODULE_BOOT_DRIVER | RC95LDR_MODULE_RESOURCE)))
            continue;
        if (!m->ImageSize || !m->PhysicalBase || !m->PhysicalSize ||
            m->PhysicalSize < m->ImageSize ||
            (m->PhysicalBase & PAGE_MASK) || (m->PhysicalSize & PAGE_MASK) ||
            m->PhysicalBase > UINT64_MAX - m->PhysicalSize)
            return Rc95LdrInvalidParameter;
        if (m->Flags & RC95LDR_MODULE_KERNEL) type = LoaderSystemCode;
        else if (m->Flags & RC95LDR_MODULE_HAL) type = LoaderHalCode;
        else if (m->Flags & RC95LDR_MODULE_RESOURCE)
            type = LoaderMemoryData; /* MmFreeLoaderBlock reclaims LoaderNlsData. */
        else type = LoaderBootDriver;
        /* A HAL image can also have a boot-driver list record (HALMPS).
           Keep one physical descriptor, with HAL-code ownership taking
           precedence over its second logical loader-list role. */
        for (j = 0; j < i; ++j) {
            const RC95LDR_LOADED_MODULE *previous = &modules[j];
            if ((previous->Flags & (RC95LDR_MODULE_KERNEL | RC95LDR_MODULE_HAL |
                                    RC95LDR_MODULE_BOOT_DRIVER | RC95LDR_MODULE_RESOURCE)) &&
                previous->PhysicalBase == m->PhysicalBase &&
                previous->PhysicalSize == m->PhysicalSize)
                break;
        }
        if (j != i) continue;
        status = Rc95LdrReserveMemoryRange(reservations, m->PhysicalBase,
            m->PhysicalSize, type);
        if (status != Rc95LdrSuccess) return status;
    }
    for (i = 0; i < allocationCount; ++i) {
        const RC95LDR_NATIVE_ALLOCATION *a = &allocations[i];
        TYPE_OF_MEMORY expected;
        uint32_t bit;
        switch (a->Kind) {
        case RC95LDR_ALLOC_REGISTRY: expected=LoaderRegistryData; bit=1u<<0; break;
        case RC95LDR_ALLOC_NLS: expected=LoaderNlsData; bit=1u<<1; break;
        case RC95LDR_ALLOC_LOADER_HEAP: expected=LoaderOsloaderHeap; bit=1u<<2; break;
        case RC95LDR_ALLOC_LOADER_STACK: expected=LoaderOsloaderStack; bit=1u<<3; break;
        case RC95LDR_ALLOC_PAGE_TABLES: expected=LoaderMemoryData; bit=1u<<4; break;
        case RC95LDR_ALLOC_UEFI_MAP: expected=LoaderMemoryData; bit=1u<<5; break;
        case RC95LDR_ALLOC_LOADER_BLOCK: expected=LoaderMemoryData; bit=1u<<6; break;
        case RC95LDR_ALLOC_METADATA: expected=LoaderNlsData; bit=1u<<8; break;
        case RC95LDR_ALLOC_RAMDISK: expected=LoaderXIPRom; bit=1u<<7; break;
        case RC95LDR_ALLOC_BIOS_FIRMWARE_STATE: expected=LoaderMemoryData; bit=1u<<9; break;
        default: return Rc95LdrInvalidParameter;
        }
        if (a->MemoryType != expected || !a->PhysicalBase || !a->Length ||
            a->PhysicalBase > UINT64_MAX - a->Length)
            return Rc95LdrInvalidParameter;
        for (j = 0; j < i; ++j) {
            uint64_t endA=a->PhysicalBase+a->Length;
            uint64_t endB=allocations[j].PhysicalBase+allocations[j].Length;
            if (a->PhysicalBase < endB && allocations[j].PhysicalBase < endA &&
                !(a->PhysicalBase==allocations[j].PhysicalBase &&
                  a->Length==allocations[j].Length &&
                  a->MemoryType==allocations[j].MemoryType))
                return Rc95LdrCorruptData;
        }
        status=Rc95LdrReserveMemoryRange(reservations,a->PhysicalBase,
            a->Length,a->MemoryType);
        if (status!=Rc95LdrSuccess) return status;
        seen |= bit;
    }
    if ((seen & 0x1fu) != 0x1fu ||
        (firmwareIsUefi && !(seen & (1u<<5))))
        return Rc95LdrInvalidParameter;
    return Rc95LdrSuccess;
}

RC95LDR_STATUS
Rc95LdrCollectBiosE820(const RC95LDR_BIOS_MEMORY_OPS *ops,
    RC95LDR_FIRMWARE_MEMORY_RANGE *ranges, size_t capacity, size_t *count)
{
    uint32_t continuation = 0;
    size_t n = 0, guard;
    if (!ops || !ops->E820Next || !ranges || !count || !capacity)
        return Rc95LdrInvalidParameter;
    *count = 0;
    for (guard = 0; guard < RC95LDR_MEMORYMAP_MAX_RAW; ++guard) {
        RC95LDR_FIRMWARE_MEMORY_RANGE r;
        RC95LDR_STATUS st;
        int done = 0;
        memset(&r, 0, sizeof(r));
        st = ops->E820Next(ops->Context, &continuation, &r, &done);
        if (st != Rc95LdrSuccess) return st;
        if (done) break;
        if (!r.Length) continue;
        if (r.Kind < Rc95LdrFirmwareMemoryAvailable ||
            r.Kind > Rc95LdrFirmwareMemoryLoaderTemporary ||
            r.Base > UINT64_MAX - r.Length) return Rc95LdrCorruptData;
        if (n == capacity) return Rc95LdrBufferTooSmall;
        ranges[n++] = r;
    }
    if (guard == RC95LDR_MEMORYMAP_MAX_RAW) return Rc95LdrCorruptData;
    *count = n;
    return n ? Rc95LdrSuccess : Rc95LdrNotFound;
}

/* EFI_MEMORY_DESCRIPTOR prefix; DescriptorSize from firmware controls stride. */
typedef struct _RC95LDR_EFI_MEMORY_DESCRIPTOR {
    uint32_t Type, Pad;
    uint64_t PhysicalStart, VirtualStart, NumberOfPages, Attribute;
} RC95LDR_EFI_MEMORY_DESCRIPTOR;

static uint32_t UefiKind(uint32_t type)
{
    switch (type) {
    case EFI_LOADER_CODE: case EFI_LOADER_DATA:
    case EFI_BOOT_SERVICES_CODE: case EFI_BOOT_SERVICES_DATA:
        return Rc95LdrFirmwareMemoryLoaderTemporary;
    case EFI_RUNTIME_SERVICES_CODE: case EFI_RUNTIME_SERVICES_DATA:
    case EFI_RESERVED_MEMORY_TYPE: case EFI_MEMORY_MAPPED_IO:
    case EFI_MEMORY_MAPPED_IO_PORT_SPACE: case EFI_PAL_CODE:
    case EFI_PERSISTENT_MEMORY: case EFI_UNACCEPTED_MEMORY:
        return Rc95LdrFirmwareMemoryReserved;
    case EFI_UNUSABLE_MEMORY: return Rc95LdrFirmwareMemoryUnusable;
    case EFI_ACPI_RECLAIM_MEMORY: return Rc95LdrFirmwareMemoryAcpiReclaim;
    case EFI_ACPI_MEMORY_NVS: return Rc95LdrFirmwareMemoryAcpiNvs;
    case 7: return Rc95LdrFirmwareMemoryAvailable; /* EfiConventionalMemory */
    default: return Rc95LdrFirmwareMemoryReserved;
    }
}

RC95LDR_STATUS
Rc95LdrCollectUefiMemoryMap(const RC95LDR_UEFI_MEMORY_OPS *ops,
    void *map, size_t mapCapacity, RC95LDR_FIRMWARE_MEMORY_RANGE *ranges,
    size_t capacity, size_t *count, size_t *rawSize, size_t *descSize,
    uint64_t *mapKey, uint32_t *descVersion)
{
    size_t bytes, stride, n = 0, off;
    RC95LDR_STATUS st;
    if (!ops || !ops->GetMemoryMap || !map || !mapCapacity || !ranges ||
        !count || !rawSize || !descSize || !mapKey || !descVersion)
        return Rc95LdrInvalidParameter;
    bytes = mapCapacity;
    st = ops->GetMemoryMap(ops->Context, map, &bytes, &stride, mapKey, descVersion);
    if (st != Rc95LdrSuccess) return st;
    if (stride < sizeof(RC95LDR_EFI_MEMORY_DESCRIPTOR) || !stride ||
        bytes > mapCapacity || bytes % stride) return Rc95LdrCorruptData;
    for (off = 0; off < bytes; off += stride) {
        RC95LDR_EFI_MEMORY_DESCRIPTOR d;
        RC95LDR_FIRMWARE_MEMORY_RANGE r;
        memcpy(&d, (const uint8_t *)map + off, sizeof(d));
        if (!d.NumberOfPages) continue;
        if (d.NumberOfPages > UINT64_MAX / RC95LDR_MEMORYMAP_PAGE_SIZE)
            return Rc95LdrIntegerOverflow;
        r.Base = d.PhysicalStart;
        r.Length = d.NumberOfPages * RC95LDR_MEMORYMAP_PAGE_SIZE;
        if ((r.Base & PAGE_MASK) || r.Base > UINT64_MAX - r.Length)
            return Rc95LdrCorruptData;
        r.Kind = UefiKind(d.Type); r.Attributes = (uint32_t)(d.Attribute & 0xffffffffu);
        if (n == capacity) return Rc95LdrBufferTooSmall;
        ranges[n++] = r;
    }
    *count = n; *rawSize = bytes; *descSize = stride;
    return n ? Rc95LdrSuccess : Rc95LdrNotFound;
}

typedef struct _SEGMENT {
    uint64_t Base, End;
    TYPE_OF_MEMORY Type;
} SEGMENT;

static int Emit(SEGMENT *s, size_t *n, size_t cap,
                uint64_t base, uint64_t end, TYPE_OF_MEMORY type)
{
    if (end <= base) return 1;
    if (*n && s[*n-1].End == base && s[*n-1].Type == type) {
        s[*n-1].End = end; return 1;
    }
    if (*n == cap) return 0;
    s[*n].Base = base; s[*n].End = end; s[*n].Type = type; ++*n;
    return 1;
}

RC95LDR_STATUS
Rc95LdrBuildFinalMemoryMap(const RC95LDR_FIRMWARE_MEMORY_RANGE *fw,
    size_t fwCount, const RC95LDR_MEMORY_RESERVATION *res, size_t resCount,
    RC95LDR_MEMORYMAP_RESULT *result)
{
    SEGMENT segments[RC95LDR_MEMORYMAP_MAX_RAW * 3];
    size_t i, j, n = 0;
    if (!fw || !fwCount || fwCount > RC95LDR_MEMORYMAP_MAX_RAW ||
        (resCount && !res) || resCount > RC95LDR_MEMORYMAP_MAX_RESERVATIONS ||
        !result) return Rc95LdrInvalidParameter;
    for (i = 0; i < resCount; ++i) {
        size_t k;
        uint64_t endI, pageEndI, pageBaseI;
        if (res[i].MemoryType >= LoaderMaximum ||
            res[i].MemoryType == LoaderFree || !res[i].Length ||
            !Add64(res[i].Base, res[i].Length, &endI))
            return Rc95LdrInvalidParameter;
        pageBaseI = res[i].Base & ~(uint64_t)PAGE_MASK;
        pageEndI = endI;
        if (pageEndI & PAGE_MASK) {
            if (pageEndI > UINT64_MAX - PAGE_MASK) return Rc95LdrIntegerOverflow;
            pageEndI = (pageEndI + PAGE_MASK) & ~(uint64_t)PAGE_MASK;
        }
        for (k = i + 1; k < resCount; ++k) {
            uint64_t endK, pageBaseK, pageEndK;
            if (!res[k].Length || !Add64(res[k].Base, res[k].Length, &endK))
                return Rc95LdrCorruptData;
            pageBaseK = res[k].Base & ~(uint64_t)PAGE_MASK;
            pageEndK = endK;
            if (pageEndK & PAGE_MASK) {
                if (pageEndK > UINT64_MAX - PAGE_MASK) return Rc95LdrIntegerOverflow;
                pageEndK = (pageEndK + PAGE_MASK) & ~(uint64_t)PAGE_MASK;
            }
            if (pageBaseI < pageEndK && pageBaseK < pageEndI &&
                !(res[i].Base == res[k].Base && endI == endK &&
                  res[i].MemoryType == res[k].MemoryType))
                return Rc95LdrCorruptData;
        }
    }
    for (i = 0; i < fwCount; ++i) {
        uint64_t rawEnd, base, end, cursor;
        TYPE_OF_MEMORY type;
        if (!fw[i].Length || !Add64(fw[i].Base, fw[i].Length, &rawEnd))
            return Rc95LdrCorruptData;
        type = FirmwareType(fw[i].Kind);
        if (type == LoaderFree) {
            /* Available RAM is rounded inward; partial pages stay unavailable. */
            base = fw[i].Base;
            if (base & PAGE_MASK) {
                if (base > UINT64_MAX - PAGE_MASK) return Rc95LdrIntegerOverflow;
                base = (base + PAGE_MASK) & ~(uint64_t)PAGE_MASK;
            }
            end = rawEnd & ~(uint64_t)PAGE_MASK;
        } else {
            /* Reserved, bad, ACPI and firmware-owned spans round outward. */
            base = fw[i].Base & ~(uint64_t)PAGE_MASK;
            end = rawEnd;
            if (end & PAGE_MASK) {
                if (end > UINT64_MAX - PAGE_MASK) return Rc95LdrIntegerOverflow;
                end = (end + PAGE_MASK) & ~(uint64_t)PAGE_MASK;
            }
        }
        if (base >= end) continue;
        if (type != LoaderFree && type != LoaderFirmwareTemporary) {
            if (!Emit(segments, &n, RC95LDR_ARRAY_COUNT(segments), base, end, type))
                return Rc95LdrBufferTooSmall;
            continue;
        }
        cursor = base;
        /* Repeatedly select the next reservation; no input-order assumption. */
        while (cursor < end) {
            uint64_t nextBase = end, nextEnd = end;
            TYPE_OF_MEMORY nextType = type;
            for (j = 0; j < resCount; ++j) {
                uint64_t rb, re;
                if (!res[j].Length || !Add64(res[j].Base, res[j].Length, &re))
                    return Rc95LdrCorruptData;
                rb = res[j].Base & ~(uint64_t)PAGE_MASK;
                if (re & PAGE_MASK) {
                    if (re > UINT64_MAX - PAGE_MASK) return Rc95LdrIntegerOverflow;
                    re = (re + PAGE_MASK) & ~(uint64_t)PAGE_MASK;
                }
                if (rb < base) rb = base;
                if (re > end) re = end;
                if (re <= cursor || rb >= end) continue;
                if (rb < nextBase) { nextBase = rb; nextEnd = re; nextType = res[j].MemoryType; }
                else if (rb == nextBase && res[j].MemoryType != nextType)
                    return Rc95LdrCorruptData;
            }
            if (nextBase > cursor &&
                !Emit(segments, &n, RC95LDR_ARRAY_COUNT(segments), cursor, nextBase, type))
                return Rc95LdrBufferTooSmall;
            if (nextBase == end) { cursor = end; break; }
            if (nextBase < cursor) nextBase = cursor;
            if (!Emit(segments, &n, RC95LDR_ARRAY_COUNT(segments), nextBase,
                      nextEnd, nextType)) return Rc95LdrBufferTooSmall;
            cursor = nextEnd;
        }
    }
    /* Firmware maps may be unordered. Sort, then reject overlap rather than
       silently losing reserved ranges or exposing conflicting RAM as free. */
    for (i = 1; i < n; ++i) {
        SEGMENT v = segments[i]; j = i;
        while (j && segments[j-1].Base > v.Base) { segments[j] = segments[j-1]; --j; }
        segments[j] = v;
    }
    /* Every loader reservation must be represented by an emitted segment.
       This catches allocations outside firmware RAM or inside permanent/bad
       firmware ranges instead of silently dropping their ownership record. */
    for (i = 0; i < resCount; ++i) {
        uint64_t rb = res[i].Base & ~(uint64_t)PAGE_MASK;
        uint64_t re = res[i].Base + res[i].Length;
        size_t k;
        if (re & PAGE_MASK) {
            if (re > UINT64_MAX - PAGE_MASK) return Rc95LdrIntegerOverflow;
            re = (re + PAGE_MASK) & ~(uint64_t)PAGE_MASK;
        }
        while (rb < re) {
            for (k = 0; k < n; ++k)
                if (segments[k].Base <= rb && segments[k].End > rb &&
                    segments[k].Type == res[i].MemoryType)
                    break;
            if (k == n) return Rc95LdrCorruptData;
            rb = segments[k].End < re ? segments[k].End : re;
        }
    }
    memset(result, 0, sizeof(*result));
    result->StructureSize = sizeof(*result); result->Version = RC95LDR_MEMORYMAP_ABI_VERSION;
    for (i = 0; i < n; ++i) {
        uint64_t bp = segments[i].Base >> PAGE_SHIFT;
        uint64_t ep = segments[i].End >> PAGE_SHIFT;
        if (i && segments[i-1].End > segments[i].Base) return Rc95LdrCorruptData;
        if (ep <= bp || bp > UINTPTR_MAX || ep-bp > UINTPTR_MAX)
            return Rc95LdrOutOfBounds;
        if (result->RangeCount == RC95LDR_MEMORY_MAX_RANGES)
            return Rc95LdrBufferTooSmall;
        result->Ranges[result->RangeCount].BasePage = bp;
        result->Ranges[result->RangeCount].PageCount = ep-bp;
        result->Ranges[result->RangeCount].Type = (uint32_t)segments[i].Type;
        result->Ranges[result->RangeCount].Reserved = 0;
        ++result->RangeCount;
    }
    return result->RangeCount ? Rc95LdrSuccess : Rc95LdrNotFound;
}

RC95LDR_STATUS
Rc95LdrUefiFinalizeNtMemoryMap(const RC95LDR_UEFI_MEMORY_OPS *ops,
    void *mapBuffer, size_t mapCapacity,
    RC95LDR_FIRMWARE_MEMORY_RANGE *firmwareRanges, size_t rangeCapacity,
    const RC95LDR_MEMORY_RESERVATION *reservations, size_t reservationCount,
    RC95LDR_MEMORYMAP_RESULT *finalMap,
    MEMORY_ALLOCATION_DESCRIPTOR *descriptors, size_t descriptorCapacity,
    LIST_ENTRY *descriptorListHead, size_t *rawMapSize,
    size_t *rawDescriptorSize, uint32_t *descriptorVersion)
{
    uint64_t mapKey;
    size_t rangeCount;
    unsigned attempt;
    RC95LDR_STATUS status;
    if (!ops || !ops->GetMemoryMap || !ops->ExitBootServices ||
        !mapBuffer || !firmwareRanges || !finalMap || !descriptors ||
        !descriptorListHead || !rawMapSize || !rawDescriptorSize ||
        !descriptorVersion)
        return Rc95LdrInvalidParameter;
    for (attempt = 0; attempt < 2; ++attempt) {
        status = Rc95LdrCollectUefiMemoryMap(ops, mapBuffer, mapCapacity,
            firmwareRanges, rangeCapacity, &rangeCount, rawMapSize,
            rawDescriptorSize, &mapKey, descriptorVersion);
        if (status != Rc95LdrSuccess) return status;
        status = Rc95LdrBuildFinalMemoryMap(firmwareRanges, rangeCount,
            reservations, reservationCount, finalMap);
        if (status != Rc95LdrSuccess) return status;
        status = Rc95LdrBuildArcMemoryDescriptorList(finalMap, descriptors,
            descriptorCapacity, descriptorListHead);
        if (status != Rc95LdrSuccess) return status;
        /* Everything above is CPU-only and uses caller-owned storage. Do not
           allocate or call firmware before this ExitBootServices attempt. */
        status = ops->ExitBootServices(ops->Context, ops->ImageHandle, mapKey);
        if (status == Rc95LdrSuccess) return Rc95LdrSuccess;
        if (status != Rc95LdrInvalidParameter) return status;
    }
    return Rc95LdrInvalidParameter;
}

RC95LDR_STATUS
Rc95LdrUefiFinalizeMemoryMap(const RC95LDR_UEFI_MEMORY_OPS *ops,
    void *map, size_t mapCapacity, RC95LDR_FIRMWARE_MEMORY_RANGE *ranges,
    size_t rangeCapacity, size_t *rangeCount, size_t *rawSize,
    size_t *descSize, uint32_t *descVersion)
{
    uint64_t key = 0;
    RC95LDR_STATUS st;
    unsigned attempt;
    if (!ops || !ops->ExitBootServices) return Rc95LdrInvalidParameter;
    for (attempt = 0; attempt != 2; ++attempt) {
        st = Rc95LdrCollectUefiMemoryMap(ops, map, mapCapacity, ranges,
            rangeCapacity, rangeCount, rawSize, descSize, &key, descVersion);
        if (st != Rc95LdrSuccess) return st;
        /* No firmware calls or allocations may occur between GetMemoryMap
           above and ExitBootServices here. Retry only stale map-key failures. */
        st = ops->ExitBootServices(ops->Context, ops->ImageHandle, key);
        if (st == Rc95LdrSuccess) return Rc95LdrSuccess;
        if (st != Rc95LdrInvalidParameter) return st;
    }
    return Rc95LdrInvalidParameter;
}

RC95LDR_STATUS
Rc95LdrBuildArcMemoryDescriptorList(const RC95LDR_MEMORYMAP_RESULT *map,
    MEMORY_ALLOCATION_DESCRIPTOR *descriptors, size_t capacity,
    LIST_ENTRY *head)
{
    size_t i;
    if (!map || map->StructureSize < sizeof(*map) ||
        map->Version != RC95LDR_MEMORYMAP_ABI_VERSION ||
        map->RangeCount > RC95LDR_MEMORY_MAX_RANGES ||
        (map->RangeCount && (!descriptors || !head)) ||
        capacity < map->RangeCount)
        return Rc95LdrInvalidParameter;
    if (!map->RangeCount) return Rc95LdrNotFound;
    for (i = 0; i < map->RangeCount; ++i) {
        const RC95LDR_MEMORY_RANGE *r = &map->Ranges[i];
        if (r->Type >= LoaderMaximum || !r->PageCount ||
            r->BasePage > UINTPTR_MAX || r->PageCount > UINTPTR_MAX ||
            r->BasePage > (uint64_t)UINTPTR_MAX - r->PageCount)
            return Rc95LdrCorruptData;
    }
    head->Flink = (PLIST_ENTRY)head;
    head->Blink = (PLIST_ENTRY)head;
    for (i = 0; i < map->RangeCount; ++i) {
        MEMORY_ALLOCATION_DESCRIPTOR *d = &descriptors[i];
        const RC95LDR_MEMORY_RANGE *r = &map->Ranges[i];
        d->MemoryType = (TYPE_OF_MEMORY)r->Type;
        d->BasePage = (PFN_NUMBER)r->BasePage;
        d->PageCount = (PFN_NUMBER)r->PageCount;
        d->ListEntry.Flink = (PLIST_ENTRY)head;
        d->ListEntry.Blink = head->Blink;
        head->Blink->Flink = &d->ListEntry;
        head->Blink = &d->ListEntry;
    }
    return Rc95LdrSuccess;
}
