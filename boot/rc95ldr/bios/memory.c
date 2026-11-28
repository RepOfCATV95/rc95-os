#include "include/rc95ldrbiosmemory.h"
#include <string.h>

static int
AddU64(uint64_t A, uint64_t B, uint64_t *Result)
{
    if (B > UINT64_MAX - A) return 0;
    *Result = A + B;
    return 1;
}

static int
AlignUp(uint64_t Value, uint64_t Alignment, uint64_t *Result)
{
    uint64_t Mask = Alignment - 1u;
    if (Value > UINT64_MAX - Mask) return 0;
    *Result = (Value + Mask) & ~Mask;
    return 1;
}

static int
Overlaps(uint64_t A, uint64_t ALength, uint64_t B, uint64_t BLength)
{
    uint64_t AEnd, BEnd;
    if (!ALength || !BLength || !AddU64(A, ALength, &AEnd) ||
        !AddU64(B, BLength, &BEnd))
        return 1;
    return A < BEnd && B < AEnd;
}

RC95LDR_STATUS
Rc95LdrBiosInitializeMemoryAllocator(RC95LDR_BIOS_MEMORY_ALLOCATOR *Allocator,
                                     RC95LDR_BIOS_STARTUP *Startup)
{
    if (!Allocator || !Startup ||
        Startup->StructureSize < sizeof(*Startup) ||
        Startup->Version != RC95LDR_BIOS_STARTUP_VERSION ||
        !Startup->MemoryReservations || !Startup->FirmwareMemoryRanges ||
        !Startup->FirmwareMemoryCapacity)
        return Rc95LdrInvalidParameter;
    memset(Allocator, 0, sizeof(*Allocator));
    Allocator->StructureSize = sizeof(*Allocator);
    Allocator->Version = RC95LDR_BIOS_MEMORY_ALLOCATOR_VERSION;
    Allocator->Startup = Startup;
    return Rc95LdrSuccess;
}

RC95LDR_STATUS
Rc95LdrBiosAllocatePages(void *Context, uint64_t Size, uint64_t Alignment,
                         uint64_t *PhysicalAddress)
{
    RC95LDR_BIOS_MEMORY_ALLOCATOR *A = Context;
    uint64_t Rounded, Candidate, End;
    size_t i, j, Slot = RC95LDR_BIOS_MEMORY_ALLOCATIONS;

    if (!A || A->StructureSize < sizeof(*A) ||
        A->Version != RC95LDR_BIOS_MEMORY_ALLOCATOR_VERSION ||
        !A->Startup || !A->Startup->MemoryReservations ||
        !A->Startup->FirmwareMemoryRanges ||
        A->Startup->FirmwareMemoryCount > A->Startup->FirmwareMemoryCapacity ||
        !PhysicalAddress || !Size ||
        (Alignment & (Alignment - 1u)) ||
        Alignment > 0x40000000u ||
        Size > UINT64_MAX - (RC95LDR_BIOS_PAGE_SIZE - 1u))
        return Rc95LdrInvalidParameter;

    if (Alignment < RC95LDR_BIOS_PAGE_SIZE)
        Alignment = RC95LDR_BIOS_PAGE_SIZE;
    if (A->Startup->MemoryReservations->Count >
        RC95LDR_MEMORYMAP_MAX_RESERVATIONS)
        return Rc95LdrCorruptData;

    *PhysicalAddress = 0;
    Rounded = (Size + RC95LDR_BIOS_PAGE_SIZE - 1u) &
              ~(uint64_t)(RC95LDR_BIOS_PAGE_SIZE - 1u);
    for (i = 0; i < RC95LDR_BIOS_MEMORY_ALLOCATIONS; ++i)
        if (!A->Allocations[i].InUse) { Slot = i; break; }
    if (Slot == RC95LDR_BIOS_MEMORY_ALLOCATIONS)
        return Rc95LdrOutOfMemory;

    for (i = 0; i < A->Startup->FirmwareMemoryCount; ++i) {
        const RC95LDR_FIRMWARE_MEMORY_RANGE *R =
            &A->Startup->FirmwareMemoryRanges[i];
        uint64_t RangeEnd;
        if (R->Kind != Rc95LdrFirmwareMemoryAvailable ||
            !R->Length || !AddU64(R->Base, R->Length, &RangeEnd))
            continue;
        Candidate = R->Base < 0x100000u ? 0x100000u : R->Base;
        if (!AlignUp(Candidate, Alignment, &Candidate)) continue;

        for (;;) {
            int Conflict = 0;
            if (!AddU64(Candidate, Rounded, &End) ||
                End > RangeEnd || End > 0x100000000ull ||
                Candidate > UINTPTR_MAX)
                break;

            for (j = 0; j < A->Startup->MemoryReservations->Count; ++j) {
                const RC95LDR_MEMORY_RESERVATION *Reserved =
                    &A->Startup->MemoryReservations->Entries[j];
                if (Overlaps(Candidate, Rounded, Reserved->Base, Reserved->Length)) {
                    uint64_t Next;
                    if (!AddU64(Reserved->Base, Reserved->Length, &Next) ||
                        !AlignUp(Next, Alignment, &Candidate))
                        return Rc95LdrIntegerOverflow;
                    Conflict = 1;
                    break;
                }
            }
            if (Conflict) continue;

            for (j = 0; j < RC95LDR_BIOS_MEMORY_ALLOCATIONS; ++j) {
                const RC95LDR_BIOS_MEMORY_ALLOCATION *Live = &A->Allocations[j];
                if (Live->InUse &&
                    Overlaps(Candidate, Rounded, Live->Base, Live->Length)) {
                    uint64_t Next;
                    if (!AddU64(Live->Base, Live->Length, &Next) ||
                        !AlignUp(Next, Alignment, &Candidate))
                        return Rc95LdrIntegerOverflow;
                    Conflict = 1;
                    break;
                }
            }
            if (Conflict) continue;

            A->Allocations[Slot].Base = Candidate;
            A->Allocations[Slot].Length = Rounded;
            A->Allocations[Slot].InUse = 1;
            *PhysicalAddress = Candidate;
            return Rc95LdrSuccess;
        }
    }
    return Rc95LdrOutOfMemory;
}

void
Rc95LdrBiosFreePages(void *Context, uint64_t PhysicalAddress, uint64_t Size)
{
    RC95LDR_BIOS_MEMORY_ALLOCATOR *A = Context;
    size_t i;
    uint64_t Rounded;
    if (!A || A->StructureSize < sizeof(*A) ||
        A->Version != RC95LDR_BIOS_MEMORY_ALLOCATOR_VERSION ||
        !Size || Size > UINT64_MAX - (RC95LDR_BIOS_PAGE_SIZE - 1u))
        return;
    Rounded = (Size + RC95LDR_BIOS_PAGE_SIZE - 1u) &
              ~(uint64_t)(RC95LDR_BIOS_PAGE_SIZE - 1u);
    for (i = 0; i < RC95LDR_BIOS_MEMORY_ALLOCATIONS; ++i) {
        RC95LDR_BIOS_MEMORY_ALLOCATION *Live = &A->Allocations[i];
        if (Live->InUse && Live->Base == PhysicalAddress &&
            Live->Length == Rounded) {
            memset(Live, 0, sizeof(*Live));
            return;
        }
    }
}

RC95LDR_STATUS
Rc95LdrBiosValidateAllocationExtent(void *Context, uint64_t VirtualBase,
                                    uint64_t PhysicalBase, uint64_t ImageSize,
                                    uint64_t AllocationSize)
{
    RC95LDR_BIOS_MEMORY_ALLOCATOR *A = Context;
    size_t i;
    uint64_t Rounded;
    if (!A || A->StructureSize < sizeof(*A) ||
        A->Version != RC95LDR_BIOS_MEMORY_ALLOCATOR_VERSION ||
        !ImageSize || !AllocationSize || ImageSize > AllocationSize ||
        VirtualBase != PhysicalBase ||
        AllocationSize > UINT64_MAX - (RC95LDR_BIOS_PAGE_SIZE - 1u))
        return Rc95LdrInvalidParameter;
    Rounded = (AllocationSize + RC95LDR_BIOS_PAGE_SIZE - 1u) &
              ~(uint64_t)(RC95LDR_BIOS_PAGE_SIZE - 1u);
    for (i = 0; i < RC95LDR_BIOS_MEMORY_ALLOCATIONS; ++i) {
        const RC95LDR_BIOS_MEMORY_ALLOCATION *Live = &A->Allocations[i];
        if (Live->InUse && Live->Base == PhysicalBase &&
            Live->Length == Rounded)
            return Rc95LdrSuccess;
    }
    return Rc95LdrOutOfBounds;
}

RC95LDR_STATUS
Rc95LdrBiosGetMemoryMap(void *Context, RC95LDR_WINLOAD_MEMORY_RANGE *Ranges,
                       size_t Capacity, size_t *Count)
{
    RC95LDR_BIOS_MEMORY_ALLOCATOR *A = Context;
    size_t i, N;
    if (!A || A->Version != RC95LDR_BIOS_MEMORY_ALLOCATOR_VERSION ||
        !A->Startup || !A->Startup->FirmwareMemoryRanges ||
        A->Startup->FirmwareMemoryCount > A->Startup->FirmwareMemoryCapacity ||
        !Ranges || !Count)
        return Rc95LdrInvalidParameter;
    N = A->Startup->FirmwareMemoryCount;
    *Count = 0;
    if (N > Capacity) return Rc95LdrBufferTooSmall;
    for (i = 0; i < N; ++i) {
        Ranges[i].Base = A->Startup->FirmwareMemoryRanges[i].Base;
        Ranges[i].Length = A->Startup->FirmwareMemoryRanges[i].Length;
        Ranges[i].Type = A->Startup->FirmwareMemoryRanges[i].Kind;
        Ranges[i].Attributes = A->Startup->FirmwareMemoryRanges[i].Attributes;
    }
    *Count = N;
    return Rc95LdrSuccess;
}

RC95LDR_STATUS
Rc95LdrBiosInstallMemoryCallbacks(RC95LDR_BIOS_MEMORY_ALLOCATOR *Allocator,
                                  RC95LDR_WINLOAD_PLATFORM *Platform)
{
    if (!Allocator || !Platform ||
        Allocator->StructureSize < sizeof(*Allocator) ||
        Allocator->Version != RC95LDR_BIOS_MEMORY_ALLOCATOR_VERSION ||
        !Allocator->Startup)
        return Rc95LdrInvalidParameter;
    /* A provider may set Context to a wrapper whose first member is this
       allocator, allowing its file callbacks and these memory callbacks to
       share one context. Otherwise use the allocator directly. */
    if (!Platform->Context) Platform->Context = Allocator;
    Platform->AllocatePages = Rc95LdrBiosAllocatePages;
    Platform->FreePages = Rc95LdrBiosFreePages;
    Platform->ValidateAllocationExtent = Rc95LdrBiosValidateAllocationExtent;
    Platform->GetMemoryMap = Rc95LdrBiosGetMemoryMap;
    return Rc95LdrSuccess;
}
