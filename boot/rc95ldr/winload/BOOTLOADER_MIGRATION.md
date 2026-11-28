

## Native executable extraction checkpoint (2026-11-24)

The BIOS build graph now separates three ownership layers:

- `rc95ldr_bios_platform`: BIOS interrupt gateway, EDD block I/O, memory
  discovery, and text-console services.
- `rc95ldr_winload_bios_entry`: RC95LDR protected-mode entry and the BIOS
  frontend to the common WINLOAD coordinator.
- `rc95ldr_bios_stage`: the real-mode transition and protected-mode stage
  coordinator, linked as its own component rather than hidden inside the
  WINLOAD policy-facing entry archive.

The split makes the stage and executable entry independently selectable by a
future image target and ensures the BIOS stage source is compiled by the normal
RC95LDR build. It does **not** create a bootable BIOS image: the stage still
expects a preceding loader to read the stage/image and descriptors into RAM,
and a concrete platform provider must still supply file I/O, native image
preparation, and the final handoff.

UEFI remains at the policy-shim stage. `rc95ldr_winload_uefi_entry` validates
firmware identity and enters the same coordinator, but there is not yet an
RC95LDR-owned PE/COFF `EfiMain`, Loaded Image/device-path bootstrap, native
Block I/O/file provider, memory-map/ExitBootServices transaction, or standalone
`winload.efi` target. Therefore the ROSLOAD and UEFILDR build descriptions and
their legacy compatibility implementation must remain in place until those
firmware entry targets exist and pass end-to-end tests. Removing their imports
or changing image packaging before then would produce an unbootable gap, not a
completed migration.

### Next implementation gates

1. Implement and test the BIOS first-stage EDD manifest reader and image
   placement, including a defined on-disk layout and boot-sector integration.
2. Supply a concrete BIOS platform provider and link a BIOS WINLOAD executable
   whose imports are checked to exclude FreeLoader.
3. Implement UEFI `EfiMain` and native protocol adapters using the SDK's UEFI
   definitions; bind Loaded Image, device path, Block I/O, filesystem access,
   memory allocation/map, text input/output, and GOP into RC95LDR contracts.
4. Implement the UEFI memory-map key retry and `ExitBootServices` handoff, then
   link a PE/COFF WINLOAD EFI application with no FreeLoader imports.
5. Add image/installer selection and boot-media tests for both firmware paths.
6. Only after both replacement images boot, remove ROSLOAD/UEFILDR execution
   paths and their FreeLoader link dependencies; retain FreeLoader only for
   unrelated legacy compatibility uses, if any.


## BIOS EDD image-format work (2026-11-24)

The v1 B95I manifest now has a reproducible image packer at
`boot/rc95ldr/bios/tools/mkrc95biosimage.py`. It emits the existing packed
536-byte manifest at LBA 1, pads it to logical-sector boundaries, lays out
payload extents consecutively, and computes IEEE CRC-32 over the exact
sector-padded bytes consumed by `Rc95LdrBiosLoadManifestImage`. The documented
layout reserves LBA 0 for a caller-supplied boot sector and uses LBA 3 as the
first payload LBA for 512-byte sectors.

This is the on-disk manifest and image-construction portion only. The packer
does not produce a bootable sector. The missing real-mode EDD bootstrap must
load a protected-mode bridge (or otherwise enter the existing BIOS gateway)
before calling the C manifest consumer. In particular, the current manifest
consumer is a protected-mode service and must not be called directly from
16-bit real mode.

A standalone BIOS `winload.exe` is also **not yet linked**. The current BIOS
stage requires a concrete provider that owns persistent startup descriptors,
the EDD-backed file/volume operations, allocator and reservation state,
native preparation callbacks, and the final non-returning kernel handoff. The
native PE entry ABI must construct this provider context before entering
`Rc95LdrBiosStageMain`; simply setting the PE entry point to
`Rc95LdrBiosI386Entry` would be incorrect because that routine expects the
protected-mode EAX/EBX handoff protocol, not the Windows PE entry ABI. Do not
remove ROSLOAD or claim a bootable `winload.exe` until this provider, PE entry
wrapper, linker target, and firmware boot test are implemented.
