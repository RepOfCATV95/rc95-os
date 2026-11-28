# RC95LDR architecture and migration plan

## 1. Boot pipeline

The intended pipeline is explicit and ordered:

    Firmware entry
      -> RC95LDR startup
      -> platform adapter (BIOS or UEFI)
      -> memory + block-device services
      -> partition discovery (MBR/GPT)
      -> BCD store discovery and policy
      -> selected boot application
      -> volume mount + filesystem reads
      -> NTFS boot-volume implementation
      -> OS image / boot-driver loading
      -> loader parameter block + memory map
      -> firmware shutdown / handoff
      -> NT kernel entry

The same policy and OS-loading pipeline must be used by BIOS and UEFI. A
platform adapter supplies firmware-specific operations through a narrow
interface; it does not duplicate BCD parsing, partition discovery, NTFS, PE
loading, kernel preparation, or handoff policy.

## 2. Interface boundaries

### Firmware/platform adapter

Required operations:

- initialize the platform and establish a known execution environment;
- expose a normalized physical memory map;
- enumerate firmware boot devices and expose sector-based reads;
- provide console output and optional framebuffer information;
- expose time/delay services where available;
- perform the correct firmware shutdown/reset operation;
- perform the final architecture/firmware handoff.

BIOS implementation uses the selected PC firmware boot protocol and must
explicitly handle the transition from real mode/protected mode to the loader's
execution mode. UEFI implementation uses loaded-image, device-path, block-I/O,
simple-text-output, GOP, memory-map, and exit-boot-services protocols as
appropriate. UEFI boot-services pointers must not be used after successful
ExitBootServices.

### Block and partition services

Block I/O exposes logical-sector reads with checked LBA/count arithmetic,
device geometry, stable device identifiers, and normalized errors. Partition
discovery is a separate layer and validates MBR signatures, GPT headers,
header CRCs, entry-array CRCs, usable-LBA ranges, and all partition extents.

### BCD service

BCD is a registry-hive-backed configuration store, not an INI file. It must
validate the hive before exposing objects/elements. The BCD policy layer maps
typed BCD objects and elements into an internal boot configuration; firmware
code must not know registry cell offsets or BCD element encodings.

### Filesystem and NTFS

Filesystem mounting consumes a partition/volume interface, not firmware
handles. NTFS owns boot-sector validation, volume geometry, MFT bootstrap,
attribute/runlist traversal, directory lookup, and file reads. The boot path
must not use the NTFS implementation to locate BCD before the BCD/volume
discovery layer has selected and mounted the system store volume.

### OS loader

The OS loader consumes file handles and platform memory services. Its stages
are PE validation, image placement/relocation, import resolution, kernel/HAL
selection, boot-start driver loading, SYSTEM hive loading, loader-block
construction, memory descriptor finalization, and transfer to the kernel.
The loader-block ABI must be versioned and tested independently of firmware.

## 3. BCD policy and object model

The store reader exposes:

- store validation and read-only open;
- object enumeration and lookup by GUID;
- element lookup by typed identifier;
- typed decoding for strings, integers, booleans, GUIDs, device descriptors,
  and lists;
- boot manager default object, display order, boot sequence, and timeout;
- Windows boot application path, system device, OS device, system root,
  load options, and boot status policy.

Mutation is a separate capability. Write support must not be enabled until
transactional hive updates, cell allocation/freeing, bin growth, checksums,
sequence numbers, and power-failure-safe replacement are implemented and
tested. Read-only booting comes first.

## 4. Failure model

All public subsystem calls return a status. A status identifies both the
stage and a stable reason (invalid firmware data, unsupported device,
I/O failure, corrupt partition metadata, corrupt BCD hive, missing BCD object,
unsupported filesystem feature, corrupt NTFS metadata, invalid PE image,
out-of-memory, or handoff failure). No subsystem may reinterpret an I/O error
as an empty file, missing BCD value, or end-of-directory.

## 5. Migration gates

### Gate A — RC95LDR common core
- canonical naming and documented ownership;
- common status/error model and boot-stage reporting;
- platform-neutral block, partition, volume, file, and image interfaces;
- compatibility wrappers for current FreeLoader call sites.

### Gate B — BCD
- hive validation and safe cell traversal;
- object/element enumeration and typed element decoding;
- boot-manager selection semantics;
- read-only BCD fixtures, malformed-hive tests, and integration with boot menu;
- write transactions only after read-only boot is reliable.

### Gate C — BIOS and UEFI
- BIOS and UEFI use the same common boot pipeline;
- each firmware adapter passes device, memory-map, console, and reset tests;
- UEFI ExitBootServices retry/map-key behavior is covered;
- BIOS disk reads and mode transitions are covered;
- legacy fallback remains available until the new path boots test images.

### Gate D — NTFS
- boot-sector and geometry validation;
- MFT bootstrap including fragmented $MFT support;
- resident/nonresident attributes and runlist validation;
- directory lookup, long names, and case folding;
- sparse/compressed/encrypted attributes fail explicitly until supported;
- bounded file reads across fragmented extents;
- malformed metadata and I/O fault-injection fixtures.

### Gate E — OS loading
- PE/COFF validation and relocation;
- kernel/HAL and boot-start driver dependency ordering;
- SYSTEM hive and required boot configuration loading;
- loader parameter block and memory-map ABI validation;
- clean firmware handoff and kernel-entry smoke tests.

A gate is not complete because code compiles. It requires tests at the
subsystem boundary and a boot integration test on each supported firmware.
