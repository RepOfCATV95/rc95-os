#include "rc95ldruefimemory.h"
#include <stddef.h>
RC95LDR_STATUS
Rc95LdrUefiTrackMemoryMapBuffer(RC95LDR_NATIVE_ALLOCATION_SET *allocations,
    uint64_t physicalBase, size_t bufferSize)
{
    if (!bufferSize) return Rc95LdrInvalidParameter;
    return Rc95LdrTrackNativeAllocation(allocations, RC95LDR_ALLOC_UEFI_MAP,
        physicalBase, (uint64_t)bufferSize);
}


#define RC95LDR_UEFI_EXIT_RETRIES 8u

static int
HeaderContains(UINT32 HeaderSize, size_t Offset, size_t Size)
{
    return Offset <= HeaderSize && Size <= (size_t)HeaderSize - Offset;
}

EFI_STATUS
Rc95LdrUefiExitBootServices(
    const EFI_SYSTEM_TABLE *SystemTable,
    EFI_HANDLE ImageHandle,
    VOID *MemoryMapBuffer,
    UINTN MemoryMapCapacity,
    UINTN *MemoryMapSize,
    UINTN *DescriptorSize,
    UINT32 *DescriptorVersion,
    UINTN *MapKey)
{
    EFI_BOOT_SERVICES *BootServices;
    UINTN Attempt;

    if (MemoryMapSize) *MemoryMapSize = 0;
    if (DescriptorSize) *DescriptorSize = 0;
    if (DescriptorVersion) *DescriptorVersion = 0;
    if (MapKey) *MapKey = 0;

    if (!SystemTable || !ImageHandle || !MemoryMapBuffer ||
        !MemoryMapCapacity || !MemoryMapSize || !DescriptorSize ||
        !DescriptorVersion || !MapKey ||
        ((uintptr_t)MemoryMapBuffer & (sizeof(UINT64) - 1u)))
        return EFI_INVALID_PARAMETER;

    if (SystemTable->Hdr.Signature != EFI_SYSTEM_TABLE_SIGNATURE ||
        !HeaderContains(SystemTable->Hdr.HeaderSize,
            offsetof(EFI_SYSTEM_TABLE, BootServices),
            sizeof(SystemTable->BootServices)) ||
        !SystemTable->BootServices)
        return EFI_INVALID_PARAMETER;

    BootServices = SystemTable->BootServices;
    if (BootServices->Hdr.Signature != EFI_BOOT_SERVICES_SIGNATURE ||
        !HeaderContains(BootServices->Hdr.HeaderSize,
            offsetof(EFI_BOOT_SERVICES, GetMemoryMap),
            sizeof(BootServices->GetMemoryMap)) ||
        !HeaderContains(BootServices->Hdr.HeaderSize,
            offsetof(EFI_BOOT_SERVICES, ExitBootServices),
            sizeof(BootServices->ExitBootServices)) ||
        !BootServices->GetMemoryMap || !BootServices->ExitBootServices)
        return EFI_UNSUPPORTED;

    /*
     * ExitBootServices invalidates the key when firmware changes its map.
     * The only legal operation between a failed exit and the retry is another
     * GetMemoryMap. In particular, do not allocate, print, close protocols,
     * or invoke any other boot service in this loop.
     */
    for (Attempt = 0; Attempt < RC95LDR_UEFI_EXIT_RETRIES; ++Attempt)
    {
        UINTN CurrentSize = MemoryMapCapacity;
        UINTN CurrentKey = 0;
        UINTN CurrentDescriptorSize = 0;
        UINT32 CurrentDescriptorVersion = 0;
        EFI_STATUS Status;

        Status = BootServices->GetMemoryMap(&CurrentSize,
            (EFI_MEMORY_DESCRIPTOR *)MemoryMapBuffer, &CurrentKey,
            &CurrentDescriptorSize, &CurrentDescriptorVersion);
        if (Status == EFI_BUFFER_TOO_SMALL)
        {
            /* Report the required capacity so the caller can resize before
               retrying; this path has not attempted the irreversible exit. */
            *MemoryMapSize = CurrentSize;
            return Status;
        }
        if (EFI_ERROR(Status))
            return Status;

        if (CurrentSize > MemoryMapCapacity ||
            !CurrentDescriptorSize ||
            CurrentDescriptorSize < sizeof(EFI_MEMORY_DESCRIPTOR) ||
            CurrentSize % CurrentDescriptorSize != 0)
            return EFI_COMPROMISED_DATA;
        if (CurrentDescriptorVersion != EFI_MEMORY_DESCRIPTOR_VERSION)
            return EFI_UNSUPPORTED;

        Status = BootServices->ExitBootServices(ImageHandle, CurrentKey);
        if (!EFI_ERROR(Status))
        {
            *MemoryMapSize = CurrentSize;
            *DescriptorSize = CurrentDescriptorSize;
            *DescriptorVersion = CurrentDescriptorVersion;
            *MapKey = CurrentKey;
            return EFI_SUCCESS;
        }

        if (Status != EFI_INVALID_PARAMETER)
            return Status;
    }

    return EFI_ABORTED;
}
