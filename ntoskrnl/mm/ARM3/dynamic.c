/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/mm/ARM3/dynamic.c
 * PURPOSE:         ARM Memory Manager Dynamic Physical Memory Support
 * PROGRAMMERS:     ReactOS Portable Systems Group
 */

/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#define MODULE_INVOLVED_IN_ARM3
#include <mm/ARM3/miarm.h>

/* FUNCTIONS *****************************************************************/

/*
 * @unimplemented
 */
NTSTATUS
NTAPI
MmAddPhysicalMemory (IN PPHYSICAL_ADDRESS StartAddress,
                     IN OUT PLARGE_INTEGER NumberOfBytes)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

/*
 * @unimplemented
 */
NTSTATUS
NTAPI
MmMarkPhysicalMemoryAsBad(IN PPHYSICAL_ADDRESS StartAddress,
                          IN OUT PLARGE_INTEGER NumberOfBytes)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

/*
 * @unimplemented
 */
NTSTATUS
NTAPI
MmMarkPhysicalMemoryAsGood(IN PPHYSICAL_ADDRESS StartAddress,
                           IN OUT PLARGE_INTEGER NumberOfBytes)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

/*
 * @unimplemented
 */
NTSTATUS
NTAPI
MmRemovePhysicalMemory(IN PPHYSICAL_ADDRESS StartAddress,
                       IN OUT PLARGE_INTEGER NumberOfBytes)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

/*
 * @implemented
 */
PPHYSICAL_MEMORY_RANGE
NTAPI
MmGetPhysicalMemoryRanges(VOID)
{
    ULONG Size, i;
    PPHYSICAL_MEMORY_RANGE Entry, Buffer;
    KIRQL OldIrql;
    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

    //
    // Calculate how much memory we'll need
    //
    Size = sizeof(PHYSICAL_MEMORY_RANGE) * (MmPhysicalMemoryBlock->NumberOfRuns + 1);

    //
    // Allocate a copy
    //
    Entry = Buffer = ExAllocatePoolWithTag(NonPagedPool, Size, 'hPmM');
    if (!Buffer) return NULL;

    //
    // Lock the PFN database
    //
    OldIrql = MiAcquirePfnLock();

    //
    // Make sure it hasn't changed before we had acquired the lock
    //
    ASSERT(Size == (sizeof(PHYSICAL_MEMORY_RANGE) *
                    (MmPhysicalMemoryBlock->NumberOfRuns + 1)));

    //
    // Now loop our block
    //
    for (i = 0; i < MmPhysicalMemoryBlock->NumberOfRuns; i++)
    {
        //
        // Copy the data, but format it into bytes
        //
        Entry->BaseAddress.QuadPart = MmPhysicalMemoryBlock->Run[i].BasePage << PAGE_SHIFT;
        Entry->NumberOfBytes.QuadPart = MmPhysicalMemoryBlock->Run[i].PageCount << PAGE_SHIFT;
        Entry++;
    }

    //
    // Last entry is empty
    //
    Entry->BaseAddress.QuadPart = 0;
    Entry->NumberOfBytes.QuadPart = 0;

    //
    // Release the lock and return
    //
    MiReleasePfnLock(OldIrql);
    return Buffer;
}

/*
 * Stream caller-selected physical memory ranges to a synchronous consumer.
 * No allocation or physical mapping is performed by this routine.
 */
NTSTATUS
NTAPI
MiSerializePhysicalMemory(
    _In_reads_(RangeCount) PPHYSICAL_MEMORY_RANGE Ranges,
    _In_ ULONG RangeCount,
    _In_ ULONG ChunkSize,
    _In_ PMI_MEMORY_SERIALIZATION_MAP_ROUTINE MapRoutine,
    _In_ PMI_MEMORY_SERIALIZATION_UNMAP_ROUTINE UnmapRoutine,
    _In_ PMI_MEMORY_SERIALIZATION_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ PULONGLONG PagesSerialized,
    _Out_ PULONGLONG BytesSerialized
)
{
    ULONGLONG PageCount = 0;
    ULONGLONG ByteCount = 0;
    ULONGLONG TotalPages = 0;
    ULONGLONG TotalBytes = 0;
    ULONG RangeIndex;

    if (Ranges == NULL || MapRoutine == NULL || UnmapRoutine == NULL ||
        Callback == NULL || PagesSerialized == NULL || BytesSerialized == NULL ||
        ChunkSize == 0 || (ChunkSize & (PAGE_SIZE - 1)) != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    *PagesSerialized = 0;
    *BytesSerialized = 0;

    for (RangeIndex = 0; RangeIndex < RangeCount; RangeIndex++)
    {
        ULONGLONG PhysicalAddress;
        ULONGLONG RangeBytes;

        if (Ranges[RangeIndex].BaseAddress.QuadPart < 0 ||
            Ranges[RangeIndex].NumberOfBytes.QuadPart <= 0)
        {
            return STATUS_INVALID_PARAMETER;
        }

        PhysicalAddress = (ULONGLONG)Ranges[RangeIndex].BaseAddress.QuadPart;
        RangeBytes = (ULONGLONG)Ranges[RangeIndex].NumberOfBytes.QuadPart;

        if ((PhysicalAddress & (PAGE_SIZE - 1)) != 0 ||
            (RangeBytes & (PAGE_SIZE - 1)) != 0 ||
            RangeBytes > (ULONGLONG)~0ULL - PhysicalAddress)
        {
            return STATUS_INVALID_PARAMETER;
        }

        if (TotalPages > (ULONGLONG)~0ULL - (RangeBytes >> PAGE_SHIFT) ||
            TotalBytes > (ULONGLONG)~0ULL - RangeBytes)
        {
            return STATUS_INTEGER_OVERFLOW;
        }

        TotalPages += RangeBytes >> PAGE_SHIFT;
        TotalBytes += RangeBytes;
    }

    for (RangeIndex = 0; RangeIndex < RangeCount; RangeIndex++)
    {
        ULONGLONG PhysicalAddress;
        ULONGLONG RemainingBytes;

        PhysicalAddress = (ULONGLONG)Ranges[RangeIndex].BaseAddress.QuadPart;
        RemainingBytes = (ULONGLONG)Ranges[RangeIndex].NumberOfBytes.QuadPart;

        while (RemainingBytes != 0)
        {
            PHYSICAL_ADDRESS ChunkAddress;
            ULONG CurrentChunkSize;
            PVOID Mapping = NULL;
            NTSTATUS Status;

            CurrentChunkSize = RemainingBytes > ChunkSize ?
                               ChunkSize :
                               (ULONG)RemainingBytes;
            ChunkAddress.QuadPart = PhysicalAddress;
            Status = MapRoutine(Context,
                                ChunkAddress,
                                CurrentChunkSize,
                                &Mapping);
            if (!NT_SUCCESS(Status))
            {
                if (Mapping != NULL)
                    UnmapRoutine(Context, Mapping, CurrentChunkSize);
                return Status;
            }
            if (Mapping == NULL)
                return STATUS_UNSUCCESSFUL;

            Status = Callback(Context,
                              ChunkAddress,
                              Mapping,
                              CurrentChunkSize);
            UnmapRoutine(Context, Mapping, CurrentChunkSize);
            if (!NT_SUCCESS(Status))
                return Status;

            PageCount += CurrentChunkSize >> PAGE_SHIFT;
            ByteCount += CurrentChunkSize;
            *PagesSerialized = PageCount;
            *BytesSerialized = ByteCount;
            PhysicalAddress += CurrentChunkSize;
            RemainingBytes -= CurrentChunkSize;
        }
    }

    return STATUS_SUCCESS;
}
