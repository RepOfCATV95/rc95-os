# RC95LDR native-input provider contract

This document defines the next implementation boundary after BCD candidate
selection and the common `LoadAndBootWindows` transaction. It is an adapter
contract, not a claim that the FreeLoader adapter can currently boot a native
Windows or RC95-OS image.

## Current state

The common loader can map a validated kernel PE image, invoke
`BuildNativePreparation`, validate the returned kernel identity, build the
native loader block, and run handoff preflight. The current
`Rc95LdrFreeLoaderBuildNativePreparation` provider returns
`Rc95LdrUnsupported`. The FreeLoader adapter's file, allocation, allocation
attestation, and memory-map snapshot services do not constitute a complete
native-input provider.

Do not change that fail-closed behavior until every required input below is
available and independently validated.

## Required provider inputs

### 1. Image and module graph

The provider must return exactly one kernel module record matching the image
base, image size, entry point, and allocator-owned physical extent passed by
`Rc95LdrLoadAndBootWindows`. It must also supply one compatible HAL and every
additional base image required by the selected OS profile.

For each image, the provider must:

- resolve the selected installation's actual file, not merely a catalog name;
- validate PE machine, optional-header format, subsystem, image bounds,
  relocations, and imports before publishing a module record;
- resolve imports against validated providers and reject unresolved, cyclic,
  or incompatible dependencies;
- retain the allocation base, allocation size, mapped image base, image size,
  entry point, and canonical path as distinct values;
- attest every physical extent against the platform allocator ledger; and
- release every temporary file buffer and partially mapped image on failure.

The dependency catalog is a candidate inventory only. SYSTEM service
configuration, hardware inventory, OS version, and image compatibility must
decide what is actually loaded.

### 2. SYSTEM hive and boot drivers

Supply the selected installation's resident SYSTEM hive and its exact byte
length. The hive must remain resident for as long as the loader block's
RegistryBase points to it.

Boot-driver selection must account for service Start and Type, Group, Tag,
ImagePath, DependOnService, DependOnGroup, and GroupOrderList. Resolve
environment-dependent image paths using the selected installation's system
root. Preserve dependency ordering and fail when a required boot-start driver
cannot be loaded. Do not publish a boot-driver record unless its image has
actually been mapped and its allocation attested.

The current hive enumerator's Start/Type filtering is not a substitute for
this full ordering and dependency policy.

### 3. NLS and ARC metadata

Provide validated ANSI, OEM, and Unicode NLS images and copy them into a
resident metadata workspace before building the loader block. Validate
lengths, alignment, and all source ranges.

Provide stable ARC device names and disk identity records for the selected
system volume, including the relevant MBR signature or GPT disk/partition
identity. The strings and records referenced by the loader block must remain
resident through handoff.

### 4. Platform and firmware inventory

Provide a validated inventory derived from the actual firmware and platform.
ACPI tables must be checksum- and length-validated before use. PCI enumeration
must respect segment, bus, bridge, and resource boundaries. Do not synthesize
device resources from PCI IDs alone.

The current inventory implementation has bounded ACPI and PCI enumeration;
MCFG multi-segment support, bridge resource decoding, SMBIOS inventory, and
complete target-specific resource-list encoding remain separate work items.

### 5. Architecture state and loader extension

The platform adapter must construct target-version-correct processor state:
stack, page tables, PCR, PRCB, and register frame. Validate that every object
is mapped at the address and with the alignment expected by the target kernel.
The generic architecture record is not a substitute for initialized
architecture-specific structures.

Implement the complete target-generation
`LOADER_PARAMETER_EXTENSION` before setting its handoff requirement complete.
Do not emit a guessed prefix or mark the extension ready because a buffer
exists. Firmware information must match the selected BIOS or UEFI path and
the exact NT generation.

### 6. Final memory map and firmware exit

Every allocation made during image loading and native preparation must be
represented in the final memory map with the correct ownership/type. Reconcile
reservations after the last allocation; a pre-allocation FreeLoader map
snapshot is not the final firmware map.

For UEFI, reacquire the final EFI memory map immediately before
`ExitBootServices`, use its current map key, and retry the complete
map/exit sequence when the key changes. After a successful exit, do not call
Boot Services or dereference Boot-Services-owned memory. BIOS shutdown and
transfer must use a separate firmware-specific implementation.

## Transaction and failure rules

1. Keep BCD selection, image mapping, native preparation, and handoff as
   distinct stages with stable error details.
2. Do not transfer control unless `HandoffReady` is true and the kernel,
   loader-block, firmware, memory-map, extension, NLS, configuration, stack,
   page-table, PCR, PRCB, and register-frame requirements all validate.
3. On any pre-handoff failure, release all allocations owned by the
   transaction, clear public result addresses, and leave firmware services
   usable.
4. A successful PE map or a structurally valid loader block is not proof that
   the target kernel can boot.
5. Never call a mapped PE entry point as an ordinary C function. The final
   transition is architecture- and firmware-specific and must not return on
   success.

## Required validation before enabling the provider

- Host tests for PE import/dependency resolution, missing imports, duplicate
  providers, cycles, wrong-machine dependencies, and malformed image ranges.
- Synthetic SYSTEM hive tests for service dependencies, group ordering,
  missing required drivers, and path expansion.
- Tests proving every published module extent is backed by the allocator
  ledger and that failure releases all partial allocations.
- Loader-block tests for each supported NT generation and architecture,
  including extension and firmware-information layout checks.
- Memory-map tests covering overlapping reservations, late allocations,
  descriptor coalescing, and UEFI map-key changes.
- Firmware tests on both BIOS and UEFI virtual machines, followed by real
  hardware validation before enabling a non-returning kernel transfer.

Until these conditions are met, `Rc95LdrUnsupported` from the native-input
provider and the absence of a kernel transfer are intentional safety
properties, not incomplete-success states.
