# RC95-OS standalone native input provider

`rc95nativeprovider.c` is the RC95-OS native-input provider. It has no
FreeLoader headers, symbols, page allocator, ARC file service, or memory-manager
dependency. The caller supplies the firmware-native physical-memory operations
(`RC95LDR_PHYSMEM_OPS`), the selected installation's already validated kernel,
HAL and SYSTEM hive, NLS files, ARC disk identities, firmware memory ranges,
and hardware access operations.

The provider ABI is version 4; the versioned input now includes an explicit I386 PAE virtual-window reservation. The provider performs a native CPUID probe when `QueryCpu` is not overridden. It records the processor vendor/signature, basic and extended CPUID leaves, physical/virtual address widths, and paging mode. The summary includes CPUID, FPU, TSC, MSR, PAE, CMPXCHG8B (`CX8`), APIC, SEP, MTRR, PGE, CMOV, MMX, FXSR, SSE generations, NX, long mode, 1 GiB pages, RDTSCP, AVX/AVX2, SMEP, SMAP, INVPCID, CMPXCHG16B, AES, XSAVE, and RDRAND. Raw CPUID leaf masks are retained for additional features. AVX-family flags indicate hardware support only; the kernel must check OSXSAVE and XCR0 before executing AVX instructions.

AMD64 is a distinct profile and requires long mode, PAE, CMPXCHG8B, FXSR, and SSE2. I386 selects PAE only when CPUID advertises it, and PAE mode also requires CMPXCHG8B. I386 without PAE is restricted to a 32-bit physical-address space. I386 with PAE can map RAM up to the reported physical-address width (up to 52 bits). The 32-bit loader's own temporary allocations remain below 4 GiB because its early pointers are identity-mapped; high physical RAM is mapped into the kernel's 32-bit virtual address space. AMD64 has no I386 4-GiB ceiling.

## I386 PAE high-memory virtual window

I386 PAE callers must provide a nonzero, page-aligned PaeWindow.VirtualBase
and PaeWindow.Length. The exclusive end must not exceed 4 GiB. This interval
is reserved exclusively for high physical RAM aliases and must not overlap
any loaded module image or any provider allocation's identity-mapped virtual
alias. Rc95LdrBuildI386PaeWindowMappings validates those collisions and emits
at most RC95LDR_PAE_WINDOW_MAPPING_MAX contiguous mappings. Only firmware
ranges marked available are considered; each physical range is clipped to
the reported CPU physical-address width and page aligned. The mapping list is
packed sequentially into the virtual window. Insufficient window capacity or
mapping capacity fails closed. BuildPageTables receives the exact window and
bounded mapping array and must install every mapping without relocating it.
Non-PAE I386 and AMD64 must supply an empty PAE window.

## Allocated objects

The provider is installed directly as WINLOAD's `BuildNativePreparation` /
`ReleaseNativePreparation` pair using `Rc95LdrRc95NativeBuildPreparation` and
`Rc95LdrRc95NativeReleasePreparation`. Call `Rc95LdrAttachRc95NativeProvider` for the selected RC95 boot entry, with
a persistent `RC95LDR_RC95_NATIVE_PLATFORM_CONTEXT`; it installs the two
callbacks and sets `Platform.NativePreparationContext`, separate from the
filesystem/firmware `Platform.Context`. The provider builds the request but
does not call `Rc95LdrPrepareNativeLoaderBlock` itself: `osload` performs that
common transaction exactly once.

The provider allocates and retains, through the supplied physical-memory
backend:

- a 64 KiB kernel stack;
- a 9 MiB page-table storage arena, initialized by the mandatory architecture
  page-table callback and extended by the provider's PAE mapping installer;
- a 64 KiB RC95 loader heap containing the RC95 per-CPU record, scheduler
  record, architecture record, CPU feature record and architecture register
  frame;
- a 256 KiB resident metadata/NLS workspace; and
- a 256 KiB RC95 native loader-block workspace.

For BIOS it also allocates a persistent firmware-state snapshot and copies the
validated snapshot supplied by the BIOS firmware provider. For UEFI it records
the caller's preallocated final memory-map buffer in the native allocation
ledger. The RC95 configuration tree is separately allocated by
`Rc95LdrBuildRc95LoaderState` using the same physical-memory backend.

All allocations are entered in the native allocation ledger with explicit
loader memory types before the common native-preparation routine builds the
final memory map. The result owns its allocations until
`Rc95LdrRc95NativeReleasePreparation` (or
`Rc95LdrReleaseRc95NativeInput`) is called. On rollback, release restores the
UEFI shared allocation ledger to its pre-provider snapshot before freeing the
provider's pages, so failed preparation cannot leave stale physical ranges in
the firmware context. The callback updates the kernel module record from the
exact image/physical extent supplied by `osload` and attests that extent with
the platform allocator validator. The caller must keep the result
object itself at a stable address until release; it contains the native
preparation result and its loader-state ownership records.

## Mandatory platform callbacks

`QueryCpu`, when supplied, must return actual processor identity, raw CPUID
masks, paging mode, and physical/virtual address widths. Otherwise
`Rc95LdrQueryX86Cpu` performs the native hardware probe. A fabricated feature
mask is not accepted as a hardware probe. The page-table callback receives the
complete CPU information record and must construct the selected I386 non-PAE,
I386 PAE, or AMD64 page-table format.

`BuildPageTables` must initialize real page tables in the supplied storage.
It must identity-map low firmware-usable RAM (including free pages from which
the common native preparer can later allocate boot drivers). For I386 PAE,
physical RAM above 4 GiB is supplied through the explicit collision-checked
window and bounded physical-to-virtual mapping list; install every entry. It must map
every loaded module from its loader virtual image base to its physical backing,
and map every provider-owned writable allocation. It must return a page-aligned
physical root inside the supplied page-table arena. Returning an empty,
uninitialized, or guessed root is a provider failure.

The physical-memory backend must return contiguous page allocations whose
writable mapping is the identity-mapped physical address. For I386, provider
allocations remain below 4 GiB even when PAE is active, because the current
early loader objects are directly addressed by 32-bit pointers; PAE extends
the physical RAM that the kernel can map, not the loader's pointer width. This provider
intentionally rejects a different virtual alias: the early RC95 page-table
contract and the loader-block pointers are identity based. The SYSTEM hive and
UEFI final-map buffer must likewise be identity mapped at their declared
physical bases. The backend must honor below-4-GiB allocation requests for
I386. The PCI/firmware inventory
callbacks must read actual hardware and validated firmware tables.

## Supported boundary

The first implementation supports native I386 and AMD64 preparation when the
build's pointer width matches the target architecture. PowerPC and ARM return
`Rc95LdrUnsupported` until their own page-table and register-transition
providers exist. The provider never calls the kernel entry as a C function.

The provider returns success only when the common native preparation reports
`HandoffReady`. A successful allocation or a constructed loader block alone
does not enable transfer. The platform firmware frontend remains responsible
for the irreversible BIOS/UEFI transition and the non-returning architecture
transfer.
