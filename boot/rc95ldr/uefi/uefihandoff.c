#include "include/rc95ldruefihandoff.h"
#include "../winload/include/rc95ldrentry.h"
#include "include/rc95ldruefimemory.h"
#include "rc95ldramd64transfer.h"
#include <limits.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

RC95LDR_STATUS
Rc95LdrUefiInitializeAmd64HandoffContext(
    const RC95LDR_UEFI_IMAGE_CONTEXT *Image,
    RC95LDR_NATIVE_ALLOCATION_SET *AllocationSet,
    RC95LDR_UEFI_AMD64_HANDOFF_CONTEXT *Context)
{
#if !defined(_M_AMD64) && !defined(_M_X64) && !defined(_AMD64_)
    (void)Image; (void)AllocationSet; (void)Context;
    return Rc95LdrUnsupported;
#else
    EFI_PHYSICAL_ADDRESS Address = 0;
    UINTN Pages = (RC95LDR_UEFI_AMD64_MEMORY_MAP_CAPACITY +
                   EFI_PAGE_SIZE - 1u) / EFI_PAGE_SIZE;
    EFI_STATUS Status;
    RC95LDR_STATUS TrackStatus;

    if (!Image || Image->StructureSize < sizeof(*Image) ||
        Image->Version != RC95LDR_UEFI_ENTRY_VERSION ||
        !Image->ImageHandle || !Image->SystemTable ||
        !Image->BootServices ||
        Image->SystemTable->BootServices != Image->BootServices ||
        !Image->BootServices->AllocatePages ||
        !Image->BootServices->FreePages ||
        !AllocationSet || !Context)
        return Rc95LdrInvalidParameter;

    memset(Context, 0, sizeof(*Context));
    Status = Image->BootServices->AllocatePages(
        AllocateAnyPages, EfiLoaderData, Pages, &Address);
    if (EFI_ERROR(Status))
        return Status == EFI_OUT_OF_RESOURCES ?
            Rc95LdrOutOfMemory : Rc95LdrIoError;

    /* The target AMD64 page tables use 48-bit canonical virtual addresses;
     * the EFI physical allocation is identity-mapped by the provider. */
    if (!Address || (Address >> 47) != 0 ||
        Address > UINT64_MAX - (Pages * EFI_PAGE_SIZE)) {
        (void)Image->BootServices->FreePages(Address, Pages);
        return Rc95LdrOutOfBounds;
    }

    TrackStatus = Rc95LdrUefiTrackMemoryMapBuffer(
        AllocationSet, (uint64_t)Address,
        (size_t)(Pages * EFI_PAGE_SIZE));
    if (TrackStatus != Rc95LdrSuccess) {
        (void)Image->BootServices->FreePages(Address, Pages);
        return TrackStatus;
    }

    Context->StructureSize = sizeof(*Context);
    Context->Version = RC95LDR_UEFI_AMD64_HANDOFF_CONTEXT_VERSION;
    Context->Image = Image;
    Context->MemoryMapBuffer = (VOID *)(UINTN)Address;
    Context->MemoryMapCapacity = Pages * EFI_PAGE_SIZE;
    Context->MemoryMapPages = Pages;
    Context->AllocationSet = AllocationSet;
    return Rc95LdrSuccess;
#endif
}

RC95LDR_STATUS
Rc95LdrUefiAttachAmd64HandoffContext(
    const RC95LDR_UEFI_IMAGE_CONTEXT *Image,
    RC95LDR_NATIVE_ALLOCATION_SET *AllocationSet,
    RC95LDR_WINLOAD_STARTUP *Startup,
    RC95LDR_UEFI_AMD64_HANDOFF_CONTEXT *Context)
{
#if !defined(_M_AMD64) && !defined(_M_X64) && !defined(_AMD64_)
    (void)Image; (void)AllocationSet; (void)Startup; (void)Context;
    return Rc95LdrUnsupported;
#else
    RC95LDR_STATUS Status;
    if (!Startup ||
        Startup->StructureSize <
            offsetof(RC95LDR_WINLOAD_STARTUP, FirmwareHandoffContext) +
            sizeof(Startup->FirmwareHandoffContext) ||
        Startup->Version != RC95LDR_STARTUP_ABI_VERSION ||
        Startup->Firmware != Rc95LdrFirmwareUefi)
        return Rc95LdrInvalidParameter;
    Status = Rc95LdrUefiInitializeAmd64HandoffContext(
        Image, AllocationSet, Context);
    if (Status != Rc95LdrSuccess)
        return Status;
    Startup->FirmwareHandoffContext = Context;
    return Rc95LdrSuccess;
#endif
}

void
Rc95LdrUefiReleaseAmd64HandoffContext(
    RC95LDR_UEFI_AMD64_HANDOFF_CONTEXT *Context)
{
#if defined(_M_AMD64) || defined(_M_X64) || defined(_AMD64_)
    if (Context && Context->StructureSize >= sizeof(*Context) &&
        Context->Image && Context->Image->BootServices &&
        Context->Image->BootServices->FreePages &&
        Context->MemoryMapBuffer && Context->MemoryMapPages) {
        EFI_PHYSICAL_ADDRESS Address =
            (EFI_PHYSICAL_ADDRESS)(UINTN)Context->MemoryMapBuffer;
        EFI_STATUS Status = Context->Image->BootServices->FreePages(
            Address, Context->MemoryMapPages);
        if (EFI_ERROR(Status))
            return; /* Keep the context and ledger entry for a safe retry. */
        if (Context->AllocationSet) {
            size_t i;
            for (i = 0; i < Context->AllocationSet->Count; ++i) {
                RC95LDR_NATIVE_ALLOCATION *Entry =
                    &Context->AllocationSet->Entries[i];
                if (Entry->Kind == RC95LDR_ALLOC_UEFI_MAP &&
                    Entry->PhysicalBase == (uint64_t)Address &&
                    Entry->Length == Context->MemoryMapCapacity) {
                    size_t remaining = Context->AllocationSet->Count - i - 1u;
                    if (remaining)
                        memmove(Entry, Entry + 1,
                            remaining * sizeof(*Entry));
                    --Context->AllocationSet->Count;
                    break;
                }
            }
        }
    }
#endif
    if (Context)
        memset(Context, 0, sizeof(*Context));
}

RC95LDR_STATUS
Rc95LdrUefiSetAmd64HandoffQuiesce(
    RC95LDR_UEFI_AMD64_HANDOFF_CONTEXT *Context,
    RC95LDR_UEFI_HANDOFF_QUIESCE Quiesce,
    void *QuiesceContext)
{
    if (!Context || Context->StructureSize < sizeof(*Context) ||
        Context->Version != RC95LDR_UEFI_AMD64_HANDOFF_CONTEXT_VERSION ||
        (Quiesce && !QuiesceContext))
        return Rc95LdrInvalidParameter;
    Context->Quiesce = Quiesce;
    Context->QuiesceContext = QuiesceContext;
    return Rc95LdrSuccess;
}

RC95LDR_STATUS
Rc95LdrUefiBindAmd64Handoff(
    RC95LDR_WINLOAD_PLATFORM *Platform,
    RC95LDR_UEFI_AMD64_HANDOFF_CONTEXT *Context)
{
#if !defined(_M_AMD64) && !defined(_M_X64) && !defined(_AMD64_)
    (void)Platform; (void)Context;
    return Rc95LdrUnsupported;
#else
    if (!Platform ||
        Platform->StructureSize <
            offsetof(RC95LDR_WINLOAD_PLATFORM, FirmwareRuntimeServices) +
            sizeof(Platform->FirmwareRuntimeServices) ||
        Platform->Version != RC95LDR_WINLOAD_ABI_VERSION ||
        !Context || Context->StructureSize < sizeof(*Context) ||
        Context->Version != RC95LDR_UEFI_AMD64_HANDOFF_CONTEXT_VERSION ||
        !Context->Image || !Context->Image->SystemTable ||
        !Context->Image->SystemTable->RuntimeServices ||
        !Context->MemoryMapBuffer || !Context->MemoryMapCapacity ||
        !Context->MemoryMapPages || !Context->AllocationSet)
        return Rc95LdrInvalidParameter;

    Platform->ExitFirmwareAndValidatedHandoff =
        Rc95LdrUefiAmd64ValidatedHandoff;
    Platform->ExitFirmwareContext = Context;
    Platform->NativeAllocationSet = Context->AllocationSet;
    Platform->FirmwareSystemTable =
        (uint64_t)(uintptr_t)Context->Image->SystemTable;
    Platform->FirmwareRuntimeServices =
        (uint64_t)(uintptr_t)Context->Image->SystemTable->RuntimeServices;
    return Rc95LdrSuccess;
#endif
}

extern void Rc95LdrAmd64HaltForever(void);

static void
UefiHandoffFatal(void)
{
    Rc95LdrAmd64HaltForever();
}

EFI_STATUS
Rc95LdrUefiAmd64ExitBootServicesAndTransfer(
    const RC95LDR_UEFI_IMAGE_CONTEXT *Image,
    RC95LDR_KERNEL_HANDOFF_CONTEXT *Handoff,
    VOID *MemoryMapBuffer,
    UINTN MemoryMapCapacity)
{
    UINTN MapSize = 0, DescriptorSize = 0, MapKey = 0;
    UINT32 DescriptorVersion = 0;
    EFI_STATUS Status;
    uint64_t SystemTableAddress, RuntimeServicesAddress;

#if !defined(_M_AMD64) && !defined(_M_X64) && !defined(_AMD64_)
    (void)Image; (void)Handoff; (void)MemoryMapBuffer;
    (void)MemoryMapCapacity;
    return EFI_UNSUPPORTED;
#else
    if (!Image || Image->StructureSize < sizeof(*Image) ||
        Image->Version != RC95LDR_UEFI_ENTRY_VERSION ||
        !Image->ImageHandle || !Image->SystemTable ||
        !Image->SystemTable->RuntimeServices || !Handoff ||
        Handoff->Firmware != Rc95LdrFirmwareUefi ||
        Handoff->Architecture != Rc95LdrNtArchAmd64 ||
        !MemoryMapBuffer || !MemoryMapCapacity ||
        ((uintptr_t)MemoryMapBuffer & 7u))
        return EFI_INVALID_PARAMETER;

    SystemTableAddress = (uint64_t)(uintptr_t)Image->SystemTable;
    RuntimeServicesAddress =
        (uint64_t)(uintptr_t)Image->SystemTable->RuntimeServices;

    /* Reject an incomplete OS-specific loader block before the irreversible
       firmware transition. The final map is refreshed after ExitBootServices. */
    if (Rc95LdrValidateKernelHandoff(Handoff) != Rc95LdrSuccess)
        return EFI_INVALID_PARAMETER;

    Status = Rc95LdrUefiExitBootServices(Image->SystemTable,
        Image->ImageHandle, MemoryMapBuffer, MemoryMapCapacity,
        &MapSize, &DescriptorSize, &DescriptorVersion, &MapKey);
    if (EFI_ERROR(Status))
        return Status;

    if (MapSize > UINT32_MAX || DescriptorSize > UINT32_MAX ||
        !DescriptorSize || DescriptorVersion != EFI_MEMORY_DESCRIPTOR_VERSION)
        UefiHandoffFatal();

    Handoff->FirmwareSystemTable = SystemTableAddress;
    Handoff->FirmwareRuntimeServices = RuntimeServicesAddress;
    Handoff->FirmwareMemoryMap = (uint64_t)(uintptr_t)MemoryMapBuffer;
    Handoff->FirmwareMemoryMapSize = (UINT32)MapSize;
    Handoff->FirmwareMemoryMapDescriptorSize = (UINT32)DescriptorSize;

    if (Rc95LdrValidateKernelHandoff(Handoff) != Rc95LdrSuccess)
        UefiHandoffFatal();

    (void)MapKey;
    (void)Rc95LdrAmd64TransferKernel(Handoff);
    UefiHandoffFatal();
    return EFI_ABORTED;
#endif
}

RC95LDR_STATUS
Rc95LdrUefiAmd64ValidatedHandoff(
    void *Context, RC95LDR_KERNEL_HANDOFF_CONTEXT *Handoff)
{
    RC95LDR_UEFI_AMD64_HANDOFF_CONTEXT *Provider =
        (RC95LDR_UEFI_AMD64_HANDOFF_CONTEXT *)Context;
    EFI_STATUS Status;

    if (!Provider || Provider->StructureSize < sizeof(*Provider) ||
        Provider->Version != RC95LDR_UEFI_AMD64_HANDOFF_CONTEXT_VERSION ||
        !Provider->Image || !Provider->MemoryMapBuffer ||
        !Provider->MemoryMapCapacity || !Handoff)
        return Rc95LdrInvalidParameter;

    /* Close provider-owned files/protocols before entering the strict final
     * GetMemoryMap -> ExitBootServices sequence. A failed quiesce is still
     * recoverable because boot services have not yet been exited. */
    if (Provider->Quiesce) {
        Status = Provider->Quiesce(Provider->QuiesceContext);
        if (Status != Rc95LdrSuccess)
            return Status;
        Provider->Quiesce = NULL;
        Provider->QuiesceContext = NULL;
    }

    Status = Rc95LdrUefiAmd64ExitBootServicesAndTransfer(
        Provider->Image, Handoff,
        Provider->MemoryMapBuffer, Provider->MemoryMapCapacity);
    if (Status == EFI_OUT_OF_RESOURCES) return Rc95LdrOutOfMemory;
    if (Status == EFI_INVALID_PARAMETER) return Rc95LdrInvalidParameter;
    if (Status == EFI_UNSUPPORTED) return Rc95LdrUnsupported;
    if (Status == EFI_BUFFER_TOO_SMALL) return Rc95LdrBufferTooSmall;
    return Rc95LdrIoError;
}
