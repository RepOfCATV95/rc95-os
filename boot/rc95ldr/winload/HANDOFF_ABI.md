# RC95LDR native loader ABI and kernel handoff boundary

## ABI source and target-width rule

The NT-compatible structures in `include/rc95ldrntabi.h` are layout transcriptions of this repository's ReactOS definitions:

- `sdk/include/reactos/arc/arc.h`: `LOADER_PARAMETER_BLOCK`, firmware information, memory descriptors, boot-driver entries, and architecture union.
- `sdk/include/ndk/ldrtypes.h`: `LDR_DATA_TABLE_ENTRY` and the Unicode/list members it embeds.

The loader-block pointer fields are `uintptr_t`-sized. **Build RC95LDR for the same pointer width and architecture as the kernel being loaded.** The builder rejects I386/PowerPC targets from a 64-bit build and AMD64 from a 32-bit build. A cross-width loader must use a separate target-layout serializer; it must not cast a host pointer into a target pointer.

ReactOS's published `LOADER_PARAMETER_BLOCK` architecture union contains I386, Alpha, IA64, PowerPC, and ARM arms. It does not define a separate AMD64 arm; the AMD64 builder leaves that union zeroed, as required by the referenced definition. PowerPC populates the PowerPC arm. Machine-specific `BootInfo` is still platform handoff work.

## Version selection

| Target | Loader-block layout |
|---|---|
| NT 4.0 | V4: three list heads, SetupLdrBlock, Spare1 |
| Windows 2000–Server 2003 | V5: Extension replaces Spare1 |
| Vista | V6: firmware-information tail, no Win7 version prefix |
| Windows 7 | V61: OS version/size prefix |
| Windows 8 | V62: EarlyLaunch/CoreDriver lists, KernelStackSize, EFI resource list |
| Windows 8.1 | V63: EFI memory-map fields |
| Windows 10/11 | V10: CoreExtensions/TPM lists and V63 firmware record |

The Windows 10/11 version selector uses the NT 10.0 ABI family. This is based on the ReactOS header layout currently in the repository; it is not a claim that every undocumented Windows servicing-build change has been independently verified. Unsupported NT versions fail closed.

ReactOS remains on its separate `{rosboot} -> rosload.exe` route. `Rc95LdrBuildReactOsLoaderBlock` is available to ReactOS-side integration when its exact `NTDDI_VERSION` is selected; RC95LDR does not bypass `rosload.exe` or hand its own block to a ReactOS kernel.

RC95-OS uses its separately versioned `RC95LDR_RC95_NATIVE_LOADER_BLOCK`. It is not an NT structure and is never passed as one.

## Loader extension and configuration-tree preparation

`loaderstate.c` constructs a caller-buffer-backed `CONFIGURATION_COMPONENT_DATA`
tree using the ReactOS `CONFIGURATION_COMPONENT` and component-data layouts.
Input components are ordered parent-before-child, have exactly one root, and
carry their identifier and optional configuration-data bytes in the same
stable allocation. The NT loader-block builders now receive and publish the
resulting `ConfigurationRoot` pointer.

The extension builder currently selects the correct early prefix for pre-Windows-7 versus Windows 7+ and links it into the loader block. **It is not yet the complete `LOADER_PARAMETER_EXTENSION`.** The remainder of the ReactOS reference structure includes generation-specific headless/network/SMBIOS/ACPI pointers, firmware descriptor lists, performance and persistent-data lists, TPM/boot entropy, hypervisor state, boot flags, API-set metadata, debug-device state, and Windows 10 code-integrity fields. These tails still require explicit target-version definitions and initialization; the extension handoff requirement therefore remains deliberately incomplete.

The native preparation request now accepts a `LoaderStateInput` for NT
profiles. Its caller supplies the component inventory and a persistent
workspace buffer; preparation builds the tree/extension before invoking the
NT loader-block builder. The workspace must remain resident through kernel
startup.

## Architecture-specific handoff preparation

`archprepare.c` consumes platform-created stack, page-table root, CPU-state,
and architecture-state addresses. It checks target pointer width, stack
alignment (I386 4, AMD64 16, PowerPC 16, ARM 8 bytes), page-table alignment,
and address bounds before recording the state in the handoff context. It
sets the stack/page-table/CPU/architecture requirement bits only after these
checks. It does **not** synthesize page tables, PCR/PRCB structures, exception
vectors, register frames, or processor mode transitions. The handoff data model
no longer has generic PCR/PRCB address fields: I386 describes a PCR plus an
embedded PRCB offset/extent, while AMD64 describes a PCR and a separately
allocated PRCB. These are typed extent contracts only; the actual KPCR/KPRCB
builders and their OS-generation layouts remain unimplemented and must be
provided before the processor-control requirement can be satisfied.

## SYSTEM hive preparation

`systemhive.c` validates the REGF header/checksum, HBIN extents, allocated cell boundaries, NK/VK cells and LI/LF/LH/RI subkey lists. It resolves `Select\Current` to the active `ControlSet00x`, walks `Services`, and extracts `Start`, `Type`, `ErrorControl`, `Tag`, `Group`, and `ImagePath`. The native preparation callback loads only boot/system-start kernel and filesystem driver service types (Start 0/1, Type 1/2), then adds the resulting modules and registry paths to the selected profile's loader block.

The image loader is supplied by the caller because filesystem path resolution, PE mapping, relocations, imports, and allocation policy belong to the existing winload image-loading layer. The callback must return a mapped image base, image size, entry point, and stable path.

Current limitations: service enumeration follows hive subkey-list order. `GroupOrderList`, `Tag` ordering, `DependOnService`/ `DependOnGroup` topological ordering, `CriticalDeviceDatabase`, REG_EXPAND_SZ environment expansion, and driver failure policy are not yet implemented. These must be completed before treating the driver list as production-ready.

## Preparation and handoff contract

`Rc95LdrPrepareOperatingSystemNative` now calls the SYSTEM hive reader, resolves and loads boot drivers through the image-loader callback, chooses the profile-specific builder, and publishes a `RC95LDR_KERNEL_HANDOFF_CONTEXT`. The context carries kernel/HAL entry data, loader-block address/size, target architecture, typed I386-or-AMD64 processor-control extents, firmware system table/runtime services, and EFI memory-map information.

`Rc95LdrValidateKernelHandoff` is a **preflight gate only**. It rejects invalid image ranges, pointer-width mismatches, missing UEFI map data, and any incomplete required-state mask. No architecture transfer routine is called.

Direct kernel transfer remains deliberately disabled. Remaining work includes implementing complete generation-specific `LOADER_PARAMETER_EXTENSION` records; expanding inventory to MCFG multi-segment and bridge/resource decoding; generating version-correct PCR/PRCB objects and real page tables from final physical-memory reservations; finalizing firmware transition state (including UEFI ExitBootServices and BIOS state); and adding architecture transfer stubs (I386, AMD64, PowerPC, ARM as supported). Test register/stack/interrupt conventions against each target kernel before enabling transfer.

Do not set `HandoffReady` manually. It is computed from `Rc95LdrValidateKernelHandoff`; until all required fields and architecture transition code are implemented, the expected result is false.
