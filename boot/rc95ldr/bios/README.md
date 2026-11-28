# RC95LDR native BIOS startup

The native BIOS frontend is an RC95LDR-owned platform adapter. It does not
include or call FreeLoader, ARC, `Int386`, or the legacy relocator.

## Entry contract

The native loader-facing assembly symbol is `Rc95LdrBiosI386Entry`. Its
register protocol is deliberately small and independent of FreeLoader:

- `EAX = RC95LDR_BIOS_ENTRY_MAGIC` (`0x52353942`);
- `EBX =` flat pointer to a fully initialized, caller-owned
  `RC95LDR_BIOS_STARTUP` descriptor.

The entry requires 32-bit protected mode (`CR0.PE=1`, `CR0.PG=0`), A20 on,
identity-mapped image/data, `CS=0x08`, and a flat writable data selector at
`0x10`. It disables interrupts, establishes RC95LDR's private 16 KiB startup
stack, and calls `Rc95LdrBiosEntryMain`; that bridge calls
`Rc95LdrPrepareNativePlatform`. The original stack is restored and
the `RC95LDR_STATUS` is returned in EAX if the coordinator returns. The
startup image must be linked/loaded at addresses valid under the identity map.

The current Stage-0 packager contract is 512-byte logical sectors only; larger sector sizes are rejected because this real-mode loader uses 512-byte EDD transfer arithmetic and the conventional BIOS boot-sector layout. The packager tests cover the B95S/B95I separation, maximum 32-KiB Stage-1 extent, CS=0 entry bounds, and rejection of larger sector sizes. Source-contract tests cover EDD probing, LBA 1/LBA 2 DAP setup, header validation, CRC-32, and the far transfer. These source tests do not replace assembling the sector or executing it in an emulator.\n\nThe disk-reading Stage 0 is `arch/i386/stage0.S`. It preserves the BIOS
boot drive, checks INT 13h extensions with AH=41h, reads B95S from LBA 1 and
Stage 1 from LBA 2 with AH=42h/DAP, validates the B95S fields and the exact
Stage-1 CRC-32, then far-jumps to the declared `CS=0` entry. It rejects a
missing EDD interface or malformed header; there is intentionally no CHS
fallback. The Stage-0 object is kept separate from the protected-mode loader
libraries. The flat boot-sector binary supplied to `mkrc95biosimage.py` must
be produced from this source by the boot-sector build/link step.

The subsequent real-mode transition stage is `Rc95LdrBiosStageRealModeEntry` in
`arch/i386/stage.S`. It accepts `DS:SI` pointing to a packed
`RC95LDR_BIOS_STAGE_HANDOFF` and `DL` containing the BIOS boot drive. It
checks the handoff signature/version, switches VGA text output to 80x50,
enables and verifies A20, loads its private flat GDT, and enters 32-bit protected mode
with paging left disabled. It then calls `Rc95LdrBiosStageMain` with the boot
drive, startup descriptor address, and RC95LDR-owned platform provider. The
packed handoff is 16 bytes: signature/version, startup-descriptor pointer,
provider pointer, and protected-mode stack top.

`Rc95LdrBiosStageMain` validates the provider, asks it to populate the BIOS
service state and every required WINLOAD callback, and validates the callback
set. On i386 it then calls `Rc95LdrBiosInvokeNativeEntry`, an assembly bridge
that places `RC95LDR_BIOS_ENTRY_MAGIC` in EAX and the startup descriptor in EBX
before entering `Rc95LdrBiosI386Entry`. Thus the real-mode stage and a future
standalone BIOS WINLOAD image converge on the same checked register ABI; the
stage no longer bypasses that ABI with a direct C coordinator call. The
non-i386 build returns `Rc95LdrUnsupported`. No FreeLoader entry, ARC adapter,
`Int386`, or relocator is used.

`Rc95LdrPrepareNativePlatform` is the first C-level BIOS startup coordinator.
It is entered by the native i386 startup/loader stage after establishing:

- flat 32-bit protected mode, with paging disabled and identity-mapped memory;
- A20 enabled;
- `CS=0x08`, `DS/ES/SS=0x10`;
- a valid writable stack and a C runtime environment;
- caller-owned `RC95LDR_BIOS_STARTUP`, BIOS service state, and firmware-neutral
  `RC95LDR_WINLOAD_PLATFORM` storage.

The entry descriptor and all callback-owned storage remain valid through the
WINLOAD call. `Context` is the caller's adapter context. The platform callbacks
own file access, physical allocation, allocation attestation, native module
mapping, SYSTEM-hive loading, loader-block storage, and final handoff.

The i386 first-stage coordinator establishes the private gateway before it
enters `Rc95LdrBiosI386Entry`: after the real-mode stage has loaded the image
and switched to flat protected mode, `Rc95LdrBiosStageMain` invokes
`Rc95LdrBiosPrepareNativePlatform`. That operation commits reservations for
physical `0x5000..0x7fff` plus the provider's separate EDD scratch range,
copies the position-independent gateway image to `0x5000`, and replaces the
rejecting interrupt callback with the real-mode thunk. Only after this succeeds
does the stage enter the native startup ABI. Consequently E820 collection,
EDD probing, display services, and later BIOS disk I/O all run through an
already-installed RC95LDR gateway; gateway installation is not deferred until
WINLOAD startup. The gateway owns `0x5000..0x5fff`, the register packet is at
`0x6000`, and the provider's scratch range must be separate and below 1 MiB.

Stage 0 uses firmware INT 13h EDD directly only for the two bootstrap reads.
It does not call the protected-mode thunk before the CPU and stack are in the
gateway's required flat protected-mode environment. Stage 1 then owns the
real-mode-to-protected-mode transition and startup-provider handoff.

## Startup sequence

1. Validate the startup ABI and the supplied BIOS/platform service tables.
2. Reserve the gateway and scratch low-memory ranges.
3. Install the RC95LDR i386 real-mode interrupt gateway.
4. Collect and normalize the E820 memory map.
5. Probe the boot drive using INT 13h EDD and expose an RC95LDR block device.
6. Switch the display to 80x50 VGA text mode immediately at real-mode takeover
   (INT 10h mode 3 plus the 8x8 font), hide the cursor during initialization,
   then reassert 80x50 through the installed BIOS gateway and initialize the
   text UI (INT 10h/16h).
7. Call `Rc95LdrBiosWinloadEntry`, which enters the shared
   `Rc95LdrWinloadStart` and `Rc95LdrLoadAndBootWindows` policy path.
8. The caller's `BuildNativePreparation` platform callback supplies the
   resident SYSTEM hive, kernel/HAL/boot-driver module records, metadata,
   allocation ledger, memory reservations, and final loader-block storage.
   The shared cold-boot path validates those records and calls
   `Rc95LdrPrepareNativeLoaderBlock` (the native loader-block preparation
   implementation exposed by `nativeprepare.c`).
9. The shared loader validates the handoff and invokes the BIOS platform's
   non-returning `ExitFirmwareAndHandoff` callback.

The BIOS adapter does not duplicate BCD, filesystem, PE, loader-block, or
kernel-handoff policy. `Rc95LdrBiosInitializeNativeServices` supplies the
identity-mapped low-memory mapping callbacks and configures the BIOS EDD
scratch area; its interrupt callback is a rejecting stub that is replaced by
the private gateway during startup. `Rc95LdrBiosInitializeMemoryAllocator`
and `Rc95LdrBiosInstallMemoryCallbacks` provide a first-fit E820 allocator,
identity-mapped allocation results, release tracking, extent attestation, and
memory-map projection. It allocates only available ranges above 1 MiB, honors
all current reservations and live allocations, and limits allocations to the
i386 4 GiB address space. A provider may embed the allocator in its shared
platform context whose first member is `RC95LDR_BIOS_MEMORY_ALLOCATOR`; set
`Platform.Context` to that wrapper before calling
`Rc95LdrBiosInstallMemoryCallbacks`, then have the file callbacks reach the
provider-specific context in the wrapper. The memory helper preserves a
pre-populated shared `Platform.Context`.

A native `BuildPlatform` provider should initialize `BiosServices` with
`Rc95LdrBiosInitializeNativeServices(BootDrive)`, point the startup descriptor
at persistent reservation/E820 buffers, initialize its allocator with
`Rc95LdrBiosInitializeMemoryAllocator`, and install the four memory callbacks
with `Rc95LdrBiosInstallMemoryCallbacks`. It must then add its own file-open,
read, close, native-preparation, release, boot-request, and final-handoff
callbacks using that shared context. All provider storage must outlive the
WINLOAD transaction. Before installing the allocator, the provider must also
reserve the loaded BIOS-stage image, the RC95LDR image, its own context and
allocator storage, the protected-mode stack, the handoff descriptor, and the
startup/provider descriptors. The real-mode stage image must be linked at its
physical load address below 1 MiB, entered with `CS=0`, and kept outside
`0x5000..0x17fff`, which is reserved for the BIOS gateway and EDD scratch. The BIOS boot-device and E820 objects are available to
the caller's request/platform callbacks through the supplied startup context.

## EDD image manifest and BIOS disk discovery

`rc95ldrbiosdisk.h` defines the RC95LDR BIOS image manifest consumed by
`Rc95LdrBiosLoadManifestImage`. The manifest is a packed 536-byte structure
(version 1) stored at a caller-selected LBA. It contains up to 16 independent
segments. Each segment specifies a starting LBA, a sector-aligned byte length,
a physical load address, a CRC-32 (IEEE) of the loaded bytes, and zero flags.
The entry address must fall inside one loaded segment. The loader rejects
out-of-disk extents, overlapping source or destination segments, addresses
below 1 MiB or above the i386 physical address space, nonzero reserved flags,
and destination ranges overlapping the supplied memory-reservation set.
Segments are loaded through INT 13h extensions using the RC95LDR BIOS gateway.
The caller must reserve the manifest/stage/loader/provider/stack and BIOS
gateway scratch regions before invoking it; the manifest itself is not a
replacement for the runtime `RC95LDR_BIOS_STAGE_HANDOFF`.

The BIOS disk discovery adapter uses the same EDD block-device implementation
as startup. It parses and bounds-checks MBR primary partitions, follows a
bounded EBR chain for logical partitions, detects protective MBRs and validates
GPT header/entry CRCs, and identifies FAT32 and NTFS boot sectors. It preserves
MBR disk signatures and GPT disk/partition GUIDs in
`RC95LDR_PARTITION_INFO`. FAT32 partitions can be mounted through
`Rc95LdrBiosMountFat32`; NTFS partitions can be mounted read-only through
`Rc95LdrBiosMountNtfs` for geometry/MFT bootstrap. The discovery context,
scratch buffer, block context, and embedded block-device adapter must remain
alive for the lifetime of mounted volumes.

The loader-side EDD services execute after the private gateway is available
in flat protected mode; Stage 0's direct EDD calls are limited to B95S and
Stage-1 bootstrap loading. The BCD discovery transaction is implemented for
FAT32 BCD stores; NTFS-resident BCD lookup and the provider's required
Windows-file validation callback remain integration work. The Stage-0 source
and object target do not by themselves prove a bootable disk image: a flat
512-byte boot-sector binary, end-to-end packaging, and BIOS/QEMU execution
still need verification.

## BCD-to-Windows discovery transaction

`Rc95LdrDiscoverBiosWindows` now connects the BIOS EDD partition and
filesystem adapters to the shared BCD candidate policy. It enumerates the
boot disk, visits each detected FAT32 system/boot partition, and probes the
firmware-appropriate BCD paths:

- GPT: `\\EFI\\Microsoft\\Boot\\BCD`, then `\\Boot\\BCD`.
- MBR: `\\Boot\\BCD`, then `\\EFI\\Microsoft\\Boot\\BCD`.

For each valid hive it walks Boot Manager Default/DisplayOrder, decodes
partition device payloads, resolves MBR disk-signature/byte-offset or GPT
disk/partition GUID identities against the enumerated disk, removes unusable
entries, and applies the common default-then-display-order selection policy.
It then mounts the selected `osdevice` as NTFS and validates the Windows
installation. A provider may override validation; otherwise the transaction calls
`Rc95LdrBiosValidateWindowsVolume` using an
`RC95LDR_BIOS_NTFS_VALIDATION_CONTEXT` passed as `Ops.Context`. The built-in
validator reads the BCD-selected loader, `SystemRoot\\system32\\ntoskrnl.exe`,
and `SystemRoot\\system32\\config\\SYSTEM`; it checks PE signatures for
the loader/kernel and the `regf` base-block signature plus minimum hive size.
A platform may still provide an override when it needs stronger policy or
additional checks. A successful NTFS geometry mount alone is never accepted.

On success the NTFS volume remains mounted and caller-owned. The caller must
keep the discovery context, embedded block device, disk/service tables, and
scratch storage alive until `Rc95LdrBiosUnmountNtfs` is called. The returned
`BcdPartitionIndex` identifies the FAT32 partition from which the successful
BCD hive was read; `Installation.PartitionIndex` and
`Installation.OsPartitionIndex` identify the BCD-resolved boot and OS
partitions. The candidate and hive buffers remain caller-owned.

The current transaction reads BCD from FAT32 volumes; NTFS-resident BCD lookup
is not yet implemented. The first native NTFS file reader is deliberately
read-only and bounded: it resolves path components by scanning the MFT, reads
resident or single-extent unnamed data streams, and rejects sparse, compressed,
encrypted, multi-extent/attribute-list file streams rather than guessing.
Directory lookup currently uses MFT scanning rather than traversing the
available `$I30` index-allocation tree, so large volumes may be slow. The
built-in validator is therefore a concrete baseline and not a claim of full
NTFS 3.x compatibility or full registry-hive integrity validation.

## Interactive BIOS BCD candidate selection

The native BIOS startup descriptor (ABI version 6) supports an optional
interactive boot menu through `FirmwareMenuOps`. When this pointer is non-NULL,
the coordinator passes its startup context into the selected NTFS or Btrfs
transaction. The caller may also provide `FirmwareMenuTitle` and
`FirmwareMenuItems`; custom item strings must follow the filtered BCD candidate
order and provide at least one string per candidate. If item strings are
omitted, the loader uses generic `Windows Boot Entry N` labels. The firmware
menu operations may be caller-provided or left empty for the native BIOS
adapter to bind.

Selection occurs after a BCD hive has been loaded and its Windows candidates
have been filtered, but before an OS volume is mounted. The callback receives
that live hive and the exact filtered candidate array. The returned pointer is
validated against the array, and discovery resolves and mounts only that
candidate's `osdevice`. Its loader path, system root, boot options and
`graphicsmodedisabled` value then flow into the same filesystem provider and
WINLOAD transaction. A failed explicit selection is reported; the loader does
not silently substitute another entry or another BCD store. If
`FirmwareMenuOps` is NULL, the existing automatic default/display-order
selection behavior is unchanged.

The menu is opt-in. A partially specified menu configuration is rejected.
The caller must keep the startup descriptor, menu operations, optional item
strings, and all referenced callback contexts alive for the complete boot
transaction. The menu uses the existing BIOS VBE/text fallback path and adds no
heap allocation.

## Scope and validation

The BIOS thunk and `Rc95LdrPrepareNativePlatform` native implementation are
built for i386. Non-i386 host builds retain the API but return
`Rc95LdrUnsupported` for native startup; the injected interrupt implementation
continues to support host-side BIOS service tests.

The BIOS stage transition, protected-mode callback bootstrap, and EAX/EBX
native-entry bridge are part of the i386 BIOS target. The `RC95LDR_BIOS_STAGE_HANDOFF` is a runtime handoff
structure, not an on-disk manifest. The stage expects the RC95LDR image,
startup descriptor, and provider to have been loaded into identity-mapped RAM
by the preceding disk-loading stage. This repository still does not define a
sector layout or implement that preceding EDD image/descriptor loader, and it
does not yet ship a concrete provider implementing FAT/NTFS file access,
physical allocation, native preparation, and kernel handoff. Those are explicit
remaining integration requirements; no bootable BIOS image is claimed. BIOS
EDD is currently required by the platform disk adapter; CHS fallback is not
silently substituted.


## BIOS bootstrap header and EDD manifest v1

The image has two deliberately separate metadata contracts:

- **B95S** is the 32-byte low-memory Stage-1 bootstrap header. Stage 0 reads
  this header directly from LBA 1 and uses it to load and verify Stage 1.
  `Rc95LdrBiosLoadManifestImage` never parses B95S.
- **B95I** is the existing 536-byte high-memory payload manifest. The
  protected-mode RC95LDR loader consumes B95I after the private BIOS gateway
  and protected-mode runtime are available. Its load-address restriction
  remains unchanged: every B95I segment must load at or above 1 MiB.

The image layout produced by `tools/mkrc95biosimage.py` is:

| LBA | Contents |
| --- | --- |
| 0 | Stage-0 BIOS boot sector |
| 1 | 32-byte B95S header, padded to one logical sector |
| 2.. | Stage-1 real-mode image, sector padded |
| following | B95I manifest, padded to whole sectors |
| following | B95I payload extents, each sector-aligned and contiguous |

B95S is packed little-endian as `Signature` (B95S), `Version` (1),
`HeaderSize` (32), `LoadAddress`, exact `ImageLength`,
`EntryOffset`, IEEE CRC-32, zero `Flags`, and zero `Reserved`.
The CRC covers exactly `ImageLength` bytes, excluding sector padding.
Stage 0 must reject unknown versions, incorrect sizes, nonzero reserved
fields, zero/overflowing lengths, an entry offset outside the image, an
image outside the CS=0 Stage-1 address window, and any overlap with its own stack,
scratch, or boot-sector reservations. It must also compare the destination
end against the conventional-memory limit reported by INT 12h; the packager's
0xA0000 ceiling is only a static upper bound, not a substitute for that
runtime check. The default load address is 0x8000. Because the existing transition stage enters with CS=0 and a 16-bit IP, the current Stage-1 image and its entry must fit below physical 0x10000; the packager enforces this narrower execution window. Stage 0 must still check the actual destination against the boot environment's reserved ranges.

B95I retains its original 24-byte header and 16 fixed 32-byte segment records.
It contains `Signature` (B95I), `Version` (1), `HeaderSize` (536),
`SegmentCount`, zero `Flags`, and physical `ImageEntry`. Each record
contains `Lba`, sector-aligned `ByteLength`, `LoadAddress`, IEEE CRC-32,
and zero `Flags`. Unused records are all zero; each CRC covers the complete
sector-padded payload bytes.

Example:

```text
python tools/mkrc95biosimage.py --boot-sector stage0.bin \\
  --stage1 stage1-realmode.bin --output rc95bios.img \\
  --entry 0x100100 --segment 0x100000:rc95ldr-protected.bin
```

Optional `--stage1-load-address` and `--stage1-entry-offset` override the
Stage-1 defaults. The packager rejects invalid boot-sector signatures,
empty images, invalid Stage-1 bounds/entry offsets, overlapping B95I
destinations, low B95I load addresses, and B95I entry points outside all
segments. It places B95I after the complete Stage-1 extent, so its LBA is
derived from the Stage-1 image length and sector size.

**Stage-0 protocol dependency:** this image layout defines the bytes and
metadata Stage 0 must consume, but it does not by itself prove that the
caller-supplied boot sector implements B95S parsing, EDD reads, CRC checking,
or the transfer convention. The Stage-0 assembly implementation and a
QEMU boot test are still required before the generated image can be called
bootable. The existing protected-mode `Rc95LdrBiosLoadManifestImage`
continues to consume B95I only and retains all existing high-memory,
reservation, overlap, disk-boundary, and integrity checks.


## AMD64 BIOS execution contract

The AMD64 frontend shares the BIOS EDD disk, partition, filesystem, BCD,
WINLOAD, allocator, and manifest code with i386. Its native entry is
`Rc95LdrBiosAmd64Entry` in `arch/amd64/entry.S`: RCX carries the startup
descriptor and EDX carries `RC95LDR_BIOS_ENTRY_MAGIC`. The bridge validates
long-mode execution, switches to a private aligned stack with Windows x64
shadow space, and calls the same `Rc95LdrBiosEntryMain` coordinator.

This is deliberately **not** described as a BIOS real-mode gateway. Legacy
BIOS interrupts cannot be invoked directly from 64-bit long mode. AMD64
providers must arrive through a boot manager/transition that has already
entered long mode and must supply `InvokeInterrupt`, low-memory map/unmap
callbacks, and an EDD scratch range below 1 MiB. The common BIOS startup
coordinator validates those services and then uses the same E820/EDD,
partition, filesystem, BCD, and WINLOAD code. It does not install the i386
protected-mode-to-real-mode thunk.

The AMD64 allocator is architecture-width-aware through `uintptr_t`, while
the legacy BIOS EDD packet and scratch memory remain below 1 MiB. The current
allocator's 4-GiB physical allocation ceiling remains intentional until the
AMD64 provider supplies a tested high-memory mapping/copy strategy. This
long-mode entry is a protected startup ABI, not an independently bootable
BIOS disk image. A real-mode first-stage loader and a provider-owned
AMD64 BIOS transition thunk remain separate boot-image integration work.


## El Torito / ISO9660 boot support

The native CD path is separate from the hard-disk B95S layout. The El Torito
no-emulation boot image is loaded by firmware at physical 0x7C00 (load segment
0x07C0). Its image layout is:

- offset 0: 512-byte stage0_cd.S boot sector;
- offset 512: B95S Stage-1 header;
- offset 1024: Stage-1 image, linked for physical 0x8000.

The CD Stage 0 normalizes CS to zero, validates the in-memory B95S header and
Stage-1 CRC, and jumps to the already-loaded Stage 1. It does not assume that
the boot image is located at ISO LBA 0 and does not call FreeLoader. The ISO
builder must use El Torito no-emulation mode, load segment 0x07C0, and a load
count equal to the boot-image length divided by 512.

mkrc95biosimage.py can emit this combined image in addition to the normal
hard-disk image by passing both --cd-boot-sector and --cd-boot-image.
The CD boot sector is rc95ldr_bios_cd_stage0.bin; the conventional disk boot
sector is rc95ldr_bios_stage0.bin. The Stage-1 image must be linked at
0x8000 and fit in the remaining 32-KiB CS=0 window.

After Stage 1 establishes the protected-mode BIOS gateway, startup probes
INT 13h AH=4Bh/AL=01h, verifies the no-emulation media type, probes EDD
geometry, and mounts the ISO9660 filesystem. If a BIOS omits the El Torito
status function, a 2048-byte EDD device is still checked for a valid ISO
Primary Volume Descriptor. The mounted read-only filesystem is installed as
the loader-scoped RC95LDR_FILE_PROVIDER for the WINLOAD startup transaction.

The ISO9660 provider validates PVD/SVD descriptors, volume/block geometry,
directory records and extent bounds. It prefers valid Joliet names, supports
ASCII paths, nested directories, ISO version suffixes, random-access reads,
and eight simultaneous file handles. Rock Ridge, non-ASCII Joliet path
components, multi-extent files and UDF are not implemented. ISO logical block
size must be 2048 bytes.

This adds the ISO9660 runtime filesystem and the El Torito in-memory bootstrap
contract. A complete product ISO still requires the image-generation pipeline
to place the emitted boot image in the El Torito catalog and set the load count
correctly; a QEMU/real-firmware boot has not yet been validated.

## Native BIOS PE module handoff

`Rc95LdrBiosLoadAndInvokeNativeModule` is the BIOS module-chain helper.
It opens a module through the active WINLOAD file provider, reads it into a
tracked page allocation, validates the PE machine/headers and image extent,
maps it with the shared PE mapper, constructs the versioned
`RC95LDR_MODULE_HANDOFF` descriptor, and transfers through an architecture-
specific trampoline. The i386 trampoline uses cdecl stack arguments; AMD64
uses the Windows x64 register ABI and reserves shadow space. No
integer-to-function-pointer cast is used in C.

The helper currently rejects PE images with unresolved imports; a module
must be import-free until a policy-controlled resolver is supplied. Both
the file buffer and mapped image are released if control returns, and every
allocation is checked by `ValidateAllocationExtent` before it is dereferenced.
The provider's version-2 `ReleasePlatform` callback is paired with every
attempted `BuildPlatform`, including partial failures, so partially acquired
startup resources can be unwound.

Providers should invoke the native module helper only after the BIOS gateway,
file provider, allocator and allocation-attestation callbacks are initialized.
The helper does not infer a module path or silently select VENEER; the
boot-policy provider must select the exact module required by the selected
filesystem and firmware route.

## Native BCD-selected NTFS startup

Protected-mode BIOS hard-disk startup now routes through
`Rc95LdrBiosStartWindowsFromNtfs()` in `ntfsboot.c`. The startup ABI is
version 3 and exposes `RC95LDR_BIOS_STARTUP.NtfsBoot`. The caller owns the
partition array, BCD candidate array, hive buffer, disk-discovery scratch,
Windows-file validation scratch/probe buffers, and NTFS provider workspace.
No heap allocation is hidden in this path.

The helper enumerates the boot disk, finds a FAT32 BCD store, resolves the
selected Windows loader and its `device`/`osdevice`, mounts that exact NTFS
partition, validates the selected loader/kernel/SYSTEM hive, binds
`Rc95LdrNtfsBindVolume()`, installs the generic file-provider scope, and
calls the shared BIOS WINLOAD entry. The build/release request callbacks
receive `RC95LDR_BIOS_NTFS_WINLOAD_CONTEXT`; its `CallerContext` retains the
original caller context while `SelectedInstallation` and `WindowsVolume`
identify the exact mounted OS installation.

Cleanup order is file-provider scope, NTFS filesystem adapter, then NTFS
volume. A successful WINLOAD handoff is non-returning; every returning failure
unwinds the resources acquired by this helper. The ISO9660/El Torito path
continues to use its own ISO provider scope.

The provider workspace size is geometry-dependent. Before entering native
startup, provision a caller-owned buffer large enough for
`Rc95LdrNtfsFileSystemWorkspaceSize()` for the selected volume geometry; the
helper checks the actual required size after mount and returns
`Rc95LdrBufferTooSmall` rather than overrunning an undersized buffer.
