# RC95LDR

RC95LDR is the canonical name for the RC95 OS bootloader.

This directory defines the replacement bootloader architecture. The existing
FreeLoader-derived implementation remains the compatibility implementation
while responsibilities are migrated into RC95LDR-owned interfaces. This is
intentional: changing the product name does not make the current BIOS/UEFI
paths, BCD handling, NTFS reader, or NT OS loader complete.

## Required execution order

1. Establish RC95LDR's common contracts and responsibility boundaries.
2. Move boot policy and configuration to the BCD subsystem.
3. Replace the BIOS/legacy-PC entry and platform services.
4. Replace the UEFI entry and firmware services.
5. Re-implement and validate NTFS boot-volume discovery and file access
   against the RC95LDR block-device and partition interfaces.
6. Implement OS image loading and the final kernel handoff.
7. Integrate boot graphics/resources after the loader and handoff contracts
   are stable; keep legacy INBV support for non-GOP paths.

NTFS work is deliberately sequenced after the common loader and firmware
interfaces. The existing filesystem code may be used as a behavioral reference,
but it is not considered the final RC95LDR NTFS implementation.

## Responsibility ownership

| Subsystem | Owns | Must not own |
| --- | --- | --- |
| Entry / startup | CPU/firmware entry, early stack, minimal diagnostics | BCD policy, filesystem parsing |
| Platform services | Firmware calls, memory map, console, timers, reset | OS selection or NTFS internals |
| Block I/O | Sector reads, device identity, geometry, media errors | Partition or filesystem policy |
| Partition manager | MBR/GPT discovery and partition identity | Firmware entry or file loading |
| BCD | Hive/store validation, object/element access, boot policy | Firmware-specific I/O |
| Filesystem layer | Volume mounting and file reads | Firmware boot policy |
| NTFS driver | NTFS metadata, allocation, file/directory lookup | BIOS/UEFI service dispatch |
| Image loader | PE validation, relocation, imports, image placement | Boot menu and device enumeration |
| OS preparation | Registry/system hive, boot drivers, loader block, memory descriptors | Firmware-specific startup |
| Handoff | Quiesce services and transfer control to the kernel | Boot selection and file parsing |
| Diagnostics | Structured stage/error reporting | Hidden global policy state |

## WINLOAD compatibility boundary

RC95LDR accepts only WINLOAD images built for the matching RC95-OS module
ABI. Regular LiberNT WINLOAD builds are unsupported because their startup,
service, loader-block, and kernel-handoff contracts are incompatible with
RC95-OS. The same restriction applies to the defunct CATV 95 LiberNT fork.
RC95LDR must reject these images before entry-point invocation; firmware
selection or filesystem support does not make them compatible. See
[MODULE_HANDOFF_AND_WINLOAD_COMPATIBILITY.md](docs/MODULE_HANDOFF_AND_WINLOAD_COMPATIBILITY.md)
for the module protocol, firmware-specific ownership rules, completion gates,
and diagnostic requirements.

## Compatibility and migration rules

- Do not delete the current FreeLoader code until the replacement path has
  equivalent tests and a working boot path on the relevant firmware.
- BIOS and UEFI must share policy, BCD, partition, filesystem, image-loading,
  and OS-preparation contracts. Only firmware/platform adapters differ.
- Do not make UEFI depend on BIOS interrupt services or make BIOS depend on
  UEFI boot/runtime services.
- Preserve the existing ARC/ReactOS boot path during migration until callers
  have been converted.
- Treat every firmware-provided pointer, memory map, disk structure, BCD hive,
  PE image, and NTFS structure as untrusted input. Validate bounds and integer
  arithmetic before dereferencing or allocating.
- Boot failure must report a stable stage and error code, then use the
  platform-specific reboot/return policy. Do not silently continue with
  partially initialized state.

## Current repository baseline

The catv95ntc2 baseline still builds the legacy FreeLoader-derived targets:
freeldr.sys for the PC/AT path and uefildr.efi for UEFI. The UEFI entry
currently initializes firmware globals, settings, machine services, UI,
memory management, filesystem services, module tracking, and boot devices
before calling RunLoader(). The PC/AT and UEFI CMake files compile separate
platform implementations around substantial shared source lists.

The first migration stage therefore establishes RC95LDR as the canonical
architecture and API owner without falsely renaming incomplete binaries or
removing the known-working legacy entry points. Output names will change only
when each replacement target is actually built and selected by the image
packaging/install paths.


## Implemented in the initial RC95LDR core

- `core`: versioned boot context, stage tracking, stable status values,
  checked size addition/multiplication/range helpers, and little-endian
  integer readers.
- `bcd`: read-only REGF hive opening, base-block checksum and sequence
  validation, HBIN/cell boundary validation, registry NK/VK key/value
  traversal, BCD object lookup by GUID, and BCD element lookup by numeric ID.
- `bgfx` and `winload`: relocated subsystem directories with ownership
  documentation; their actual graphics and OS-loading implementations remain
  future migration stages.

The BCD API currently accepts a caller-owned in-memory hive image. The caller
must retain that buffer while using the hive view. It does not modify the
store. Unsupported or malformed structures return explicit errors.


## UEFI Windows discovery WIP

The current WIP branch adds a UEFI discovery path without claiming that the
full RC95LDR replacement loader is complete:

- EFI Block I/O handle/media and device-path access through FreeLoader's
  initialized UEFI handle table.
- GPT header/entry CRC validation, partition bounds checks, and association of
  logical partition handles by GPT unique GUID and extent.
- A read-only FAT32 adapter with VFAT long-name lookup for the ESP BCD path.
- A FreeLoader ARC volume adapter that explicitly requires NTFS for the
  discovered Windows volume, reusing the existing NTFS filesystem driver.
- A BCD Boot Manager walker for Default/DisplayOrder and Windows-loader
  `device`, `osdevice`, and `systemroot` elements.
- A policy result that supplies the selected BCD entry, loader application path, boot/OS partitions,
  system root, and mounted NTFS volume to the future winload implementation.

Call `Rc95LdrFreeldrUefiInitializeDiscovery` after
`UefiInitializeBootDevices` to construct the firmware/filesystem callbacks.
The host tests include GPT/device-path and FAT32 long-name fixtures; they are
registered in CMake but still require execution in a configured build.

The FAT32 adapter is read-only and currently supports ASCII paths with VFAT
long-name entries and logical sectors up to 4096 bytes. The generic
`Rc95LdrInitializeVolumeFileProvider` adapter exposes RC95LDR filesystems
through the loader-scoped provider ABI, and FreeLoader winload can run inside
that provider scope. Native NTFS now validates FILE-record update-sequence
fixups, walks bounded resident/non-resident attributes, bootstraps the `$MFT`
run map from record 0, and reads bounded MFT records through the partition
block interface. Directory `$I30` traversal and ordinary file data reads are
not implemented yet, so the operational Windows NTFS volume adapter remains
the FreeLoader ARC bridge. BCD locate,
ramdisk, VHD, and other virtual-device forms remain unsupported, and PE image
loading/kernel handoff are not implemented.

RC95LDR is an RC95-OS-only bootloader. It is not compatible with builds of the
defunct CATV 95 LiberNT fork (October 15, 2026 through November 12, 2026).


## Shared module handoff services (implementation in progress)

The core now defines the versioned `RC95LDR_MODULE_SERVICES` (`R95S`)
table alongside the `R95M` handoff. The table exposes typed file and page
allocation callbacks with firmware/architecture identity and strict size,
version, callback, and flags validation. The BIOS PE module loader builds the
table from its RC95LDR-owned platform provider and keeps the provider and
table alive for the synchronous native transfer. The BIOS transfer bridge
validates the table immediately before calling the child entry; i386 uses the
cdecl bridge and AMD64 uses the Windows x64 register/stack ABI bridge.
Architecture-mismatched native BIOS module requests are rejected.

A host regression target, `rc95ldr_module_services_tests`, covers valid
service tables, truncated tables, missing callbacks, version mismatch,
architecture mismatch, and service-range overflow in the parent handoff.
These tests are registered in the standalone host CMake build.

The UEFI path now builds the same R95S table from the child image's
Simple File System / File Protocol and Boot Services interfaces (opened
using the parent image as protocol agent). It provides
bounded UTF-16 file opens, checked offset reads, page allocation/free with
alignment tracking that retains the complete raw EFI allocation, retryable
cleanup after firmware free failures, allocation-extent validation, and a
mandatory quiesce callback that closes UEFI file/protocol handles before `ExitBootServices`,
retaining failed closes for retry.
The provider and table are attached to the R95M handoff and remain alive for
the synchronous `StartImage` call; child-side validation checks the R95S
signature, version, firmware, architecture, size, and required callbacks.
WINLOAD must call `Quiesce` after its last file operation and before the final
memory-map/key transaction.

New host targets cover UEFI service file I/O, allocation tracking, path and
range validation, malformed allocation results, exact raw-range release,
retryable free failures, idempotent quiesce, service-table population,
handoff validation, and cleanup after a child returns or unload fails. They are registered but have not been
executed in this environment. Native BIOS assembly has not been boot-tested
on an emulator or physical hardware, and host tests do not establish a
complete RC95LDR -> VENEER -> WINLOAD firmware boot. See
[MODULE_HANDOFF_AND_WINLOAD_COMPATIBILITY.md](docs/MODULE_HANDOFF_AND_WINLOAD_COMPATIBILITY.md)
and [uefi/README.md](uefi/README.md).

## RC95LDR text-mode UI (current presentation path)

The text UI is the active presentation layer until BGFX is implemented. It
provides a welcome screen, a caller-populated boot-selection screen, a progress
screen, and a diagnostic error screen with status/detail codes. The winload
platform contract accepts an optional `RC95LDR_TEXTUI`; older platform
initializers can omit the size-gated trailing field and continue headlessly.

The firmware integration must bind `RC95LDR_TEXTUI.Write` to the active text
console (UEFI Simple Text Output or the BIOS text-console implementation) and
may provide `Clear`. Screen rendering does not discover firmware protocols,
read keyboard input, or choose a boot entry. Those remain platform and boot
manager responsibilities. No BGFX or framebuffer dependency is introduced.


## Firmware boot-menu integration (BGFX pre-OS path)

The shared firmware-menu adapter is under `firmware/`. BIOS callers bind
legacy INT 10h VBE mode enumeration/set-mode and the BIOS keyboard service;
UEFI callers bind Graphics Output Protocol mode enumeration/SetMode and
Simple Text Input Ex. Both normalize framebuffer metadata and keyboard
events into the same RC95LDR contract and call the same menu runner. The
adapter chooses the supported mode nearest 1024x768, maps the physical
framebuffer through the firmware's memory-mapping service, and falls back
to the existing text UI if no usable mode is available or BGFX cannot acquire
a valid framebuffer. It never assumes a firmware framebuffer pointer is
already mapped.

The OS-loader BCD `graphicsmodedisabled` Boolean is decoded from element
`0x26000046`; an absent element defaults to graphics enabled, while malformed
or non-Boolean payloads fail closed. The resulting value is passed to the
BGFX renderer configuration and forces the text-mode menu when set.

The BIOS and UEFI entry implementations must invoke
`Rc95LdrRunBiosBootMenu` / `Rc95LdrRunUefiBootMenu` after BCD candidate
enumeration and before dispatching the selected OS. The adapter deliberately
does not call BIOS services from UEFI or GOP services from BIOS: those remain
firmware-owned callbacks. Host tests exercise both wrappers, mode selection,
framebuffer writes, keyboard navigation, and text fallback. NTOSKRNL BGFX
support is not implied; the kernel boot screen continues to use BOOTVID.


## Native filesystem migration

The filesystem ownership boundary, native FAT32 status, FreeLoader ARC/NTFS compatibility contract, and staged native NTFS implementation plan are documented in [NATIVE_FILESYSTEM_MIGRATION.md](docs/NATIVE_FILESYSTEM_MIGRATION.md). Native FAT32 remains read-only; FreeLoader's existing NTFS path is retained as the compatibility/recovery implementation until native NTFS passes equivalent tests and real boot validation.


## Btrfs boot-volume policy

RC95LDR does not currently mount or read Btrfs volumes. The supported initial
boot layout is a read-only FAT32 UEFI ESP plus a Windows volume accessed
through the existing read-only NTFS ARC bridge. Btrfs remains in the OS tree
and the legacy FreeLoader reader remains present, but that reader is limited
(single-device only; compressed extents fail) and is not wired into RC95LDR.
A selected Btrfs-backed Windows installation must fail closed before WINLOAD
handoff; do not substitute an unrelated NTFS volume. Native RC95LDR Btrfs
support is deferred until a validated deployment need exists. See
[ BTRFS_SUPPORT.md ](docs/BTRFS_SUPPORT.md) for the retained components,
compatibility boundary, and future read-only adapter requirements.


## Independent build boundary

RC95LDR's portable libraries can be configured and linked without configuring
the ReactOS/FreeLoader build:

```sh
cmake -S boot/rc95ldr/tests/host -B build/rc95ldr-standalone -DCMAKE_BUILD_TYPE=Debug
cmake --build build/rc95ldr-standalone --parallel
ctest --test-dir build/rc95ldr-standalone --output-on-failure
```

This standalone build compiles the RC95LDR core, BCD, text UI, winload policy,
UEFI discovery, FAT32, and NTFS geometry, runlist, FILE-record, MFT-volume,
and filename-index libraries, then links host regression executables against
those libraries. It does not include
`freeldr.h`, link the `freeldr` library, or require the ReactOS generated
SDK build tree. The compatibility primitive header under
`tests/host/compat` is intentionally scoped to this host build.

The standalone host build is a library and regression-test build, **not yet a
bootable BIOS or UEFI image**. A production image still needs independent
firmware entry points and platform service implementations. In particular,
the current BIOS/rosload integration still imports FreeLoader, the current
UEFI entry still resides in FreeLoader, and the ARC filesystem/platform
bridges under `boot/freeldr/freeldr` remain compatibility adapters. These
must not be mistaken for RC95LDR-owned platform implementations.

## Legacy dependency removal gates

The remaining extraction work is deliberately explicit:

1. Implement RC95LDR-owned BIOS and UEFI entry targets, with firmware-native
   Block I/O, file access, memory allocation/map, console/input, and handoff
   callbacks.
2. Replace the FreeLoader ARC/NTFS bridge with a native read-only NTFS volume
   implementation before making NTFS boot discovery independent.
3. Move boot selection, BCD discovery, and winload dispatch into the new
   entry targets; retain only a thin, optional compatibility adapter for
   existing FreeLoader boot paths.
4. Remove RC95LDR source injection from the FreeLoader and rosload source
   lists only after the replacement targets are linked and boot-tested.
5. Add BIOS and UEFI image-link checks that fail if RC95LDR targets import
   FreeLoader symbols or libraries.

Do not remove the compatibility path prematurely: the current discovery code
still receives platform operations through callbacks, but the production
callbacks are not all RC95LDR-owned yet.

## Extraction progress

The portable RC95LDR core, BCD parser, text UI, BGFX renderer, and shared
firmware-menu implementation are now compiled by the `boot/rc95ldr` CMake
targets and linked as libraries by the remaining FreeLoader compatibility
targets. The FreeLoader common source list no longer recompiles those
portable sources; it retains the FreeLoader-specific ARC filesystem bridge.
`rosload` links the shared firmware-menu library rather than compiling a
second copy of the portable core/BCD/UI/graphics/menu sources.

The UEFI FreeLoader common target no longer injects RC95LDR's generic GPT
enumerator, device-path parser, or FAT32 implementation as individual source
files. It links the `rc95ldr_uefi_discovery` library, which also contains the
native UEFI logical-partition-to-FAT32 adapter. The adapter validates the
exact GPT partition identity and geometry before mounting a read-only FAT32
volume. It currently rejects `IoAlign` values greater than one until an
aligned RC95LDR bounce-buffer allocator is available.

The native NTFS host library validates FILE-record and INDX USA fixups,
attributes, MFT sequence-numbered file references, and resident
`$ATTRIBUTE_LIST` continuation for fragmented `$MFT::$DATA` streams. It
merges validated extension extents and locates/enumerates bounded `$I30`
INDEX_ROOT entries. Nonresident `$ATTRIBUTE_LIST` streams remain unsupported.
This is still metadata infrastructure: index-allocation/bitmap traversal,
path resolution, ordinary `$DATA` stream reads, and full boot-volume file
access are outstanding. The host suite includes dedicated geometry, runlist,
record, MFT-volume, and index-root tests.

This is an extraction of portable components, not yet a standalone
bootable replacement image. The BIOS and UEFI entry points, production
firmware protocol callbacks, NTFS volume implementation, and some
winload/Configuration Manager integration remain in the FreeLoader/rosload
build. Those files are retained intentionally until the independent entry
and native NTFS path can complete an end-to-end Windows boot. Do not remove
the compatibility bridge based solely on the new library boundaries.

The production winload sources are now compiled once into the explicit
`rc95ldr_winload_freeldr` compatibility library with `_BLDR_` enabled. The
`rosload` and `uefildr` source lists no longer inject each winload source
individually; they link this library and retain only the
`cmbootadapter.c`/platform shims that still require FreeLoader's heap and
Configuration Manager services. The standalone `rc95ldr_winload_policy`
remains the host-testable, FreeLoader-independent policy build.
\n\n## WINLOAD startup coordinator (in progress)\n\nThe shared WINLOAD policy now has a startup coordinator in\n`winload/entry.c`. Separate BIOS and UEFI policy-facing shims validate the\nfirmware identity and call that coordinator. The coordinator initializes the\nboot context, asks the firmware boot manager to construct a versioned\n`RC95LDR_WINLOAD_REQUEST`, invokes the common image/native-preparation\npolicy, and releases request-owned resources on every return path.\n\nThe shims are currently static-library components, not standalone executable\nimages. A real BIOS loader entry/transition and a UEFI PE/COFF `EfiMain` are\nstill required, as are concrete BIOS and UEFI implementations of file access,\npage allocation/free, memory-map translation, native preparation, console,\nand final architecture handoff. The UEFI adapter must perform a fresh memory\nmap acquisition and retry `ExitBootServices` on a changed map key without\nallocating or printing between the successful map acquisition and exit. BIOS\nmust provide equivalent ownership and reservation guarantees without calling\nFreeLoader APIs. Do not remove ROSLOAD/UEFILDR or describe either shim as a\nbootable WINLOAD binary until those implementations are linked and boot-tested.\n

## Independent BIOS platform backend (initial implementation)

`bios/` now contains an RC95LDR-owned BIOS service ABI and backend. It has
no FreeLoader headers, symbols, globals, or ARC dependencies. The backend
implements INT 15h E820 memory-map enumeration, INT 13h EDD installation
and drive-parameter probing, bounded INT 13h AH=42h LBA reads through a
caller-provided low-memory transfer window, and INT 10h/INT 16h text output
and blocking keyboard input. Disk reads validate LBA arithmetic and transfer
sizes, cap each EDD request at 127 blocks, and split at the 64-KiB DMA-style
boundary for conservative firmware compatibility. The resulting disk context
implements the common `RC95LDR_BLOCK_DEVICE` read callback.

The backend deliberately treats the real-mode interrupt gateway and low
memory mapping as explicit startup-owned primitives (`InvokeInterrupt`,
`MapLowMemory`, and `UnmapLowMemory`). This keeps firmware calls out of the
portable policy and prevents accidental linkage to FreeLoader's
`Int386`/`Int386x`/real-mode relocator. A production BIOS image still needs
an RC95LDR-owned I386/AMD64 startup and protected-to-real-mode thunk to
implement those primitives, as well as page allocation, VBE mode services,
native preparation, and the final kernel transfer. The current backend is
therefore an independently testable BIOS service layer, not yet a bootable
BIOS executable.


## RC95LDR-owned i386 BIOS interrupt gateway

The native i386 BIOS backend now includes `bios/arch/i386/gateway.S` and
`gateway.c`. `Rc95LdrBiosEnableNativeGateway` copies the RC95LDR-owned
real-mode trampoline into its reserved low-memory window and replaces the
service table's interrupt callback with `Rc95LdrBiosRealModeInvoke`.
The trampoline owns its GDT and performs the protected-mode/real-mode
transitions itself; it marshals the BIOS register packet, dispatches through
the real-mode IVT, and restores the caller's GDT, IDT, segment registers,
stack, and general registers. It does not link to or call FreeLoader
`Int386`, `SwitchToReal`, or the FreeLoader relocator.

The i386 gateway contract requires a flat 32-bit protected-mode caller using
CS=0x08 and DS=SS=0x10, paging disabled, A20 enabled, identity-mapped low memory, and exclusive
reservation of physical `0x5000-0x7fff` (gateway, packet and temporary real-mode stack).
The BIOS scratch window must not overlap that range;
the usual `0x8000` scratch base is outside them. The startup/platform memory
allocator must reserve these ranges before allocating loader memory.
This is an i386 legacy-BIOS gateway; it is not an AMD64 CSM thunk. The BIOS
entry path must reject unsupported CPU/firmware execution modes rather than
attempting the thunk from long mode or with paging enabled.

The host BIOS tests continue to inject a mock interrupt callback, so they do
not execute privileged mode transitions. Native assembly and actual BIOS
interrupt behavior still require an i386 emulator or physical-machine boot
validation. The UEFI backend is unaffected.

The native i386 startup path should call `Rc95LdrBiosPrepareNativePlatform`
with its service table and reservation set before issuing any BIOS service.
That helper reserves the gateway/stack and scratch windows, installs the
trampoline, and replaces the service table's interrupt callback. Pass the
resulting reservation set to `Rc95LdrBuildFinalMemoryMap` so those ranges are
loader-owned rather than advertised as available RAM.


## RC95-OS kernel boot resources

For RC95-OS cold boots, the selected BCD OS-loader element
`0x26000046` (`graphicsmodedisabled`) is retained with its exact boot
candidate. Missing means graphics enabled; malformed or non-Boolean payloads
are rejected by the BCD element decoder. After resolving the selected
candidate, the firmware startup structure supplies its exact
`SelectedInstallation` to `Rc95LdrWinloadStart()`; the common entry coordinator
automatically calls `Rc95LdrApplyInstallationGraphicsPolicy()` to copy that
value into `RC95LDR_WINLOAD_REQUEST.GraphicsModeDisabled` and sets
`GraphicsModePolicyValid`. RC95LDR loads the DLL only when that validity bit
is set and the selected value is false; an absent/unknown policy therefore
fails closed to INBV. A request builder may call the helper itself when it
does not use `SelectedInstallation`. This avoids consulting the boot-manager
object or a different candidate during image load.

When graphics are enabled, WINLOAD looks for
`\\SystemRoot\\System32\\bootres.dll` on the selected OS volume. It
validates and maps the resource-only PE image (matching the running I386 or
AMD64 architecture, DLL characteristic, zero entry point, and no imports),
then appends it to the kernel load-order module list as
`RC95LDR_MODULE_RESOURCE`. It is not added to the boot-driver list and is
never executed by RC95LDR. Its page allocation is registered as retained
`LoaderMemoryData` memory (rather than reclaimable `LoaderNlsData` or
`LoaderBootDriver` memory)
and remains owned by the kernel through handoff. Native preparation appends
this optional module after all required boot-start drivers; if the module table
has no remaining slot, the DLL is omitted and its allocation is released. The
temporary on-disk file buffer is released after mapping.

When `graphicsmodedisabled` is true, RC95LDR does not open or allocate
`bootres.dll`. When the optional DLL is absent, corrupt, unsupported, or
cannot be allocated, boot continues without it; the kernel's existing INBV
path is the fallback. Other OS families do not load this RC95-OS resource DLL.

The RC95-OS packaging step adds the built `bootres.dll` to the system
directory on the installation media (`reactos/system32` in the current
CD layout). The selected installation's `SystemRoot` determines the actual
runtime path, `\\SystemRoot\\System32\\bootres.dll`; the DLL must be
present on that OS volume before boot. The
resource image is retained only for the current cold-boot transaction; it is
not used by the RC95LDR firmware boot-menu renderer.

The resource PE reader caps both the on-disk DLL and its mapped image at 64 MiB.
It accepts only an image with a bounded resource data directory, the DLL file
characteristic, no entry point, and no import table; resource-only images may
be placed without a relocation directory because their resource references
are RVA-relative.

### Boot-resource handoff regression fixture

The standalone `rc95ldr_bootres_handoff_tests` target exercises the production
`Rc95LdrAppendBootResourceModule()` admission helper and the production
`Rc95LdrBuildRc95NativeLoaderBlock()` builder together. The fixture checks
that an allocator-attested resource-only `bootres.dll` becomes the final
load-order entry with its image base, size, zero entry point, and resource
attribute intact. It walks the serialized RC95 load-order list rather than
checking only the temporary module array.

The fixture also covers the BCD `graphicsmodedisabled` outcome: the request
passes a null optional resource module, which must not invoke the allocation
attestation callback or add a loader-list entry. Invalid allocation
attestation and a full module table similarly omit the optional resource
without displacing the kernel or HAL. The test is part of the standalone
GCC/Clang RC95LDR host suite.

## UEFI discovery-to-WINLOAD transaction

`Rc95LdrUefiDiscoverAndStartWindows()` in `uefi/winload_flow.c` is the orchestration boundary for the UEFI cold-boot path. It calls `Rc95LdrDiscoverUefiWindows()`, retains the exact BCD-selected installation and mounted OS volume, initializes the common read-only volume file provider, scopes that provider around `Rc95LdrUefiWinloadEntry()`, and unwinds the provider scope and mounted volume on every returning path.

The request-builder and request-release callbacks receive an `RC95LDR_UEFI_WINLOAD_FLOW_CONTEXT` so they can use the original caller context, selected installation, and exact mounted OS volume. The common `Rc95LdrWinloadStart()` coordinator remains responsible for request construction, image loading, native preparation, and handoff policy.

This coordinator does not manufacture a filesystem implementation. The `RC95LDR_UEFI_DISCOVERY_OPS.MountVolume` callback must return a mounted `RC95LDR_VOLUME` with a complete read-only `RC95LDR_FILESYSTEM` interface for the selected OS volume. The native NTFS code currently mounts NTFS and supports bounded path-based prefix reads for validation, but it does not yet expose the arbitrary-offset `OpenRead`/`ReadAt`/`Close` adapter required by the common WINLOAD file provider.

Consequently, the new transaction coordinator is integration plumbing, not a claim that native UEFI NTFS WINLOAD boots are complete. Do not supply a null or partial NTFS filesystem interface, and do not fall back to an unrelated volume.

## Native NTFS random-access provider

`fs/ntfs_provider.c` binds a mounted `RC95LDR_NTFS_VOLUME` to the generic read-only `RC95LDR_FILESYSTEM` ABI. `Rc95LdrNtfsBindVolume()` initializes the adapter and fills an `RC95LDR_VOLUME`, so a platform `RC95LDR_UEFI_DISCOVERY_OPS.MountVolume` implementation can return this volume for `Rc95LdrFsNtfs` directly. Keep the `RC95LDR_NTFS_VOLUME`, provider context, and workspace alive until `UnmountVolume` is called.

Before binding, call `Rc95LdrNtfsFileSystemWorkspaceSize()` and allocate the returned amount from loader-owned memory. The adapter reserves eight concurrent file slots; each slot has two FILE-record buffers, one INDEX buffer, and two sector buffers. `Rc95LdrNtfsBindVolume()` checks the supplied workspace length. The filesystem is strictly read-only.

`OpenRead` resolves a root-relative UTF-8 path through NTFS directory `$I30` indexes, validates the referenced MFT record, and collects nonresident `$DATA` mapping-pair extents (including resident `$ATTRIBUTE_LIST` continuation records). Resident streams are read directly from the retained FILE record. `ReadAt` supports arbitrary byte offsets and lengths: aligned interiors are read in multi-sector batches while unaligned edges use sector bounce buffers. Uninitialized stream tails are zero-filled by the NTFS stream reader. `Close` releases the slot.

Compressed, encrypted, sparse, and named `$DATA` streams are deliberately rejected; no decompression, decryption, sparse-run synthesis, or alternate-data-stream interpretation is attempted. The current directory collation is ASCII case-insensitive and non-ASCII names compare exactly; boot-critical Windows paths are expected to use ordinary ASCII names. The adapter rejects `.`/`..` traversal and colon/ADS syntax. The existing MFT and index readers retain their current run-count and resident `$ATTRIBUTE_LIST` limits.

Example platform mount sequence:

```c
RC95LDR_NTFS_VOLUME Ntfs;
RC95LDR_NTFS_PROVIDER_CONTEXT NtfsFiles;
RC95LDR_VOLUME Mounted;
size_t WorkspaceBytes;
void *Workspace = LoaderAllocate(...);

status = Rc95LdrNtfsMountPartition(&Ntfs, &Device, StartLba, BlockCount,
                                   MountScratch, MountScratchBytes);
if (status == Rc95LdrSuccess)
    status = Rc95LdrNtfsFileSystemWorkspaceSize(&Ntfs, &WorkspaceBytes);
if (status == Rc95LdrSuccess)
    status = Rc95LdrNtfsBindVolume(&Ntfs, &NtfsFiles, Workspace,
                                   WorkspaceBytes, &Mounted);
/* Return Mounted from MountVolume; destroy provider and unmount Ntfs in UnmountVolume. */
```

NTFS metadata validation and file reads are implemented here; a full Windows boot still depends on the caller's platform block-I/O adapter, correct mount/unmount lifetime management, and WINLOAD's image/hive/driver preparation path.


## UEFI native NTFS mount lifecycle and Windows preflight

The UEFI-owned **uefi/uefinativemounts.c** module supplies concrete
RC95LDR_UEFI_DISCOVERY_OPS mount/unmount callbacks for FAT32 ESPs and NTFS OS
partitions. Initialize RC95LDR_UEFI_NATIVE_MOUNT_CONTEXT with
Rc95LdrUefiInitializeNativeMountsFromBootServices() and the validated UEFI
firmware Block I/O adapters. The convenience initializer uses
EFI_BOOT_SERVICES.AllocatePool(EfiLoaderData) / FreePool for mount scratch
and the exact NTFS provider workspace. The lower-level initializer remains
available for host tests and nonstandard allocators.

Wire Rc95LdrUefiMountNativeVolume and Rc95LdrUefiUnmountNativeVolume directly
to the matching discovery callbacks. The adapter opens the firmware handle
indexed by the discovered partition, verifies the complete GPT device-path
identity and logical-media geometry, rejects unsupported IoAlign
requirements, and mounts NTFS read-only. It permits one ESP and one NTFS
volume concurrently, which is the lifetime required while the ESP's BCD
store selects and validates the OS partition. The NTFS provider, MFT state,
block-I/O context and both workspaces remain alive until the exact returned
volume is unmounted.

Use Rc95LdrUefiValidateNativeWindowsVolume as
RC95LDR_UEFI_DISCOVERY_OPS.ValidateWindowsVolume. It validates files on the
exact BCD-selected NTFS volume before discovery succeeds:

- The BCD-selected LoaderPath is inside the selected SystemRoot, ends in
  winload.efi, and is an executable PE image for the running I386/AMD64 architecture.
- System32\ntoskrnl.exe and System32\hal.dll are valid PE images for that
  same architecture.
- System32\Config\SYSTEM is a valid registry hive with a resolvable
  CurrentControlSet.

The preflight reads each PE file in full (64 MiB per-image limit) before
calling Rc95LdrPeValidate, and reads the complete SYSTEM hive (256 MiB limit)
before opening it with the WINLOAD hive parser. Missing, truncated,
wrong-architecture, malformed, or oversized files fail closed. The preflight
is intentionally limited to core loader/kernel/HAL/hive files; boot-start
driver image paths are subsequently resolved and loaded by the WINLOAD
preparation path.

Example wiring (the platform supplies its partition-enumeration callback):

    RC95LDR_UEFI_NATIVE_MOUNT_CONTEXT Mounts;
    RC95LDR_UEFI_DISCOVERY_OPS Discovery;

    status = Rc95LdrUefiInitializeNativeMountsFromBootServices(
        &Mounts, &FirmwareOps, SystemTable->BootServices);
    if (status != Rc95LdrSuccess) return status;

    memset(&Discovery, 0, sizeof(Discovery));
    Discovery.Context = &Mounts;
    Discovery.MountVolume = Rc95LdrUefiMountNativeVolume;
    Discovery.UnmountVolume = Rc95LdrUefiUnmountNativeVolume;
    Discovery.ValidateWindowsVolume = Rc95LdrUefiValidateNativeWindowsVolume;

Rc95LdrUefiDestroyNativeMounts() is the final safety cleanup for early-return
paths. The normal discovery/WINLOAD flow unmounts the selected volume when
WINLOAD returns; a successful non-returning kernel handoff leaves loader-owned
allocations for the OS memory-map reclamation rules.

The standalone NTFS volume regression now mounts the same fragmented test
volume through the UEFI mount callbacks, opens a file through the returned
RC95LDR_VOLUME, reads across the physical run boundary, and verifies that
unmount releases both allocated workspaces. This is a synthetic NTFS fixture,
not a Windows installation image. No real Windows disk image or boot-file
set was supplied in this session, so the new preflight has not yet been run
against an actual Windows installation and no real-machine boot is claimed.


### One-call UEFI discovery initialization

For the production UEFI path, use the combined native discovery context rather
than manually assembling the callbacks. Rc95LdrUefiInitializeNativeDiscovery()
calls the existing UEFI V2 module-service initializer, obtains the concrete
Block I/O/device-path operations, uses the already validated partition
inventory, and initializes the native mount lifecycle. The returned discovery
operations include partition enumeration, ESP/NTFS mount/unmount and the
Windows boot-file preflight. Then obtain the
discovery table with Rc95LdrUefiGetNativeDiscoveryOps():

    RC95LDR_UEFI_NATIVE_DISCOVERY_CONTEXT NativeDiscovery;
    RC95LDR_UEFI_DISCOVERY_OPS Discovery;

    status = Rc95LdrUefiInitializeNativeDiscovery(
        &NativeDiscovery, &ImageContext, Architecture);
    if (status != Rc95LdrSuccess) return status;

    status = Rc95LdrUefiGetNativeDiscoveryOps(
        &NativeDiscovery, &Discovery);
    if (status != Rc95LdrSuccess) {
        Rc95LdrUefiDestroyNativeDiscovery(&NativeDiscovery);
        return status;
    }

    /* Pass Discovery to Rc95LdrDiscoverUefiWindows() for candidate discovery,
       or Rc95LdrUefiDiscoverAndStartWindows() for the integrated WINLOAD flow. */

DestroyNativeDiscovery() unmounts FAT32/NTFS before destroying the UEFI
service inventory. The inventory and its partition handles must remain live
for the full discovery and WINLOAD transaction. Do not call the module-service
destroy routine before the mount cleanup.

### Production UEFI one-call boot path

Rc95LdrUefiNativeDiscoverAndStartWindows() in uefi/nativeboot.c is the
platform entry orchestration that binds the pieces together. The caller
provides its validated UEFI image context, architecture, scratch
arrays/buffers and WINLOAD startup callbacks. The function then:

1. Initializes UEFI V2 module services and the firmware Block I/O/device-path
   inventory.
2. Enumerates the discovered GPT/MBR partitions.
3. Mounts FAT32 ESPs, reads the BCD store and selects the BCD-referenced
   Windows installation.
4. Mounts the exact selected NTFS OS partition and performs the strict
   loader/kernel/HAL/SYSTEM preflight.
5. Binds the mounted NTFS volume as the WINLOAD file provider and calls the
   existing Rc95LdrUefiWinloadEntry() path through
   Rc95LdrUefiDiscoverAndStartWindows().
6. On every returning path, closes file-provider scope, unmounts the OS
   volume, destroys mount state, then releases UEFI service inventory.

The native wrapper is built into rc95ldr_winload_uefi_entry; it does not
create a dependency cycle from the lower-level UEFI discovery library back
into WINLOAD. A successful kernel handoff does not return and follows the
existing firmware memory-map handoff contract.

This is the complete native UEFI integration path. Actual media validation
still requires a Windows installation image: the synthetic NTFS regression
only verifies mount lifetime, arbitrary-offset reads across fragmented runs,
and cleanup; it is not evidence that a real BCD store, SYSTEM hive or
Windows PE set has passed preflight.
