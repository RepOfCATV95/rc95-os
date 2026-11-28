# RC95LDR native loader-block and handoff boundary

## ABI source and target selection

The NT-compatible structures in `include/rc95ldrntabi.h` are a standalone,
pointer-width-sensitive transcription of the structure definitions in:

- `sdk/include/reactos/arc/arc.h`: `LOADER_PARAMETER_BLOCK`,
  `MEMORY_ALLOCATION_DESCRIPTOR`, `BOOT_DRIVER_LIST_ENTRY`,
  architecture union and firmware-information block.
- `sdk/include/ndk/ldrtypes.h`: `LDR_DATA_TABLE_ENTRY`.

The builder selects the loader-block field sequence using the explicit
`RC95LDR_NT_TARGET_VERSION` value:

| Target | Structure sequence |
| --- | --- |
| NT 4.0 | `RC95LDR_NT_LOADER_BLOCK_V4` |
| Windows 2000–Server 2003 | `RC95LDR_NT_LOADER_BLOCK_V5` |
| Vista | `RC95LDR_NT_LOADER_BLOCK_V6` |
| Windows 7 | `RC95LDR_NT_LOADER_BLOCK_V61` |
| Windows 8 | `RC95LDR_NT_LOADER_BLOCK_V62` |
| Windows 8.1 | `RC95LDR_NT_LOADER_BLOCK_V63` |
| Windows 10/11 | `RC95LDR_NT_LOADER_BLOCK_V10` |

The layout uses the compiler's native pointer width. The referenced ReactOS
architecture union defines I386, Alpha, IA64, PowerPC and ARM records, but no
separate AMD64 member. AMD64 therefore leaves the union zeroed unless the
platform supplies the appropriate I386-slot machine state through
`ArchitectureBlock`. PowerPC is accepted only in a 32-bit build. ARM's union
member follows ReactOS's `_ARM_`-conditional placeholder/full definition.

ReactOS uses the same base `LOADER_PARAMETER_BLOCK` family, selected by
`NtVersion`; its builder is available for rosload-side integration. RC95LDR's
`{rosboot}` path itself delegates directly to the supplied `rosload.exe`
launcher callback rather than replacing ReactOS's own preparation sequence.
RC95-OS retains its own explicitly versioned native structure, list-entry type,
module record, memory descriptor and boot-driver record; it does not reuse the
NT `LDR_DATA_TABLE_ENTRY` or NT boot-driver records.

## What is now connected

`Rc95LdrPrepareOperatingSystemNative` routes a validated OS profile into
`Rc95LdrPrepareNativeLoaderBlock`. ReactOS is dispatched directly to
`rosload.exe`; for the NT-compatible and RC95-OS paths, the native preparation path:

1. consumes already-loaded kernel and HAL module records from `BaseInput` and
   validates the SYSTEM hive base block and HBIN/cell boundaries;
2. resolves `Select\Current` and the active `ControlSet00x`;
3. enumerates `Services` entries selected for boot loading (`Start=0`)
   and kernel / file-system driver types; `Start=1` services are system-start
   drivers and are intentionally left for the kernel;
4. asks the platform's driver loader to map each selected image;
5. constructs load-order module entries, boot-driver entries and memory
   descriptor lists in the selected native layout;
6. for NT profiles, builds a caller-described configuration component tree
   and links its root into the native loader block;
7. deliberately does not fabricate a loader-extension prefix; non-NT4 extension
   readiness remains blocked until its complete generation ABI is built;
8. optionally validates caller-prepared architecture stack, page-table,
   CPU-state and architecture-state addresses before recording them in the
   handoff context.

The builder accepts caller-supplied machine-dependent union state through
`ArchitectureBlock` and writes absolute pointers into the supplied buffer. Consequently,
the buffer must already be allocated at its final kernel-visible virtual
address, and that address must remain valid through handoff. The SYSTEM hive
buffer must likewise remain resident at the address placed in
`RegistryBase`.

Generated boot-driver registry paths are stored in the
`RC95LDR_NATIVE_PREPARE_RESULT` itself, not in global mutable storage. Keep the
result object at a stable address until the loader block has copied the
registry strings; do not place this large result object (now well over 100 KiB with the
expanded PCI/SMBIOS inventory and per-driver path storage) on a small firmware
stack. `Rc95LdrReleaseNativePrepareResult` clears this path storage
when the preparation result is retired.

## Deliberately not handoff-ready

The current preparation path marks the kernel, HAL, SYSTEM hive, boot-driver enumeration, loader block and available memory/firmware state as completed, then runs the handoff validator. `HandoffReady` remains false while any required prerequisite is missing. Before
a kernel jump is enabled, the implementation still needs:

- construction and initialization of the complete target-version
  `LOADER_PARAMETER_EXTENSION` (no temporary prefix is emitted, and the
  extension requirement remains incomplete in the handoff mask);
- population of the configuration tree from actual firmware/ACPI/PCI inventory
  rather than caller-provided component descriptors, plus ARC disk/signature
  information;
- NLS data and other mandatory boot metadata;
- complete registry service-dependency resolution and topological ordering
  in the portable hive-reader path. `DependOnService` entries are validated
  against the complete Services tree, then resolved to boot-start driver
  candidates and passed to the bounded stable topological sorter. Missing
  targets, dependencies outside the boot-start driver set, self-dependencies,
  and cycles fail closed before any driver callback is invoked.
  `DependOnGroup` entries are still validated but non-empty group dependencies
  remain unsupported until their SCM group semantics are implemented and
  verified. Synthetic SYSTEM-hive integration fixtures now exercise the public
  enumerator for prerequisite-first chains, missing and non-boot targets,
  self-dependencies, cycles, valid non-empty group dependencies, malformed
  dependency metadata, an independent group-rank ordering case, a tag-rank-only case where
  the hive order conflicts with the configured tag order, and malformed
  ServiceGroupOrder and GroupOrderList fixtures (duplicate group identities
  and a truncated tag array), plus malformed service `Group` (wrong registry
  type) and `Tag` (wrong registry type) values. These malformed ordering and
  service-rank cases fail before any driver callback. A combined group-rank/dependency case verifies that the
  prerequisite edge overrides the earlier group-ranked candidate. Every
  failure case asserts that no driver callback runs. The in-tree FreeLoader Configuration
  Manager adapter delegates sorting and dependency-cycle handling to
  `CmpSortDriverList` and `CmpResolveDriverDependencies`;
- complete versioned EFI firmware information (including the Win8 resource list and Win8.1+ memory-map fields) and architecture-specific
  kernel/HAL startup state (the current architecture adapter validates and
  consumes supplied state; it does not create PCR/PRCBs, page tables or CPU
  register frames);
- final memory-map reservation and descriptor reconciliation after every
  allocation;
- the final architecture-specific register/stack transition and production BIOS
  provider integration. BIOS firmware-state capture is now implemented as a
  separate RC95-owned snapshot (IVT, BDA, boot drive, and validated E820
  ranges), with a caller-owned persistent buffer and a handoff pointer/size/
  version. The buffer must be reserved as
  RC95LDR_ALLOC_BIOS_FIRMWARE_STATE and supplied to native preparation;
  capture alone does not perform the final processor transition.

The UEFI adapter now has a low-level `Rc95LdrUefiExitBootServices` primitive.
It uses caller-preallocated memory-map storage, validates descriptor sizing,
and retries `ExitBootServices` only when firmware rejects a stale map key.
The retry loop performs no allocations, console output, protocol closes, or
other boot-service calls between the failed exit and the next map capture.
It returns `EFI_BUFFER_TOO_SMALL` before the irreversible transition when
the supplied buffer is inadequate. Host fixtures cover stale-key retry,
buffer exhaustion, retry exhaustion, and non-retryable firmware errors.
This primitive is not itself a complete UEFI handoff: the platform provider
must still reserve/track the final map buffer, translate descriptors into the
native loader memory map, prepare the target architecture state, and invoke
the non-returning kernel transfer after successful exit.

`LoadAndBootWindows` invokes the native builder through the
`BuildNativePreparation` callback. A complete provider must return a request
whose base module array contains exactly the mapped kernel plus the selected
HAL and other mapped base images. The native builder then resolves the SYSTEM
hive, loads boot-start drivers through `LoadBootDriver`, constructs
metadata/configuration and the loader block, and runs handoff preflight.

The first concrete FreeLoader adapter is
`ntldr/rc95ldr_freeldr_platform.c`, exposed through
`Rc95LdrInitializeFreeLoaderPlatform` and
`Rc95LdrFreeLoaderLoadAndBootWindows`. It implements ARC-backed open/read/
close with generation-tagged file slots and StartingAddress-aware random reads, uses the
FreeLoader page allocator for aligned identity-mapped allocations, tracks
allocation ownership independently because the current `MmFreeMemory`
implementation is a no-op, and exports a coalesced snapshot of the current
FreeLoader page-lookup allocation map. It is shared by BIOS and UEFI builds
for i386 and AMD64. The adapter uses exported FreeLoader APIs only: the
underlying memory-manager change implements `MmFreeMemory` using the
allocation length stored at the first page, validates the lookup-table range,
updates free-page accounting, and refreshes the high-page allocation hint.
`MmGetLowestPhysicalPage` is exported so rosload can translate page-map
indices without importing private globals.

This is a real low-level service adapter, not yet a complete native-input
provider. Its `BuildNativePreparation` callback currently returns
`Rc95LdrUnsupported`: FreeLoader's current path does not yet provide the
complete HAL import/dependency graph, selected NLS triplet, generation-specific
loader extension, ARC disk metadata, final EFI memory-map descriptor buffer,
or initialized PCR/PRCB, stack and page-table state. Its
`ExitFirmwareAndHandoff` callback also returns `Rc95LdrUnsupported`; it does
not call a kernel entry point or exit firmware prematurely. The exported
wrapper is not yet connected to a selected BCD/RC95LDR boot-entry dispatcher;
that caller must provide the resolved request and ARC system-volume path.

A separate RC95-only native input provider now exists in
`rc95nativeprovider.c` / `include/rc95ldrrc95nativeprovider.h`. It does not
include or call FreeLoader APIs. A BIOS/UEFI platform can attach it with
`Rc95LdrAttachRc95NativeProvider`, using a dedicated
`NativePreparationContext` rather than the filesystem platform context. It
allocates RC95 stack, loader heap, per-CPU/scheduler/CPU/architecture records,
register-frame, page-table arena, NLS/ARC metadata workspace, and native
loader-block storage; it copies BIOS firmware state or joins the UEFI final-map
allocation ledger. The common `Rc95LdrPrepareNativeLoaderBlock` remains called
once by `osload`, not by the provider callback.

This provider is an integration boundary, not a turnkey firmware backend. It
requires a real firmware-native physical-memory implementation, a CPUID/CPU
probe, and a page-table initializer that maps usable RAM and all image/loader
extents. It currently supports matching I386 and AMD64 builds only. PowerPC
and ARM require their own page-table and transfer backends. The existing
FreeLoader adapter does not install this provider; a native BIOS/UEFI frontend
must explicitly construct the provider context and attach it for an RC95 boot
entry.

The orchestrator verifies kernel identity whenever a provider returns a
request, allocates the large preparation result outside the firmware stack,
and only calls `ExitFirmwareAndHandoff` when `HandoffReady` is true and the
handoff's kernel and loader-block addresses match the transaction. Returning
error paths clear result fields and release the mapped kernel and adapter
resources. A successful PE map alone is never treated as a callable kernel
entry point.
The portable hive driver callback receives the registry ImagePath
verbatim. It supports literal `REG_SZ` paths only; `REG_EXPAND_SZ` is rejected
rather than passed to the image provider unresolved. Environment expansion
(such as `%SystemRoot%`), NT device-prefix translation
(`\\SystemRoot\\`, `\\??\\`) and volume/device-path translation remain
the responsibility of the platform adapter. A missing ImagePath uses the
service-name fallback under `System32\\drivers\\`. The FreeLoader
Configuration Manager adapter instead receives the shared CM driver's
resolved `FilePath`.

A bounded, allocation-free dependency-graph primitive is available in
`serviceorder.c`. It accepts adapter-resolved prerequisite/dependent index
pairs, rejects out-of-range, self and cyclic edges (repeated identical edges
are redundant), and produces a deterministic stable topological order using
the current candidate order as its tie-breaker. Its unit test covers ordering,
cycles, duplicate edges, invalid indices and insufficient workspace.

The portable SYSTEM-hive enumerator preflights the complete Services tree,
collects at most 64 boot-start driver candidates without invoking the caller,
and exposes `Rc95LdrHiveEnumerateBootDriversWithWorkspace` so firmware callers
can supply persistent or heap-backed scratch instead of consuming roughly
50 KiB of stack through the compatibility wrapper. It reads the current
control set's `Control\ServiceGroupOrder\List` and `Control\GroupOrderList`
values and applies stable group/tag ordering before dependency resolution.
Listed groups precede unlisted groups; listed tags precede unlisted tags
within the same group; original hive order breaks ties. Missing order keys
preserve stable input order.

For `DependOnService`, the validation-only pass checks every reference against
the complete Services key set, including references declared by non-boot
services. After boot-start candidates are collected and group/tag ordered,
the resolver adds prerequisite edges for dependencies that are also boot-start
kernel or file-system drivers. A dependency that is missing, self-referential,
or outside the boot-start candidate set fails closed. The stable topological
sort runs before the first consumer callback, so malformed metadata, missing
targets and cycles cannot expose a partial driver list.

For `DependOnGroup`, the portable reader currently validates the UTF-16LE
`REG_MULTI_SZ` encoding and rejects malformed names, but returns
`Unsupported` for a valid non-empty list. Group dependency expansion is not
implemented; it must follow verified SCM semantics rather than guessing that
a group dependency means either one arbitrary member or every member. The
in-tree FreeLoader Configuration Manager adapter continues to use
`CmpSortDriverList` and `CmpResolveDriverDependencies`.

The portable service enumerator validates `DependOnService` and
`DependOnGroup` for every key in the Services tree, including non-boot
services, during the validation-only preflight. Each value must be a
well-formed UTF-16 `REG_MULTI_SZ`; malformed encodings fail as corrupt data.
Lists larger than the portable reader's fixed stack buffer return
`Unsupported` rather than being misclassified as corrupt. The preflight runs
before the Start/Type filter, so dependency metadata on a non-boot service
cannot escape validation.

Dependency names are constrained to single ASCII components: control
characters, path delimiters, and the dot-directory names `.` and `..` are
rejected. `DependOnService` references are resolved against the Services tree
and boot-start candidate set, then topologically ordered before callbacks.
A missing target, a target outside the boot-start driver set, a
self-dependency, or a cycle fails closed. `DependOnGroup` is syntax-checked
but a valid non-empty list remains unsupported pending verified SCM group
expansion semantics. The byte-level `REG_MULTI_SZ` validator is allocation
free and validates the complete list after detecting unsupported non-ASCII
names so malformed trailing data cannot be masked. Its direct regression
coverage includes empty and multi-entry lists, embedded empty elements, path
delimiters, dot components, non-ASCII entries, and missing terminators. The
in-tree Configuration Manager adapter continues to use `CmpSortDriverList`
and `CmpResolveDriverDependencies`.

The portable service enumerator also validates SCM metadata domains before
classifying a service: `Start` must be 0 through 4, `ErrorControl` must be
0 through 3, and `Type` must be zero (only when absent) or one of the defined
service-type values (kernel driver, file-system driver, adapter, recognizer,
own-process service, or shared-process service). Only `Start=0` kernel and
file-system drivers are returned as boot-driver candidates. Invalid values
fail the complete validation pass before any callback receives a candidate.

The portable SYSTEM hive reader validates every VK record's inline length or
external data-cell extent while scanning a key's complete value list, even
when the requested value appears earlier or the malformed record has another
name. Unsupported large-data encodings remain rejected by the bounded cell
reader. Boot-driver enumeration also performs a validation-only pass over all
service records and their consumed values before invoking the caller's first
callback; this prevents malformed trailing service metadata from exposing a
partial candidate set. VK value-name flags are restricted to the defined
compressed-name bit, and a synthesized driver path may use only a single
service-name filename component (no separators, drive prefix or dot-directory
component). Empty literal ImagePath values are rejected rather than treated as
loadable paths.

The version-4 `RC95LDR_KERNEL_HANDOFF_CONTEXT` and
`Rc95LdrValidateKernelHandoff` now provide a preflight contract for kernel,
HAL, loader block, memory map, firmware, stack, page tables, configuration,
NLS, ARC, extension, architecture and CPU state. This validator is not a
transfer stub. No builder in this stage transfers control to a kernel. A
returned preparation success means only that a bounded buffer containing the
selected ABI-shaped structures was produced; it is not proof that the target
kernel can boot from it.


## New inventory and architecture preparation boundary

The NT preparation path now requires an inventory provider and uses it to
construct the configuration tree automatically. The provider supplies a
physical-memory mapping callback and PCI configuration-space callback. ACPI
RSDP/RSDT/XSDT checksums and SDT lengths are validated; present PCI functions
are enumerated into bounded component descriptors. Current limits are 32 ACPI
tables, 64 PCI functions and 64 total configuration components. PCI MCFG
multi-segment enumeration, bridge-window/resource decoding, SMBIOS inventory
and complete CM resource-list encoding are not yet implemented.

NLS and ARC metadata are now copied into a separate resident workspace. The
NLS block points to the copied ANSI/OEM/Unicode tables; ARC disk information
contains linked MBR/GPT signature records and stable ARC device-name strings.
The NT and RC95 loader blocks receive those pointers. The metadata workspace
must remain resident.

The architecture preparation layer now emits explicit I386, AMD64, PowerPC
and ARM register-frame records and validates supplied stack, page-table root,
PCR and PRCB extents. It requires the PCR/PRCB and page-table objects to be
provided by a target-specific platform/kernel adapter. It does not synthesize
the private, OS-generation-dependent KPCR/KPRCB contents or page-table
hierarchies. The handoff mask now separately requires PCR, PRCB and register
frame completion, so a missing object cannot be hidden behind a generic
architecture-state pointer.

No `LOADER_PARAMETER_EXTENSION` prefix is emitted in this revision. The complete
generation-specific record (network/headless/SMBIOS/ACPI, firmware descriptor
and persistent-data lists, boot entropy, hypervisor, API-set, debug and
code-integrity fields) must be implemented and initialized before the extension
requirement can be marked complete. BIOS firmware state also remains
incomplete; only validated UEFI system-table/runtime-service/map inputs can
currently satisfy that bit. No kernel transfer stub is enabled.

## BIOS firmware-state snapshot

The BIOS snapshot ABI is defined in bios/include/rc95ldrbiosfirmwarestate.h.
Rc95LdrBiosCaptureFirmwareState captures the interrupt vector table, BIOS
Data Area, boot drive, selected display/memory fields, and a bounded validated
E820 range array into caller-preallocated storage. The snapshot is attached to
the common handoff through BiosFirmwareStateAddress,
BiosFirmwareStateSize, and BiosFirmwareStateVersion; these fields are
disjoint from the EFI system-table/runtime-services/memory-map fields.

For native preparation, the BIOS platform provider must capture the state
before calling the preparation transaction, supply its kernel-visible address
and version, and add a matching RC95LDR_ALLOC_BIOS_FIRMWARE_STATE physical
allocation to NativeAllocations. The memory-map builder then reserves that
allocation as loader memory. If the snapshot is absent, malformed, or not
backed by the matching reservation, BIOS handoff preflight remains incomplete
or fails closed. The capture API does not allocate memory, invoke BIOS
interrupts from protected mode, or claim that the final BIOS-to-kernel
processor transition is complete.
