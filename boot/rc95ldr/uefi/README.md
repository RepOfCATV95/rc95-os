# RC95LDR native UEFI entry and module services

RC95LDR uses the repository's EDK2-compatible SDK declarations from
`sdk/include/reactos/edk2`. The module service adapter defines only the
Simple File System and File Protocol ABI members it consumes; it does not
redeclare the system-table or boot-services table layouts.

## Entry validation and child image loading

`Rc95LdrUefiInitializeImage` validates the incoming `EFI_HANDLE`,
`EFI_SYSTEM_TABLE` signature/revision/header extent, the
`EFI_BOOT_SERVICES` signature/revision/header extents and required service
pointers, then obtains and validates this image's
`EFI_LOADED_IMAGE_PROTOCOL`. It rejects truncated tables, missing protocols,
mismatched system-table ownership, oversized images and pointer-range overflow.

`Rc95LdrUefiLoadAndStartImage` validates a bounded, terminated device path,
uses firmware `LoadImage`, verifies the child's Loaded Image protocol and
system-table ownership, attaches optional bounded load options, and invokes
`StartImage`. If the child returns, its exit-data pool is freed and its image
is unloaded. A successful OS handoff is expected not to return.

## Shared module services

`Rc95LdrUefiInitializeModuleServices` constructs the same version-1
`RC95LDR_MODULE_SERVICES` (`R95S`) ABI used by the BIOS module provider.
It opens `EFI_SIMPLE_FILE_SYSTEM_PROTOCOL` on the child image's
`LoadedImage->DeviceHandle` (using the parent image as the protocol agent),
opens that volume root, and supplies:

- Bounded UTF-16, read-only file open (16 simultaneous handles).
- File-size discovery through `EFI_FILE_INFO`.
- Checked offset reads using `SetPosition` followed by `Read`, rejecting
  overflow, out-of-file ranges, and short reads.
- UEFI `AllocatePages`/`FreePages` wrappers with page rounding, power-of-two
  alignment, and a bounded allocation ledger (64 tracked allocations).
- Allocation-extent validation against the ledger; UEFI identity-mapped
  physical/virtual extents must match.

The V2 provider also uses `LocateHandleBuffer(ByProtocol)` to enumerate EFI
Block I/O handles, obtains each `EFI_BLOCK_IO_PROTOCOL` and its Device Path
protocol through `HandleProtocol`, and adapts firmware `ReadBlocks` into
the loader partition-discovery interface. It bounds the handle inventory,
validates device-path node lengths and termination, enforces each media
`IoAlign`, and uses page-aligned scratch storage for GPT/MBR and filesystem
probing. The V2 callbacks expose block counts/sizes, validated GPT/MBR
partition records, and read-only block reads. MBR partition type bytes are
retained in the V2 wire record. Empty device/partition inventories are valid;
malformed media, misaligned reads, unsupported alignment requirements, and
truncated device paths fail closed.

The mandatory service-table `Quiesce` callback closes outstanding child file
handles, the volume root, the opened Simple File System protocol, the block
handle inventory pool, and temporary discovery scratch pages before
`ExitBootServices`. It leaves tracked page allocations owned by the child so
they can be included in the final memory map. After quiescing, provider
operations are rejected. If the child returns, parent cleanup releases tracked
pages and retries any handle closes that previously failed.

The provider rejects malformed paths, path traversal components, unsupported
alignments, exhausted handle/allocation tables, and stale or forged file
handles. `Rc95LdrUefiDestroyModuleServices` closes any remaining handles and
releases tracked page allocations on a returning child path.

`Rc95LdrUefiLoadAndStartModule` now constructs the service provider and
service table before `StartImage`, attaches both pointers and the exact table
size to the R95M handoff, and keeps the provider, table, and parent handoff
alive for the entire synchronous child call. When the child returns, the
provider is torn down before the child image is unloaded. The child-side
`Rc95LdrUefiGetModuleHandoff` validates both R95M and R95S identity/version/
size/callback requirements before returning the handoff.

This provider is deliberately synchronous and boot-services-owned. Children
must finish all file and page-service use before `ExitBootServices`; these
callbacks are not runtime services and must not be called after boot services
are exited. WINLOAD must call the service table's `Quiesce` callback after
its last file operation and before acquiring a fresh memory map and performing
the `ExitBootServices` map-key transaction.

## PE/COFF entry and single native boot pipeline

`Rc95LdrUefiImageEntry(EFI_HANDLE, EFI_SYSTEM_TABLE *)` is the PE/COFF
application entry point. The final EFI executable links
`rc95ldr_uefi_image_entry`, sets its PE/COFF entry symbol to
`Rc95LdrUefiImageEntry`, and defines exactly one writable, image-lifetime
instance of `RC95LDR_UEFI_IMAGE_BOOT_CONFIG` named
`Rc95LdrUefiImageBootConfiguration`.

The image configuration is deliberately limited to:

- Native architecture and caller-owned discovery scratch/output storage:
  partition records, BCD candidates, and the BCD hive buffer.
- The `RC95LDR_WINLOAD_STARTUP` and result records.
- Selected-installation/ESP result storage and optional text UI.

The startup record is where the final image supplies the platform-dependent
WINLOAD contract: boot context, platform operations, request construction and
release callbacks, memory-map/allocation services, and architecture-specific
kernel-handoff preparation/transfer. Those callbacks must remain valid for
the complete synchronous boot transaction. The final image does **not**
provide a second disk enumerator, GPT parser, ESP selector, BCD walker, NTFS
mount implementation, or independent WINLOAD dispatcher.

After validating the configuration, `Rc95LdrUefiImageEntry` initializes the
UEFI image context and calls exactly one discovery-to-WINLOAD orchestration
function: `Rc95LdrUefiNativeDiscoverAndStartWindows`. That native transaction
owns UEFI Block I/O and Device Path inventory, partition enumeration,
ESP/BCD resolution, selection of the Windows OS volume, native NTFS mounting
and file-provider binding, WINLOAD dispatch, and reverse-order teardown of
mounts and firmware inventory on every returning path. A successful kernel
handoff is non-returning; a returned status is translated to an EFI status.

The configuration object and all supplied buffers are image-owned. They must
be static or otherwise remain valid for the entire synchronous call; the
native discovery context and its temporary firmware resources are owned and
released by the native pipeline. The shared library intentionally does not
invent the final platform's memory-map policy or kernel transition code:
those remain explicit WINLOAD platform callbacks, not duplicate discovery
logic.

## Current integration boundary and validation

UEFI storage discovery, GPT/MBR partition enumeration, ESP/BCD discovery,
native NTFS access, NTFS provider binding, and the discovery-to-WINLOAD
dispatch are implemented in the native path. They are not outstanding
image-integration tasks.

The remaining platform work is downstream of discovery: the final EFI image
must provide and validate its WINLOAD memory-management/memory-map policy,
complete target-specific loader metadata and architecture state, and the
non-returning kernel transfer. In particular, the final handoff still needs
the target-version loader extension and all required loader metadata, final
memory-map reservation/descriptor reconciliation, architecture-specific
stack/page-table/CPU state, and a correct `ExitBootServices` plus kernel
transfer sequence. The low-level `Rc95LdrUefiExitBootServices` primitive
handles caller-preallocated map storage and stale-key retry; it is not by
itself the complete kernel handoff.

Host tests cover V1/V2 service-table construction and V1-prefix preservation,
synthetic EFI Block I/O and Device Path adapters, partition translation and
block reads, file operations, path bounds, allocation alignment/extent
checks, cleanup, handoff population, and child unload failure. Separate
native-discovery and image-entry tests cover their respective contracts and
negative paths. Test registration is not evidence that tests have executed.
Firmware validation still requires QEMU/OVMF and physical firmware testing,
including i386/AMD64 builds and the full RC95LDR-to-WINLOAD-to-kernel chain.

The UEFI adapter does not call `ExitBootServices` from the discovery entry
shim. WINLOAD's final platform handoff owns the final memory-map/key
transaction.
