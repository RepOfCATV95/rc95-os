# BCD hive reader

The BCD subsystem is read-only. `Rc95BcdOpenHive` validates the REGF base block,
HBIN chain, allocated cell boundaries, and the root key. Object and element
lookups return raw registry value bytes; `Rc95BcdDecodeElement` classifies
the format nibble from bits 24..27 (the application type is in bits 28..31)
and decodes scalar integer/boolean forms while leaving device payload tails
as bounded byte views.

## Volume discovery

`Rc95BcdLoadFromVolume` accepts an `RC95LDR_VOLUME`: a block-device reference
plus a mounted filesystem adapter. The filesystem adapter owns partition and
filesystem translation and implements open/read-at/close. The BCD loader
uses caller-provided storage, enforces the reported file size against capacity,
reads the complete file, validates it, and always closes the handle. No heap
allocator or writable filesystem operation is required.

The adapter is expected to perform reads through the associated
`RC95LDR_BLOCK_DEVICE`; the common core exposes checked `Rc95LdrReadBlocks`
for its underlying block-I/O implementation. This keeps BIOS, UEFI, and future
NTFS adapters outside the BCD parser.

## Regression tests

Configure with `-DRC95LDR_BUILD_TESTS=ON` to build the synthetic fixture test.
It creates a minimal REGF hive with a root key and a valid `li` subkey index,
then corrupts cell extents, index signatures, and sequence numbers. It also
checks typed integer decoding and invalid scalar lengths.

For a real-world regression input, export a BCD store on Windows:

```bat
bcdedit /export rc95-bcd-real.hiv
```

Pass the exported hive path to `rc95ldr_bcd_tests`. The test opens the file
read-only and validates the complete hive. Keep machine-specific BCD exports
out of source control unless they have been reviewed and sanitized; they can
contain machine identifiers and boot configuration details.

The fixture test is intentionally small and deterministic. It does not claim
that the generated hive is a Windows-produced hive, and it does not yet
exercise every BCD element payload format or every registry index variant.

## Typed payload decoding

`Rc95BcdDecodeElement` validates the payload class selected by the element
identifier and returns non-owning views into the hive image:

- Device: little-endian device type and additional-options DWORDs, followed
  by a bounded device-specific byte view. Device-specific tails are deliberately
  not interpreted as a universal structure; the device type determines their
  format and future versioned decoders must validate those formats separately.
- String: validated UTF-16LE, including surrogate-pair checks.
- Object: a 16-byte GUID decoded into its Windows GUID fields.
- Object list: a DWORD count followed by exactly that many packed GUIDs.
  Use `Rc95BcdReadGuid` for endian-safe access to each entry.
- Integer: one little-endian 64-bit value.
- Boolean: one little-endian DWORD restricted to 0 or 1.
- Integer list: a DWORD count followed by exactly that many 64-bit values.
- Binary: a bounded opaque byte view.

All views are borrowed from the original value and become invalid when the
caller releases or replaces the hive image. Decoders do not allocate memory.

## FreeLoader filesystem adapter

`boot/freeldr/freeldr/lib/rc95ldr/rc95ldrfs.c` bridges the common RC95LDR
volume interface to FreeLoader's ARC file and raw-device APIs. It opens the
currently selected `FrLdrBootPath` as a raw block device, then uses the
existing ARC filesystem dispatch for file operations. Block reads use
checked LBA-to-byte conversion and bounded, exact ARC reads.

The exported `Rc95LdrFreeldrLoadBcd` entry point searches the boot volume:

- BIOS build: NTFS `\\Boot\\BCD` only.
- UEFI build: `\\EFI\\Microsoft\\Boot\\BCD` first, then
  `\\Boot\\BCD`; NTFS is accepted in either location, while FAT is
  accepted only when FreeLoader identifies the mounted volume as FAT32.

FAT12/FAT16 and FATX are not accepted as UEFI BCD sources by this adapter.
The selected boot volume is resolved through FreeLoader's existing ARC device
registration, so the same adapter is compiled into BIOS and UEFI FreeLoader.
The caller supplies the BCD image buffer; the hive parser remains independent
of ARC and firmware APIs.

This ARC bridge remains the legacy selected-boot-volume BCD reader. The new
RC95LDR UEFI discovery orchestrator is separate: it enumerates GPT disks and
all ESP candidates, mounts FAT32 ESPs, reads each BCD store, resolves the
Windows loader's `device` and `osdevice` independently,
then mounts and validates the selected NTFS Windows installation. It uses
this ARC adapter for NTFS file access rather than requiring the ESP and
Windows volume to be the same partition.


## Boot Manager object walker

`Rc95BcdCollectWindowsBootCandidates` walks the BCD Boot Manager object
(`{9dea862c-5cdd-4e70-acc1-f32b344d4795}`). It decodes the Default object
reference and DisplayOrder object list, resolves each listed object under
`Objects`, and extracts the Windows-loader application-device, application-path,
OS-device, and SystemRoot elements. Candidate-owned storage keeps the decoded device
views, UTF-16 loader application path, and UTF-16 SystemRoot stable after the
walker returns. Entries without
the required loader elements are skipped; malformed hive structures and
malformed typed elements fail closed.

The resulting candidate array is consumed by
`Rc95LdrSelectWindowsInstallation`: the valid enabled default is preferred,
then remaining enabled entries are tried in DisplayOrder. BCD `device` and
`osdevice` are resolved independently; the boot-files volume may be the
FAT32 ESP, while `osdevice` must uniquely identify an NTFS partition.

## Device payload support

The current partition payload decoder recognizes the BCD partition device
type and the MBR signature/byte-offset and GPT disk-GUID/partition-GUID
payload forms. Locate, locate-ex, ramdisk, VHD, and other virtual-device
descriptors are not yet decoded.
