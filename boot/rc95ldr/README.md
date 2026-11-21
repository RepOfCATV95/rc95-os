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
long-name entries and logical sectors up to 4096 bytes. The NTFS adapter is a
FreeLoader ARC bridge, not a newly implemented NTFS driver. BCD locate,
ramdisk, VHD, and other virtual-device forms remain unsupported, and PE image
loading/kernel handoff are not implemented.

RC95LDR is an RC95-OS-only bootloader. It is not compatible with builds of the
defunct CATV 95 LiberNT fork (October 15, 2026 through November 12, 2026).


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
