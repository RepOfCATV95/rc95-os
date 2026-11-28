# Btrfs support policy for RC95LDR

## Decision

**Btrfs superblock discovery, read-only file access, and the UEFI WINLOAD provider bridge are implemented; end-to-end firmware boot validation remains outstanding.** VENEER is linked into the native BIOS Stage-1/RC95LDR path and is called during BIOS partition enumeration. It is deliberately read-only.

The current VENEER milestone validates primary and mirror superblocks, CRC32C, checksum type, geometry (including the on-disk device-item offsets and single-device size agreement), first-chunk-tree keys, single-device/single-stripe SYSTEM chunks using the correct on-disk block-group flag (`BTRFS_BLOCK_GROUP_SYSTEM == 2`, distinct from `METADATA == 4`), non-overlapping logical ranges, root-directory identity, complete-node root bounds, and partition-relative bounds. Host tests cover mirror fallback, nonzero partition starts, 4 KiB logical block devices, adjacent and overlapping chunk ranges, malformed keys, invalid root pointers, and rejection of RAID0, RAID1, RAID10, RAID5, RAID6, and DUP profiles (including parity layouts). A deterministic 1,024-case mutation pass exercises truncated buffers, malformed metadata, and checksum-valid mutations. An optional Clang libFuzzer target instruments both VENEER and its core dependency with AddressSanitizer and UndefinedBehaviorSanitizer; CI runs a bounded fuzz smoke test. It rejects unsupported chunk profiles and refuses a filesystem whose advertised total size exceeds the selected partition. It marks a partition as Btrfs only after successful validation. Unsupported checksum algorithms, multi-device volumes, and unsupported incompatibility flags fail closed.

WINLOAD now has an appended, size-guarded boot-filesystem attestation. If the selected OS volume is identified as Btrfs, WINLOAD requires both the native-VENEER-provider and read-only capability flags before it opens a kernel image. FAT32 and NTFS provider behavior is unchanged; legacy platform structures that do not expose this extension retain their previous behavior.

A minimal native Btrfs volume binding is now present under `boot/rc95ldr/fs/`. `Rc95LdrBtrfsMountPartition` snapshots the RC95LDR block-device descriptor, calls VENEER to validate the selected superblock, limits the exposed partition-relative device to the validated `total_bytes`, and publishes it through `RC95LDR_VOLUME.BlockDevice`. The exported device is read-only even when the underlying platform device has a write callback. `Rc95LdrBtrfsLoadChunkTree` now seeds a bounded logical-to-physical map from the validated SYSTEM chunk array, walks the chunk-tree B-tree using that bootstrap map, verifies node CRC32C/FSID/bytenr/owner/generation and leaf item bounds, and adds single-device, single-stripe chunk items. `Rc95LdrBtrfsMapLogical` exposes translations only after the complete chunk-tree walk succeeds. `Rc95LdrBtrfsLoadRootTree` then walks the validated root tree and locates the default filesystem tree (subvolume ID 5), validating the root-item generation, root bytenr alignment, and level before publishing `FsTreeRoot`, `FsTreeGeneration`, and `FsTreeLevel`. The new `Rc95LdrBtrfsFindFsTreeItem` API performs bounded B-tree separator routing in that selected filesystem tree and returns a caller-buffered raw item payload after validating node identity, CRC, generation, ordering, and leaf data bounds. The `Rc95LdrBtrfsReadInode` helper now decodes the fixed-size inode item, validates its generation and file type, and exposes size, allocation, mode, ownership, link count, flags, and sequence metadata. At this development stage, inode and inline-extent metadata were the available file primitives; later sections below record the subsequent regular-file, directory, and read-provider implementation. The UEFI mount lifecycle now initializes the Btrfs filesystem and binds it to the generic read-only file-provider ABI. In the direct BCD-selected OS-volume path, UEFI keeps that exact mounted volume in scope for WINLOAD and derives the Btrfs-native/read-only attestation from the selected partition. In a ramdisk/WIM path, WINLOAD instead reads from the selected WIM image; UEFI explicitly clears Btrfs attestation so the installed OS partition is never mistaken for the active provider. the BIOS Btrfs mount/provider binding is now implemented; BIOS BCD/WINLOAD selection and end-to-end firmware boot validation remain outstanding. The stable detail code `RC95LDR_DETAIL_BTRFS_PROVIDER_UNAVAILABLE` (`0x42544653`) remains the fail-closed result when the exact selected Btrfs OS volume lacks a complete native provider; malformed provider identities/capabilities use `RC95LDR_DETAIL_INVALID_FILESYSTEM_PROVIDER` (`0x46535953`).

RC95LDR's UEFI discovery and boot-file paths now include:

- FAT32, read-only, for locating and reading the EFI System Partition and BCD store.
- NTFS, read-only, through the native partition-relative block-device and NTFS file provider.
- Btrfs, read-only, through the native VENEER-backed filesystem provider for the exact selected OS volume when WINLOAD reads directly from that volume.

The UEFI discovery flow mounts the selected Btrfs partition and keeps its provider scope active through WINLOAD. Ramdisk/WIM boots instead bind the selected WIM image and do not claim that the underlying OS partition is the active file provider. BIOS Btrfs mount/provider binding is implemented; BIOS BCD/WINLOAD selection remains pending.


## VENEER and BIOS stage integration

Source component: `boot/rc95ldr/veneer/veneer.c`; public contract:
`boot/rc95ldr/veneer/include/veneer.h`.

The BIOS partition enumerator invokes VENEER after Stage 1 has established the
RC95LDR EDD block-device interface. It passes both the partition start LBA and
partition block count; VENEER checks all superblock mirror reads and validated
system-chunk stripe ranges against those bounds. Both BIOS and UEFI probe only
partitions whose existing boot-sector detector returned Unknown. The FAT32
and NTFS detection and mount paths therefore receive no additional Btrfs
mirror reads and are not redirected through Btrfs code. Stage 0 remains a fixed-layout bootstrap: it reads B95S at LBA 1 and
Stage 1 at LBA 2. Filesystem parsing is intentionally deferred until the
protected-mode RC95LDR services are available.

The current build target is the freestanding `rc95ldr_veneer` library linked
into the BIOS RC95LDR platform. It is not yet a separately loadable PE
`VENEER.EXE`; a standalone executable/module loader contract is still needed
before an actual on-disk EXE can be safely loaded and invoked.

## What remains in the repository

The legacy FreeLoader tree still contains Btrfs-related code:

- `boot/freeldr/freeldr/lib/fs/btrfs.c` and `boot/freeldr/freeldr/include/fs/btrfs.h` implement a legacy read path.
- `boot/freeldr/freeldr/bootsect/btrfs.S` remains part of the legacy boot sector sources.
- The OS also retains its Btrfs filesystem driver under `drivers/filesystems/btrfs/`, plus related filesystem-recognition and shell integration components.

Keeping those legacy files does **not** mean RC95LDR uses the legacy FreeLoader Btrfs reader. RC95LDR's separate native UEFI Btrfs provider is wired through its own firmware-neutral block-device and filesystem-provider interfaces.

The legacy reader is itself limited. Its source explicitly says that it does not support multi-device Btrfs volumes, and both inline and regular extents return read errors when compression is present. Consequently, its presence must not be advertised as complete Btrfs compatibility, and it is not a suitable implementation to copy wholesale into RC95LDR.

## Boot policy and expected behavior

1. UEFI discovery uses FAT32 for the ESP and resolves the exact BCD-selected OS partition before mounting its native filesystem provider.
2. A supported single-device Btrfs OS partition can be mounted read-only and used by WINLOAD through the scoped provider. Unsupported Btrfs profiles, virtual-device forms, and filesystem features are rejected with a clear diagnostic.
3. RC95LDR must not infer that the ESP is also the Windows volume, nor select an unrelated partition as a fallback.
4. If the exact selected Btrfs volume cannot be mounted or a requested file uses an unsupported feature, RC95LDR must stop before kernel handoff. The existing FreeLoader fallback remains available for configurations it can actually read.
5. This restriction applies to RC95LDR's boot-time file access only. It does not disable the in-kernel Btrfs driver after the kernel and required boot-start storage/filesystem drivers have initialized.

## Conditions for reconsidering Btrfs

Btrfs is now an independent, **read-only boot filesystem adapter** in the UEFI path. Remaining work is feature hardening, BIOS integration, and end-to-end firmware validation.

The existing native implementation is standalone and does not depend on FreeLoader private Btrfs state or the in-kernel driver. Remaining hardening work includes:

- Superblock mirror selection, checksum and feature-flag validation, bounds checks, and safe tree-node parsing.
- Explicit device discovery and chunk mapping. Single-device support may be an initial milestone, but multi-device profiles (including RAID profiles) must be rejected safely until implemented.
- Compression support for boot-volume formats, or continued explicit fail-closed rejection of compressed extents.
- Checksummed data verification before exposing file contents.
- Multi-device/RAID chunk profiles and device discovery, or continued explicit rejection.
- Symlink traversal and non-default subvolume/snapshot selection.
- Full Unicode path semantics and Btrfs case-sensitivity behavior.
- BIOS BCD/WINLOAD selection and firmware-independent end-to-end tests proving that the exact BCD-selected volume supplies every file passed to WINLOAD.

Writable Btrfs support is out of scope for the bootloader. Do not add Btrfs formatting, repair, transaction, or metadata-write paths to RC95LDR.

## Status summary

| Area | Status |
| --- | --- |
| In-kernel Btrfs filesystem driver | Retained in the OS source tree |
| Legacy FreeLoader Btrfs reader | Retained; limited, read-only boot-time use |
| RC95LDR VENEER superblock adapter | Implemented; CRC32C, strict single-device system-chunk validation and bounded discovery |
| RC95LDR Btrfs boot-volume discovery | BIOS and UEFI partition identification implemented |\n| RC95LDR Btrfs `RC95LDR_VOLUME` binding | Implemented; partition-relative read-only block device backed by validated VENEER metadata; can be bound to the native filesystem provider |\n| RC95LDR Btrfs chunk-tree reader | Implemented; validated SYSTEM chunks bootstrap a bounded chunk-tree walk and logical-to-physical map; single-device/single-stripe only |
| RC95LDR Btrfs root-tree reader | Implemented; locates and validates the default filesystem tree (subvolume ID 5); bounded single-device traversal |
| RC95LDR Btrfs filesystem-tree item lookup | Implemented; bounded separator routing, exact/predecessor/successor item lookup, and validated multi-leaf traversal |
| RC95LDR Btrfs inode metadata | Implemented; decodes validated regular-file, directory, and symlink inode items |
| RC95LDR Btrfs directory name lookup | Implemented DIR_ITEM lookup and component-by-component path resolver; exact UTF-8 byte matching, bounded collision bucket; no symlink following or case folding |

| RC95LDR Btrfs file access for WINLOAD | UEFI scoped provider bridge and BIOS read-only mount/provider binding implemented; BIOS BCD/WINLOAD selection and end-to-end BCD/firmware boot validation pending |
| RC95LDR Btrfs file provider | Implemented for read-only regular-file access, including inline, regular, preallocated, sparse-hole and cross-leaf/cross-extent reads; compression, checksummed data verification, non-default subvolume selection and symlink traversal remain pending |
| Btrfs writes/repair from RC95LDR | Not planned |

This policy is specific to RC95-OS. RC95LDR remains an RC95-OS-only bootloader and is not compatible with builds of the defunct CATV 95 LiberNT fork.


## Parser test and build matrix

The ordinary host regression target runs the deterministic Btrfs mutation corpus as part of `rc95ldr_veneer_btrfs_tests` and the read-only partition binding plus chunk-tree/logical-map checks as `rc95ldr_btrfs_volume_tests`. The separate `rc95ldr-btrfs-fuzz` CI job builds the optional libFuzzer target with Clang plus ASan/UBSan and runs a 30-second smoke session. The repository's main build workflow also compiles the RC95LDR integration in its existing GCC i386/amd64 and MSVC i386/amd64 build matrix; those are full repository builds, distinct from the standalone host regression tests. CI results must be checked for the exact branch commit before treating any compiler configuration as verified.


The Rc95LdrBtrfsReadFileExtent API looks up the exact (inode, EXTENT_DATA, file-offset) key. It validates the 21-byte file-extent header, generation, compression/encryption/encoding flags, and extent bounds. Inline data is returned only when uncompressed and within the caller buffer; regular and preallocated descriptors expose the logical disk bytenr, allocation length, extent offset, and file length without claiming that file data has been read. The API does not yet search for the predecessor extent covering an arbitrary file offset or perform disk I/O for file contents.

The inline extent regression also verifies that an undersized destination buffer returns the required safe failure and that compressed inline data is rejected; the parser does not silently copy compressed bytes as file contents.

The FS-tree item reader now also supports bounded predecessor-key lookup (`Rc95LdrBtrfsFindFsTreeItemAtOrBefore`). For a requested file offset, it routes to the greatest internal separator not greater than the key and selects the greatest matching leaf key not greater than the requested offset. `Rc95LdrBtrfsReadFileExtent` uses that lookup and verifies the requested offset lies within the returned inline/regular/preallocated extent. This is metadata lookup only; it does not yet read regular extent data or synthesize sparse/preallocated zeroes.

`Rc95LdrBtrfsReadExtentData` now reads bounded byte ranges from validated regular extents using a caller-provided one-device-block bounce buffer and the loaded logical chunk map. Preallocated extents are returned as zeroes. Inline extents continue to be copied by `Rc95LdrBtrfsReadFileExtent`; holes, compression, checksums for data sectors, and cross-device/RAID layouts remain unsupported. The caller must not expose file contents to WINLOAD until its requested range and extent metadata have been validated.


## Latest hardening checkpoint — 2026-11-27

The filesystem-tree directory resolver now distinguishes malformed directory records from valid Btrfs objects that are outside the bootloader's supported file-type contract:

- DT_UNKNOWN and valid special-file directory types return Rc95LdrUnsupported, rather than being misreported as corrupt metadata.
- Directory entries whose location key is a ROOT_ITEM are recognized as subvolume references and rejected with Rc95LdrUnsupported; they are not treated as inode numbers in the default filesystem tree.
- Inode-item directory locations must use key offset zero and must not carry unexpected directory-item data bytes.
- A disagreement between a supported directory-entry type and the referenced inode's supported mode remains Rc95LdrCorruptData.
- Directory-entry type values above the defined Btrfs range remain malformed.

Regression coverage now exercises subvolume-root references, DT_UNKNOWN, a special-file entry, and a supported-type/inode-mode mismatch. The generic file-read tests also cover zero-length reads at and beyond EOF, a one-byte read ending exactly at EOF, a read crossing EOF (including destination-buffer preservation), and an invalid opaque handle.

The read loop no longer carries an unused item-length local. These changes are source and test updates only; the host test executable and full build have not been run in this environment. The latest branch commit must be checked by CI before this checkpoint is considered build-verified.


## File-provider reliability checkpoint — 2026-11-27 (continued)

The current implementation supersedes the earlier file-access limitations recorded in the historical API notes above. The mounted Btrfs filesystem now exposes a generic read-only regular-file provider. It resolves paths from the default filesystem-tree root and reads inline, regular, preallocated, and sparse-hole ranges, including reads that cross filesystem-tree leaf boundaries and extent transitions. This is not a claim of complete Btrfs compatibility: compression, checksummed data verification, RAID/multi-device layouts, symlink traversal, and non-default subvolume selection remain unsupported.

Opaque file handles encode a slot index and generation. Closing a handle invalidates its token; reusing the slot advances the generation so an old token cannot access the new file. Regression coverage exercises close/reopen stale-token rejection and exhaustion of all `RC95LDR_BTRFS_FILE_HANDLES` slots. The exhaustion case also verifies that a failed open clears the output handle and size rather than leaving caller-provided sentinel values in place.

The Btrfs volume test also builds a synthetic multi-leaf filesystem tree and exercises a single read spanning inline data, a regular extent, sparse gaps, and a preallocated extent. The handle-table exhaustion test is source-level regression coverage; CI must compile and execute it before it is considered verified. No writable Btrfs path is introduced.


## Partial-read regression checkpoint — 2026-11-27

The synthetic multi-leaf filesystem-tree fixture now checks partial reads beginning inside an inline extent and continuing into the following regular extent. It also checks a read wholly inside the regular extent at a nonzero file offset, verifying that the physical data offset advances by the same delta. These cases complement the existing full-range read spanning leading sparse space, inline content, a regular extent, a sparse gap, and preallocation.

The parser-history notes above describe intermediate development states and should not be read as the current feature summary where they conflict with the status table or later checkpoints. In particular, bounded predecessor/successor lookup and regular/preallocated extent reads have since been implemented. Compression, checksummed data verification, multi-device/RAID layouts, symlink traversal, non-default subvolume selection, and WINLOAD integration remain outstanding.

## Latest extent-range regression coverage

The extent-data tests now exercise malformed regular-extent descriptors directly:
an extent offset beyond the disk allocation, an unrepresentable logical address,
and a requested range that exceeds the remaining disk allocation. These checks
complement the on-disk item parser tests by validating the lower-level extent
reader's defensive boundary behavior independently of B-tree decoding.

## Transactional file-read behavior

The Btrfs filesystem context accepts a caller-owned staging buffer and length.
The buffer is the transaction workspace for each nonempty `ReadAt`; requests
larger than its capacity return `Rc95LdrBufferTooSmall` before reading or
modifying the caller's destination. The implementation assembles sparse,
inline, regular, and preallocated bytes in staging storage and copies the
complete range to the destination only after every extent succeeds. Thus an
I/O, metadata, or mapping error leaves the destination unchanged. No heap
allocation is introduced.

The staging buffer must remain valid for the lifetime of the filesystem
context, must not overlap a `ReadAt` destination, and must be large enough for
the largest single read request. The caller must serialize reads that share
one filesystem context/staging buffer; concurrent reads would otherwise
overwrite the same transaction workspace.

Regression coverage injects an I/O failure after an inline prefix has been
staged and verifies that every destination sentinel byte remains unchanged.
It also verifies that an undersized staging buffer is rejected without
touching the destination, and that reads succeed after the injected fault is
disabled.


The transactional-read regression now also corrupts the identity of the next filesystem-tree leaf while preserving that leaf's checksum. The test performs a read that has already staged sparse, inline, and regular-extent bytes before it needs a successor lookup to locate the end of a later hole. It verifies that the successor lookup returns a metadata error and that none of the staged prefix is committed to the caller's destination.


Additional transactional-read regressions exercise three distinct failures after earlier bytes have been staged: a successor leaf with an out-of-node item payload range but a recomputed valid checksum, and a block-device I/O failure while reading the successor leaf itself. Both verify the caller's entire destination remains unchanged. Filesystem initialization is also checked to reject a null staging pointer and a zero-length staging workspace.

The multi-leaf transactional fixture also mutates the generation field of a regular extent that follows already-staged sparse and inline bytes. With the containing leaf checksum recomputed, the extent decoder must reject the generation mismatch as corrupt metadata while preserving the complete caller destination. Together, these cases distinguish extent-payload validation, successor-leaf item-bound validation, successor-node identity validation, and successor-node I/O failure.

The direct successor-lookup tests additionally verify strict-key exhaustion (NotFound after the final matching extent) and that both output fields are cleared when the search cannot proceed because the caller's node scratch buffer is one byte too small.


### Multi-level tree traversal regression coverage

The synthetic volume suite now exercises a level-two filesystem tree: a level-two root selects a level-one internal node, which selects two leaf nodes using distinct separator keys. Exact-item lookups target records in both leaves; predecessor and strict-successor lookups cross the internal-node boundary and select the globally correct key. The same fixture rejects mismatched child generations, non-increasing internal separator keys, and repeated leaf references during full successor scans.

The root-tree loader is also exercised with a level-one root and a separate leaf containing the default subvolume root item. The test verifies successful descent, rejects a child-generation mismatch, and rejects two distinct separator keys that alias the same child node. It restores the original single-leaf root tree after the regression cases.

These remain synthetic on-disk node fixtures. They test the native parser's traversal and validation paths but are not a substitute for mounting independently generated `mkfs.btrfs` images or testing mirrored/multi-device chunk layouts.


### Current integration boundary

The native Btrfs volume/filesystem API and generic read-only volume provider are implemented and covered by host tests. The production UEFI native-mount dispatcher now owns a single Btrfs mount state alongside FAT32 and NTFS. It validates the exact partition handle/device path, enforces a read-only one-device block adapter, allocates bounded caller-owned tree/item/inline/staging workspaces, loads the chunk and default root trees, initializes the generic filesystem interface, and releases every workspace on unmount or partial failure. The UEFI lifecycle regression mounts the synthetic Btrfs image through mocked firmware Block I/O, opens and reads a file through the mounted filesystem callbacks, verifies the block device has no write callback, and injects failure at each workspace allocation to check cleanup.

The UEFI WINLOAD flow now binds the provider created from the exact discovered OS-volume object into the loader-scoped provider scope. The shared WINLOAD file-I/O boundary (OpenFile, ReadFileAt, and CloseFile in osload.c) consumes that scope, translating bounded ASCII-compatible NT paths to root-relative provider paths and using provider seek/read operations. The original platform callbacks remain the fallback when no scoped provider is installed. The UEFI flow derives the Btrfs filesystem kind from the selected OS partition and sets the read-only/native capability bits only on its local platform copy while the exact-volume provider scope is active. WINLOAD's Btrfs policy now rejects those bits if no complete scoped provider is active; platform callbacks or capability bits alone do not authorize the filesystem. Host policy tests cover the no-scope rejection and active-provider acceptance, while the Btrfs volume test exercises file reads through the same generic provider ABI. The shared WINLOAD file boundary is a separately compiled unit and has direct regression coverage for NT-style path translation, bounded reads, seek-error propagation, short-read rejection, invalid path rejection, and closing a handle when file-information retrieval fails. The UEFI Btrfs mount regression now also attests the mounted Btrfs volume as the active WINLOAD provider, then opens and reads its /boot fixture through the exported WINLOAD file boundary while the provider scope is active, proving the mounted-volume-to-attestation-to-file-adapter path. UEFI provider attestation is now derived from the provider selected for the current boot transaction: direct OS-volume access can attest the selected partition's filesystem, while ramdisk/WIM-backed loading explicitly clears Btrfs attestation because WINLOAD is reading the WIM image, not the installed OS partition. A dedicated regression target covers direct Btrfs, direct NTFS, WIM-backed-on-Btrfs, and old-platform-ABI behavior. WINLOAD path-adapter tests also reject traversal, empty/repeated path components and trailing separators, and verify cleanup when a provider returns a handle alongside an open error. End-to-end BCD-selected boot-file loading and firmware CI verification remain outstanding; do not treat this provider-boundary integration as proof of a complete Btrfs Windows boot.

The multi-level filesystem-tree suite additionally checks that levels beyond the supported depth are rejected, child bytenrs are node-aligned, and child generations are nonzero and bounded by the selected filesystem-tree generation. These checks use checksum-valid mutated nodes so the regressions exercise structural validation rather than checksum rejection.

Root-tree traversal regressions also reject unaligned child bytenrs and non-increasing internal separator keys, in addition to generation mismatches and repeated child references.


## BIOS mount/provider checkpoint — 2026-11-28

The BIOS disk layer now exposes `Rc95LdrBiosMountBtrfs` and `Rc95LdrBiosUnmountBtrfs` alongside its FAT32 and NTFS mount helpers. The mount helper:

1. Requires the partition to have been classified as Btrfs by BIOS partition discovery and validates the existing EDD-backed discovery device.
2. Mounts the exact partition through the shared read-only Btrfs volume binding.
3. Loads and validates the chunk tree and default filesystem tree (subvolume 5).
4. Initializes the allocation-free Btrfs file provider using caller-owned scratch, item, inline, and transactional staging buffers.
5. Publishes the read-only block device and generic filesystem provider together in a caller-owned `RC95LDR_BIOS_BTRFS_VOLUME` object.
6. Tears down the entire object on any intermediate failure; unmount clears the filesystem/provider and volume bindings.

This is the BIOS mount/provider lifecycle, not yet the BIOS boot orchestrator. The current BCD discovery routine still selects NTFS Windows volumes; wiring the exact BCD-selected Btrfs OS partition into BIOS WINLOAD and adding a positive BIOS Btrfs image fixture remain the next steps. No Btrfs writes are exposed.


### BIOS WINLOAD provider-scope binding — 2026-11-28

The BIOS Btrfs mount object now owns the generic volume-provider context, provider descriptor, and provider-scope token. `Rc95LdrBiosBeginBtrfsFileProvider` initializes the common read-only volume adapter from the mounted Btrfs `RC95LDR_VOLUME` and activates it through `Rc95LdrBeginFileProviderScope`. WINLOAD's existing file-open/read/close boundary therefore reaches the same Btrfs filesystem interface used by the mount, without a second filesystem-specific WINLOAD parser.

`Rc95LdrBiosEndBtrfsFileProvider` unwinds the scope in LIFO order and clears provider state. BIOS unmount refuses to invalidate the mounted volume while its provider scope cannot be unwound, preventing a dangling process-wide provider pointer. The BIOS disk regression test exercises provider activation, duplicate-activation rejection, open/information/read/seek/close, scope teardown, and provider removal using a test filesystem interface.

This establishes the reusable BIOS-to-WINLOAD provider boundary, but does not yet select a Btrfs installation from the BCD discovery routine. `Rc95LdrDiscoverBiosWindows` still returns an NTFS-specific mount output and deliberately rejects a Btrfs OS partition. The next integration change must introduce a filesystem-neutral selected-volume result (or a parallel API) so the selected Btrfs mount and provider context can remain alive for the full WINLOAD transaction. A positive on-disk Btrfs BIOS boot fixture is still required.


### BIOS BCD-selected Btrfs volume handoff — 2026-11-28

`Rc95LdrDiscoverBiosWindows` now has an opt-in Btrfs result path through appended `RC95LDR_BIOS_DISCOVERY_OPS` fields. A caller supplies a stable `RC95LDR_BIOS_BTRFS_VOLUME` output object, caller-owned item/inline/staging workspaces, and `ValidateBtrfsWindowsVolume` callback. When the shared BCD policy resolves the selected OS partition as Btrfs, BIOS discovery mounts that exact partition, activates its common volume-backed WINLOAD provider scope, and invokes the callback against the resulting `RC95LDR_VOLUME`. On success it returns the selected installation and BCD partition index while leaving the Btrfs mount and provider scope alive for the caller's subsequent WINLOAD transaction. `WindowsVolume` remains the NTFS-specific output and is zero for this Btrfs result.

The Btrfs branch deliberately does not fall through to a lower-priority unrelated NTFS installation when the selected BCD entry names Btrfs. If the caller does not configure the Btrfs output, workspaces and validation callback, discovery returns an explicit unsupported status. If mounting or validation fails, the provider scope is unwound and the Btrfs volume is unmounted before trying the next candidate. Caller teardown order is `Rc95LdrBiosEndBtrfsFileProvider` followed by `Rc95LdrBiosUnmountBtrfs`.

The callback is responsible for validating the Windows loader, kernel and SYSTEM hive using the generic read-only volume/provider interface. The BIOS caller must then configure WINLOAD's Btrfs native/read-only attestation and invoke WINLOAD while the provider scope is active. This connects BCD selection to the exact Btrfs file provider, but a fully automatic BIOS boot coordinator and positive Btrfs disk-image integration fixture remain outstanding.


### BIOS Btrfs WINLOAD transaction — 2026-11-28

Added `Rc95LdrBiosStartWindowsFromBtrfs` in `bios/btrfsboot.c` with a dedicated caller-owned configuration. It invokes BIOS BCD discovery with the Btrfs mount output/workspaces and caller-supplied Btrfs Windows-volume validator, rejects an NTFS-selected result, requires the selected Btrfs volume and provider scope to be live, and then calls the shared `Rc95LdrBiosWinloadEntry`.

Before WINLOAD, the transaction binds the exact selected installation and Btrfs provider into the RC95 native-preparation input (when that native provider is selected), and sets the WINLOAD filesystem attestation to native read-only Btrfs on a transaction-local platform copy. The caller's original platform structure is not mutated. On every returning path, native-provider input is restored, the provider scope is ended, and the Btrfs mount is released. Successful kernel handoff is expected not to return.

The BIOS target now builds `btrfsboot.c` and registers `rc95ldr_bios_btrfsboot_tests` for argument/lifecycle validation. This is the first complete source-level BCD-to-Btrfs-to-WINLOAD transaction path; it still requires a positive BIOS Btrfs disk fixture and completed CI before it can be considered build-verified or boot-validated.


The native BIOS startup coordinator now exposes an alternative `BtrfsBoot` transaction pointer alongside `NtfsBoot`. For hard-disk startup it requires exactly one of these configurations and dispatches to the matching NTFS or Btrfs WINLOAD transaction; it will not choose an implicit/default filesystem. `RC95LDR_BIOS_STARTUP_VERSION` was advanced to 5 for the appended startup field. The normal BIOS startup path can therefore enter the Btrfs transaction without an external caller bypassing the coordinator.


### Built-in BIOS Btrfs Windows-volume validation — 2026-11-28

The BIOS Btrfs boot configuration now selects `Rc95LdrBiosValidateBtrfsWindowsVolume` by default when no custom validation callback is supplied. It reads files through the mounted `RC95LDR_VOLUME` filesystem interface and verifies the BCD-selected loader and `SystemRoot/System32/ntoskrnl.exe` as MZ/PE images, then verifies that `SystemRoot/System32/config/SYSTEM` is at least 4 KiB and begins with the registry hive `regf` signature. It uses a caller-owned 4 KiB probe buffer and closes each filesystem handle on success and failure. No write interface is used.

The optional custom callback remains available for platform-specific checks. The built-in path intentionally accepts ASCII Windows paths only at this checkpoint; non-ASCII UTF-16 path characters fail closed with `Rc95LdrUnsupported` rather than being lossy-converted. The validator checks basic PE signatures, not machine type, section layout, signatures, boot-start driver consistency, or full hive structure. Those are further validation milestones, not claims of full Windows boot compatibility.

A mocked filesystem regression test covers a valid loader/kernel/hive set, corrupt kernel PE signature, and handle closure. This is not a substitute for the still-needed positive BIOS Btrfs disk-image fixture and end-to-end firmware boot test.
