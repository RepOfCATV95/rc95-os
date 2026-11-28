# RC95LDR native AMD64 final handoff

This implementation adds the final AMD64 transfer primitive while reusing the
existing BIOS, UEFI, WINLOAD, native-preparation, memory-map and module
infrastructure. It does not define a new loader-block ABI.

## Shared WINLOAD contract

Cold-boot preparation continues through
`Rc95LdrPrepareNativeLoaderBlock`. The selected OS profile owns construction
of its loader block and metadata. The resulting
`RC95LDR_KERNEL_HANDOFF_CONTEXT` is validated with
`Rc95LdrValidateKernelHandoff` before the firmware callback is entered.

The appended `ExitFirmwareAndValidatedHandoff` platform callback receives the
complete mutable handoff. AMD64 must use this callback; WINLOAD will not fall
back to the older entry-point/loader-block-only callback for AMD64. Existing
i386 providers retain the original callback and module handoff ABI.

## BIOS

The AMD64 BIOS startup binds
`Rc95LdrBiosAmd64TransferHandoff` when the caller has not supplied a
provider-specific validated callback. The BIOS provider must already have
entered long mode, completed its provider-owned transition, and ensured that
the target address space maps the handoff frame, stack, kernel, HAL and loader
block. No legacy BIOS interrupt is invoked by the final transfer.

## UEFI

`Rc95LdrUefiAmd64ValidatedHandoff` is the WINLOAD callback adapter.
Its `RC95LDR_UEFI_AMD64_HANDOFF_CONTEXT` must be embedded in the platform
provider's composite context and contain the validated image context plus a
preallocated, identity-mapped final memory-map buffer.

The provider initializes `RC95LDR_UEFI_AMD64_HANDOFF_CONTEXT` with
`Rc95LdrUefiInitializeAmd64HandoffContext` before WINLOAD starts. This
allocates a 256-KiB EFI_LOADER_DATA buffer and records its exact physical extent
in the provider-wide `RC95LDR_NATIVE_ALLOCATION_SET`. The same allocation set
must track every native allocation made by that provider; WINLOAD publishes
the complete set into `RC95LDR_NATIVE_PREPARE_REQUEST` so the final OS memory
map reserves the buffer.

The UEFI startup record carries that context in
`FirmwareHandoffContext`. `Rc95LdrUefiWinloadEntry` copies the platform
record, invokes `Rc95LdrUefiBindAmd64Handoff`, and dispatches the full context
through the dedicated `ExitFirmwareContext` slot. This does not overwrite
the ordinary platform context used by filesystem, allocation and preparation
callbacks. An optional idempotent quiesce callback can be attached with
`Rc95LdrUefiSetAmd64HandoffQuiesce`; it closes provider-owned file/protocol
handles before the final memory-map capture. No boot-service calls are made
between the final GetMemoryMap and ExitBootServices. If WINLOAD returns before
ExitBootServices succeeds, the entry releases the preallocated map pages and
removes them from the allocation ledger.

The UEFI frontend attaches it with the single provider call
`Rc95LdrUefiAttachAmd64HandoffContext(&Image, &NativeAllocations, &Startup, &FinalHandoff)`.
The platform's allocation callbacks and native-preparation request builder
must use that same `NativeAllocations` ledger for every loader-owned
allocation; WINLOAD overwrites the request's allocation list with this
authoritative ledger and rejects an untracked/missing UEFI map allocation.
The startup record and handoff context must remain alive until
`Rc95LdrUefiWinloadEntry` returns (which happens only on a pre-exit failure).

The adapter reuses `Rc95LdrUefiExitBootServices`: it captures the final map,
retries a stale map key as that helper specifies, calls ExitBootServices,
publishes the final map/system-table/runtime-service addresses into the
handoff, validates the completed context again, and transfers. Once
ExitBootServices succeeds, any validation/transfer failure is fatal and
halts; it never attempts to return to dead boot services. The map buffer must
be reserved before this callback and must remain mapped under the target CR3.

## AMD64 transfer preconditions

`Rc95LdrAmd64TransferKernel` always calls
`Rc95LdrValidateKernelHandoff` before entering assembly. It additionally
checks canonical addresses, CR3 alignment, register-frame layout/alignment,
the kernel entry and loader-block argument, and stack bounds/alignment.
Compile-time offset checks keep the C register frame synchronized with the
assembly offsets.

The assembly disables interrupts, loads the supplied CR3, switches to the
prepared stack, restores the prepared general-purpose register image, and
transfers to the validated RIP. The target GDT and kernel selectors must
already be installed. The frame, stack, kernel and loader block must all be
mapped by the new page tables. The transfer stub does not construct a PCR,
PRCB, page tables, HAL, hive, metadata or any OS loader block.

## OS-family separation and compatibility

The transfer code is architecture-only. It does not branch on, reinterpret or
merge Windows NT legacy, modern Windows, ReactOS or RC95-OS loader structures.
Those remain the responsibility of their existing WINLOAD profile and
native-preparation paths. In particular, RC95-OS preparation remains distinct
from Windows NT preparation. The R95M module handoff ABI and i386 BIOS/UEFI
entry paths are unchanged.
