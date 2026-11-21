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
3. enumerates `Services` entries with `Start=0` or `Start=1` and kernel /
   file-system driver types;
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
- registry dependency and `GroupOrderList` processing for exact boot-driver
  ordering (the current enumeration discovers Start/Type/Group/Tag but does
  not yet implement full dependency/group ordering);
- complete versioned EFI firmware information (including the Win8 resource list and Win8.1+ memory-map fields) and architecture-specific
  kernel/HAL startup state (the current architecture adapter validates and
  consumes supplied state; it does not create PCR/PRCBs, page tables or CPU
  register frames);
- final memory-map reservation and descriptor reconciliation after every
  allocation;
- BIOS/UEFI transition code, including UEFI ExitBootServices with the final
  memory-map key and architecture-specific register/stack setup.

`LoadAndBootWindows` still does not orchestrate this API: the caller must
supply the loaded kernel/HAL records, resident SYSTEM hive, final-address
loader-block allocation, and a platform boot-driver image-loader callback.
The current hive driver callback receives the registry ImagePath verbatim.
Environment expansion such as `%SystemRoot%` and device-path translation
remain the responsibility of the platform adapter.

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
