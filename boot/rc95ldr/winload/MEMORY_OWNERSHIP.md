# Native loader physical-memory ownership

The firmware map is not safe to publish until every allocation made by the
platform adapter and winload has an explicit physical extent. Virtual pointers
are never converted to physical addresses by casting.

## Platform allocation contract

Initialize one `RC95LDR_NATIVE_ALLOCATION_SET` before loading the image.
Whenever the platform allocator successfully obtains a physical allocation,
record its physical base and byte length with
`Rc95LdrTrackNativeAllocation()`. The returned set is passed unchanged in
`RC95LDR_NATIVE_PREPARE_REQUEST.NativeAllocations` and
`NativeAllocationCount`.

Required allocation kinds (the loader-block row is only needed when its storage is not already covered by the loader heap):

| Kind | NT descriptor |
| --- | --- |
| `RC95LDR_ALLOC_REGISTRY` | `LoaderRegistryData` |
| `RC95LDR_ALLOC_NLS` | `LoaderNlsData` |
| `RC95LDR_ALLOC_LOADER_HEAP` | `LoaderOsloaderHeap` |
| `RC95LDR_ALLOC_LOADER_STACK` | `LoaderOsloaderStack` |
| `RC95LDR_ALLOC_PAGE_TABLES` | `LoaderMemoryData` |
| `RC95LDR_ALLOC_LOADER_BLOCK` | `LoaderMemoryData` |
| `RC95LDR_ALLOC_UEFI_MAP` (UEFI only) | `LoaderMemoryData` |

The UEFI adapter must call `Rc95LdrUefiTrackMemoryMapBuffer()` immediately
after allocating the persistent buffer used for the final
`GetMemoryMap()`. It records the EFI physical address returned by
`AllocatePages`, not the buffer's C pointer. BIOS adapters use the generic
tracker for the same loader-owned allocations.

The metadata buffer holding NLS tables may be recorded as
`RC95LDR_ALLOC_NLS`; if NLS and ARC metadata occupy separate allocations,
record each extent with its corresponding ownership kind. Do not record
overlapping suballocations as separate physical ranges.

## Winload registration

`KernelImageBase` is the CPU virtual mapping used to read/execute the PE image; `KernelPhysicalBase` is the exact physical address returned by `AllocatePages`. The current firmware backend identity-maps those pages, so their numeric values can match, but code must not infer one from the other. `KernelAllocationSize` is the exact page-rounded allocation length. The build callback receives all four values separately, and winload copies only the physical allocator result into the physical reservation fields. The platform preparation callback must provide complete, page-aligned physical extents for the HAL and each preloaded module; the hive-driver callback must do the same for every boot driver it loads. A HAL image copied into a second boot-driver list record retains the original extent and is emitted only once in the physical memory map.


Before building the NT loader block, winload:

1. Registers every kernel, HAL and boot-driver image from the loaded-module
   array. `RC95LDR_LOADED_MODULE.PhysicalBase` and `PhysicalSize` are mandatory
   and describe the complete contiguous physical backing allocation;
   `ImageBase` and `ImageSize` remain the loader virtual image mapping and PE
   image size. PhysicalSize must include page padding. The NT descriptor types are `LoaderSystemCode`,
   `LoaderHalCode`, and `LoaderBootDriver`.
2. Imports the platform allocation manifest and validates that registry,
   NLS, heap, stack, and page tables are present; a separately allocated
   loader block may additionally be recorded;
   UEFI additionally requires the final memory-map buffer.
3. Registers the configuration-tree allocation created by winload as
   `LoaderMemoryData`.
4. Builds the final sorted NT descriptor array from the firmware ranges and
   reservations, and passes that array to the selected native loader-block
   builder.

The reservation set is copied locally and published back to the caller only
after successful loader-block preparation. Conflicting or partially
overlapping physical allocations are rejected. The same physical HAL image
may have multiple logical loader-list roles; it receives one physical
descriptor, with `LoaderHalCode` taking precedence.

## Final UEFI transition

All memory-map buffers, descriptor-list storage, loader-block storage, and
other handoff allocations must be allocated and registered before the final
map refresh. Use `Rc95LdrUefiFinalizeNtMemoryMap()` with caller-preallocated
buffers; it performs the final `GetMemoryMap()`, converts the map, and calls
`ExitBootServices()` with the matching map key. No allocation, logging, or
firmware call may be inserted between those operations.

The platform must retain the physical allocation manifest and reservation
set through handoff. The kernel assumes ownership only after the handoff;
until then none of these allocations may be reclassified as `LoaderFree`.
