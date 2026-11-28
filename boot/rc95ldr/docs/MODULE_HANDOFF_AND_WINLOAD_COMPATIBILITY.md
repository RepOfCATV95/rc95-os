# RC95LDR module handoff and WINLOAD compatibility policy

## Scope

RC95LDR is the bootloader for RC95-OS. Its module protocol is an internal,
versioned contract between RC95LDR, VENEER, and the RC95-OS WINLOAD build.
It is not a general-purpose loader ABI and is not a compatibility promise for
arbitrary Windows-derived or LiberNT loader binaries.

The supported module chain is:

```
BIOS or UEFI firmware
  -> RC95LDR
       -> VENEER (when the selected volume requires its services)
            -> RC95-OS WINLOAD
                 -> RC95-OS kernel
```

BIOS and UEFI may use different image-loading and entry-transfer mechanisms,
but must agree on module identity, architecture, firmware identity, structure
version, ownership, and lifetime rules.

## Handoff record

The current wire record is `RC95LDR_MODULE_HANDOFF` in
`core/include/rc95ldrmodule.h`. It carries:

- Signature, structure size, and protocol version.
- Module kind (RC95LDR, VENEER, or WINLOAD).
- Firmware kind (BIOS or UEFI) and CPU architecture (i386 or AMD64).
- Image base, mapped image size, and entry-point token/address.
- Optional service-table and parent-handoff ranges.

Consumers must validate the complete record before using any field. Validation
must reject unknown flags, nonzero reserved fields, unsupported versions,
wrong module/firmware/architecture identities, overflowed ranges, out-of-image
entry points, oversized images, and malformed parent records. All additions
and range checks must be overflow-safe. A pointer being numerically in range
does not by itself prove that its memory is readable; the platform adapter
must guarantee mappings and lifetime.

## Firmware-specific transfer rules

### BIOS

BIOS modules are PE images mapped by the RC95LDR BIOS module loader. The
loader validates the PE machine and image bounds, maps the image, constructs
the handoff, and calls the architecture transfer callback. That callback is
responsible for the actual i386 cdecl or AMD64 platform ABI transition and
must not guess or cast between incompatible function-pointer types.

The image, parent handoff, and service context must remain valid for the
entire duration of the transfer. If the child returns, the loader may reclaim
its image and temporary file buffer. A successful final kernel transfer is
normally non-returning; cleanup must not be assumed to run after that point.

A production BIOS chain is not complete merely because the callback type
exists. The platform must provide and test a real architecture-specific
transfer implementation, stack/alignment rules, register arguments, memory
mapping, and a defined return/error path.

### UEFI

UEFI firmware owns PE/COFF image relocation and entry-point invocation.
RC95LDR uses `LoadImage`, obtains and validates the child's Loaded Image
protocol, installs the handoff in `LoadOptions`, and invokes `StartImage`.
The handoff and all referenced data must remain valid for the complete
synchronous `StartImage` call. If the child returns, RC95LDR must release
owned allocations and unload the child; if unload fails, it must not leave
dangling pointers in the child's Loaded Image protocol.

UEFI children must validate the image handle, system table, boot-services
table, loaded-image protocol, load-options size, handoff version, and expected
module identity before using the parent context.

## Service-table contract and completion gate

The shared ABI is `RC95LDR_MODULE_SERVICES` in
`core/include/rc95ldrmodule.h` (signature `R95S`, version 1). It carries
firmware and architecture identity, a provider-owned context, and typed
callbacks for file open/read/close, page allocation/free, allocation
extent validation, and a mandatory `Quiesce` operation. The latter closes
firmware-owned handles before the final handoff while leaving child-owned page
allocations available for the final memory map. `Rc95LdrValidateModuleServices`
rejects truncated or oversized tables, wrong identities, unknown flags,
unsupported versions, and missing mandatory callbacks.

The BIOS PE loader constructs this table from its active
`RC95LDR_WINLOAD_PLATFORM` using typed adapter functions (not incompatible
function-pointer casts), places the table address and exact size in the
handoff, and keeps the table and provider context alive for the synchronous
child transfer. The service callbacks delegate to the RC95LDR-owned platform
provider. The UEFI PE loader now constructs the same table from the child image's `EFI_SIMPLE_FILE_SYSTEM_PROTOCOL`, `EFI_FILE_PROTOCOL`, and
`EFI_BOOT_SERVICES` interfaces (opened by the parent image as protocol
agent). It supplies bounded UTF-16 file opens, checked offset reads,
tracked page allocations with alignment, and allocation extent validation.
Aligned allocations retain the exact requested size and complete original EFI
page range in the provider's ownership ledger; an incorrectly sized free is
ignored, while freeing or destroying the provider releases that exact raw
range rather than attempting unchecked prefix/suffix trims. A
failed firmware `FreePages` call leaves the ledger entry intact so cleanup
can be retried. Quiesce is idempotent and retries failed file-handle or
protocol closes while retaining failed handles in the provider ledger; it
closes open file handles and the Simple File System protocol, and disables
further firmware-backed services
while leaving allocated pages tracked for the final memory handoff. The UEFI
handoff points to the parent-owned service table; both table and provider
context remain valid until the synchronous `StartImage` call returns.
Child-side handoff validation requires a valid R95S table matching the UEFI
firmware and requested architecture.

The handoff validator accepts a bounded external service-table range because
the table is parent-owned rather than part of the child PE image. Its lifetime
is limited to the child call. UEFI callbacks are boot-services-owned and must
not be called after `ExitBootServices`; WINLOAD must complete file and page
service operations, call `Quiesce`, and then acquire its final memory-map/key
transaction. If quiescing fails, the final handoff must fail closed.

The UEFI host service tests exercise file open/read/close, short reads,
out-of-range offsets, path traversal and path-length rejection, open-file
exhaustion, page-alignment and overflow handling, rejection of malformed firmware page
addresses, exact raw-allocation release, retry after failed `FreePages` calls
(including provider destruction), retry after failed file/protocol closes,
allocation-ledger exhaustion,
provider-initialization failure cleanup, idempotent quiesce, and rejection
of service use after quiesce. These are
host-side protocol tests; they do not replace OVMF or hardware validation.

The BIOS transfer path has i386 cdecl and AMD64 Windows-x64 assembly bridges.
The BIOS PE loader rejects a requested architecture that does not match the
native build target; the host-only injected transfer path remains available
for tests. The UEFI path uses firmware `LoadImage`/`StartImage` and a
validated `LoadOptions` handoff. Neither path is considered firmware-boot
validated until QEMU/OVMF and native BIOS integration tests complete.

Do not place an arbitrary firmware table pointer in `Services`. UEFI
firmware services remain accessed through the validated system-table context;
BIOS services are accessed only through RC95LDR's BIOS gateway. A service
table must never allow UEFI code to invoke BIOS interrupts or BIOS code to
invoke UEFI boot services.

The following are required before declaring the firmware-to-module handoff
fully production-ready:

1. Exercise native i386 and AMD64 BIOS assembly transfer on emulators, and
   UEFI `LoadImage`/`StartImage` with OVMF and real firmware.
2. Define and test a stable allocation/ownership model for the handoff,
   service table, and parent chain on both firmware paths, including
   provider behavior at the `ExitBootServices` boundary.
3. Add end-to-end RC95LDR -> VENEER -> WINLOAD tests for BIOS and UEFI.
4. Add negative tests for truncated records, invalid module kinds, wrong
   architecture/firmware, overflow, stale pointers, malformed paths,
   allocation exhaustion, short reads, and child-return cleanup.

Until all gates pass, describe the handoff as an implemented protocol
foundation or integration-in-progress, not as a fully validated production
boot chain.

## WINLOAD compatibility policy: LiberNT

RC95LDR **does not support regular builds of LiberNT**. This is an intentional
compatibility boundary, not a firmware-selection option and not a filesystem
limitation.

The WINLOAD supplied by regular LiberNT builds is incompatible with the
RC95-OS WINLOAD contract. RC95LDR supplies RC95-OS-specific startup state,
module identity, boot request semantics, platform services, loader-block
expectations, and kernel handoff requirements. A LiberNT WINLOAD built for
its own operating-system tree does not implement that complete RC95-OS
contract. Treating the two binaries as interchangeable can cause invalid
structure interpretation, ABI violations, incorrect ownership assumptions,
or an unsafe kernel handoff.

This is the same incompatibility boundary that applies to the defunct CATV 95
LiberNT fork. Renaming a binary, changing its file extension, or selecting
BIOS versus UEFI does not adapt its ABI.

### Required behavior

- Accept only WINLOAD images built for the matching RC95-OS module ABI and
  architecture.
- Validate module kind, firmware, architecture, protocol version, image
  bounds, and required service capabilities before transfer.
- Reject regular LiberNT WINLOAD builds before invoking their entry point.
- Report a clear diagnostic identifying an unsupported WINLOAD ABI/build
  family; do not silently fall back to FreeLoader or another operating
  system's loader.
- Keep this restriction independent of FAT, NTFS, ISO9660, Btrfs, BIOS, or
  UEFI discovery. A filesystem being readable does not make the WINLOAD
  executable compatible.

There is no supported compatibility shim that converts a regular LiberNT
WINLOAD into an RC95-OS WINLOAD. Supporting it would require a deliberately
designed and tested adapter for the full startup, service, loader-block, and
kernel-handoff contracts; no such adapter is part of RC95LDR.

## Validation checklist

- [ ] Build RC95LDR, VENEER, and RC95-OS WINLOAD for i386 and AMD64.
- [ ] Verify PE machine, subsystem, entry point, section bounds, and image
      size before transfer.
- [ ] Verify the module handoff and service-table ABI in every consumer.
- [ ] Exercise BIOS transfer and UEFI LoadImage/StartImage paths.
- [ ] Verify parent/service lifetime through child return and failure cleanup.
- [ ] Verify wrong-family (including regular LiberNT and CATV 95 LiberNT)
      images are rejected before entry-point invocation.
- [ ] Run firmware-level boot tests; host unit tests alone do not establish a
      working firmware-to-kernel boot.
