#include "include/rc95ldrrc95nativeprovider.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define RC95_PROVIDER_STACK_BYTES       (64u * 1024u)
#define RC95_PROVIDER_PAGETABLE_BYTES   (9u * 1024u * 1024u)
#define RC95_PROVIDER_HEAP_BYTES        (64u * 1024u)
#define RC95_PROVIDER_METADATA_BYTES    (256u * 1024u)
#define RC95_PROVIDER_LOADERBLOCK_BYTES (256u * 1024u)

static uint64_t
ProviderPages(size_t Bytes)
{
    return ((uint64_t)Bytes + RC95LDR_PHYSMEM_PAGE_SIZE - 1u) /
           RC95LDR_PHYSMEM_PAGE_SIZE;
}

static RC95LDR_STATUS
ProviderAllocate(
    RC95LDR_RC95_NATIVE_PROVIDER_RESULT *Result,
    size_t Bytes,
    uint32_t Flags,
    RC95LDR_PHYSMEM_ALLOCATION **Allocation)
{
    RC95LDR_STATUS Status;
    RC95LDR_PHYSMEM_ALLOCATION *Entry;

    if (!Result || !Allocation || !Bytes ||
        Result->AllocationCount >= RC95LDR_RC95_PROVIDER_ALLOCATION_MAX)
        return Rc95LdrBufferTooSmall;

    Entry = &Result->Allocations[Result->AllocationCount];
    Status = Rc95LdrPhysMemAllocate(&Result->PhysicalMemory,
        ProviderPages(Bytes), 1u,
        RC95LDR_PHYSMEM_ALLOC_CONTIGUOUS |
            RC95LDR_PHYSMEM_ALLOC_ZERO | Flags,
        RC95LDR_PHYSMEM_MAP_WRITABLE | RC95LDR_PHYSMEM_MAP_CACHED,
        Entry);
    if (Status != Rc95LdrSuccess)
        return Status;
    /* RC95's initial page tables and loader ABI use identity-mapped physical
       addresses. Do not publish an allocation whose writable alias would
       disappear or differ at the kernel transition. */
    if ((uint64_t)(uintptr_t)Entry->WritableMapping != Entry->PhysicalBase) {
        Rc95LdrPhysMemRelease(&Result->PhysicalMemory, Entry);
        return Rc95LdrUnsupported;
    }

    ++Result->AllocationCount;
    *Allocation = Entry;
    return Rc95LdrSuccess;
}

static RC95LDR_STATUS
ProviderTrack(
    RC95LDR_RC95_NATIVE_PROVIDER_RESULT *Result,
    uint32_t Kind,
    uint64_t PhysicalBase,
    uint64_t Length)
{
    RC95LDR_STATUS Status;
    RC95LDR_NATIVE_ALLOCATION_SET Set;
    if (!Result || !PhysicalBase || !Length ||
        Result->NativeAllocationCount >= RC95LDR_MEMORYMAP_MAX_RESERVATIONS)
        return Rc95LdrBufferTooSmall;

    memset(&Set, 0, sizeof(Set));
    Set.Count = Result->NativeAllocationCount;
    if (Set.Count)
        memcpy(Set.Entries, Result->NativeAllocations,
            Set.Count * sizeof(Set.Entries[0]));
    Status = Rc95LdrTrackNativeAllocation(&Set, Kind, PhysicalBase, Length);
    if (Status != Rc95LdrSuccess)
        return Status;
    memcpy(Result->NativeAllocations, Set.Entries,
        Set.Count * sizeof(Set.Entries[0]));
    Result->NativeAllocationCount = Set.Count;
    return Rc95LdrSuccess;
}


static RC95LDR_STATUS
ProviderTrackAllocation(
    RC95LDR_RC95_NATIVE_PROVIDER_RESULT *Result,
    uint32_t Kind,
    const RC95LDR_PHYSMEM_ALLOCATION *Allocation)
{
    uint64_t Length;
    if (!Allocation || Allocation->PageCount >
        UINT64_MAX / RC95LDR_PHYSMEM_PAGE_SIZE)
        return Rc95LdrInvalidParameter;
    Length = Allocation->PageCount * RC95LDR_PHYSMEM_PAGE_SIZE;
    return ProviderTrack(Result, Kind, Allocation->PhysicalBase, Length);
}

static void *
ProviderHeapTake(uint8_t *Heap, size_t Capacity, size_t *Offset,
                 size_t Bytes, size_t Alignment)
{
    size_t Aligned;
    if (!Heap || !Offset || !Alignment || (Alignment & (Alignment - 1u)) ||
        *Offset > SIZE_MAX - (Alignment - 1u))
        return NULL;
    Aligned = (*Offset + Alignment - 1u) & ~(Alignment - 1u);
    if (Aligned > Capacity || Bytes > Capacity - Aligned)
        return NULL;
    *Offset = Aligned + Bytes;
    return Heap + Aligned;
}

static const RC95LDR_LOADED_MODULE *
ProviderFindModule(const RC95LDR_NATIVE_LOADER_INPUT *Input, uint32_t Flag)
{
    uint32_t Index;
    if (!Input || !Input->Modules)
        return NULL;
    for (Index = 0; Index < Input->ModuleCount; ++Index)
        if (Input->Modules[Index].Flags & Flag)
            return &Input->Modules[Index];
    return NULL;
}

void
Rc95LdrReleaseRc95NativeInput(
    RC95LDR_RC95_NATIVE_PROVIDER_RESULT *Result)
{
    uint32_t Index;
    if (!Result)
        return;
    if (Result->StructureSize != sizeof(*Result) ||
        Result->Version != RC95LDR_RC95_PROVIDER_VERSION ||
        Result->AllocationCount > RC95LDR_RC95_PROVIDER_ALLOCATION_MAX) {
        memset(Result, 0, sizeof(*Result));
        return;
    }

    Rc95LdrRc95NativeReleaseBootDrivers(Result);
    if (Result->SharedAllocationSetMerged &&
        Result->SharedNativeAllocationSet)
        *Result->SharedNativeAllocationSet =
            Result->SharedNativeAllocationSetBefore;
    for (Index = Result->AllocationCount; Index > 0; --Index)
        Rc95LdrPhysMemRelease(&Result->PhysicalMemory,
            &Result->Allocations[Index - 1u]);
    memset(Result, 0, sizeof(*Result));
}

RC95LDR_STATUS
Rc95LdrPrepareRc95NativeInput(
    const RC95LDR_RC95_NATIVE_PROVIDER_INPUT *Input,
    RC95LDR_RC95_NATIVE_PROVIDER_RESULT *Result)
{
    RC95LDR_STATUS Status;
    RC95LDR_PHYSMEM_ALLOCATION *StackAllocation = NULL;
    RC95LDR_PHYSMEM_ALLOCATION *PageTableAllocation = NULL;
    RC95LDR_PHYSMEM_ALLOCATION *HeapAllocation = NULL;
    RC95LDR_PHYSMEM_ALLOCATION *MetadataAllocation = NULL;
    RC95LDR_PHYSMEM_ALLOCATION *LoaderBlockAllocation = NULL;
    RC95LDR_PHYSMEM_ALLOCATION *BiosStateAllocation = NULL;
    RC95LDR_RC95_CPU_INFO Cpu;
    RC95LDR_RC95_CPU_STATE *CpuState;
    RC95LDR_RC95_ARCH_STATE *ArchState;
    RC95LDR_RC95_PERCPU_STATE *PerCpu;
    RC95LDR_RC95_SCHEDULER_STATE *Scheduler;
    RC95LDR_ARCH_REGISTER_FRAME *RegisterFrame;
    RC95LDR_NATIVE_LOADER_INPUT BaseInput;
    RC95LDR_NATIVE_PREPARE_REQUEST *Request;
    const RC95LDR_LOADED_MODULE *Kernel, *Hal;
    uint8_t *Heap;
    size_t HeapOffset = 0;
    uint64_t PageTableRoot = 0;
    size_t PaeMappingCount = 0;
    size_t PageTableStorageUsed = 0;
    uint32_t AllocationFlags = 0;

    if (!Input || !Result ||
        Input->StructureSize < sizeof(*Input) ||
        Input->Version != RC95LDR_RC95_PROVIDER_VERSION ||
        !Input->Profile || Input->Profile->OsFamily != Rc95LdrOsRc95 ||
        !Input->BaseInput ||
        Input->BaseInput->StructureSize < sizeof(*Input->BaseInput) ||
        Input->BaseInput->Version != RC95LDR_NATIVE_LOADER_INPUT_VERSION ||
        !Input->SystemHive || !Input->SystemHiveSize ||
        !Input->SystemHivePhysicalBase || !Input->SystemHiveAllocationSize ||
        Input->SystemHiveSize > Input->SystemHiveAllocationSize ||
        (uint64_t)(uintptr_t)Input->SystemHive != Input->SystemHivePhysicalBase ||
        (Input->SystemHivePhysicalBase & 0xfffu) ||
        (Input->SystemHiveAllocationSize & 0xfffu) ||
        (!Input->LoadBootDriver &&
         !(Input->BootFileProvider && Input->SelectedInstallation &&
           Input->WinloadPlatform)) ||
        !Input->ValidateModuleAllocation ||
        !Input->PhysicalMemory ||
        !Input->FirmwareRanges ||
        !Input->FirmwareRangeCount ||
        Input->FirmwareRangeCount > RC95LDR_MEMORYMAP_MAX_RAW ||
        !Input->Metadata.Nls.AnsiData || !Input->Metadata.Nls.AnsiSize ||
        !Input->Metadata.Nls.OemData || !Input->Metadata.Nls.OemSize ||
        !Input->Metadata.Nls.UnicodeData || !Input->Metadata.Nls.UnicodeSize ||
        !Input->Metadata.DiskCount || !Input->Metadata.Disks ||
        Input->Metadata.DiskCount > RC95LDR_ARC_DISK_MAX ||
        !Input->BootDevice || !Input->BootDevice[0] ||
        !Input->HalDevice || !Input->HalDevice[0] ||
        !Input->SystemRoot || !Input->SystemRoot[0] ||
        !Input->PlatformInventory.MapPhysical ||
        !Input->PlatformInventory.ReadPciConfig)
        return Rc95LdrInvalidParameter;
    if ((Input->BootFileProvider || Input->SelectedInstallation ||
         Input->WinloadPlatform) &&
        (!Input->BootFileProvider || !Input->SelectedInstallation ||
         !Input->WinloadPlatform ||
         Input->Profile->Firmware != Rc95LdrFirmwareBios ||
         Input->BootFileProvider->StructureSize <
             sizeof(*Input->BootFileProvider) ||
         !Input->BootFileProvider->VolumeContext ||
         !Input->BootFileProvider->Open ||
         !Input->BootFileProvider->GetFileInformation ||
         !Input->BootFileProvider->Read ||
         !Input->BootFileProvider->Seek ||
         !Input->BootFileProvider->Close ||
         Input->WinloadPlatform->Version != RC95LDR_WINLOAD_ABI_VERSION ||
         Input->WinloadPlatform->StructureSize <
             offsetof(RC95LDR_WINLOAD_PLATFORM, ValidateAllocationExtent) +
             sizeof(Input->WinloadPlatform->ValidateAllocationExtent) ||
         !Input->WinloadPlatform->AllocatePages ||
         !Input->WinloadPlatform->FreePages ||
         !Input->WinloadPlatform->ValidateAllocationExtent))
        return Rc95LdrInvalidParameter;

    memset(Result, 0, sizeof(*Result));
    Result->StructureSize = sizeof(*Result);
    Result->Version = RC95LDR_RC95_PROVIDER_VERSION;
    Result->PhysicalMemory = *Input->PhysicalMemory;
    Result->BootFileProvider = Input->BootFileProvider;
    Result->InputInstallation = Input->SelectedInstallation;
    Result->WinloadPlatform = Input->WinloadPlatform;
    BaseInput = *Input->BaseInput;
    if (Input->SystemHiveSize > UINT32_MAX ||
        Input->Profile->StructureSize < sizeof(*Input->Profile) ||
        Input->Profile->Version != RC95LDR_OS_PROFILE_VERSION ||
        BaseInput.ModuleCount > RC95LDR_MODULE_MAX || !BaseInput.Modules ||
        !Input->KernelVirtualBase || !Input->KernelPhysicalBase ||
        !Input->KernelImageSize || !Input->KernelAllocationSize ||
        Input->KernelAllocationSize < Input->KernelImageSize ||
        !Input->KernelEntry || Input->KernelImageSize > UINT32_MAX ||
        !Input->KernelPeSections ||
        !Input->KernelPeSectionCount ||
        Input->KernelPeSectionCount > RC95LDR_PE_MAX_SECTIONS ||
        !Input->KernelPeSizeOfHeaders ||
        (Input->KernelVirtualBase & 0xfffu) ||
        (Input->KernelPhysicalBase & 0xfffu) ||
        (Input->KernelAllocationSize & 0xfffu) ||
        Input->KernelVirtualBase > UINT64_MAX - Input->KernelImageSize ||
        Input->KernelEntry < Input->KernelVirtualBase ||
        Input->KernelEntry >= Input->KernelVirtualBase + Input->KernelImageSize ||
        Input->KernelPhysicalBase > UINT64_MAX - Input->KernelAllocationSize)
        return Rc95LdrInvalidParameter;
    Result->Profile = *Input->Profile;
    if (Input->Profile->Firmware == Rc95LdrFirmwareUefi &&
        !Input->SharedNativeAllocationSet)
        return Rc95LdrInvalidParameter;
    memcpy(Result->Modules, BaseInput.Modules,
        BaseInput.ModuleCount * sizeof(Result->Modules[0]));
    BaseInput.Modules = Result->Modules;

    if (BaseInput.Architecture != Rc95LdrNtArchAmd64 &&
        BaseInput.Architecture != Rc95LdrNtArchI386)
        return Rc95LdrUnsupported;
    if ((BaseInput.Architecture == Rc95LdrNtArchAmd64 && sizeof(void *) != 8u) ||
        (BaseInput.Architecture == Rc95LdrNtArchI386 && sizeof(void *) != 4u))
        return Rc95LdrUnsupported;

    {
        uint32_t KernelCount = 0, HalCount = 0, ModuleIndex;
        for (ModuleIndex = 0; ModuleIndex < BaseInput.ModuleCount; ++ModuleIndex) {
            RC95LDR_LOADED_MODULE *M = &Result->Modules[ModuleIndex];
            if (M->Flags & RC95LDR_MODULE_KERNEL) {
                ++KernelCount;
                M->ImageBase = Input->KernelVirtualBase;
                M->PhysicalBase = Input->KernelPhysicalBase;
                M->ImageSize = Input->KernelImageSize;
                M->PhysicalSize = Input->KernelAllocationSize;
                M->EntryPoint = Input->KernelEntry;
                M->PeSections = Input->KernelPeSections;
                M->PeSectionCount = Input->KernelPeSectionCount;
                M->PeSizeOfHeaders = Input->KernelPeSizeOfHeaders;
            }
            if (M->Flags & RC95LDR_MODULE_HAL)
                ++HalCount;
        }
        if (KernelCount != 1u || HalCount != 1u)
            return Rc95LdrInvalidImage;
        for (ModuleIndex = 0; ModuleIndex < BaseInput.ModuleCount; ++ModuleIndex) {
            const RC95LDR_LOADED_MODULE *M = &Result->Modules[ModuleIndex];
            if (!M->ImageBase || !M->ImageSize || M->ImageSize > UINT32_MAX ||
                !M->PeSections || !M->PeSectionCount ||
                M->PeSectionCount > RC95LDR_PE_MAX_SECTIONS ||
                !M->PeSizeOfHeaders || M->PeSizeOfHeaders > M->ImageSize ||
                !M->PhysicalBase || !M->PhysicalSize ||
                (M->ImageBase & 0xfffu) || (M->PhysicalBase & 0xfffu) ||
                (M->PhysicalSize & 0xfffu) ||
                M->ImageBase > UINT64_MAX - M->ImageSize ||
                M->PhysicalBase > UINT64_MAX - M->PhysicalSize ||
                (M->EntryPoint &&
                 (M->EntryPoint < M->ImageBase ||
                  M->EntryPoint >= M->ImageBase + M->ImageSize)) ||
                Input->ValidateModuleAllocation(
                    Input->AllocationValidationContext, M->ImageBase,
                    M->PhysicalBase, M->ImageSize, M->PhysicalSize) !=
                    Rc95LdrSuccess)
                return Rc95LdrInvalidImage;
        }
    }
    Kernel = ProviderFindModule(&BaseInput, RC95LDR_MODULE_KERNEL);
    Hal = ProviderFindModule(&BaseInput, RC95LDR_MODULE_HAL);
    if (!Kernel || !Hal || !Kernel->ImageBase || !Kernel->EntryPoint ||
        !Kernel->ImageSize || !Kernel->PhysicalBase || !Kernel->PhysicalSize ||
        !Hal->ImageBase || !Hal->ImageSize || !Hal->PhysicalBase ||
        !Hal->PhysicalSize)
        return Rc95LdrInvalidImage;
    if (Input->ValidateModuleAllocation(Input->AllocationValidationContext,
            Kernel->ImageBase, Kernel->PhysicalBase, Kernel->ImageSize,
            Kernel->PhysicalSize) != Rc95LdrSuccess)
        return Rc95LdrInvalidImage;

    if (Input->Profile->Firmware == Rc95LdrFirmwareUefi) {
        if (!Input->FirmwareSystemTable || !Input->FirmwareRuntimeServices ||
            !Input->FirmwareMapAddress || !Input->FirmwareMapSize ||
            !Input->FirmwareMapDescriptorSize ||
            Input->FirmwareMapSize % Input->FirmwareMapDescriptorSize ||
            !Input->UefiMapPhysicalBase || !Input->UefiMapAllocationSize ||
            Input->FirmwareMapAddress != Input->UefiMapPhysicalBase ||
            Input->FirmwareMapSize > Input->UefiMapAllocationSize ||
            (Input->UefiMapPhysicalBase & 0xfffu) ||
            (Input->UefiMapAllocationSize & 0xfffu))
            return Rc95LdrInvalidParameter;
        {
            size_t MapIndex;
            int FoundMap = 0;
            if (Input->SharedNativeAllocationSet->Count >
                RC95LDR_MEMORYMAP_MAX_RESERVATIONS)
                return Rc95LdrInvalidParameter;
            for (MapIndex = 0;
                 MapIndex < Input->SharedNativeAllocationSet->Count;
                 ++MapIndex) {
                const RC95LDR_NATIVE_ALLOCATION *A =
                    &Input->SharedNativeAllocationSet->Entries[MapIndex];
                if (A->Kind == RC95LDR_ALLOC_UEFI_MAP &&
                    A->MemoryType == LoaderMemoryData &&
                    A->PhysicalBase == Input->UefiMapPhysicalBase &&
                    A->Length == Input->UefiMapAllocationSize)
                    FoundMap = 1;
            }
            if (!FoundMap)
                return Rc95LdrInvalidParameter;
        }
    } else if (Input->Profile->Firmware == Rc95LdrFirmwareBios) {
        if (!Input->BiosFirmwareState || Input->BiosFirmwareStateSize < 16u ||
            Input->BiosFirmwareStateVersion != 1u ||
            Input->BiosFirmwareStateSize > (1024u * 1024u))
            return Rc95LdrInvalidParameter;
    } else {
        return Rc95LdrUnsupported;
    }

    memset(&Cpu, 0, sizeof(Cpu));
    Cpu.StructureSize = sizeof(Cpu);
    Cpu.Version = RC95LDR_RC95_CPU_INFO_VERSION;
    Status = Input->QueryCpu ?
        Input->QueryCpu(Input->CpuContext, BaseInput.Architecture, &Cpu) :
        Rc95LdrQueryX86Cpu(NULL, BaseInput.Architecture, &Cpu);
    if (Status != Rc95LdrSuccess)
        return Status;
    if (Cpu.StructureSize < sizeof(Cpu) ||
        Cpu.Version != RC95LDR_RC95_CPU_INFO_VERSION ||
        !(Cpu.FeatureFlags & RC95LDR_CPU_FEATURE_CPUID) ||
        !(Cpu.FeatureFlags & RC95LDR_CPU_FEATURE_CX8) ||
        !Cpu.PhysicalAddressBits || Cpu.PhysicalAddressBits > 52u)
        return Rc95LdrUnsupported;
    if (BaseInput.Architecture == Rc95LdrNtArchAmd64) {
        if (Cpu.PagingMode != RC95LDR_CPU_PAGING_AMD64 ||
            !(Cpu.FeatureFlags & RC95LDR_CPU_FEATURE_PAE) ||
            !(Cpu.FeatureFlags & RC95LDR_CPU_FEATURE_LONG_MODE) ||
            !(Cpu.FeatureFlags & RC95LDR_CPU_FEATURE_FXSR) ||
            !(Cpu.FeatureFlags & RC95LDR_CPU_FEATURE_SSE2) ||
            Cpu.PhysicalAddressBits < 32u || Cpu.VirtualAddressBits < 48u)
            return Rc95LdrUnsupported;
    } else if (Cpu.PagingMode == RC95LDR_CPU_PAGING_I386_PAE) {
        if (!(Cpu.FeatureFlags & RC95LDR_CPU_FEATURE_PAE) ||
            Cpu.PhysicalAddressBits < 32u)
            return Rc95LdrUnsupported;
        /* Keep the 32-bit provider's own pointer-backed allocations below
           4 GiB; the PAE kernel page tables can map high RAM separately. */
        AllocationFlags |= RC95LDR_PHYSMEM_ALLOC_BELOW_4GB;
    } else if (Cpu.PagingMode == RC95LDR_CPU_PAGING_I386_NONPAE) {
        if (Cpu.PhysicalAddressBits != 32u)
            return Rc95LdrUnsupported;
        AllocationFlags |= RC95LDR_PHYSMEM_ALLOC_BELOW_4GB;
    } else {
        return Rc95LdrUnsupported;
    }
    Result->CpuInformation = Cpu;
    Result->PaeWindow = Input->PaeWindow;

    Status = ProviderAllocate(Result, RC95_PROVIDER_STACK_BYTES,
        AllocationFlags, &StackAllocation);
    if (Status != Rc95LdrSuccess) goto Failure;
    Status = ProviderTrackAllocation(Result, RC95LDR_ALLOC_LOADER_STACK,
        StackAllocation);
    if (Status != Rc95LdrSuccess) goto Failure;

    Status = ProviderAllocate(Result, RC95_PROVIDER_PAGETABLE_BYTES,
        AllocationFlags, &PageTableAllocation);
    if (Status != Rc95LdrSuccess) goto Failure;
    Status = ProviderTrackAllocation(Result, RC95LDR_ALLOC_PAGE_TABLES,
        PageTableAllocation);
    if (Status != Rc95LdrSuccess) goto Failure;

    Status = ProviderAllocate(Result, RC95_PROVIDER_HEAP_BYTES,
        AllocationFlags, &HeapAllocation);
    if (Status != Rc95LdrSuccess) goto Failure;
    Status = ProviderTrackAllocation(Result, RC95LDR_ALLOC_LOADER_HEAP,
        HeapAllocation);
    if (Status != Rc95LdrSuccess) goto Failure;

    Status = ProviderAllocate(Result, RC95_PROVIDER_METADATA_BYTES,
        AllocationFlags, &MetadataAllocation);
    if (Status != Rc95LdrSuccess) goto Failure;
    Status = ProviderTrackAllocation(Result, RC95LDR_ALLOC_NLS,
        MetadataAllocation);
    if (Status != Rc95LdrSuccess) goto Failure;

    Status = ProviderAllocate(Result, RC95_PROVIDER_LOADERBLOCK_BYTES,
        AllocationFlags, &LoaderBlockAllocation);
    if (Status != Rc95LdrSuccess) goto Failure;
    Status = ProviderTrackAllocation(Result, RC95LDR_ALLOC_LOADER_BLOCK,
        LoaderBlockAllocation);
    if (Status != Rc95LdrSuccess) goto Failure;

    if (Input->Profile->Firmware == Rc95LdrFirmwareBios) {
        Status = ProviderAllocate(Result, Input->BiosFirmwareStateSize,
            AllocationFlags, &BiosStateAllocation);
        if (Status != Rc95LdrSuccess) goto Failure;
        if (BiosStateAllocation->MappingLength < Input->BiosFirmwareStateSize) {
            Status = Rc95LdrBufferTooSmall;
            goto Failure;
        }
        memcpy(BiosStateAllocation->WritableMapping, Input->BiosFirmwareState,
            Input->BiosFirmwareStateSize);
        Status = ProviderTrackAllocation(Result,
            RC95LDR_ALLOC_BIOS_FIRMWARE_STATE, BiosStateAllocation);
        if (Status != Rc95LdrSuccess) goto Failure;
        Result->BiosFirmwareStateAddress =
            (uint64_t)(uintptr_t)BiosStateAllocation->WritableMapping;
        Result->BiosFirmwareStateSize = Input->BiosFirmwareStateSize;
        Result->BiosFirmwareStateVersion = Input->BiosFirmwareStateVersion;
    }

    Status = ProviderTrack(Result, RC95LDR_ALLOC_REGISTRY,
        Input->SystemHivePhysicalBase, Input->SystemHiveAllocationSize);
    if (Status != Rc95LdrSuccess) goto Failure;
    if (Input->Profile->Firmware == Rc95LdrFirmwareUefi) {
        Status = ProviderTrack(Result, RC95LDR_ALLOC_UEFI_MAP,
            Input->UefiMapPhysicalBase, Input->UefiMapAllocationSize);
        if (Status != Rc95LdrSuccess) goto Failure;
    }

    Heap = (uint8_t *)HeapAllocation->WritableMapping;
    /* The architecture handoff ABI requires both RC95 processor-control
       records to begin on distinct physical-page boundaries. */
    PerCpu = (RC95LDR_RC95_PERCPU_STATE *)ProviderHeapTake(
        Heap, HeapAllocation->MappingLength, &HeapOffset, sizeof(*PerCpu),
        RC95LDR_PHYSMEM_PAGE_SIZE);
    Scheduler = (RC95LDR_RC95_SCHEDULER_STATE *)ProviderHeapTake(
        Heap, HeapAllocation->MappingLength, &HeapOffset, sizeof(*Scheduler),
        RC95LDR_PHYSMEM_PAGE_SIZE);
    ArchState = (RC95LDR_RC95_ARCH_STATE *)ProviderHeapTake(
        Heap, HeapAllocation->MappingLength, &HeapOffset, sizeof(*ArchState), 64u);
    CpuState = (RC95LDR_RC95_CPU_STATE *)ProviderHeapTake(
        Heap, HeapAllocation->MappingLength, &HeapOffset, sizeof(*CpuState), 64u);
    RegisterFrame = (RC95LDR_ARCH_REGISTER_FRAME *)ProviderHeapTake(
        Heap, HeapAllocation->MappingLength, &HeapOffset,
        sizeof(*RegisterFrame), 16u);
    if (!PerCpu || !Scheduler || !ArchState || !CpuState || !RegisterFrame) {
        Status = Rc95LdrBufferTooSmall;
        goto Failure;
    }

    Result->StackBase = (uint64_t)(uintptr_t)StackAllocation->WritableMapping;
    Result->StackSize = StackAllocation->MappingLength;
    Result->StackPointer = (Result->StackBase + Result->StackSize) & ~15ULL;
    Result->LoaderBlockPhysicalBase = LoaderBlockAllocation->PhysicalBase;
    Result->MetadataPhysicalBase = MetadataAllocation->PhysicalBase;

    PerCpu->Signature = RC95LDR_RC95_PERCPU_SIGNATURE;
    PerCpu->Size = sizeof(*PerCpu);
    PerCpu->Version = 1u;
    PerCpu->ProcessorId = Cpu.ProcessorId;
    PerCpu->HardwareId = Cpu.HardwareId;
    PerCpu->KernelStackBase = Result->StackBase;
    PerCpu->KernelStackSize = Result->StackSize;
    PerCpu->FeatureFlags = Cpu.FeatureFlags;

    Scheduler->Signature = RC95LDR_RC95_SCHED_SIGNATURE;
    Scheduler->Size = sizeof(*Scheduler);
    Scheduler->Version = 1u;
    Scheduler->ProcessorCount = 1u;
    /* Thread objects and queue links are kernel-owned and are deliberately
       left empty until RC95-OS initializes its idle thread after entry. */
    Scheduler->IdleStackBase = Result->StackBase;
    Scheduler->IdleStackSize = Result->StackSize;
    Scheduler->Quantum = 10u;
    Scheduler->Flags = 1u; /* RC95 scheduler must finish idle-thread bootstrap. */
    PerCpu->Flags = 1u;    /* Bootstrap processor; APs are not yet online. */

    CpuState->Signature = RC95LDR_RC95_CPU_SIGNATURE;
    CpuState->Size = sizeof(*CpuState);
    CpuState->Version = 2u;
    CpuState->ProcessorId = Cpu.ProcessorId;
    CpuState->FeatureFlags = Cpu.FeatureFlags;
    CpuState->PhysicalAddressBits = Cpu.PhysicalAddressBits;
    memcpy(CpuState->Vendor, Cpu.Vendor, sizeof(Cpu.Vendor));
    CpuState->PagingMode = Cpu.PagingMode;
    CpuState->VirtualAddressBits = Cpu.VirtualAddressBits;
    CpuState->Leaf1Ecx = Cpu.Leaf1Ecx;
    CpuState->Leaf1Edx = Cpu.Leaf1Edx;
    CpuState->Leaf7Ebx = Cpu.Leaf7Ebx;
    CpuState->Leaf7Ecx = Cpu.Leaf7Ecx;
    CpuState->Leaf7Edx = Cpu.Leaf7Edx;
    CpuState->ExtendedLeaf1Ecx = Cpu.ExtendedLeaf1Ecx;
    CpuState->ExtendedLeaf1Edx = Cpu.ExtendedLeaf1Edx;
    CpuState->ProcessorFamily = Cpu.ProcessorFamily;
    CpuState->ProcessorModel = Cpu.ProcessorModel;
    CpuState->ProcessorStepping = Cpu.ProcessorStepping;
    CpuState->MaxBasicLeaf = Cpu.MaxBasicLeaf;
    CpuState->MaxExtendedLeaf = Cpu.MaxExtendedLeaf;
    CpuState->ProcessorSignature = Cpu.ProcessorSignature;
    CpuState->InitialStackPointer = Result->StackPointer;
    CpuState->InitialInstructionPointer = Kernel->EntryPoint;

    if (Cpu.PagingMode == RC95LDR_CPU_PAGING_I386_PAE) {
        if (!Input->PaeWindow.VirtualBase || !Input->PaeWindow.Length ||
            (Input->PaeWindow.VirtualBase & 0xfffu) ||
            (Input->PaeWindow.Length & 0xfffu) ||
            Input->PaeWindow.VirtualBase >= 0x100000000ULL ||
            Input->PaeWindow.Length > 0x100000000ULL -
                Input->PaeWindow.VirtualBase) {
            Status = Rc95LdrInvalidParameter;
            goto Failure;
        }
        Status = Rc95LdrBuildI386PaeWindowMappings(&Cpu,
            &Input->PaeWindow, BaseInput.Modules, BaseInput.ModuleCount,
            Result->NativeAllocations, Result->NativeAllocationCount,
            Input->FirmwareRanges, Input->FirmwareRangeCount,
            Result->PaeMappings, RC95LDR_PAE_WINDOW_MAPPING_MAX,
            &PaeMappingCount);
        if (Status != Rc95LdrSuccess) goto Failure;
        Result->PaeMappingCount = PaeMappingCount;
    } else if (Input->PaeWindow.VirtualBase || Input->PaeWindow.Length) {
        Status = Rc95LdrInvalidParameter;
        goto Failure;
    }
    Status = (Input->BuildPageTables ? Input->BuildPageTables :
        Rc95LdrBuildBaselinePageTables)(
        Input->BuildPageTables ? Input->PageTableContext : NULL,
        BaseInput.Architecture, &Cpu,
        Cpu.PagingMode == RC95LDR_CPU_PAGING_I386_PAE ? &Input->PaeWindow : NULL,
        PageTableAllocation, BaseInput.Modules,
        BaseInput.ModuleCount, Result->NativeAllocations,
        Result->NativeAllocationCount, Input->FirmwareRanges,
        Input->FirmwareRangeCount, &PageTableStorageUsed, &PageTableRoot);
    if (Status != Rc95LdrSuccess) goto Failure;
    /* A successful callback must have built a complete root page inside the
       consumed portion of the supplied arena. Reject empty, truncated,
       unaligned or out-of-arena results before modifying its page tables. */
    if (!PageTableRoot ||
        (PageTableRoot & (RC95LDR_PHYSMEM_PAGE_SIZE - 1u)) ||
        PageTableStorageUsed < RC95LDR_PHYSMEM_PAGE_SIZE ||
        (PageTableStorageUsed & (RC95LDR_PHYSMEM_PAGE_SIZE - 1u)) ||
        PageTableStorageUsed > PageTableAllocation->MappingLength ||
        PageTableRoot < PageTableAllocation->PhysicalBase ||
        PageTableRoot - PageTableAllocation->PhysicalBase >
            PageTableStorageUsed - RC95LDR_PHYSMEM_PAGE_SIZE) {
        Status = Rc95LdrInvalidImage;
        goto Failure;
    }
    if (Cpu.PagingMode == RC95LDR_CPU_PAGING_I386_PAE) {
        Status = Rc95LdrInstallI386PaeWindowMappings(&Cpu,
            &Result->PaeWindow, Result->PaeMappings,
            Result->PaeMappingCount, PageTableAllocation, PageTableRoot,
            &PageTableStorageUsed);
        if (Status != Rc95LdrSuccess) goto Failure;
    }
    if (PageTableStorageUsed > PageTableAllocation->MappingLength ||
        (PageTableStorageUsed & (RC95LDR_PHYSMEM_PAGE_SIZE - 1u))) {
        Status = Rc95LdrInvalidImage;
        goto Failure;
    }
    Result->PageTableRoot = PageTableRoot;

    ArchState->Signature = RC95LDR_RC95_ARCH_SIGNATURE;
    ArchState->Size = sizeof(*ArchState);
    ArchState->Version = 2u;
    ArchState->Architecture = BaseInput.Architecture;
    ArchState->PageTableRoot = PageTableRoot;
    ArchState->KernelEntry = Kernel->EntryPoint;
    ArchState->LoaderBlock =
        (uint64_t)(uintptr_t)LoaderBlockAllocation->WritableMapping;
    ArchState->StackBase = Result->StackBase;
    ArchState->StackSize = Result->StackSize;
    ArchState->StackPointer = Result->StackPointer;
    ArchState->PhysicalAddressBits = Cpu.PhysicalAddressBits;
    ArchState->FeatureFlags = Cpu.FeatureFlags;
    ArchState->PagingMode = Cpu.PagingMode;
    ArchState->VirtualAddressBits = Cpu.VirtualAddressBits;

    PerCpu->SchedulerState = (uint64_t)(uintptr_t)Scheduler;
    PerCpu->CpuState = (uint64_t)(uintptr_t)CpuState;
    PerCpu->ArchitectureState = (uint64_t)(uintptr_t)ArchState;
    Result->PerCpu = PerCpu;
    Result->Scheduler = Scheduler;
    Result->ArchitectureState = ArchState;
    Result->CpuState = CpuState;

    memset(&Result->ArchitectureHandoff, 0,
        sizeof(Result->ArchitectureHandoff));
    Result->ArchitectureHandoff.StructureSize =
        sizeof(Result->ArchitectureHandoff);
    Result->ArchitectureHandoff.Version =
        RC95LDR_ARCH_HANDOFF_INPUT_VERSION;
    Result->ArchitectureHandoff.Architecture = BaseInput.Architecture;
    Result->ArchitectureHandoff.StackBase = Result->StackBase;
    Result->ArchitectureHandoff.StackSize = Result->StackSize;
    Result->ArchitectureHandoff.StackPointer = Result->StackPointer;
    Result->ArchitectureHandoff.PageTableRoot = PageTableRoot;
    Result->ArchitectureHandoff.ArchitectureState =
        (uint64_t)(uintptr_t)ArchState;
    Result->ArchitectureHandoff.CpuState = (uint64_t)(uintptr_t)CpuState;
    Result->ArchitectureHandoff.KernelEntry = Kernel->EntryPoint;
    Result->ArchitectureHandoff.LoaderBlockArgument =
        (uint64_t)(uintptr_t)LoaderBlockAllocation->WritableMapping;
    Result->ArchitectureHandoff.RegisterFrameBuffer = RegisterFrame;
    Result->ArchitectureHandoff.RegisterFrameCapacity =
        sizeof(*RegisterFrame);
    Result->ArchitectureHandoff.EnableNx =
        (Cpu.PagingMode != RC95LDR_CPU_PAGING_I386_NONPAE &&
         (Cpu.FeatureFlags & RC95LDR_CPU_FEATURE_NX) &&
         (Cpu.FeatureFlags & RC95LDR_CPU_FEATURE_MSR)) ? 1u : 0u;
    if (BaseInput.Architecture == Rc95LdrNtArchAmd64) {
        Result->ArchitectureHandoff.ProcessorControl.Rc95Amd64.PerCpuDataAddress =
            (uint64_t)(uintptr_t)PerCpu;
        Result->ArchitectureHandoff.ProcessorControl.Rc95Amd64.PerCpuDataSize =
            sizeof(*PerCpu);
        Result->ArchitectureHandoff.ProcessorControl.Rc95Amd64.SchedulerStateAddress =
            (uint64_t)(uintptr_t)Scheduler;
        Result->ArchitectureHandoff.ProcessorControl.Rc95Amd64.SchedulerStateSize =
            sizeof(*Scheduler);
    } else {
        Result->ArchitectureHandoff.ProcessorControl.Rc95I386.PerCpuDataAddress =
            (uint64_t)(uintptr_t)PerCpu;
        Result->ArchitectureHandoff.ProcessorControl.Rc95I386.PerCpuDataSize =
            sizeof(*PerCpu);
        Result->ArchitectureHandoff.ProcessorControl.Rc95I386.SchedulerStateAddress =
            (uint64_t)(uintptr_t)Scheduler;
        Result->ArchitectureHandoff.ProcessorControl.Rc95I386.SchedulerStateSize =
            sizeof(*Scheduler);
    }

    Result->MetadataInput = Input->Metadata;
    Result->MetadataInput.StructureSize = sizeof(Result->MetadataInput);
    Result->MetadataInput.Version = RC95LDR_METADATA_VERSION;
    Result->MetadataInput.Buffer = MetadataAllocation->WritableMapping;
    Result->MetadataInput.BufferSize = MetadataAllocation->MappingLength;

    Result->Rc95LoaderStateInput.StructureSize =
        sizeof(Result->Rc95LoaderStateInput);
    Result->Rc95LoaderStateInput.Version =
        RC95LDR_RC95_LOADERSTATE_VERSION;
    Result->Rc95LoaderStateInput.PhysicalMemory = &Result->PhysicalMemory;

    memset(&Result->NativeRequest, 0, sizeof(Result->NativeRequest));
    Request = &Result->NativeRequest;
    Request->StructureSize = sizeof(*Request);
    Request->Version = RC95LDR_NATIVE_PREPARE_VERSION;
    Request->Profile = &Result->Profile;
    Request->BaseInput = &Result->BaseInput;
    Request->SystemHiveBytes = Input->SystemHive;
    Request->SystemHiveSize = Input->SystemHiveSize;
    Request->LoaderBlockBuffer = LoaderBlockAllocation->WritableMapping;
    Request->LoaderBlockCapacity = LoaderBlockAllocation->MappingLength;
    if (Input->BootFileProvider) {
        Request->LoadBootDriver = Rc95LdrRc95NativeLoadBootDriver;
        Request->DriverLoadContext = Result;
    } else {
        Request->LoadBootDriver = Input->LoadBootDriver;
        Request->DriverLoadContext = Input->DriverLoadContext;
    }
    Request->ValidateModuleAllocation = Input->ValidateModuleAllocation;
    Request->AllocationValidationContext = Input->AllocationValidationContext;
    Request->BootResourceModule = Input->BootResourceModule;
    Request->FirmwareSystemTable = Input->FirmwareSystemTable;
    Request->Rc95LoaderStateInput = &Result->Rc95LoaderStateInput;
    Request->ArchitectureHandoffInput = &Result->ArchitectureHandoff;
    Request->PlatformInventoryInput = &Input->PlatformInventory;
    Request->MetadataInput = &Result->MetadataInput;
    Request->NativeAllocations = Result->NativeAllocations;
    Request->NativeAllocationCount = Result->NativeAllocationCount;
    Request->MemoryReservations = &Result->MemoryReservations;
    Request->FirmwareMemoryRanges = Input->FirmwareRanges;
    Request->FirmwareMemoryRangeCount = Input->FirmwareRangeCount;
    Request->KernelPhysicalBase = Kernel->PhysicalBase;
    Request->KernelPhysicalSize = Kernel->PhysicalSize;
    Request->BiosFirmwareStateAddress = Result->BiosFirmwareStateAddress;
    Request->BiosFirmwareStateSize = Result->BiosFirmwareStateSize;
    Request->BiosFirmwareStateVersion = Result->BiosFirmwareStateVersion;
    if (BiosStateAllocation) {
        Request->BiosFirmwareStatePhysicalBase = BiosStateAllocation->PhysicalBase;
        Request->BiosFirmwareStateAllocationSize =
            BiosStateAllocation->PageCount * RC95LDR_PHYSMEM_PAGE_SIZE;
    }
    if (Input->Profile->Firmware == Rc95LdrFirmwareUefi) {
        BaseInput.FirmwareIsUefi = 1u;
        BaseInput.FirmwareSystemTable = Input->FirmwareSystemTable;
        BaseInput.VirtualEfiRuntimeServices = Input->FirmwareRuntimeServices;
        BaseInput.FirmwareMapAddress = Input->FirmwareMapAddress;
        BaseInput.FirmwareMapSize = Input->FirmwareMapSize;
        BaseInput.FirmwareMapDescriptorSize = Input->FirmwareMapDescriptorSize;
        Request->FirmwareSystemTable = Input->FirmwareSystemTable;
    } else {
        BaseInput.FirmwareIsUefi = 0u;
        BaseInput.FirmwareSystemTable = 0;
        BaseInput.VirtualEfiRuntimeServices = 0;
        BaseInput.FirmwareMapAddress = 0;
        BaseInput.FirmwareMapSize = 0;
        BaseInput.FirmwareMapDescriptorSize = 0;
        Request->FirmwareSystemTable = 0;
    }
    BaseInput.RegistryBase = Input->SystemHive;
    BaseInput.RegistryLength = (uint32_t)Input->SystemHiveSize;
    BaseInput.ConfigurationRoot = 0;
    BaseInput.NlsData = 0;
    BaseInput.ArcDiskInformation = 0;
    if (Input->BootDevice) BaseInput.ArcBootDevice = Input->BootDevice;
    if (Input->HalDevice) BaseInput.ArcHalDevice = Input->HalDevice;
    if (Input->SystemRoot) BaseInput.NtBootPath = Input->SystemRoot;
    if (Input->LoadOptions) BaseInput.LoadOptions = Input->LoadOptions;

    Result->BaseInput = BaseInput;
    Request->BaseInput = &Result->BaseInput;

    /* Preserve the UEFI frontend's map allocation and merge all allocations
       owned by this RC95 provider into the same ledger before osload publishes
       the native request. */
    if (Input->SharedNativeAllocationSet) {
        RC95LDR_NATIVE_ALLOCATION_SET Shared =
            *Input->SharedNativeAllocationSet;
        size_t AllocationIndex;
        Result->SharedNativeAllocationSet =
            Input->SharedNativeAllocationSet;
        Result->SharedNativeAllocationSetBefore = Shared;
        Result->SharedAllocationSetMerged = 1u;
        for (AllocationIndex = 0;
             AllocationIndex < Result->NativeAllocationCount;
             ++AllocationIndex) {
            const RC95LDR_NATIVE_ALLOCATION *A =
                &Result->NativeAllocations[AllocationIndex];
            Status = Rc95LdrTrackNativeAllocation(&Shared, A->Kind,
                A->PhysicalBase, A->Length);
            if (Status != Rc95LdrSuccess)
                goto Failure;
        }
        *Input->SharedNativeAllocationSet = Shared;
        Request->NativeAllocations = Shared.Entries;
        Request->NativeAllocationCount = Shared.Count;
    }

    return Rc95LdrSuccess;

Failure:
    Rc95LdrReleaseRc95NativeInput(Result);
    return Status;
}

RC95LDR_STATUS
Rc95LdrRc95NativeBuildPreparation(
    void *Opaque,
    const RC95LDR_WINLOAD_REQUEST *WinloadRequest,
    uint64_t KernelVirtualBase,
    uint64_t KernelPhysicalBase,
    uint64_t KernelImageSize,
    uint64_t KernelAllocationSize,
    uint64_t KernelEntry,
    RC95LDR_NATIVE_PREPARE_REQUEST *PrepareRequest)
{
    RC95LDR_RC95_NATIVE_PLATFORM_CONTEXT *Context =
        (RC95LDR_RC95_NATIVE_PLATFORM_CONTEXT *)Opaque;
    RC95LDR_RC95_NATIVE_PROVIDER_INPUT ProviderInput;
    RC95LDR_OS_IDENTITY Identity;
    RC95LDR_STATUS Status;

    if (!Context || !Context->Input || !Context->Input->Profile ||
        !Context->Result || !WinloadRequest || !PrepareRequest ||
        WinloadRequest->StructureSize < sizeof(*WinloadRequest) ||
        WinloadRequest->OsFamily != Rc95LdrOsRc95 ||
        WinloadRequest->Firmware != Context->Input->Profile->Firmware ||
        WinloadRequest->BootEntryKind != Rc95LdrEntryRc95Winload)
        return Rc95LdrInvalidParameter;

    memset(&Identity, 0, sizeof(Identity));
    Identity.StructureSize = sizeof(Identity);
    Identity.Version = RC95LDR_OS_PROFILE_VERSION;
    Identity.OsFamily = WinloadRequest->OsFamily;
    Identity.Firmware = WinloadRequest->Firmware;
    Identity.BootEntry = (RC95LDR_BOOT_ENTRY_KIND)WinloadRequest->BootEntryKind;
    Identity.OsMajorVersion = WinloadRequest->OsMajorVersion;
    Identity.OsMinorVersion = WinloadRequest->OsMinorVersion;
    Identity.OsBuildNumber = WinloadRequest->OsBuildNumber;
    Status = Rc95LdrSelectOsProfile(&Identity, &Context->Profile);
    if (Status != Rc95LdrSuccess)
        return Status;

    if (Context->Result->StructureSize ==
            sizeof(*Context->Result) &&
        Context->Result->Version == RC95LDR_RC95_PROVIDER_VERSION)
        Rc95LdrReleaseRc95NativeInput(Context->Result);

    ProviderInput = *Context->Input;
    ProviderInput.Profile = &Context->Profile;
    ProviderInput.KernelVirtualBase = KernelVirtualBase;
    ProviderInput.KernelPhysicalBase = KernelPhysicalBase;
    ProviderInput.KernelImageSize = KernelImageSize;
    ProviderInput.KernelAllocationSize = KernelAllocationSize;
    ProviderInput.KernelEntry = KernelEntry;
    ProviderInput.KernelPeSections = WinloadRequest->KernelPeSections;
    ProviderInput.KernelPeSectionCount = WinloadRequest->KernelPeSectionCount;
    ProviderInput.KernelPeSizeOfHeaders = WinloadRequest->KernelPeSizeOfHeaders;

    Status = Rc95LdrPrepareRc95NativeInput(&ProviderInput, Context->Result);
    if (Status != Rc95LdrSuccess)
        return Status;

    *PrepareRequest = Context->Result->NativeRequest;
    return Rc95LdrSuccess;
}

void
Rc95LdrRc95NativeReleasePreparation(
    void *Opaque,
    RC95LDR_NATIVE_PREPARE_REQUEST *PrepareRequest)
{
    RC95LDR_RC95_NATIVE_PLATFORM_CONTEXT *Context =
        (RC95LDR_RC95_NATIVE_PLATFORM_CONTEXT *)Opaque;
    if (Context && Context->Result)
        Rc95LdrReleaseRc95NativeInput(Context->Result);
    if (PrepareRequest)
        memset(PrepareRequest, 0, sizeof(*PrepareRequest));
}

RC95LDR_STATUS
Rc95LdrAttachRc95NativeProvider(
    RC95LDR_WINLOAD_PLATFORM *Platform,
    RC95LDR_RC95_NATIVE_PLATFORM_CONTEXT *Context)
{
    if (!Platform || !Context ||
        Platform->StructureSize <
            offsetof(RC95LDR_WINLOAD_PLATFORM, NativePreparationContext) +
            sizeof(Platform->NativePreparationContext) ||
        Platform->Version != RC95LDR_WINLOAD_ABI_VERSION ||
        !Context->Input || !Context->Result || !Context->Input->Profile ||
        Context->Input->Profile->OsFamily != Rc95LdrOsRc95)
        return Rc95LdrInvalidParameter;
    if (Context->Input->Profile->Firmware == Rc95LdrFirmwareUefi) {
        if (!Platform->NativeAllocationSet ||
            Context->Input->SharedNativeAllocationSet !=
                (RC95LDR_NATIVE_ALLOCATION_SET *)(uintptr_t)
                    Platform->NativeAllocationSet ||
            !Platform->FirmwareSystemTable ||
            !Platform->FirmwareRuntimeServices ||
            Context->Input->FirmwareSystemTable !=
                Platform->FirmwareSystemTable ||
            Context->Input->FirmwareRuntimeServices !=
                Platform->FirmwareRuntimeServices)
            return Rc95LdrInvalidParameter;
    }

    memset(Context->Result, 0, sizeof(*Context->Result));
    Platform->BuildNativePreparation = Rc95LdrRc95NativeBuildPreparation;
    Platform->ReleaseNativePreparation = Rc95LdrRc95NativeReleasePreparation;
    Platform->NativePreparationContext = Context;
    return Rc95LdrSuccess;
}
