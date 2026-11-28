# BCD hive reader

The RC95LDR BCD subsystem owns REGF hive validation, key/value lookup,
typed element decoding, boot-object projection, and bounded transactional
hive mutation. `Rc95BcdOpenHive` validates the REGF base block, HBIN chain,
allocated/free cell boundaries, and root key before exposing any cell
references. Lookup and enumeration APIs return borrowed views into the
caller-owned image; `Rc95BcdDecodeElement` classifies the element format
nibble (bits 24..27) independently from its application type (bits 28..31)
and validates each supported scalar/list representation.

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
that the generated hive is a Windows-produced hive. It covers REGF/HBIN and
cell corruption, typed element payloads, object/element enumeration, boot
candidate projection, partition-device payloads, and transactional cell/value
updates. A sanitized Windows-exported hive remains the integration regression
input for compatibility with real-world registry index layouts.

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


## Missing and corrupt store diagnostics

The UEFI discovery path probes the expected ESP store at
`\\EFI\\Microsoft\\Boot\\BCD` on each eligible FAT32 ESP. A missing file
is treated as a candidate miss so another ESP can still be tried. If no
usable installation is found, the optional `TextUi` in
`RC95LDR_UEFI_DISCOVERY_OPS` reports a dedicated **Boot Configuration Data is
missing** screen when no store exists, or **Boot Configuration Data is corrupt**
when a present hive fails REGF/HBIN/cell validation or its boot-object walk is
malformed. Corrupt stores are rejected; they are never treated as empty/default
configuration. The diagnostic includes a detail code (`0xBCD00001` for missing,
`0xBCD00002` for corrupt) and recovery guidance. The legacy FreeLoader BIOS/UEFI boot-volume adapter exposes
`Rc95LdrFreeldrLoadBcdWithUi`; pass the active text UI to display the same
missing/corrupt diagnostics. The original `Rc95LdrFreeldrLoadBcd` entry point
remains available for callers that manage diagnostics themselves.


## In-place binary value updates

`Rc95BcdReplaceBinaryValue` validates the hive and replaces an existing
`REG_BINARY` value only when the replacement has exactly the same byte
length. It preserves registry cell/index layout and updates the REGF sequence
numbers and checksum. Missing values, non-binary values, and length changes
are rejected.

This is a constrained write primitive, not a general hive allocator. It cannot
create keys, values, objects, or new element cells, so it cannot yet create a
missing Windows loader object or add absent `device`/`osdevice` elements.
Full structural editing still requires free-cell allocation, HBIN growth,
index rebuilding, and transactional validation.

 
## Free-cell allocation

Rc95BcdAllocateCell performs a bounded first-fit allocation from an existing
free registry cell. Requested payload sizes are rounded together with the
four-byte cell header to an eight-byte boundary. The allocated cell is marked
used, its payload is zero-initialized, and remaining space is emitted as a
separate free cell. Splitting occurs only when the remainder can form a valid
eight-byte-minimum aligned cell; otherwise the entire original cell is used.

The returned cell offset is relative to the beginning of the hive bins, which
is the offset convention used by registry key/value references. The returned
capacity reports the actual payload capacity. The allocator updates both REGF
sequence numbers and the base-block checksum. It does not grow HBINs or the
image: exhaustion returns Rc95LdrOutOfMemory, and the original image remains
unchanged when no suitable free cell exists. The caller must still link the
allocated cell into the appropriate registry key/index structures.

The allocator first validates the complete hive with Rc95BcdOpenHive, then
scans the validated HBIN/cell chain. All size rounding and boundaries are
checked before mutation. It does not retain pointers across image relocation.


## Creating NK and VK cells

Rc95BcdCreateKeyCell creates a standalone compressed-name NK cell. It
initializes parent, subkey, value, security, and class references to safe
empty/sentinel values and stores a validated printable-ASCII key name.

Rc95BcdCreateValueCell creates a standalone compressed-name VK cell. Data
payloads up to four bytes are encoded inline in the VK cell; larger payloads
receive a separately allocated external data cell. The data length, registry
type, name length, and cell offsets are encoded explicitly in little-endian
form.

Both APIs require a distinct caller-provided scratch buffer at least as large
as the hive image. They build in the scratch copy, reopen/validate the result,
and copy it back to the caller's image only on success. When a parent is
provided, key creation rebuilds the parent's subkey index and value creation
appends the new VK cell to its value list transactionally. Cell offsets
returned by these APIs are relative to the start of the hive bins, matching
NK/VK references in the hive.

`Rc95BcdRebuildKeyStructure` rebuilds subkey and value-list structures while
preserving caller-specified order, and reclaims detached index/list cells
only after checking that they are no longer referenced. The variable-length
`Rc95BcdReplaceValueCell` operation creates replacement VK/data/list cells,
switches the parent link, reclaims obsolete cells, reopens the hive, and
verifies the replacement before committing. All structural mutations require
caller-owned scratch storage and leave the original image unchanged on failure.

`Rc95BcdEnumerateSubKeys` exposes bounded RI/LI/LF/LH traversal to consumers
without requiring them to parse registry index cells. `Rc95BcdEnumerateElements`
uses the same bounded-index approach for BCD element keys. Both APIs validate
the declared child count and every referenced NK/VK cell before invoking the
callback.


## Command-line tools

BCDBOOT uses the shared RC95LDR parser to validate the source template and the
resulting store. It resolves the destination volume to exactly one MBR/GPT
partition, writes the partition device descriptor using the MBR signature and
byte offset or GPT disk/partition GUID pair, creates the Windows OS-loader
object and its four required elements, appends the object to Boot Manager
DisplayOrder, sets it as Default, then reopens the hive and verifies that the
Windows-loader candidate walker can project at least one usable candidate.
The destination BCD file is replaced from a fully validated temporary image.

BCDEDIT uses the same parser for offline `/store ... /enum` operations. It
validates the complete hive, enumerates every object under `Objects` through
`Rc95BcdEnumerateSubKeys`, and decodes element payloads for display. It does
not load the hive into the live registry and does not mutate the store.
