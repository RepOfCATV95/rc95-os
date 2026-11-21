# FreeLoader and rosload migration into RC95LDR

This plan moves responsibilities, not files blindly. Keep the existing
FreeLoader BIOS and UEFI entry points as compatibility front ends until each
replacement has a tested equivalent. Preserve ARC boot compatibility while
callers are converted. Do not import leaked Windows source; use the repository's
permitted FreeLoader/ReactOS-derived implementation and public interface
contracts as behavioral references.

## Target ownership

| Existing source/role | Destination | Replacement boundary |
| --- | --- | --- |
| FreeLoader bootsector stages, `frldr16`, real-mode entry and BIOS disk bootstrap | `rc95ldr/entry/bios` | Small real-mode stage transfers to protected/long-mode loader; BIOS disk services are isolated behind platform block I/O |
| UEFI entry and firmware setup | `rc95ldr/entry/uefi` and `platform/uefi` | EFI application entry, image/device-path capture, GOP, memory map, Block I/O and exit-boot-services lifecycle |
| ARC path parsing and ARC device naming | `rc95ldr/arc` | Compatibility parser/formatter maps old ARC paths to normalized device and partition identities; no storage implementation |
| ARC disk, partition, RAM disk and SCSI enumeration | `rc95ldr/storage` | Common device model, partition manager, firmware-specific block adapters |
| FreeLoader filesystem dispatch and file API | `rc95ldr/fs` | Read-only filesystem interface, FAT32 ESP adapter, NTFS adapter, then other filesystems |
| FreeLoader boot options, INI parsing, OS list/menu and custom options | `rc95ldr/policy` and `rc95ldr/ui` | BCD is primary policy; legacy INI/ARC options remain a compatibility provider |
| FreeLoader display, video and boot graphics | `rc95ldr/bgfx` | Firmware-neutral progress/diagnostic interface with GOP and legacy framebuffer backends |
| FreeLoader platform-specific hardware services | `rc95ldr/platform/{bios,uefi}` | Firmware services only; no OS selection or filesystem parsing |

## FreeLoader bootsector, ARC, real-mode and boot-options sequence

### Stage A — inventory and contracts

1. Inventory every `CreateBootSectorTarget`, `frldr16`, boot-sector binary,
   linker entry, real-mode assembly file, BIOS interrupt wrapper and disk
   bootstrap dependency in `pcat.cmake` and architecture directories.
2. Define versioned `RC95LDR_BOOT_CONTEXT`, firmware context, block-device,
   partition, memory-map, console and handoff contracts.
3. Define an ARC compatibility layer that parses legacy paths into a stable
   `device-id + partition-id + path` tuple. Preserve spelling/round-trip tests.
4. Define a boot-option provider interface. BCD, legacy INI, command line and
   recovery options become providers; the policy engine consumes normalized
   options rather than parsing formats itself.

### Stage B — BIOS and real-mode

1. Move the boot-sector source and generation rules under
   `boot/rc95ldr/entry/bios/bootsector`; retain output compatibility and
   byte-for-byte boot-sector regression fixtures.
2. Keep the real-mode stage minimal: establish known CPU state, collect BIOS
   drive identity, load the next stage with bounded reads, and transfer to
   protected mode. Do not put BCD, NTFS, menu or ARC policy in 16-bit code.
3. Move BIOS interrupt wrappers and EDD/CHS fallback into
   `platform/bios`. The block adapter exposes LBA reads and reports geometry
   and media errors; upper layers never issue INT 13h.
4. Move BIOS memory-map discovery and A20/CPU transition code behind startup
   contracts. Add tests for EDD absence, CHS bounds, geometry overflow,
   partial reads, and boot-drive preservation.
5. Keep the old FreeLoader bootsector as the default until the new image
   passes BIOS/CSM boot tests on both i386 and AMD64 configurations.

### Stage C — UEFI

1. Introduce a separate EFI application entry and UEFI platform adapter.
2. Enumerate Block I/O and device paths; discover ESPs by GPT type GUID.
3. Mount FAT32 ESPs read-only, locate BCD, and resolve the BCD-selected
   Windows NTFS partition (see `UEFI_ESP_NTFS_DISCOVERY.md`).
4. Keep all firmware protocol calls in the UEFI adapter. Capture the memory
   map and perform ExitBootServices only at the final point where winload
   no longer needs boot services.
5. Retain the existing `uefildr.efi` until the replacement passes x86 and
   AMD64 Secure Boot policy (where supported), GOP/no-GOP, multiple-disk,
   and memory-map-change tests.

### Stage D — ARC and legacy boot options

1. Move ARC name parsing, device aliases, partition syntax and path conversion
   into `rc95ldr/arc`; ARC is a compatibility namespace, not the internal
   device model.
2. Move boot option parsing and normalization into `rc95ldr/policy/options`.
   Preserve legacy INI behavior as a provider, including defaults and
   documented option precedence.
3. Move menu/OS-list presentation into `rc95ldr/ui`; menu code receives
   already validated policy entries and cannot open disks or parse BCD.
4. Convert consumers incrementally: boot device selection, ramdisk, Linux
   chainload, custom boot entries, recovery options and debugging options.
5. Remove duplicate FreeLoader implementations only after all consumers use
   the new interfaces and compatibility tests pass.

## rosload to winload migration (IA-32 and AMD64)

The current `rosload.cmake` source list is the migration inventory. Move
functionality by responsibility into `boot/rc95ldr/winload`, not by retaining
the rosload executable as a hidden dependency.

| rosload source/function | winload destination |
| --- | --- |
| `bootmgr.c`, `custom.c`, `oslist.c`, `options.c` | BCD-backed boot policy and UI remain in RC95LDR policy/UI; winload receives one selected entry and resolved devices |
| `linuxboot.c`, `miscboot.c` | RC95LDR chainload providers, separate from Windows winload |
| `ntldr/advopts.c`, `ntldropts.c`, `conversion.c` | Windows boot-option normalization and compatibility conversion |
| `ntldr/inffile.c` | Boot-driver INF dependency reader under winload |
| `ntldr/registry.c`, `wlregistry.c`, `cmboot.c`, `ke/config.c` integration | SYSTEM hive loading, control-set selection and boot registry preparation |
| `ntldr/winldr.c`, `wlmemory.c` | Common PE image loader, memory placement, relocations and loader memory descriptors |
| `ntldr/arch/i386/winldr.c` | `winload/arch/ia32`: IA-32 calling convention, page tables, PAE policy, context and kernel handoff |
| `ntldr/arch/amd64/winldr.c` | `winload/arch/amd64`: AMD64 relocations, page tables, long-mode context and kernel handoff |
| `ntldr/resume.c` | Architecture-neutral resume orchestration plus per-architecture restore hooks |
| `arch/i386/halstub.c`, `ntoskrnl.c` | IA-32 compatibility HAL/NT kernel handoff support only where still required |
| `disk/scsiport.c` | Boot-storage miniport support under a narrowly defined boot-driver interface; storage enumeration itself stays in RC95LDR |

### Winload component boundaries

- `winload/pe`: PE32/PE32+ validation, section mapping, relocations, imports,
  checksum/architecture checks and image lifetime.
- `winload/kernel`: kernel and HAL selection, loading and compatibility checks.
- `winload/drivers`: boot-start driver discovery, group/tag ordering,
  dependency resolution and image loading.
- `winload/registry`: SYSTEM hive loading, control-set selection and boot
  driver/service configuration.
- `winload/memory`: loader heap, page-table preparation, memory descriptors,
  firmware-reserved ranges and architecture-specific constraints.
- `winload/loaderblock`: stable loader parameter block construction and
  validation.
- `winload/arch/ia32` and `winload/arch/amd64`: architecture-only mechanics.
- `winload/handoff`: final quiesce and transfer to the kernel entry point.

The common PE/registry/driver algorithms should be shared between IA-32 and
AMD64 where their data model permits it. Never compile IA-32 pointer-width
assumptions into AMD64 structures. Use explicit fixed-width on-disk and
loader-block fields, architecture-sized virtual addresses, and compile-time
layout assertions.

## Build and target migration

1. Keep `freeldr.sys` and `uefildr.efi` packaging unchanged while RC95LDR
   libraries are compiled into host tests and compatibility front ends.
2. Add independent `rc95ldr_bios` and `rc95ldr_uefi` targets with explicit
   architecture/platform source sets. Do not share firmware-specific sources.
3. Add `winload_ia32` and `winload_amd64` library/test configurations before
   changing the final executable or handoff.
4. Move one responsibility at a time; each stage must build in i386 and AMD64
   configurations and retain the legacy path as a fallback.
5. Change image names, boot image packaging and installer references only
   after the replacement target is selected by the actual boot flow.

## Required gates before deleting legacy code

- Host unit tests for parsers, bounds, BCD policy, partition resolution and PE
  metadata; malformed inputs must fail closed.
- i386 and AMD64 build matrix, including MSVC and GCC/Clang configurations
  supported by the tree.
- BIOS/CSM and UEFI VM boots, with separate ESP and NTFS disks.
- Real hardware coverage for disk controllers, removable media and firmware
  quirks where available.
- Boot traces proving selected BCD object, resolved device identity, kernel,
  HAL, boot drivers, loader block and final entry address.
- Recovery path and legacy ARC/INI compatibility tests.

## RC95-OS product boundary and LiberNT incompatibility

RC95LDR is designed exclusively for **RC95-OS**. It is not a general-purpose
FreeLoader replacement, a shared bootloader SDK, or a compatibility layer for
other operating-system forks. In particular, RC95LDR is **incompatible with
builds of the defunct CATV 95 fork of LiberNT**, whose development window was
October 15, 2026 through November 12, 2026.

Do not link RC95LDR libraries into CATV 95 LiberNT builds, reuse its boot
artifacts, or assume that its loader-block, startup, filesystem, firmware,
configuration, or kernel-handoff contracts match RC95-OS. The two projects
must retain separate build targets, image packaging, configuration formats,
ABI/structure definitions, and boot integration. Shared code is permissible
only after an explicit source-level review establishes a genuinely
OS-independent component and both projects independently validate its
contract; this does not make RC95LDR itself cross-compatible. The end of the
CATV 95 fork does not change RC95LDR's RC95-OS-only scope.

## Implemented discovery and policy contracts

The WIP branch now contains a callback-driven UEFI discovery orchestrator in
`boot/rc95ldr/uefi`. It enumerates normalized partition records, visits every
GPT/FAT32 ESP candidate, mounts it through the platform adapter, loads
`\\EFI\\Microsoft\\Boot\\BCD`, asks the BCD object walker to supply
bounded Windows-loader candidate views, applies the common policy, mounts
the selected NTFS `osdevice`, and calls a platform/filesystem validator.
The selected NTFS volume remains mounted on success and is returned with the
installation record; the caller owns its eventual unmount. Failed ESP
candidates are unmounted before trying the next candidate.

The BCD device decoder currently supports the bounded physical partition
payload forms:
- MBR: disk signature plus partition byte offset.
- GPT: disk GUID plus unique partition GUID (the partition unique GUID is
  used for normalized partition matching).

The winload policy interface prefers a resolvable enabled default Windows
loader entry, then falls back to the lowest display-order resolvable enabled
Windows loader entry. It decodes `device` and `osdevice` independently and
requires the `osdevice` to resolve uniquely to NTFS; `device` is resolved
independently and may identify a FAT32 ESP or another supported boot-files
volume. Its output is a
`RC95LDR_WINDOWS_INSTALLATION` record containing the selected BCD object ID,
boot/OS partition records, `winload` application path, and `systemroot` text. That record and the mounted
OS volume are the handoff inputs for future winload PE/kernel loading code.

The FreeLoader UEFI integration now supplies concrete adapters:
- EFI Block I/O handles are enumerated from FreeLoader's UEFI handle table;
  media properties are copied into RC95LDR's portable block interface and
  reads use the existing aligned UEFI bounce buffer.
- Device paths are walked and hard-drive media nodes are parsed. GPT logical
  partition handles are associated with GPT entries by unique partition GUID,
  start LBA, and block count rather than partition-size heuristics.
- GPT headers and partition-entry arrays are validated with CRC32, checked
  bounds, entry-size/count limits, and usable-LBA ranges.
- The read-only FAT32 adapter mounts ESP volumes and reads short names plus
  VFAT long names, including the standard `Microsoft` directory.
- NTFS mounting/file access uses FreeLoader's existing ARC NTFS filesystem
  dispatch through `Rc95LdrFreeldrOpenVolume`; RC95LDR does not fork or
  duplicate the NTFS implementation.
- `Rc95BcdCollectWindowsBootCandidates` walks Boot Manager's Default and
  DisplayOrder elements, resolves each referenced object, and extracts the
  Windows-loader `device`, `path`, `osdevice`, and `systemroot` elements into
  candidate-owned bounded storage.

The UEFI FreeLoader bridge exposes
`Rc95LdrFreeldrUefiInitializeDiscovery`. Call it after
`UefiInitializeBootDevices`, supplying caller-owned scratch memory; it
initializes partition enumeration, FAT32/NTFS mount/unmount, and Windows
volume-validation callbacks. The discovery orchestrator uses the built-in BCD
walker when no custom candidate callback is supplied.

The physical partition decoder currently supports MBR (disk signature plus
partition byte offset) and GPT (disk GUID plus unique partition GUID). The
policy independently resolves BCD `device` and `osdevice`; the former may
identify the FAT32 ESP, while the latter must resolve uniquely to NTFS. It
prefers a valid enabled default entry, then tries enabled Windows loader
entries in display order. The output
`RC95LDR_WINDOWS_INSTALLATION` carries the loader application path, system
root, selected boot/OS partitions, and BCD object ID alongside the mounted
NTFS volume. These are the inputs for future winload PE/kernel loading code.

The filesystem adapters are read-only. FAT32 currently supports the FAT32
cluster layout and ASCII path names (including VFAT LFN directory entries);
it is not a general-purpose Unicode filesystem implementation. The NTFS
adapter depends on FreeLoader's ARC filesystem registration and the UEFI
partition-to-ARC path mapping. BCD locate, locate-ex, boot, ramdisk, VHD, and
other non-partition device payloads remain unsupported. The winload target
currently implements selection policy only; PE32/PE32+ loading, registry,
drivers, memory preparation, loader block, architecture-specific handoff, and
actual kernel transfer remain future work.

## Current implementation status

The branch now includes EFI Block I/O/device-path enumeration, GPT CRC
validation and partition association, a read-only FAT32 adapter, a FreeLoader
ARC-backed NTFS adapter, BCD Boot Manager/Windows-loader object walking,
physical MBR/GPT BCD device decoding, and winload installation-selection
policy. Concrete PE image loading, registry/driver preparation, architecture
loader blocks, and kernel handoff remain unimplemented. The migration and
compatibility gates above remain mandatory before legacy FreeLoader/rosload
code is retired.
