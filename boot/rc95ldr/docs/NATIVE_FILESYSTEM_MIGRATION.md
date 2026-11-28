# RC95LDR native filesystem and FreeLoader compatibility boundary

## Ownership model

RC95LDR owns the filesystem contract in `boot/rc95ldr/core/include/rc95ldr.h`:
block reads, mounted-volume interfaces, bounded file handles, and read-only
random-access file operations. Filesystem implementations belong under
`boot/rc95ldr/fs/`; they must not call BIOS interrupts, UEFI protocols, or
FreeLoader ARC routines directly. Firmware-specific block adapters provide
the `RC95LDR_BLOCK_DEVICE` contract.

RC95LDR has no build-time or runtime dependency on FreeLoader. Its BIOS and
UEFI frontends provide native block-device adapters; FAT32 and NTFS providers
consume the RC95LDR block-device contract. The FreeLoader fallback retains
its own ARC filesystem implementation and is built independently. It must not
be included, called, or linked by RC95LDR.

- **Native RC95LDR FAT32:** `fs/fat32.c` mounts a partition from an RC95LDR
  block device and exposes a read-only `RC95LDR_VOLUME`. UEFI ESP BCD reads
  use this implementation.
- **Native RC95LDR NTFS:** the NTFS geometry, MFT, index, and bounded file-read
  modules operate on an RC95LDR block device. BIOS/UEFI platform code supplies
  the device; the native NTFS path remains deliberately read-only and has the
  limitations listed below.

The native NTFS implementation is not yet a fully qualified replacement for
every Windows-volume workload. Unsupported streams, fragmented data layouts,
and incomplete index-tree cases must fail closed. FreeLoader remains a
separate recovery option for installations its own filesystem stack supports.

## Native filesystem status

The native NTFS layer now includes geometry, mapping-pairs, FILE-record, and
volume-bootstrap modules. Geometry parsing validates the NTFS OEM/signature,
logical-sector and cluster geometry, selected-partition bounds, MFT/MFT-mirror
locations, and signed file-record/index-buffer size encodings. The mapping-pairs
decoder validates variable-width lengths and signed LCN deltas, rejects
physical run aliasing, checks VCN/LCN bounds and arithmetic, and requires an
explicit terminator. FILE-record processing validates the update-sequence
array and every sector trailer before applying fixups, then walks the complete
bounded attribute list and exposes resident values or non-resident mapping
pairs with their size/VCN metadata. `Rc95LdrNtfsMountPartition` validates the
partition boot sector, reads and fixes up MFT record 0, extracts the initial
non-resident `$MFT::$DATA` run map, and `Rc95LdrNtfsReadMftRecord` reads and
fixes up later records through the discovered runs. Host fixtures cover
fixup failures, attribute bounds, MFT bootstrap, record reads, and MFT EOF.
The resident `$I30` filename index-root parser now locates the named INDEX_ROOT
attribute in a fixed-up directory FILE record and validates the index-root
header, filename collation, index-buffer geometry, index-header bounds, entry
lengths, child VCN trailers, and terminal entry. It exposes bounded index
entries and decodes the embedded FILE_NAME key (parent reference, UTF-16 name,
and namespace). A separate `INDX` allocation-buffer parser validates the
buffer signature, expected VCN, update-sequence-array geometry, index-header
bounds, and terminal entry before exposing child entries. The caller must
apply USA fixups before passing an allocation buffer to this parser. A FILE-reference validator rejects stale sequence numbers, unused records,
and mismatched nonzero NTFS 3.x record numbers. The volume API now reads an
MFT record by its 64-bit file reference and validates that identity before
returning it to directory consumers. The native file reader now consults a directory's resident `$I30`
INDEX_ROOT first and validates the selected FILE reference before returning a
child. If the root does not contain the name, it retains a bounded MFT scan
fallback so directories whose entries live in index-allocation buffers remain
discoverable while full tree traversal is implemented. Ordinary unnamed
resident and single-extent nonresident `$DATA` prefixes can be read through
the partition-relative block interface. This remains an initial read-only
metadata/file path, not a complete NTFS filesystem: it does not yet traverse
the `$I30` index-allocation tree/bitmap, merge fragmented file `$DATA`
attributes through `$ATTRIBUTE_LIST`, or support sparse, compressed,
encrypted, and nonresident attribute-list streams. The BIOS validator uses this native path for its loader/kernel/SYSTEM probes.
FreeLoader's ARC NTFS implementation is retained only inside the independent
FreeLoader fallback; RC95LDR does not call or link it. Native NTFS still needs
full-volume and end-to-end BIOS/UEFI boot qualification before it can be
considered complete.

The FAT32 implementation is read-only and currently provides:

- strict FAT32 BPB geometry validation (including FAT32-only total-sector/FAT-size fields, version, cluster-size limits, and reserved ExtFlags bits) and partition-bounded sector reads;
- FAT32-only cluster-count validation and active-FAT selection;
- cluster-chain traversal with explicit rejection of free, reserved, bad,
  and out-of-volume cluster markers;
- root/subdirectory lookup, short names, and VFAT long-name lookup;
- bounded open-file slots and random-access reads with EOF checks;
- 512-, 1024-, 2048-, and 4096-byte logical sectors.

It does not yet provide writes, FAT repair, FAT12/FAT16 mounting, Unicode
case folding beyond ASCII, 8Kn sectors, or full VFAT alias/Unicode semantics.
The path API is intentionally narrow and read-only. Unsupported names and
formats must return an explicit status rather than being guessed.

The host test fixture covers a VFAT directory lookup, multi-cluster file read,
reserved-cluster rejection, and EOF bounds. It is registered as
`rc95ldr_fat32_tests` in the host CMake project and CI workflow. A registered
test is not evidence that CI has passed; inspect the workflow result for the
specific commit before declaring validation successful.

## FreeLoader fallback isolation contract

FreeLoader remains available as a separately built fallback and continues to
own its legacy ARC startup, firmware/disk enumeration, and ARC NTFS driver.
That retained fallback is not an RC95LDR service provider or link dependency.

The ownership boundary is enforced in both directions:

- RC95LDR source and build files must not include `freeldr.h`, call
  `ArcOpen`/`ArcRead` or other FreeLoader ARC filesystem entry points, or link
  the FreeLoader library.
- FreeLoader source and build files must not include RC95LDR headers, call
  `Rc95Ldr*` APIs, or link RC95LDR targets.
- RC95LDR may construct ARC-compatible disk and memory descriptors required by
  the NT kernel handoff ABI. Those data structures are an OS handoff contract,
  not a call into FreeLoader's ARC runtime.
- If native RC95LDR initialization or a selected filesystem is unsupported,
  the system may offer the independent FreeLoader fallback; RC95LDR must not
  silently invoke FreeLoader internals.

The two source-level isolation checks are registered with the RC95LDR test
suite so this boundary cannot regress unnoticed.

## Native NTFS implementation sequence

1. **Block and volume contract:** use the same partition-relative block device
   used by native FAT32; validate sector size, volume length, and arithmetic
   before reads.
2. **Boot-sector validation:** parse NTFS OEM ID, bytes/sector, sectors/cluster,
   total sectors, MFT and MFT mirror LCNs, and signed record-size encodings.
   Reject unsupported geometry before reading metadata.
3. **Runlists and MFT records (initial implementation present):** geometry,
   mapping-pairs decoding, USA fixups, bounded attribute iteration, MFT
   file-reference sequence validation, and `$MFT::$DATA` run-map bootstrap
   are implemented. Resident `$ATTRIBUTE_LIST` entries now resolve unnamed
   `$MFT::$DATA` extension records, validate base-record references and
   attribute IDs/VCNs, and merge physically non-overlapping extents. A second
   caller-owned record buffer is required when extensions are present.
   Nonresident `$ATTRIBUTE_LIST` streams and compressed/encrypted MFT metadata
   remain explicitly unsupported and fail closed.
4. **Directory and file access (initial indexed lookup present):** parse the
   resident `$I30` root; when the name is not in that root, read the directory's
   `$INDEX_ALLOCATION` stream and consult its `$BITMAP` before applying INDX
   USA fixups and checking allocated buffers. Resolve matching file references
   through the MFT sequence number. Ordinary directory lookup no longer scans
   the MFT for `$FILE_NAME` attributes. Lookup follows B-tree separator keys
   and child VCNs from the resident root into only the selected INDX branch.
   Every visited buffer is checked against `$BITMAP`, VCN bounds, USA fixups,
   and repeated-child detection; matching FILE references are sequence-checked.
   This removes the scan of every allocated INDX buffer for ordinary lookups.
   Directory `$INDEX_ALLOCATION` and `$BITMAP` now use the shared bounded
   multi-extent stream mapper. It validates resident `$ATTRIBUTE_LIST` entries,
   base and extension file references/sequences, attribute IDs/names, contiguous
   VCN coverage, mapping-pair bounds, and physical non-aliasing before publishing
   a stream map. Sector-aligned reads cross fragmented runs and extent boundaries;
   sparse/compressed/encrypted streams and nonresident `$ATTRIBUTE_LIST` data
   remain unsupported and fail closed.
   Filename ordering currently uses ASCII case folding, not the complete NTFS
   `$UpCase` collation table, so non-ASCII names are not fully supported.
   Lookup remains read-only.
5. **Boot-critical validation:** read the BCD-selected loader, kernel, HAL,
   SYSTEM hive, and boot-start driver files through the native interface.
6. **Compatibility comparison:** run identical fixture images through native
   NTFS and FreeLoader ARC NTFS; compare file sizes, contents, error classes,
   and rejection behavior.
7. **Default-path promotion:** use native NTFS only after host regressions,
   sanitizer/fuzz coverage, and BIOS plus UEFI boot tests pass. Keep FreeLoader
   as an explicit fallback until the replacement has demonstrated equivalent
   behavior across supported RC95-OS layouts.

## Non-goals and fail-closed policy

Btrfs is not part of this native FAT32/NTFS milestone. Existing OS-tree Btrfs
support and FreeLoader compatibility code remain untouched. RC95LDR must not
claim to mount Btrfs or substitute a different NTFS volume when the selected
Windows installation is Btrfs-backed. Unsupported Btrfs layouts fail closed
before WINLOAD handoff.

## Next validation work

- Extend MFT fixtures to cover nonresident `$ATTRIBUTE_LIST` streams,
  extension records that are themselves located in later extents, malformed
  attribute names, and compressed/encrypted metadata rejection.
- Extend `$I30` lookup with multi-level tree, fragmented-run, bitmap-tail,
  stale-sequence, malformed-buffer, and duplicate/cyclic-child regression cases.
  The shared multi-extent stream mapper and resident directory
  `$ATTRIBUTE_LIST` extension resolver are implemented; add explicit end-to-end
  directory fixtures that force reads across an extent boundary.
- Add non-resident file-stream reads across fragmented mapping pairs, sparse
  ranges (explicitly rejected for boot-critical inputs initially), initialized
  size/EOF handling, and short/corrupt block-device reads.
- Add path tests for empty components, overlong components, separators,
  case-folding, and unsupported non-ASCII names.
- Run the native filesystem host suite in CI and build both FreeLoader BIOS
  and UEFI targets to catch adapter ABI/integration regressions. The latest
  GitHub Actions run has been queued but has not reported a result yet.


## Native UEFI FAT32 volume adapter

`uefi/uefivolume.c` now provides an RC95LDR-owned read-only FAT32 mount
adapter for a validated UEFI logical Block I/O handle. It resolves the exact
firmware handle recorded by GPT discovery, checks the media block size and
partition extent, validates the hard-drive device-path node against the GPT
unique partition GUID/start LBA/length, and only then exposes a partition-
relative `RC95LDR_BLOCK_DEVICE` backed by UEFI `ReadBlocks`.

The adapter rejects non-FAT32 partitions, stale or mismatched handles,
non-present media, mismatched geometry, and `IoAlign` values greater than
one. The latter is intentionally fail-closed until an RC95LDR-owned aligned
bounce-buffer allocator is wired into the firmware service contract. The
volume must be unmounted with `Rc95LdrUefiUnmountFat32Volume`.

This is a native firmware-to-FAT32 volume path, not yet a complete native
UEFI boot path: the caller still supplies firmware protocol callbacks, and
the shared UEFI discovery orchestrator still receives its mount operation
through `RC95LDR_UEFI_DISCOVERY_OPS`. Native NTFS mounting, UEFI entry/boot
services ownership, and the final `uefildr.efi` image remain outstanding.
The standalone host target `rc95ldr_uefi_volume_tests` validates the exact
partition binding, FAT32 mount, unmount, GUID mismatch, alignment rejection,
and invalid handle behavior.
