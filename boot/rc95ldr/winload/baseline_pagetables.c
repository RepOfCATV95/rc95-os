#include "include/rc95ldrrc95nativeprovider.h"
#include "include/rc95ldrphysmem.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define PT_PAGE 4096ULL
#define PT_PRESENT 0x001ULL
#define PT_WRITE 0x002ULL
#define PT_LARGE 0x080ULL
#define PT_ADDR 0x000ffffffffff000ULL
#define PT_LARGE_ADDR 0x000fffffffe00000ULL
#define PT_2M 0x200000ULL
#define PT_NX (1ULL << 63)
#define PT_SECTION_WRITE 0x80000000u

typedef struct _PT_BUILD {
    RC95LDR_PHYSMEM_ALLOCATION *Storage;
    size_t Used;
    uint32_t Mode;
    uint32_t PhysicalBits;
    uint32_t NxEnabled;
    uint64_t PhysicalLimit;
    uint64_t VirtualLimit;
} PT_BUILD;

static int PtAdd(uint64_t a, uint64_t b, uint64_t *v)
{
    if (b > UINT64_MAX - a) return 0;
    *v = a + b;
    return 1;
}

static int PtCanonical(uint64_t v, uint32_t bits)
{
    uint64_t top;
    if (bits < 32 || bits > 63) return 0;
    top = v >> bits;
    return top == 0 || top == ((1ULL << (64u - bits)) - 1u);
}

static RC95LDR_STATUS PtAllocate(PT_BUILD *b, uint64_t **table, uint64_t *pa)
{
    size_t off;
    if (!b || !table || !pa || (b->Used & (PT_PAGE - 1u)) ||
        b->Used > b->Storage->MappingLength ||
        PT_PAGE > b->Storage->MappingLength - b->Used)
        return Rc95LdrBufferTooSmall;
    off = b->Used;
    b->Used += (size_t)PT_PAGE;
    *table = (uint64_t *)((uint8_t *)b->Storage->WritableMapping + off);
    *pa = b->Storage->PhysicalBase + off;
    if ((*pa & (PT_PAGE - 1u)) || *pa >= b->PhysicalLimit ||
        PT_PAGE > b->PhysicalLimit - *pa)
        return Rc95LdrOutOfBounds;
    memset(*table, 0, (size_t)PT_PAGE);
    return Rc95LdrSuccess;
}

static RC95LDR_STATUS PtChild(PT_BUILD *b, uint64_t *parent,
                              uint64_t **child)
{
    uint64_t pa;
    RC95LDR_STATUS st;
    size_t off;
    if (!(*parent & PT_PRESENT)) {
        st = PtAllocate(b, child, &pa);
        if (st != Rc95LdrSuccess) return st;
        *parent = (pa & PT_ADDR) | PT_PRESENT | PT_WRITE;
        return Rc95LdrSuccess;
    }
    if (*parent & PT_LARGE) return Rc95LdrInvalidImage;
    pa = *parent & PT_ADDR;
    if (pa < b->Storage->PhysicalBase) return Rc95LdrInvalidImage;
    pa -= b->Storage->PhysicalBase;
    if ((pa & (PT_PAGE - 1u)) || pa > b->Storage->MappingLength ||
        PT_PAGE > b->Storage->MappingLength - (size_t)pa)
        return Rc95LdrInvalidImage;
    off = (size_t)pa;
    *child = (uint64_t *)((uint8_t *)b->Storage->WritableMapping + off);
    *parent |= PT_WRITE;
    return Rc95LdrSuccess;
}

static RC95LDR_STATUS PtMap4K(PT_BUILD *b, uint64_t va, uint64_t pa, int writable, int executable)
{
    uint64_t *l4 = NULL, *l3 = NULL, *l2 = NULL, *l1 = NULL;
    uint64_t *entry;
    RC95LDR_STATUS st;
    if ((va | pa) & (PT_PAGE - 1u) || pa >= b->PhysicalLimit)
        return Rc95LdrInvalidParameter;
    if (b->Mode == RC95LDR_CPU_PAGING_I386_NONPAE) {
        uint32_t *pd = (uint32_t *)b->Storage->WritableMapping;
        uint32_t *pt;
        uint64_t *allocatedTable;
        uint32_t pde = pd[(va >> 22) & 0x3ffu];
        if (!(pde & 1u)) {
            uint64_t tablePa;
            st = PtAllocate(b, &allocatedTable, &tablePa);
            if (st != Rc95LdrSuccess) return st;
            if (tablePa > 0xfffff000ULL) return Rc95LdrOutOfBounds;
            pt = (uint32_t *)allocatedTable;
            pd[(va >> 22) & 0x3ffu] = (uint32_t)tablePa | 3u;
        } else {
            uint64_t off = pde & 0xfffff000u;
            if (off < b->Storage->PhysicalBase) return Rc95LdrInvalidImage;
            off -= b->Storage->PhysicalBase;
            if (off > b->Storage->MappingLength ||
                PT_PAGE > b->Storage->MappingLength - (size_t)off)
                return Rc95LdrInvalidImage;
            pt = (uint32_t *)((uint8_t *)b->Storage->WritableMapping + (size_t)off);
        }
        if (pt[(va >> 12) & 0x3ffu] & 1u)
            return ((pt[(va >> 12) & 0x3ffu] & 0xfffff000u) == pa &&
                    (((pt[(va >> 12) & 0x3ffu] & 2u) != 0) == (writable != 0))) ?
                Rc95LdrSuccess : Rc95LdrInvalidImage;
        pt[(va >> 12) & 0x3ffu] = (uint32_t)pa | 1u | (writable ? 2u : 0u);
        return Rc95LdrSuccess;
    }

    l4 = (uint64_t *)b->Storage->WritableMapping;
    if (b->Mode == RC95LDR_CPU_PAGING_I386_PAE) {
        st = PtChild(b, &l4[(va >> 30) & 3u], &l3);
        if (st != Rc95LdrSuccess) return st;
        /* In legacy PAE mode PDPTE.RW is reserved; keep only Present. */
        l4[(va >> 30) & 3u] &= ~PT_WRITE;
        if (l3[(va >> 21) & 0x1ffu] & PT_LARGE) {
            uint64_t large = l3[(va >> 21) & 0x1ffu];
            return ((large & PT_LARGE_ADDR) == (pa & PT_LARGE_ADDR)) ?
                Rc95LdrSuccess : Rc95LdrInvalidImage;
        }
        st = PtChild(b, &l3[(va >> 21) & 0x1ffu], &l2);
        if (st != Rc95LdrSuccess) return st;
        entry = &l2[(va >> 12) & 0x1ffu];
    } else {
        st = PtChild(b, &l4[(va >> 39) & 0x1ffu], &l3);
        if (st != Rc95LdrSuccess) return st;
        st = PtChild(b, &l3[(va >> 30) & 0x1ffu], &l2);
        if (st != Rc95LdrSuccess) return st;
        if (l2[(va >> 21) & 0x1ffu] & PT_LARGE) {
            uint64_t large = l2[(va >> 21) & 0x1ffu];
            return ((large & PT_LARGE_ADDR) == (pa & PT_LARGE_ADDR)) ?
                Rc95LdrSuccess : Rc95LdrInvalidImage;
        }
        st = PtChild(b, &l2[(va >> 21) & 0x1ffu], &l1);
        if (st != Rc95LdrSuccess) return st;
        entry = &l1[(va >> 12) & 0x1ffu];
    }
    if (*entry & PT_PRESENT)
        return ((*entry & PT_ADDR) == pa && (((*entry & PT_WRITE) != 0) == (writable != 0)) &&
            (((*entry & PT_NX) != 0) == ((b->NxEnabled && !executable) != 0))) ?
            Rc95LdrSuccess : Rc95LdrInvalidImage;
    /* x86 has no read-disable PTE bit. Preserve PE write intent exactly.
       AMD64 NX is emitted only when the transfer stub is instructed to set
       EFER.NXE before loading this CR3. */
    *entry = (pa & PT_ADDR) | PT_PRESENT | (writable ? PT_WRITE : 0u) |
        ((b->NxEnabled && !executable) ? PT_NX : 0u);
    return Rc95LdrSuccess;
}

static RC95LDR_STATUS PtMap2M(PT_BUILD *b, uint64_t va, uint64_t pa)
{
    uint64_t *root, *pdpt, *pd;
    RC95LDR_STATUS st;
    if (b->Mode == RC95LDR_CPU_PAGING_I386_NONPAE ||
        ((va | pa) & (PT_2M - 1u)))
        return Rc95LdrUnsupported;
    root = (uint64_t *)b->Storage->WritableMapping;
    if (b->Mode == RC95LDR_CPU_PAGING_I386_PAE) {
        st = PtChild(b, &root[(va >> 30) & 3u], &pd);
        if (st != Rc95LdrSuccess) return st;
        /* In legacy PAE mode PDPTE.RW is reserved; keep only Present. */
        root[(va >> 30) & 3u] &= ~PT_WRITE;
    } else {
        st = PtChild(b, &root[(va >> 39) & 0x1ffu], &pdpt);
        if (st != Rc95LdrSuccess) return st;
        st = PtChild(b, &pdpt[(va >> 30) & 0x1ffu], &pd);
        if (st != Rc95LdrSuccess) return st;
    }
    if (pd[(va >> 21) & 0x1ffu] & PT_PRESENT) {
        uint64_t old = pd[(va >> 21) & 0x1ffu];
        if (old & PT_LARGE)
            return ((old & PT_LARGE_ADDR) == (pa & PT_LARGE_ADDR)) ?
                Rc95LdrSuccess : Rc95LdrInvalidImage;
        return Rc95LdrUnsupported; /* caller fills an existing PT page-by-page */
    }
    pd[(va >> 21) & 0x1ffu] = (pa & PT_LARGE_ADDR) |
        PT_PRESENT | PT_WRITE | PT_LARGE;
    return Rc95LdrSuccess;
}

static RC95LDR_STATUS PtMapRange(PT_BUILD *b, uint64_t va, uint64_t pa,
                                 uint64_t length, int writable, int executable)
{
    uint64_t offset;
    if (!length || ((va | pa | length) & (PT_PAGE - 1u)))
        return Rc95LdrInvalidParameter;
    for (offset = 0; offset < length; offset += PT_PAGE) {
        RC95LDR_STATUS st = PtMap4K(b, va + offset, pa + offset, writable, executable);
        if (st != Rc95LdrSuccess) return st;
    }
    return Rc95LdrSuccess;
}

static RC95LDR_STATUS
PtValidateModuleSections(const RC95LDR_LOADED_MODULE *m)
{
    uint64_t headerBytes, headerEnd;
    uint32_t i, j;
    if (!m || !m->PeSections || !m->PeSectionCount ||
        m->PeSectionCount > RC95LDR_PE_MAX_SECTIONS || !m->PeSizeOfHeaders ||
        m->PeSizeOfHeaders > m->ImageSize ||
        !PtAdd(m->PeSizeOfHeaders, PT_PAGE - 1u, &headerBytes))
        return Rc95LdrInvalidImage;
    headerBytes &= ~(PT_PAGE - 1u);
    if (!headerBytes || headerBytes > m->ImageSize)
        return Rc95LdrInvalidImage;
    headerEnd = headerBytes;
    for (i = 0; i < m->PeSectionCount; ++i) {
        const RC95LDR_PE_SECTION_INFO *a = &m->PeSections[i];
        uint64_t end, first, last;
        if (!a->MappedSize)
            continue;
        if (a->MappedSize < a->VirtualSize ||
            !PtAdd(a->VirtualAddress, a->MappedSize, &end) ||
            end > m->ImageSize ||
            !PtAdd(end, PT_PAGE - 1u, &last))
            return Rc95LdrInvalidImage;
        first = a->VirtualAddress & ~(PT_PAGE - 1u);
        last &= ~(PT_PAGE - 1u);
        if (first < headerEnd || last <= first ||
            last > ((m->ImageSize + PT_PAGE - 1u) & ~(PT_PAGE - 1u)))
            return Rc95LdrInvalidImage;
        for (j = 0; j < i; ++j) {
            const RC95LDR_PE_SECTION_INFO *b = &m->PeSections[j];
            uint64_t bEnd, bLast, bFirst;
            if (!b->MappedSize) continue;
            if (!PtAdd(b->VirtualAddress, b->MappedSize, &bEnd) ||
                !PtAdd(bEnd, PT_PAGE - 1u, &bLast))
                return Rc95LdrInvalidImage;
            bFirst = b->VirtualAddress & ~(PT_PAGE - 1u);
            bLast &= ~(PT_PAGE - 1u);
            if (first < bLast && bFirst < last)
                return Rc95LdrInvalidImage;
        }
    }
    return Rc95LdrSuccess;
}

static int PtModuleCovers(const RC95LDR_LOADED_MODULE *modules,
                          uint32_t count, uint64_t address)
{
    uint32_t i;
    for (i = 0; i < count; ++i) {
        uint64_t end;
        if (modules[i].ImageSize &&
            PtAdd(modules[i].ImageBase, modules[i].ImageSize, &end) &&
            address >= modules[i].ImageBase && address < end)
            return 1;
    }
    return 0;
}

static int PtModulePhysicalCovers(const RC95LDR_LOADED_MODULE *modules,
                                uint32_t count, uint64_t address)
{
    uint32_t i;
    for (i = 0; i < count; ++i) {
        uint64_t end;
        if (modules[i].PhysicalSize &&
            PtAdd(modules[i].PhysicalBase, modules[i].PhysicalSize, &end) &&
            address >= modules[i].PhysicalBase && address < end)
            return 1;
    }
    return 0;
}

static int PtModulePhysicalOverlaps(const RC95LDR_LOADED_MODULE *modules,
                                    uint32_t count, uint64_t base,
                                    uint64_t length)
{
    uint32_t i;
    uint64_t end;
    if (!PtAdd(base, length, &end)) return 1;
    for (i = 0; i < count; ++i) {
        uint64_t moduleEnd;
        if (modules[i].PhysicalSize &&
            PtAdd(modules[i].PhysicalBase, modules[i].PhysicalSize, &moduleEnd) &&
            base < moduleEnd && modules[i].PhysicalBase < end)
            return 1;
    }
    return 0;
}

static int PtModuleOverlaps(const RC95LDR_LOADED_MODULE *modules,
                            uint32_t count, uint64_t base, uint64_t length)
{
    uint32_t i;
    uint64_t end;
    if (!PtAdd(base, length, &end)) return 1;
    for (i = 0; i < count; ++i) {
        uint64_t moduleEnd;
        if (PtAdd(modules[i].ImageBase, modules[i].ImageSize, &moduleEnd) &&
            base < moduleEnd && modules[i].ImageBase < end)
            return 1;
    }
    return 0;
}

static RC95LDR_STATUS PtMapAvailableRange(PT_BUILD *b, uint64_t base,
                                          uint64_t length,
                                          const RC95LDR_LOADED_MODULE *modules,
                                          uint32_t moduleCount,
                                          const RC95LDR_PAE_VIRTUAL_WINDOW *window)
{
    uint64_t end, cursor, limit = b->PhysicalLimit;
    if (b->VirtualLimit && limit > b->VirtualLimit) limit = b->VirtualLimit;
    if (!length || !PtAdd(base, length, &end)) return Rc95LdrCorruptData;
    if (base >= limit) return Rc95LdrSuccess;
    if (end > limit) end = limit;
    if (!PtAdd(base, PT_PAGE - 1u, &cursor)) return Rc95LdrIntegerOverflow;
    cursor &= ~(PT_PAGE - 1u);
    end &= ~(PT_PAGE - 1u);
    while (cursor < end) {
        RC95LDR_STATUS st;
        if (b->Mode != RC95LDR_CPU_PAGING_I386_NONPAE &&
            !(cursor & (PT_2M - 1u)) && end - cursor >= PT_2M &&
            !PtModuleOverlaps(modules, moduleCount, cursor, PT_2M) &&
            !PtModulePhysicalOverlaps(modules, moduleCount, cursor, PT_2M) &&
            (!window || cursor + PT_2M <= window->VirtualBase ||
             cursor >= window->VirtualBase + window->Length)) {
            st = PtMap2M(b, cursor, cursor);
            if (st == Rc95LdrSuccess) {
                cursor += PT_2M;
                continue;
            }
            if (st != Rc95LdrUnsupported) return st;
        }
        if (!PtModuleCovers(modules, moduleCount, cursor) &&
            !PtModulePhysicalCovers(modules, moduleCount, cursor) &&
            (!window || cursor < window->VirtualBase ||
             cursor >= window->VirtualBase + window->Length)) {
            st = PtMap4K(b, cursor, cursor, 1, 1);
            if (st != Rc95LdrSuccess) return st;
        }
        cursor += PT_PAGE;
    }
    return Rc95LdrSuccess;
}

RC95LDR_STATUS
Rc95LdrBuildBaselinePageTables(
    void *Context, RC95LDR_NT_TARGET_ARCH Architecture,
    const RC95LDR_RC95_CPU_INFO *Cpu,
    const RC95LDR_PAE_VIRTUAL_WINDOW *PaeWindow,
    const RC95LDR_PHYSMEM_ALLOCATION *PageTableStorage,
    const RC95LDR_LOADED_MODULE *Modules, uint32_t ModuleCount,
    const RC95LDR_NATIVE_ALLOCATION *Allocations, size_t AllocationCount,
    const RC95LDR_FIRMWARE_MEMORY_RANGE *FirmwareRanges, size_t RangeCount,
    size_t *PageTableStorageUsed, uint64_t *RootPhysicalAddress)
{
    PT_BUILD b;
    uint64_t rootPa, storageEnd;
    uint64_t *root;
    size_t i;
    RC95LDR_STATUS st;
    (void)Context;

    if (!Cpu || !PageTableStorage || !Modules || !ModuleCount ||
        (AllocationCount && !Allocations) || !FirmwareRanges || !RangeCount ||
        !PageTableStorageUsed || !RootPhysicalAddress ||
        (Architecture != Rc95LdrNtArchI386 && Architecture != Rc95LdrNtArchAmd64) ||
        Cpu->StructureSize < sizeof(*Cpu) ||
        Cpu->Version != RC95LDR_RC95_CPU_INFO_VERSION ||
        !PageTableStorage->WritableMapping ||
        Rc95LdrPhysMemValidate(PageTableStorage) != Rc95LdrSuccess ||
        (PageTableStorage->PhysicalBase & (PT_PAGE - 1u)) ||
        PageTableStorage->MappingLength < PT_PAGE ||
        (uint64_t)(uintptr_t)PageTableStorage->WritableMapping !=
            PageTableStorage->PhysicalBase ||
        *PageTableStorageUsed != 0)
        return Rc95LdrInvalidParameter;

    memset(&b, 0, sizeof(b));
    b.Storage = (RC95LDR_PHYSMEM_ALLOCATION *)(uintptr_t)PageTableStorage;
    b.Mode = Cpu->PagingMode;
    b.PhysicalBits = Cpu->PhysicalAddressBits;
    b.NxEnabled = (b.Mode != RC95LDR_CPU_PAGING_I386_NONPAE &&
        (Cpu->FeatureFlags & RC95LDR_CPU_FEATURE_NX) &&
        (Cpu->FeatureFlags & RC95LDR_CPU_FEATURE_MSR)) ? 1u : 0u;
    if (b.Mode == RC95LDR_CPU_PAGING_I386_NONPAE) {
        if (Architecture != Rc95LdrNtArchI386 || b.PhysicalBits != 32u ||
            PageTableStorage->PhysicalBase >= 0x100000000ULL ||
            PageTableStorage->MappingLength >
                0x100000000ULL - PageTableStorage->PhysicalBase)
            return Rc95LdrUnsupported;
        b.PhysicalLimit = 0x100000000ULL;
    } else if (b.Mode == RC95LDR_CPU_PAGING_I386_PAE) {
        if (Architecture != Rc95LdrNtArchI386 ||
            !(Cpu->FeatureFlags & RC95LDR_CPU_FEATURE_PAE) ||
            !(Cpu->FeatureFlags & RC95LDR_CPU_FEATURE_CX8) ||
            b.PhysicalBits < 33u || b.PhysicalBits > 52u ||
            PageTableStorage->PhysicalBase >= 0x100000000ULL ||
            PageTableStorage->MappingLength >
                0x100000000ULL - PageTableStorage->PhysicalBase)
            return Rc95LdrUnsupported;
        b.PhysicalLimit = 1ULL << b.PhysicalBits;
        b.VirtualLimit = 0x100000000ULL;
        if (!PaeWindow || !PaeWindow->VirtualBase || !PaeWindow->Length ||
            (PaeWindow->VirtualBase & (PT_PAGE - 1u)) ||
            (PaeWindow->Length & (PT_PAGE - 1u)) ||
            PaeWindow->VirtualBase >= b.VirtualLimit ||
            PaeWindow->Length > b.VirtualLimit - PaeWindow->VirtualBase)
            return Rc95LdrInvalidParameter;
    } else if (b.Mode == RC95LDR_CPU_PAGING_AMD64) {
        if (Architecture != Rc95LdrNtArchAmd64 ||
            !(Cpu->FeatureFlags & RC95LDR_CPU_FEATURE_LONG_MODE) ||
            !(Cpu->FeatureFlags & RC95LDR_CPU_FEATURE_PAE) ||
            b.PhysicalBits < 32u || b.PhysicalBits > 52u ||
            Cpu->VirtualAddressBits != 48u)
            return Rc95LdrUnsupported;
        b.PhysicalLimit = 1ULL << b.PhysicalBits;
        b.VirtualLimit = 1ULL << (Cpu->VirtualAddressBits - 1u);
        if (PaeWindow) return Rc95LdrInvalidParameter;
    } else return Rc95LdrUnsupported;

    if (!PtAdd(PageTableStorage->PhysicalBase,
        PageTableStorage->MappingLength, &storageEnd) ||
        storageEnd > b.PhysicalLimit ||
        (b.VirtualLimit && storageEnd > b.VirtualLimit))
        return Rc95LdrOutOfBounds;

    b.Used = (size_t)PT_PAGE;
    root = (uint64_t *)PageTableStorage->WritableMapping;
    memset(root, 0, (size_t)PT_PAGE);
    rootPa = PageTableStorage->PhysicalBase;

    /* First map explicit virtual module images. This lets their page-table
       pages take precedence over the low-RAM identity map. */
    for (i = 0; i < ModuleCount; ++i) {
        uint64_t bytes, mapped;
        if (!Modules[i].ImageBase || !Modules[i].PhysicalBase ||
            !Modules[i].ImageSize || Modules[i].PhysicalSize < Modules[i].ImageSize ||
            (Modules[i].ImageBase & (PT_PAGE - 1u)) ||
            (Modules[i].PhysicalBase & (PT_PAGE - 1u)) ||
            !PtAdd(Modules[i].ImageSize, PT_PAGE - 1u, &bytes))
            return Rc95LdrInvalidImage;
        mapped = bytes & ~(PT_PAGE - 1u);
        if (Modules[i].PhysicalSize < mapped ||
            !PtAdd(Modules[i].ImageBase, mapped, &bytes) ||
            !PtAdd(Modules[i].PhysicalBase, mapped, &storageEnd) ||
            storageEnd > b.PhysicalLimit ||
            !PtAdd(Modules[i].PhysicalBase, Modules[i].PhysicalSize, &storageEnd) ||
            storageEnd > b.PhysicalLimit)
            return Rc95LdrOutOfBounds;
        if (b.Mode != RC95LDR_CPU_PAGING_AMD64 &&
            bytes > 0x100000000ULL)
            return Rc95LdrOutOfBounds;
        if (b.Mode == RC95LDR_CPU_PAGING_AMD64 &&
            (!PtCanonical(Modules[i].ImageBase, Cpu->VirtualAddressBits) ||
             !PtCanonical(bytes - 1u, Cpu->VirtualAddressBits)))
            return Rc95LdrOutOfBounds;
        {
            uint32_t previousModule;
            for (previousModule = 0; previousModule < i; ++previousModule) {
                uint64_t previousVirtualBytes, previousVirtualEnd;
                uint64_t previousPhysicalEnd, currentPhysicalEnd;
                if (!PtAdd(Modules[previousModule].ImageSize,
                        PT_PAGE - 1u, &previousVirtualBytes))
                    return Rc95LdrIntegerOverflow;
                previousVirtualBytes &= ~(PT_PAGE - 1u);
                if (!PtAdd(Modules[previousModule].ImageBase,
                        previousVirtualBytes, &previousVirtualEnd) ||
                    !PtAdd(Modules[previousModule].PhysicalBase,
                        Modules[previousModule].PhysicalSize, &previousPhysicalEnd) ||
                    !PtAdd(Modules[i].PhysicalBase,
                        Modules[i].PhysicalSize, &currentPhysicalEnd))
                    return Rc95LdrIntegerOverflow;
                if ((Modules[i].ImageBase < previousVirtualEnd &&
                     Modules[previousModule].ImageBase < bytes) ||
                    (Modules[i].PhysicalBase < previousPhysicalEnd &&
                     Modules[previousModule].PhysicalBase < currentPhysicalEnd))
                    return Rc95LdrInvalidImage;
            }
        }
        st = PtValidateModuleSections(&Modules[i]);
        if (st != Rc95LdrSuccess) return st;
        {
            uint64_t headerBytes = ((uint64_t)Modules[i].PeSizeOfHeaders +
                PT_PAGE - 1u) & ~(PT_PAGE - 1u);
            st = PtMapRange(&b, Modules[i].ImageBase,
                Modules[i].PhysicalBase, headerBytes, 0, 0);
            if (st != Rc95LdrSuccess) return st;
            for (uint32_t sectionIndex = 0;
                 sectionIndex < Modules[i].PeSectionCount; ++sectionIndex) {
                const RC95LDR_PE_SECTION_INFO *section =
                    &Modules[i].PeSections[sectionIndex];
                uint64_t first, end, last;
                if (!section->MappedSize) continue;
                first = section->VirtualAddress & ~(PT_PAGE - 1u);
                if (!PtAdd(section->VirtualAddress, section->MappedSize, &end) ||
                    !PtAdd(end, PT_PAGE - 1u, &last))
                    return Rc95LdrInvalidImage;
                last &= ~(PT_PAGE - 1u);
                /* A writable, non-executable PE section requires NX. If the
                   active paging mode cannot safely enforce NX, fail closed
                   rather than publish an accidental RWX data mapping. */
                if (!b.NxEnabled &&
                    (section->Characteristics & PT_SECTION_WRITE) &&
                    !(section->Characteristics & RC95LDR_PE_SCN_MEM_EXECUTE))
                    return Rc95LdrUnsupported;
                st = PtMapRange(&b, Modules[i].ImageBase + first,
                    Modules[i].PhysicalBase + first, last - first,
                    (section->Characteristics & PT_SECTION_WRITE) != 0,
                    (section->Characteristics & RC95LDR_PE_SCN_MEM_EXECUTE) != 0);
                if (st != Rc95LdrSuccess) return st;
            }
        }
    }

    /* Every provider allocation remains identity mapped across the transition,
       including the page-table arena, stack, heap, loader block and metadata. */
    for (i = 0; i < AllocationCount; ++i) {
        uint64_t end;
        if (!Allocations[i].PhysicalBase || !Allocations[i].Length ||
            (Allocations[i].Length & (PT_PAGE - 1u)) ||
            !PtAdd(Allocations[i].PhysicalBase, Allocations[i].Length, &end) ||
            end > b.PhysicalLimit ||
            (b.VirtualLimit && end > b.VirtualLimit))
            return Rc95LdrOutOfBounds;
        st = PtMapRange(&b, Allocations[i].PhysicalBase,
                        Allocations[i].PhysicalBase,
                        (Allocations[i].Length + PT_PAGE - 1u) & ~(PT_PAGE - 1u), 1, 1);
        if (st != Rc95LdrSuccess) return st;
    }
    st = PtMapRange(&b, PageTableStorage->PhysicalBase,
                    PageTableStorage->PhysicalBase,
                    (PageTableStorage->MappingLength + PT_PAGE - 1u) & ~(PT_PAGE - 1u), 1, 1);
    if (st != Rc95LdrSuccess) return st;

    /* Only firmware-designated available RAM is identity mapped. I386 PAE
       maps the low 4-GiB aperture here; high RAM is installed in its explicit
       bounded virtual window by the provider after this callback returns. */
    for (i = 0; i < RangeCount; ++i) {
        if (FirmwareRanges[i].Kind != Rc95LdrFirmwareMemoryAvailable)
            continue;
        st = PtMapAvailableRange(&b, FirmwareRanges[i].Base,
                                 FirmwareRanges[i].Length, Modules, ModuleCount,
                                 PaeWindow);
        if (st != Rc95LdrSuccess) return st;
    }

    if (b.Used > PageTableStorage->MappingLength ||
        (b.Used & (PT_PAGE - 1u)) || !rootPa)
        return Rc95LdrBufferTooSmall;
    *PageTableStorageUsed = b.Used;
    *RootPhysicalAddress = rootPa;
    return Rc95LdrSuccess;
}
