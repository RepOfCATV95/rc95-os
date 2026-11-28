#include "include/rc95ldrrc95nativeprovider.h"
#include <stdint.h>
#include <string.h>

static int
PaeOverlap(uint64_t A, uint64_t ALength, uint64_t B, uint64_t BLength)
{
    uint64_t AEnd, BEnd;

    if (!ALength || !BLength)
        return 0;
    if (ALength > UINT64_MAX - A || BLength > UINT64_MAX - B)
        return 1;
    AEnd = A + ALength;
    BEnd = B + BLength;
    return A < BEnd && B < AEnd;
}

#define PAE_ENTRY_PRESENT 0x001ULL
#define PAE_ENTRY_WRITABLE 0x002ULL
#define PAE_ENTRY_PWT 0x008ULL
#define PAE_ENTRY_PCD 0x010ULL
#define PAE_ENTRY_LARGE 0x080ULL
#define PAE_ADDRESS_MASK 0x000ffffffffff000ULL
#define PAE_PAGE_SIZE 4096u

static int
PaeAddU64(uint64_t A, uint64_t B, uint64_t *End)
{
    if (B > UINT64_MAX - A)
        return 0;
    *End = A + B;
    return 1;
}

static int
PaeStorageOffset(
    const RC95LDR_PHYSMEM_ALLOCATION *Storage,
    uint64_t PhysicalAddress,
    size_t *Offset)
{
    uint64_t Delta;
    if (PhysicalAddress < Storage->PhysicalBase)
        return 0;
    Delta = PhysicalAddress - Storage->PhysicalBase;
    if ((Delta & (PAE_PAGE_SIZE - 1u)) ||
        Delta > Storage->MappingLength ||
        PAE_PAGE_SIZE > Storage->MappingLength - (size_t)Delta)
        return 0;
    *Offset = (size_t)Delta;
    return 1;
}

static RC95LDR_STATUS
PaeAllocateTable(
    RC95LDR_PHYSMEM_ALLOCATION *Storage,
    size_t *Used,
    uint64_t **Table,
    uint64_t *PhysicalAddress)
{
    size_t Offset;
    if (!Storage || !Used || !Table || !PhysicalAddress ||
        (*Used & (PAE_PAGE_SIZE - 1u)) ||
        *Used > Storage->MappingLength ||
        PAE_PAGE_SIZE > Storage->MappingLength - *Used)
        return Rc95LdrBufferTooSmall;
    Offset = *Used;
    *Used += PAE_PAGE_SIZE;
    *Table = (uint64_t *)((uint8_t *)Storage->WritableMapping + Offset);
    *PhysicalAddress = Storage->PhysicalBase + Offset;
    memset(*Table, 0, PAE_PAGE_SIZE);
    return Rc95LdrSuccess;
}

RC95LDR_STATUS
Rc95LdrInstallI386PaeWindowMappings(
    const RC95LDR_RC95_CPU_INFO *Cpu,
    const RC95LDR_PAE_VIRTUAL_WINDOW *Window,
    const RC95LDR_PAE_PHYSICAL_MAPPING *Mappings,
    size_t MappingCount,
    const RC95LDR_PHYSMEM_ALLOCATION *PageTableStorage,
    uint64_t RootPhysicalAddress,
    size_t *PageTableStorageUsed)
{
    RC95LDR_PHYSMEM_ALLOCATION Storage;
    uint64_t PhysicalLimit, WindowEnd, AddressMask;
    uint64_t *Pdpt;
    size_t RootOffset, I, J;

    if (!Cpu || !Window || (MappingCount && !Mappings) ||
        MappingCount > RC95LDR_PAE_WINDOW_MAPPING_MAX ||
        !PageTableStorage || !PageTableStorageUsed ||
        Cpu->PagingMode != RC95LDR_CPU_PAGING_I386_PAE ||
        !(Cpu->FeatureFlags & RC95LDR_CPU_FEATURE_PAE) ||
        !(Cpu->FeatureFlags & RC95LDR_CPU_FEATURE_CX8) ||
        Cpu->PhysicalAddressBits < 33u ||
        Cpu->PhysicalAddressBits > 52u ||
        !Window->VirtualBase || !Window->Length ||
        (Window->VirtualBase & (PAE_PAGE_SIZE - 1u)) ||
        (Window->Length & (PAE_PAGE_SIZE - 1u)) ||
        !PaeAddU64(Window->VirtualBase, Window->Length, &WindowEnd) ||
        WindowEnd > 0x100000000ULL ||
        Rc95LdrPhysMemValidate(PageTableStorage) != Rc95LdrSuccess ||
        !PaeStorageOffset(PageTableStorage, RootPhysicalAddress, &RootOffset) ||
        (*PageTableStorageUsed & (PAE_PAGE_SIZE - 1u)) ||
        *PageTableStorageUsed > PageTableStorage->MappingLength ||
        RootOffset >= *PageTableStorageUsed)
        return Rc95LdrInvalidParameter;

    Storage = *PageTableStorage;
    PhysicalLimit = 1ULL << Cpu->PhysicalAddressBits;
    AddressMask = (PhysicalLimit - 1u) & PAE_ADDRESS_MASK;
    Pdpt = (uint64_t *)((uint8_t *)Storage.WritableMapping + RootOffset);

    for (I = 0; I < MappingCount; ++I) {
        const RC95LDR_PAE_PHYSICAL_MAPPING *M = &Mappings[I];
        uint64_t PhysicalEnd, VirtualEnd;
        if (!M->Length ||
            (M->PhysicalBase & (PAE_PAGE_SIZE - 1u)) ||
            (M->VirtualBase & (PAE_PAGE_SIZE - 1u)) ||
            (M->Length & (PAE_PAGE_SIZE - 1u)) ||
            (M->Flags & ~(RC95LDR_PAE_MAPPING_WRITABLE |
                          RC95LDR_PAE_MAPPING_CACHEABLE)) ||
            M->Reserved ||
            !PaeAddU64(M->PhysicalBase, M->Length, &PhysicalEnd) ||
            !PaeAddU64(M->VirtualBase, M->Length, &VirtualEnd) ||
            M->PhysicalBase < RC95LDR_PAE_WINDOW_PHYSICAL_MIN ||
            PhysicalEnd > PhysicalLimit ||
            M->VirtualBase < Window->VirtualBase ||
            VirtualEnd > WindowEnd ||
            (M->PhysicalBase & ~AddressMask) ||
            ((PhysicalEnd - PAE_PAGE_SIZE) & ~AddressMask))
            return Rc95LdrInvalidParameter;
        for (J = 0; J < I; ++J) {
            uint64_t OtherPhysicalEnd, OtherVirtualEnd;
            const RC95LDR_PAE_PHYSICAL_MAPPING *N = &Mappings[J];
            if (!PaeAddU64(N->PhysicalBase, N->Length, &OtherPhysicalEnd) ||
                !PaeAddU64(N->VirtualBase, N->Length, &OtherVirtualEnd))
                return Rc95LdrInvalidParameter;
            if (M->VirtualBase < OtherVirtualEnd &&
                N->VirtualBase < VirtualEnd)
                return Rc95LdrInvalidImage;
            if (M->PhysicalBase < OtherPhysicalEnd &&
                N->PhysicalBase < PhysicalEnd)
                return Rc95LdrInvalidImage;
        }
    }

    for (I = 0; I < MappingCount; ++I) {
        const RC95LDR_PAE_PHYSICAL_MAPPING *M = &Mappings[I];
        uint64_t Offset;
        for (Offset = 0; Offset < M->Length; Offset += PAE_PAGE_SIZE) {
            uint64_t Va = M->VirtualBase + Offset;
            uint64_t Pa = M->PhysicalBase + Offset;
            unsigned PdptIndex = (unsigned)(Va >> 30);
            unsigned PdIndex = (unsigned)((Va >> 21) & 0x1ffu);
            unsigned PtIndex = (unsigned)((Va >> 12) & 0x1ffu);
            uint64_t PdptEntry = Pdpt[PdptIndex];
            uint64_t *Pd, *Pt;
            size_t PdOffset, PtOffset;
            uint64_t PdPhysical, PtPhysical;
            RC95LDR_STATUS Status;

            if (!(PdptEntry & PAE_ENTRY_PRESENT)) {
                Status = PaeAllocateTable(&Storage, PageTableStorageUsed,
                    &Pd, &PdPhysical);
                if (Status != Rc95LdrSuccess)
                    return Status;
                Pdpt[PdptIndex] = (PdPhysical & AddressMask) |
                    PAE_ENTRY_PRESENT;
            } else {
                PdPhysical = PdptEntry & PAE_ADDRESS_MASK;
                if ((PdPhysical & ~AddressMask) ||
                    !PaeStorageOffset(&Storage, PdPhysical, &PdOffset))
                    return Rc95LdrInvalidImage;
                Pd = (uint64_t *)((uint8_t *)Storage.WritableMapping + PdOffset);
            }

            if (!(Pd[PdIndex] & PAE_ENTRY_PRESENT)) {
                Status = PaeAllocateTable(&Storage, PageTableStorageUsed,
                    &Pt, &PtPhysical);
                if (Status != Rc95LdrSuccess)
                    return Status;
                Pd[PdIndex] = (PtPhysical & AddressMask) |
                    PAE_ENTRY_PRESENT | PAE_ENTRY_WRITABLE;
            } else {
                if (Pd[PdIndex] & PAE_ENTRY_LARGE)
                    return Rc95LdrInvalidImage;
                PtPhysical = Pd[PdIndex] & PAE_ADDRESS_MASK;
                if ((PtPhysical & ~AddressMask) ||
                    !PaeStorageOffset(&Storage, PtPhysical, &PtOffset))
                    return Rc95LdrInvalidImage;
                Pt = (uint64_t *)((uint8_t *)Storage.WritableMapping + PtOffset);
            }

            if (Pt[PtIndex] & PAE_ENTRY_PRESENT)
                return Rc95LdrInvalidImage;
            Pt[PtIndex] = (Pa & AddressMask) | PAE_ENTRY_PRESENT;
            if (M->Flags & RC95LDR_PAE_MAPPING_WRITABLE)
                Pt[PtIndex] |= PAE_ENTRY_WRITABLE;
            if (!(M->Flags & RC95LDR_PAE_MAPPING_CACHEABLE))
                Pt[PtIndex] |= PAE_ENTRY_PWT | PAE_ENTRY_PCD;
        }
    }

    return Rc95LdrSuccess;
}

RC95LDR_STATUS
Rc95LdrBuildI386PaeWindowMappings(
    const RC95LDR_RC95_CPU_INFO *Cpu,
    const RC95LDR_PAE_VIRTUAL_WINDOW *Window,
    const RC95LDR_LOADED_MODULE *Modules,
    size_t ModuleCount,
    const RC95LDR_NATIVE_ALLOCATION *Allocations,
    size_t AllocationCount,
    const RC95LDR_FIRMWARE_MEMORY_RANGE *Ranges,
    size_t RangeCount,
    RC95LDR_PAE_PHYSICAL_MAPPING *Mappings,
    size_t Capacity,
    size_t *MappingCount)
{
    uint64_t WindowEnd, Cursor, PhysicalLimit;
    size_t I, Count = 0;

    if (!Cpu || !Window || !Mappings || !MappingCount ||
        (ModuleCount && !Modules) || (AllocationCount && !Allocations) ||
        !Ranges || !RangeCount || !Window->VirtualBase || !Window->Length ||
        (Window->VirtualBase & 0xfffu) || (Window->Length & 0xfffu) ||
        Window->VirtualBase >= 0x100000000ULL ||
        !PaeAddU64(Window->VirtualBase, Window->Length, &WindowEnd) ||
        WindowEnd > 0x100000000ULL ||
        Cpu->PagingMode != RC95LDR_CPU_PAGING_I386_PAE ||
        !(Cpu->FeatureFlags & RC95LDR_CPU_FEATURE_PAE) ||
        !(Cpu->FeatureFlags & RC95LDR_CPU_FEATURE_CX8) ||
        Cpu->PhysicalAddressBits < 33u || Cpu->PhysicalAddressBits > 52u ||
        ModuleCount > RC95LDR_MODULE_MAX ||
        AllocationCount > RC95LDR_MEMORYMAP_MAX_RESERVATIONS ||
        RangeCount > RC95LDR_MEMORYMAP_MAX_RAW)
        return Rc95LdrInvalidParameter;

    for (I = 0; I < ModuleCount; ++I) {
        if (!Modules[I].ImageBase || !Modules[I].ImageSize ||
            PaeOverlap(Window->VirtualBase, Window->Length,
                       Modules[I].ImageBase, Modules[I].ImageSize))
            return Rc95LdrInvalidImage;
    }
    for (I = 0; I < AllocationCount; ++I) {
        if (!Allocations[I].PhysicalBase || !Allocations[I].Length ||
            PaeOverlap(Window->VirtualBase, Window->Length,
                       Allocations[I].PhysicalBase, Allocations[I].Length))
            return Rc95LdrInvalidImage;
    }

    PhysicalLimit = 1ULL << Cpu->PhysicalAddressBits;
    Cursor = Window->VirtualBase;
    for (I = 0; I < RangeCount; ++I) {
        uint64_t End, Start, AlignedStart, AlignedEnd, Length;
        const RC95LDR_FIRMWARE_MEMORY_RANGE *Range = &Ranges[I];
        if (Range->Kind != Rc95LdrFirmwareMemoryAvailable)
            continue;
        if (!Range->Length || !PaeAddU64(Range->Base, Range->Length, &End))
            return Rc95LdrCorruptData;
        if (End <= RC95LDR_PAE_WINDOW_PHYSICAL_MIN)
            continue;
        Start = Range->Base < RC95LDR_PAE_WINDOW_PHYSICAL_MIN ?
            RC95LDR_PAE_WINDOW_PHYSICAL_MIN : Range->Base;
        if (Start >= PhysicalLimit)
            continue;
        if (End > PhysicalLimit)
            End = PhysicalLimit;
        if (Start > UINT64_MAX - 0xfffu)
            return Rc95LdrIntegerOverflow;
        AlignedStart = (Start + 0xfffu) & ~0xfffULL;
        AlignedEnd = End & ~0xfffULL;
        if (AlignedEnd <= AlignedStart)
            continue;
        Length = AlignedEnd - AlignedStart;
        if (Length > WindowEnd - Cursor || Count == Capacity ||
            Count == RC95LDR_PAE_WINDOW_MAPPING_MAX)
            return Rc95LdrBufferTooSmall;
        Mappings[Count].PhysicalBase = AlignedStart;
        Mappings[Count].VirtualBase = Cursor;
        Mappings[Count].Length = Length;
        Mappings[Count].Flags = RC95LDR_PAE_MAPPING_WRITABLE |
            RC95LDR_PAE_MAPPING_CACHEABLE;
        Mappings[Count].Reserved = 0;
        ++Count;
        Cursor += Length;
    }
    *MappingCount = Count;
    return Rc95LdrSuccess;
}
