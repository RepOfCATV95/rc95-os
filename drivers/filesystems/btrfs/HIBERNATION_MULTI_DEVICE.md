# Multi-device Btrfs hibernation mapping interface

## Purpose

`FSCTL_BTRFS_GET_HIBER_MAPPING` exposes the chunk layout needed by a dump/hibernation writer that can target more than one physical member. It complements, rather than changes, `FSCTL_GET_RETRIEVAL_POINTERS`. The latter is a legacy single-LCN interface and must not be interpreted as a complete mapping for striped or parity chunks.

The request is a versioned `btrfs_hiber_mapping_request` containing a Btrfs logical address, nonzero sector-aligned length, exact structure size, and ABI version (`BTRFS_HIBER_MAPPING_VERSION`). Reserved and unknown request flags must be zero. The response includes the ABI version and fixed-header size so consumers can validate the variable-length tail. The control code uses `METHOD_OUT_DIRECT`: the request is in the system buffer and the variable-length response is in the MDL-backed output buffer. The driver currently restricts this physical-layout query to kernel-mode callers. It takes the filesystem tree and chunk locks in the established tree-before-chunk order while copying member identities, preventing concurrent device teardown from invalidating the response.

The response contains:
- `logical_address` and `segment_length`: requested start and the portion covered by this response. The segment is bounded by both the chunk boundary and the next stripe boundary.
- `chunk_logical`, `chunk_length`, `stripe_length`, `chunk_type`, `sub_stripes` and `sector_size`: on-disk chunk geometry and profile bits.
- One `btrfs_hiber_mapping_stripe` per chunk stripe: Btrfs `devid`, device UUID, physical stripe base offset, member size, and read-only/seeding/missing state flags. Missing members are represented explicitly with a stable devid/UUID and zero device size; a missing member is not silently converted into a malformed response. Header flags summarize missing, degraded and read-only members. The driver checks the complete rounded-up per-member chunk allocation span against each present device size, not just the stripe base offset.

The output is variable length. If the buffer is too small, the driver returns `STATUS_BUFFER_TOO_SMALL` and reports the required byte count in `IoStatus.Information`. Allocate that size and retry.

## Consumer requirements

The returned identifiers are deliberately not kernel object pointers. A writer must resolve each `(devid, UUID)` to an opened member device, verify the UUID again, retain the device reference/handle for the entire dump session, and reject members marked missing, replaced, read-only or seeding, as well as undersized members. The mapping is a snapshot, not a lifetime pin: the caller must keep the special-file usage notification active for the entire dump session. It must also keep the hibernation/paging usage notification active so balance and device removal cannot relocate the extents.

For logical delta `d = logical_address - chunk_logical`, let `S` be `stripe_length` and `N` the stripe count:
- SINGLE: physical offset is `stripe[0].physical_offset + d`.
- DUP/RAID1/RAID1C3/RAID1C4: the same logical bytes exist at `base + d` on each mirror; the writer may choose a healthy member and retain a failover choice.
- RAID0: `stripe_number = d / S`, `stripe_index = stripe_number % N`, `stripe_set = stripe_number / N`; physical offset is `stripe[stripe_index].physical_offset + stripe_set*S + (d % S)`.
- RAID10: `data_stripes = N / sub_stripes`; select the mirror group beginning at `(stripe_number % data_stripes) * sub_stripes`, with `stripe_number = d / S`; physical offset within each mirror is `base + (stripe_number / data_stripes)*S + (d % S)`.
- RAID5/RAID6: parity rotation and parity generation are required. The current driver interface reports the layout but does not implement parity writes. A consumer without a verified parity implementation must reject these profiles.

The caller must validate `version`, `header_size`, `size`, `stripe_count`, and all reserved fields before consuming the stripe array. Non-RAID10 `sub_stripes` values are reported unchanged and must not be used for placement; only RAID10 uses that field to select mirror groups. Logical address, requested length, chunk base, stripe length and returned segment are sector-aligned. The caller advances by `segment_length` and queries again. It must not assume a segment is contiguous across a stripe or chunk boundary. All arithmetic must be checked for overflow and device bounds.

## Compatibility and current limitations

The existing retrieval-pointer path continues to return a valid single-device LCN only for layouts it can represent. It deliberately returns an invalid LCN for striped/parity layouts rather than inventing a physical address.

This change establishes the driver-side mapping ABI; it does **not** by itself make Windows' standard hibernation dump stack multi-device-aware. A dump-writer integration must consume this control code and implement the profile algorithms above before multi-device hibernation/resume can be claimed. RAID5/6 remain unsupported until parity-aware dump writes are implemented and tested.

## Kernel chunk-to-member mapping helpers

`ntoskrnl/po/hibdump_btrfs_map.h` adds a side-effect-free consumer layer for
the existing `FSCTL_BTRFS_GET_HIBER_MAPPING` response. It validates the
versioned variable-length header, reserved fields, segment/chunk bounds,
sector geometry, stripe identities and member bounds, then translates a
logical byte range into one or more member-relative physical targets.

The helper currently computes SINGLE, DUP, RAID1, RAID1C3, RAID1C4, RAID0 and
RAID10 placements. It caps mirror targets at four, rejects unavailable,
read-only or seeding members, checks every physical end offset, and explicitly
rejects RAID5/6 rather than issuing incomplete parity writes. It performs no
allocation, device lookup, reference acquisition or I/O; callers must copy
validated targets into nonpaged state and separately retain each resolved
member for the complete dump session.

`PoHiber` regression coverage exercises RAID0 stripe selection, RAID1 mirror
offsets, RAID10 mirror-group selection, parity rejection, missing-member
rejection, member-bound failures and malformed reserved fields.

The mapper now also enforces profile topology before producing targets:
SINGLE requires one stripe; DUP requires two stripes on the same device;
RAID1 requires two stripes; RAID1C3/C4 require three/four; RAID0 rejects
sub-stripe layouts; and RAID10 requires at least two data stripes with at
least two copies per stripe. Logical addresses and transfer lengths must be
sector aligned. Stripe-row multiplication and physical-offset additions are
checked for 64-bit overflow, and requests are clipped at stripe boundaries
where striping requires a new target calculation. The regression suite covers
these accepted and rejected layouts, degraded-state rejection, unaligned
requests, and stripe-boundary clipping.

This is the chunk-logical-to-member-physical half of the mapping pipeline. It
does not yet translate hibernation-file VCNs/file offsets into Btrfs logical
extents, resolve and pin member devices, or connect the helper to the polled
writer. The raw multi-member dump path therefore remains disabled and the
filesystem-mediated fallback remains mandatory.

## Kernel dump-stack preflight status

`ntoskrnl/po/hibdump.inc` now queries `FileFsAttributeInformation` before issuing the private device-list FSCTL. Non-Btrfs filesystems retain the legacy single-disk path; Btrfs filesystems must successfully report one present, writable member with a valid device ID and size. A second member, a missing/read-only member, malformed list metadata, or a Btrfs driver too old to implement the device-list query disables the polled ATA path.

The device-list parser is isolated in `ntoskrnl/po/hibdump_btrfs.h` and covered by `modules/rostests/kmtests/ntos_po/PoHiber.c`. The dump preparation path also checks that preallocation succeeds, validates every retrieval extent against the containing partition bounds, and coalesces adjacent physical extents to reduce nonpaged map usage.

This is still a conservative single-member preflight, not multi-device dump writing. The kernel writer does not yet translate file VCNs into Btrfs logical addresses, resolve and pin each `(devid, UUID)` member, or issue polled writes to multiple devices. It therefore does not consume `FSCTL_BTRFS_GET_HIBER_MAPPING` yet. Multi-device volumes remain on the normal filesystem-mediated path.

## Resume display handoff

After `PopHiberSystemHandler` has restored execution and the power manager has
sent the system-wide return-to-working-state transitions, `ntoskrnl/po/power.c`
calls `InbvSetDisplayOwnership(FALSE)` on a successful device restore. This
marks the boot-video display as relinquished without replacing the reset
callback that VIDEOPRT registered through `InbvNotifyDisplayOwnershipLost`.
The call is deliberately outside the low-level restore/interrupt-disabled
path and is skipped when device restoration fails, preserving the boot-video
diagnostic path for that failure case. The resumed WIN32K/VIDEOPRT session is
then free to continue presenting the existing desktop or lock screen; this
does not bypass console-lock policy or synthesize a user logon.

## Multi-member writer enablement gate

### Writer integration boundary and ABI checks

The kernel-side mapping consumer now has compile-time layout assertions for the
version-1 stripe and mapping structures, matching the offsets in
`drivers/filesystems/btrfs/btrfsioctl.h`. The kmtest fixtures were corrected
to use valid two-stripe RAID1 and DUP layouts; previously the strict topology
checks would correctly reject those four-stripe fixtures. Conflicting profile
bits are now covered as a corrupt-layout case.

The current Btrfs retrieval-pointer implementation does not expose logical
extents for arbitrary multi-member files: it calls
`get_hiberfile_disk_cluster()`, which only succeeds when a chunk stripe maps
to the mounted volume's real device. Other member placements are emitted as
sparse/unavailable LCNs. Therefore the dump stack cannot yet build a complete
file-offset-to-logical-extent plan from `FSCTL_GET_RETRIEVAL_POINTERS`.

Even after that file-extent query is added, the existing `PO_DUMP_STACK`
contains exactly one legacy-ATA command/control port pair and one selected
device. The Btrfs mapping response deliberately returns stable `devid` and
UUID identities rather than raw kernel pointers or polled-I/O endpoints.
There is currently no kernel contract that resolves each member to a retained
dump-capable transport and pins its lifetime across hibernation.

For that reason, the mapping helper is not connected to a multi-member write
loop and the multi-member gate must not be removed. Doing so would risk writing
a RAID0/10 member using a different member's ATA endpoint, or silently omitting
mirror writes. The next required interfaces are (1) a kernel-only Btrfs
file-VCN-to-logical-extent query, and (2) a storage dump-stack/member binding
contract that returns retained, polled-write-capable endpoints for every
member. Only then can a precomputed nonpaged write plan safely call the
profile mapper and fan out sectors, propagate per-member timeouts/errors, and
flush all participating devices. RAID5/6 still additionally require parity
generation and recovery semantics.

The raw multi-member path remains disabled. The current polled writer still
targets one legacy-IDE device and must not be enabled merely because the
filesystem can return chunk mappings. Before enabling it, the kernel needs a
versioned file-offset/VCN-to-Btrfs-logical-extent query, validated member
resolution by both devid and UUID, retained member references and a stable
hibernation mapping lifetime, plus per-member polled I/O with checked bounds,
flush/error propagation, and profile-specific mirror/stripe placement.
Regression coverage must include fragmented and sparse files, chunk and stripe
boundaries, overflow/truncation, reordered or replaced members, device loss,
short writes, timeout, and interrupted preparation. RAID5/6 stay rejected
until parity generation and recovery behavior are implemented and verified.
The normal filesystem-mediated path remains the fallback for every unsupported
or ambiguous case. No multi-member volume is currently advertised as eligible
for the raw dump writer.

## BtrfsChkdsk

`BtrfsChkdsk` opens and checks one physical member at a time. It now validates the member device ID, filesystem UUID and device usage accounting in the superblock, and reports when the filesystem has multiple members. Run it against every member for per-device coverage. It does not currently assemble all members or perform cross-device scrub/repair.

The balance startup and device-removal paths serialize against special-file usage notifications. Paging/hibernation preallocation forces regular extents and fails rather than reporting success when physical allocation cannot be completed. These are runtime driver policies; they do not change the on-disk chunk or superblock formats inspected by `BtrfsChkdsk`.