# UEFI ESP-to-NTFS Windows-volume discovery

## Goal and scope

UEFI firmware normally starts RC95LDR from an EFI System Partition (ESP), but
the Windows installation selected by BCD is not required to reside on that
partition. RC95LDR must therefore distinguish:

1. the **firmware/loader volume** containing `\\EFI\\Microsoft\\Boot\\BCD`;
2. the **Windows boot volume** named by the selected Windows Boot Loader
   object's `device` and `osdevice` elements; and
3. the **Windows directory** selected by the loader object's `path` and
   `systemroot` elements.

The ESP is a BCD-store source, not an implicit Windows root. Never use the
ESP as the NTFS boot volume merely because the EFI image was loaded from it.

## Discovery sequence

1. Enumerate the Block I/O handles retained by FreeLoader's UEFI
   `LocateHandleBuffer` initialization. Capture media presence, logical block
   size, last block, removable/read-only flags, IoAlign, and the handle/device
   path. Reject zero block sizes, overflowed capacities, and absent media.
2. Enumerate partition handles and associate each partition with its parent
   whole-disk Block I/O handle. Keep the firmware handle, device path, GPT
   partition type GUID, GPT unique partition GUID, start LBA, and block count.
3. Select ESP candidates by the EFI System Partition type GUID
   `C12A7328-F81F-11D2-BA4B-00A0C93EC93B`, not by FAT volume label.
   Mount FAT32 read-only. FAT12/FAT16 and non-GPT partitions are not ESP
   candidates for this UEFI path.
4. Probe `\\EFI\\Microsoft\\Boot\\BCD` on each ESP. Treat missing
   stores as a normal candidate miss; treat malformed hives as a hard error
   for that store and continue only according to the explicit multi-ESP
   fallback policy. Do not silently fall back to `\\Boot\\BCD` on the
   ESP unless a compatibility policy explicitly requests it.
5. Open and validate the hive using the common RC95LDR BCD parser. Resolve
   the Boot Manager object, its `default`/display-order references, and the
   selected Windows Boot Loader object. Validate every object GUID and
   element class before interpreting the payload.
6. Decode the loader object's `device` and `osdevice` separately. The
   current UEFI partition enumerator resolves GPT disk GUID + unique partition
   GUID descriptors; the shared BCD decoder also understands MBR signature +
   byte-offset descriptors, but UEFI MBR partition enumeration is not yet
   implemented. Locate-style, ramdisk, and virtual-disk descriptors remain
   unsupported and are never silently mapped to the ESP.
7. Mount the resolved `osdevice` through the NTFS adapter (FreeLoader ARC
   NTFS dispatch). Independently resolve and mount `device` when it differs,
   then verify the BCD loader application path. On the NTFS `osdevice`,
   verify the requested SystemRoot, `System32\\ntoskrnl.exe`, and
   `System32\\Config\\SYSTEM` before declaring discovery successful.
8. Return the mounted NTFS volume, selected `RC95LDR_WINDOWS_INSTALLATION`
   (BCD object GUID, loader path, SystemRoot, boot/OS partition records), and
   the ESP partition index. The BCD hive is copied into caller-owned memory;
   the ESP is unmounted before success is returned. The caller owns the
   returned NTFS volume and must release it through the matching unmount
   callback/context.
9. Pass the resolved volume and installation record to winload. The current
   code supplies this handoff contract; PE loading, kernel/HAL preparation,
   and kernel transfer remain future work.

## Device payload decoding contract

The generic BCD element decoder remains responsible for the outer element
class and bounds. Device decoding is a second dispatch keyed by the
`DeviceType` DWORD in the device descriptor. Each device decoder must:

- receive only the bounded `DeviceData` slice returned by
  `Rc95BcdDecodeElement`;
- validate its own version/length/signature before reading fields;
- normalize the result to a firmware-neutral identity (GPT GUID, MBR
  signature plus partition offset, ramdisk file reference, or VHD reference);
- preserve the original descriptor and element ID for diagnostics;
- return `Unsupported` for an unknown device type or unknown payload
  revision, and `CorruptData` for a known but malformed payload;
- never turn an unrecognized descriptor into "current boot partition".

The first implementation target is physical partition identity (GPT and MBR).
The follow-on decoders are locate/locate-ex, boot, ramdisk, and VHD. The
partition resolver must not perform filesystem parsing; it returns a block
device + partition extent for the filesystem layer.

## Shared interfaces and ownership

- UEFI Block I/O adapter: firmware handles, media and device-path metadata,
  raw block reads. No BCD or NTFS policy.
- GPT/MBR partition manager: partition extents and stable identities.
- FAT32 adapter: read-only ESP file access.
- BCD: object graph, element class and device payload interpretation.
- NTFS adapter: read-only NTFS volume mounting and file reads.
- Boot policy: select the BCD entry and resolve its device requirements.
- winload: PE loading, kernel/HAL, boot drivers, registry and loader block.

BIOS uses its own disk/partition adapter and can share BCD policy, partition
identity, NTFS, and winload. It must not call UEFI protocols. UEFI must not
depend on BIOS interrupts.

## Failure handling and diagnostics

Use distinct stages for firmware enumeration, ESP discovery, BCD open,
object selection, device decoding, partition resolution, NTFS mount, and
Windows-directory validation. Preserve the first meaningful failure while
continuing to another ESP candidate. Log candidate identities and reason
codes, but do not log raw BCD data or untrusted strings as format strings.

## Regression matrix

- One ESP + Windows on the same partition (compatibility case).
- ESP on disk A + Windows NTFS on disk B.
- Multiple ESPs, first without BCD and second with valid BCD.
- Multiple BCD stores with different default entries.
- GPT unique partition GUID match and mismatch.
- MBR signature/offset match and stale signature.
- Separate `device` and `osdevice` values.
- Missing, malformed, truncated, or checksum-invalid BCD.
- FAT32 ESP with invalid cluster geometry.
- NTFS target with invalid boot sector, corrupt MFT, or missing systemroot.
- Removable media, absent media, 4Kn media, and non-512-byte logical sectors.
- Unknown device type and known type with unsupported payload revision.

A successful BCD parse alone is not a successful Windows-volume discovery.

## Implemented WIP path

The branch now has an end-to-end discovery orchestrator plus FreeLoader UEFI
bindings:

1. `Rc95LdrFreeldrUefiGetFirmwareOps` exposes FreeLoader's initialized UEFI
   Block I/O handle table, media properties, aligned `ReadBlocks`, and
   device-path protocol access. The implementation is in
   `boot/freeldr/freeldr/arch/uefi/uefidisk.c` and uses the existing firmware
   handle inventory established by `UefiInitializeBootDevices`.
2. `Rc95LdrUefiEnumerateGptPartitions` parses GPT headers and entry arrays,
   validates their CRC32 values and usable-LBA ranges, detects FAT32/NTFS boot
   sectors, and associates logical partition handles using the UEFI hard-drive
   device-path unique partition GUID, start LBA, and block count.
3. `Rc95LdrFreeldrUefiInitializeDiscovery` wires partition enumeration and
   mount callbacks after `UefiInitializeBootDevices`. It uses the read-only
   FAT32 adapter for ESPs and FreeLoader's existing ARC NTFS dispatch for
   Windows partitions. `Rc95LdrFreeldrUefiDiscoverWindows` is the convenience
   entry point that initializes those callbacks and executes the complete
   discovery routine using caller-owned partition, candidate, hive, and
   scratch buffers. The caller must retain the returned NTFS volume and
   eventually release it through the matching unmount callback/context.
4. `Rc95BcdCollectWindowsBootCandidates` reads Boot Manager's Default and
   DisplayOrder, walks referenced object keys, and copies each qualifying
   Windows-loader object's `device`, `path`, `osdevice`, and `systemroot`
   into bounded candidate storage.
5. The common policy resolves `device` separately from `osdevice`,
   validates the BCD loader application path on the device partition, mounts
   the NTFS Windows volume, and validates `System32\\ntoskrnl.exe` plus the
   SYSTEM registry hive before returning the installation to winload.

The FAT32 adapter is read-only, supports standard FAT32 cluster chains and
VFAT long-name directory entries (up to 255 ASCII path characters per
component), uses 64-bit cluster-to-LBA arithmetic, and has bounded open-file
slots. It accepts
logical sectors up to 4096 bytes; 8Kn media can be enumerated by the Block I/O
layer but are not yet mountable by this FAT32 adapter. Standard GPT layouts
(128 entries of 128 bytes) fit in roughly 17 KiB of caller scratch; larger
entry arrays return `BufferTooSmall` unless the caller supplies more memory.
The NTFS
adapter intentionally reuses FreeLoader's existing NTFS ARC filesystem
implementation rather than adding a second NTFS implementation.

The device decoder supports physical MBR (signature + partition byte offset)
and GPT (disk GUID + unique partition GUID) partition descriptors. Other BCD
device forms such as locate, locate-ex, ramdisk, VHD, and virtual boot devices
remain unsupported. The GPT enumerator currently handles GPT whole disks;
MBR enumeration is not part of this UEFI path.

The host regression suite now includes UEFI GPT/device-path and FAT32
long-name/file-read fixtures, in addition to the existing BCD policy and
partition-resolution tests. These tests have been registered in CMake but
have not yet been run in a ReactOS/RC95-OS build environment.

