#include "rc95ldrrc95nativeprovider.h"
#include "rc95ldrphysmem.h"
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static uint64_t *TableAt(RC95LDR_PHYSMEM_ALLOCATION *a, uint64_t pa)
{
    assert(pa >= a->PhysicalBase);
    assert(pa - a->PhysicalBase <= a->MappingLength - 4096u);
    return (uint64_t *)((uint8_t *)a->WritableMapping +
        (size_t)(pa - a->PhysicalBase));
}

static void TestBuildsAmd64Baseline(void)
{
    RC95LDR_PHYSMEM_ALLOCATION storage;
    RC95LDR_RC95_CPU_INFO cpu;
    RC95LDR_LOADED_MODULE module;
    RC95LDR_PE_SECTION_INFO sections[2];
    RC95LDR_FIRMWARE_MEMORY_RANGE range;
    uint8_t *raw, *aligned;
    size_t used = 0;
    uint64_t rootPa, *pml4, *pdpt, *pd, *pt, va;
    const size_t arenaSize = 4u * 1024u * 1024u;

    raw = (uint8_t *)malloc(arenaSize + 4095u);
    assert(raw != NULL);
    aligned = (uint8_t *)(((uintptr_t)raw + 4095u) & ~(uintptr_t)4095u);
    memset(aligned, 0, arenaSize);
    memset(&storage, 0, sizeof(storage));
    storage.PhysicalBase = (uint64_t)(uintptr_t)aligned;
    storage.PageCount = arenaSize / 4096u;
    storage.AllocationToken = raw;
    storage.WritableMapping = aligned;
    storage.MappingLength = arenaSize;
    storage.MappingFlags = RC95LDR_PHYSMEM_MAP_WRITABLE;
    storage.State = 3u;

    memset(&cpu, 0, sizeof(cpu));
    cpu.StructureSize = sizeof(cpu);
    cpu.Version = RC95LDR_RC95_CPU_INFO_VERSION;
    cpu.PagingMode = RC95LDR_CPU_PAGING_AMD64;
    cpu.PhysicalAddressBits = 48u;
    cpu.VirtualAddressBits = 48u;
    cpu.FeatureFlags = RC95LDR_CPU_FEATURE_LONG_MODE |
        RC95LDR_CPU_FEATURE_PAE | RC95LDR_CPU_FEATURE_CX8 |
        RC95LDR_CPU_FEATURE_NX;

    memset(&module, 0, sizeof(module));
    module.ImageBase = 0xffff800000100000ULL;
    module.ImageSize = 0x20000u;
    module.PhysicalBase = 0x00300000u;
    module.PhysicalSize = 0x20000u;
    memset(sections, 0, sizeof(sections));
    sections[0].VirtualAddress = 0x1000u;
    sections[0].VirtualSize = 0x1000u;
    sections[0].MappedSize = 0x1000u;
    sections[0].Characteristics = RC95LDR_PE_SCN_MEM_READ |
        RC95LDR_PE_SCN_MEM_EXECUTE;
    sections[1].VirtualAddress = 0x2000u;
    sections[1].VirtualSize = 0x1000u;
    sections[1].MappedSize = 0x1000u;
    sections[1].Characteristics = RC95LDR_PE_SCN_MEM_READ |
        RC95LDR_PE_SCN_MEM_WRITE;
    module.PeSections = sections;
    module.PeSectionCount = 2u;
    module.PeSizeOfHeaders = 0x400u;

    memset(&range, 0, sizeof(range));
    range.Base = 0x00200000u;
    range.Length = 0x00400000u;
    range.Kind = Rc95LdrFirmwareMemoryAvailable;

    assert(Rc95LdrBuildBaselinePageTables(NULL, Rc95LdrNtArchAmd64,
        &cpu, NULL, &storage, &module, 1u, NULL, 0u, &range, 1u,
        &used, &rootPa) == Rc95LdrSuccess);
    assert(rootPa == storage.PhysicalBase);
    assert(used >= 5u * 4096u);
    assert((used & 4095u) == 0u);
    assert(used <= storage.MappingLength);

    pml4 = TableAt(&storage, rootPa);
    assert(pml4[(module.ImageBase >> 39) & 0x1ffu] & 1u);
    pdpt = TableAt(&storage,
        pml4[(module.ImageBase >> 39) & 0x1ffu] & 0x000ffffffffff000ULL);
    pd = TableAt(&storage,
        pdpt[(module.ImageBase >> 30) & 0x1ffu] & 0x000ffffffffff000ULL);
    pt = TableAt(&storage,
        pd[(module.ImageBase >> 21) & 0x1ffu] & 0x000ffffffffff000ULL);
    va = module.ImageBase;
    assert((pt[(va >> 12) & 0x1ffu] & 0x000ffffffffff000ULL) ==
        module.PhysicalBase);
    assert(pt[(va >> 12) & 0x1ffu] & 1u);
    /* Headers are read-only; RX text is read-only; RW data is writable.
       Gaps in SizeOfImage are intentionally not mapped. */
    assert((pt[((module.ImageBase + 0x0000u) >> 12) & 0x1ffu] & 2u) == 0u);
    assert((pt[((module.ImageBase + 0x1000u) >> 12) & 0x1ffu] & 2u) == 0u);
    assert((pt[((module.ImageBase + 0x2000u) >> 12) & 0x1ffu] & 2u) != 0u);
    assert((pt[((module.ImageBase + 0x0000u) >> 12) & 0x1ffu] & (1ULL << 63)) != 0u);
    assert((pt[((module.ImageBase + 0x1000u) >> 12) & 0x1ffu] & (1ULL << 63)) == 0u);
    assert((pt[((module.ImageBase + 0x2000u) >> 12) & 0x1ffu] & (1ULL << 63)) != 0u);
    assert((pt[((module.ImageBase + 0x3000u) >> 12) & 0x1ffu] & 1u) == 0u);

    /* The builder independently rejects a page-sharing layout even if a
       caller bypasses the PE parser. */
    sections[1].VirtualAddress = 0x1800u;
    used = 0;
    memset(aligned, 0, arenaSize);
    assert(Rc95LdrBuildBaselinePageTables(NULL, Rc95LdrNtArchAmd64,
        &cpu, NULL, &storage, &module, 1u, NULL, 0u, &range, 1u,
        &used, &rootPa) == Rc95LdrInvalidImage);

    /* Without NX support, writable non-executable data must not become RWX. */
    sections[1].VirtualAddress = 0x2000u;
    cpu.FeatureFlags &= ~RC95LDR_CPU_FEATURE_NX;
    used = 0;
    memset(aligned, 0, arenaSize);
    assert(Rc95LdrBuildBaselinePageTables(NULL, Rc95LdrNtArchAmd64,
        &cpu, NULL, &storage, &module, 1u, NULL, 0u, &range, 1u,
        &used, &rootPa) == Rc95LdrUnsupported);

    free(raw);
}

static void TestRejectsEmptyAndWrongMode(void)
{
    RC95LDR_PHYSMEM_ALLOCATION storage;
    RC95LDR_RC95_CPU_INFO cpu;
    RC95LDR_LOADED_MODULE module;
    RC95LDR_FIRMWARE_MEMORY_RANGE range;
    size_t used = 0;
    uint64_t root = 0;
    memset(&storage, 0, sizeof(storage));
    memset(&cpu, 0, sizeof(cpu));
    memset(&module, 0, sizeof(module));
    memset(&range, 0, sizeof(range));
    cpu.StructureSize = sizeof(cpu);
    cpu.Version = RC95LDR_RC95_CPU_INFO_VERSION;
    cpu.PagingMode = RC95LDR_CPU_PAGING_I386_NONPAE;
    cpu.PhysicalAddressBits = 32u;
    assert(Rc95LdrBuildBaselinePageTables(NULL, Rc95LdrNtArchAmd64,
        &cpu, NULL, &storage, &module, 1u, NULL, 0u, &range, 1u,
        &used, &root) == Rc95LdrInvalidParameter);
}

int main(void)
{
    TestBuildsAmd64Baseline();
    TestRejectsEmptyAndWrongMode();
    return 0;
}
