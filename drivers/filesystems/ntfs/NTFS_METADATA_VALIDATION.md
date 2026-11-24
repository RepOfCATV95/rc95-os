/*
 * PROJECT:    RC95-OS NTFS filesystem driver
 * LICENSE:    GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:    Clean-room validation requirements for NTFS metadata traversal and deletion
 * COPYRIGHT:  Copyright 1997-2028 Republic of CATV 95 <catv95republic@proton.me>
 */

# NTFS metadata traversal and deletion: clean-room validation plan

## Status and scope

This document defines validation requirements for the NTFS driver's MFT-record
and attribute traversal paths, with particular attention to file deletion.
It is a design and test contract, not a claim that the current implementation
already satisfies every item.

The requirements are derived from independently authored format descriptions,
the NTFS on-disk structures represented by the volume, and the repository's
own public interfaces. Do not use leaked material, GPL filesystem source, or
legacy ReactOS implementation code as a design or test oracle.

This work is confined to the NTFS filesystem driver on
`rc95ntfsenhanced`. It does not authorize changes to `catv95ntc2`.

## Required invariants

### 1. MFT record envelope

Before any attribute pointer is formed, validate the record envelope:

- The record signature and update-sequence-array fixups must be valid.
- The record's allocated size must equal the volume's configured file-record
  size, and its in-use size must not exceed that allocation.
- The first-attribute offset must be within the record and suitably aligned.
- Every attribute header and complete attribute extent must fit inside
  `BytesInUse`; arithmetic must be checked before addition.
- A zero-length attribute, a length smaller than its resident/nonresident
  header, a misaligned extent, or an absent end marker is corruption.
- Traversal must stop at the validated end marker, never at a guessed bound.

A corrupt record must fail closed. It must not cause an out-of-record read,
pool overrun, bitmap mutation, or partial destructive operation.

### 2. Attribute envelope and names

For every attribute:

- Validate the common header before reading the resident/nonresident union.
- Validate the name offset and the complete UTF-16 name span against the
  attribute's own length. A zero-length name does not require a name offset.
- For resident attributes, validate the value offset and value length as a
  contained range, including the minimum resident header size.
- For nonresident attributes, validate the nonresident header size, VCN
  ordering, mapping-pairs offset, and mapping-pairs extent.
- Treat the declared VCN count (inclusive HighestVCN plus one) as an exclusive
  upper bound: every decoded or materialized run must end at or before it.
  Reject an MCB entry that begins beyond that bound or whose length crosses it;
  do not silently truncate an out-of-range physical run.
- Reject integer overflow and values that cannot be represented by the
  driver's allocation or I/O interfaces.
- Preserve attribute identity as the tuple of type, name, instance, and
  lowest VCN where applicable. Type and name alone do not uniquely identify
  every extent of a stream.

Name comparison must use the volume's loaded NTFS upcase mapping when the
operation is case-insensitive. Bootstrap behavior before that table is
available must be explicit and must not silently substitute a host locale.

### 3. Attribute-list resolution

An attribute-list reference is untrusted on-disk input. Before following it:

- Validate the complete list-entry extent and its optional name span.
- Validate the referenced MFT record number and sequence number.
- Require the referenced record to be in use and to identify the expected
  base record.
- Resolve the exact attribute identity, including instance and starting VCN.
- Reject duplicate, overlapping, missing, stale, or contradictory extent
  references instead of silently selecting a convenient match.
- Bound traversal and detect cycles or repeated references.

A clean end-of-list is distinct from malformed data and from an I/O failure.
Callers must preserve that distinction.

### 4. Deletion must be failure-safe

Deletion is a multi-object metadata operation involving at least directory
indexes, file records, stream allocation, and allocation bitmaps. Until a
recoverable transaction/logging protocol exists, the driver must not imply
crash atomicity.

The safe behavior for each failure point must be specified and tested:

- Invalid file record: no directory index or allocation bitmap changes.
- Invalid filename attribute: no destructive changes.
- Failure removing any name: retain the MFT record and all stream clusters.
- Failure resolving any nonresident stream extent: do not free the MFT record.
- Failure releasing a stream allocation: retain the MFT record and report the
  failure; do not proceed as if the stream were completely released.
- Failure clearing the MFT allocation bit: report failure and retain enough
  state for diagnosis/recovery.
- Repeated deletion: return a deterministic not-found or invalid-state result
  without clearing unrelated allocation bits.

Where a failure occurs after earlier metadata has already been changed, the
implementation must document the exact residual state. A future transactional
design must replace this best-effort contract before claiming power-loss-safe
deletion.

### 5. Shared allocation state

Every allocation-bit change must be serialized with the in-memory bitmap and
the corresponding on-disk bitmap update. Validate ranges using checked
arithmetic and ensure the requested range lies within the bitmap's declared
bit count.

Do not release clusters belonging to a stream extent more than once. Extent
coverage must be validated as a coherent VCN range before converting it into
cluster counts. Sparse runs do not own physical clusters and must never be
freed as physical allocation.

Bitmap persistence has an additional short-write contract:

- Validate the requested cluster range against `ClusterBitmap.SizeOfBitMap`
  before converting cluster numbers to byte offsets.
- Require the RTL bitmap's backing pointer to match the volume's owned bitmap
  buffer, and require its bit count to fit within the recorded bitmap byte
  length. Allocation, persistence, stream release, and free-space reporting
  fail closed if those descriptor fields disagree; releasing the buffer also
  clears the RTL descriptor, bitmap size, and cached free count while
  preserving the sticky write-failure latch.
- Reject invalid sector geometry and arithmetic that could overflow while
  rounding the bitmap byte range to sector boundaries.
- Apply the same sector-geometry and checked-rounding validation when loading
  `$Bitmap`, before allocating its sector-padded in-memory buffer. Reject an
  empty bitmap or zero-cluster volume as corrupt geometry; neither represents
  a usable allocation map.
- Reject zero-length cluster-allocation requests and missing output pointers
  before acquiring the bitmap resource or changing any allocation bits. When
  the assigned-count output is present, initialize it to zero even when the
  remaining arguments are invalid. Validate the volume pointer before taking
  the address of its embedded bitmap, and validate bitmap backing storage and
  bit count under the bitmap resource before calling RTL bitmap routines.
- Reject a null volume argument to the bitmap persistence routine before
  reading its sector geometry or flags.
- Treat a successful attribute lookup that returns a null bitmap context or
  null attribute record as corrupt metadata; never pass either to length or
  attribute-I/O helpers.
- A successful `WriteAttribute` status is insufficient unless the reported
  byte count equals the complete sector-aligned range requested by
  `NtfsWriteVolumeBitmap`. A short transfer must be surfaced as
  `STATUS_DEVICE_DATA_ERROR`.
- A failed or short bitmap write may already have changed part of the on-disk
  bitmap. The status reports incomplete persistence; it does not prove that
  the disk was unchanged.

Before applying or removing update-sequence fixups, resolve the record
geometry from its signature: `FILE` records use
`BytesPerFileRecord`, while `INDX` records use
`BytesPerIndexRecord`. Require a sector-aligned record size, exactly one
update-sequence entry per sector plus the sequence number, and an update
sequence array fully contained in the first sector before its trailer. When
validating a record read, check every sector trailer against the sequence
number before restoring any trailer bytes; malformed later sectors must not
leave earlier sectors partially restored in memory.

Before traversing index-allocation blocks, require a nonzero sector-aligned
block size and an allocation length that is an exact multiple of that size.
Read each complete block before applying fixups; a short block read must stop
the traversal with a device-data error. Use a 64-bit byte offset so large
index-allocation streams cannot wrap a 32-bit loop counter.

The same restoration rule applies to directory index-allocation blocks:
after preparing an `INDX` block for persistence, restore its in-memory sector
trailers whether the write succeeds, fails, or reports a short transfer. A
successful status with a byte count smaller than the index-block size is a
device-data error and must not be treated as a committed directory update.

For stream truncation, write the shortened MFT file record before clearing
the corresponding cluster-bitmap bits. This ordering avoids deliberately
making still-referenced clusters available for reuse. If the later bitmap
write fails, the shortened stream remains authoritative and any clusters
whose bits were not cleared are leaked rather than referenced after reuse.
MFT byte-offset validation must cover the complete half-open write range,
not only the multiplication that computes its starting offset. Reject an MFT
record index when either the start offset or the end offset would overflow
64-bit arithmetic; test the last valid index and the first invalid index at
the arithmetic boundary.

`UpdateFileRecord` must treat a successful status with fewer than one full
file-record's bytes written as `STATUS_DEVICE_DATA_ERROR`; a short MFT write
must never be reported as a committed metadata update. It must also restore
the in-memory update-sequence fixups after the write attempt. This is not
crash atomicity: either the MFT record or bitmap write can still fail after
partial device I/O, and there is no log-based rollback protocol.

## Regression-test matrix

The following cases should be implemented as deterministic fixtures. Tests
must assert both the returned status and that protected metadata remains
byte-for-byte unchanged after rejected input.

| Area | Fixture | Expected result |
|---|---|---|
| MFT fixup | Unknown FILE/INDX signature | Corruption; no mutation |
| MFT fixup | Record size is not sector-aligned | Corruption; no mutation |
| MFT fixup | USA count differs from sector count plus one | Corruption; no mutation |
| MFT fixup | USA range crosses the first-sector trailer | Corruption; no mutation |
| MFT fixup | Later sector trailer has a mismatched sequence number | Corruption; no sector trailers restored |
| MFT envelope | Attribute offset before header end | Corruption; no mutation |
| MFT envelope | Bytes-in-use beyond record allocation | Corruption; no mutation |
| MFT envelope | Attribute length zero before end marker | Corruption; no mutation |
| MFT envelope | Attribute extent crosses BytesInUse | Corruption; no mutation |
| Attribute name | Name offset outside attribute | Corruption; no mutation |
| Resident value | Value range overflows attribute | Corruption; no mutation |
| Resident value | Value offset or length crosses its attribute extent but remains inside the MFT record | Corruption; no read or write beyond the attribute |
| Resident read | Offset exceeds value length | Zero bytes returned; no memory access |
| Resident read | Requested length crosses value end | Read is clipped to the remaining value bytes |
| Resident write | Offset plus length wraps 64-bit arithmetic | Invalid parameter; no mutation |
| Resident write | Attribute value range exceeds resident value length | Invalid parameter; no mutation |
| Resident write | Value offset or payload exceeds MFT record allocation | Corruption/invalid parameter; no mutation |
| Resident write | Required file-record read fails | Propagate read failure; no metadata write |
| Resident write | MFT record persistence fails after staging a value update | Restore the staged value in the caller's in-memory record; leave the attribute context unchanged |
| Resident write | Backup allocation fails before staging | Return insufficient resources; do not modify the record |
| Nonresident stream | Mapping-pairs offset inside fixed header | Corruption; no mutation |
| Nonresident stream | Highest VCN below lowest VCN | Corruption; no mutation |
| Mapping-pair conversion | MCB run begins at or extends beyond declared VCN count | Corruption; no output accepted |
| Attribute list | Stale extension-record sequence | Corruption; no mutation |
| Attribute list | Wrong base-record reference | Corruption; no mutation |
| Attribute list | Instance or starting-VCN mismatch | Corruption; no mutation |
| Attribute list | Duplicate/overlapping extent reference | Corruption; no mutation |
| Deletion | Second filename-index removal fails | MFT and stream allocations retained |
| Deletion | Nonresident extent cannot be resolved | MFT allocation retained |
| Deletion | Cluster release fails | MFT allocation retained |
| Deletion | MFT bitmap write fails | Error surfaced; no success reported |
| Bitmap | Range extends beyond bitmap bits | Invalid parameter; no mutation |
| Bitmap | Zero cluster count, empty bitmap, or invalid sector geometry | Corruption; no write |
| Allocation | Zero requested clusters, null volume, or missing output pointer | Invalid parameter; no bitmap mutation |
| Allocation | Missing bitmap backing buffer, zero bit count, or free count above bit count | Corruption; no RTL bitmap access |
| Bitmap | Successful lookup with null attribute context or record | Corruption; no attribute access |
| Bitmap | Successful status with short byte count | Device-data error; incomplete persistence reported |
| Bitmap | Failed/short write followed by another allocation | Sticky bitmap-write failure blocks reuse until remount |
| Bitmap | Failed/short write followed by another stream release | Sticky bitmap-write failure blocks further metadata release |
| Bitmap | Bitmap write fails after MFT truncation | Truncated stream retained; unreferenced clusters may leak |
| FreeClusters | MFT record write fails | Bitmap is not intentionally released |
| FreeClusters | Bitmap write fails after MFT record update | Stream remains shortened; no rollback/atomicity claim |
| FreeClusters | Sparse/compressed attribute with zero cluster size | Corruption; no metadata mutation |
| FreeClusters | Sparse hole between physical runs | Hole contributes no released physical clusters |
| FreeClusters | Physical run at final VCN | Only its allocated physical cluster is released |
| FreeClusters | MCB run length or VCN endpoint overflows signed range | Corruption; no metadata mutation |
| Bitmap | Sparse run in stream mapping | No physical cluster release |
| Retry | Delete same record twice | Deterministic failure; unrelated bits unchanged |
| Nonresident mapping | Cluster count multiplied by bytes per cluster overflows 64-bit | Corruption; no I/O run emitted |
| Nonresident mapping | Relative LCN underflows below cluster zero or overflows signed MCB range | Corruption; no physical run emitted |
| Nonresident mapping | Physical LCN interval exceeds the boot-sector volume cluster count | Corruption; no out-of-volume I/O, including before $Bitmap is loaded |
| Nonresident mapping | LCN-to-byte conversion or byte-range end overflows 64-bit | Corruption; no I/O run emitted |
| Nonresident mapping | Requested offset plus length overflows 64-bit | Corruption; no mapping attempted |
| Nonresident mapping | Mapping-pairs buffer ends before a complete run or terminator | Corruption; no malformed run is submitted |
| Nonresident mapping | Attribute ends before requested range | Successful short map; caller observes mapped byte count |
| Nonresident mapping | Temporary mapping buffer is released | Cached run pointer is null; no pointer into freed pool remains |
| Nonresident mapping | Request crosses multiple mapping-pair runs | Cached byte offset remains the original request offset plus total mapped bytes |
| Storage I/O | A disk run reports fewer bytes than requested | Fail the transfer; do not aggregate an ambiguous partial run |
| Storage I/O | A disk run reports more bytes than requested | Device-data error; do not add the untrusted count |
| Storage I/O | Several runs complete with one short run | Batch fails even if aggregate bytes equal the logical request length |
| Storage I/O | Direct forwarded IRP reports an oversized byte count | Device-data error; never narrow the count to signed LONG |
| Storage I/O | Null run list, run storage, buffer, or byte-count output | Invalid parameter before dereferencing or forwarding |
| Storage I/O | Zero sector size or unsupported major function | Invalid parameter; no IRP submitted |
| Storage I/O | Direct-forward candidate has a nonzero buffer offset, mismatched total length, zero byte count, saved IRP, or invalid LBO | Fall back to validated run-list I/O; never forward malformed geometry |

## Test execution status

The table above is the required regression matrix, not evidence that those
fixtures already exist or have passed. The current repository's NTFS driver
build target contains only the driver sources; the existing kernel-mode
filesystem tests exercise mounted filesystem behavior and do not expose
NTFS-private bitmap, MCB, or metadata-write fault-injection seams. Do not
represent those integration cases as covered by generic filesystem tests.

The next test-infrastructure change must provide a narrow test seam for the
NTFS-private operations (bitmap persistence and MFT-record persistence), plus
a deterministic fixture for LARGE_MCB input. Until that seam exists, tests
that merely reimplement the expected arithmetic or inspect source text are
not substitutes for executing the production code.

## Implementation sequence

1. Introduce a bounded, side-effect-free MFT-record/attribute validator.
2. Route read-only attribute enumeration and attribute-list traversal through
   validated extents.
3. Add host-side fixture tests for the validator, including integer-boundary
   and malformed-offset cases.
4. Add deletion failure-injection tests around directory-index, cluster-bitmap,
   and MFT-bitmap writes.
5. Only then change destructive deletion ordering or introduce recovery
   metadata. Do not attempt to simulate transaction atomicity with ad-hoc
   rollback writes.

## Acceptance criteria

- All malformed fixtures fail before destructive writes.
- Valid resident, nonresident, and multi-extent streams pass validation.
- Tests cover 32-bit and 64-bit arithmetic boundaries relevant to record and
  volume sizes.
- Allocation bitmap tests prove that sparse and out-of-range runs cannot
  clear physical allocation bits.
- Failure injection verifies that no error path reports success or frees the
  MFT record while stream allocation may remain.
- Build and the relevant host test suite pass on the target branch.
- The implementation and its tests cite their clean-room format/ABI evidence
  in code comments or accompanying documentation.


## Sticky failure handling for uncertain $Bitmap writes

A failed `WriteAttribute()` or a successful short transfer from
`NtfsWriteVolumeBitmap()` can leave an unknown prefix of the requested
bitmap range on disk. The in-memory bitmap cannot then safely be treated as
authoritative: for example, an allocation rollback that clears the in-memory
bits could otherwise permit a second file to claim clusters whose on-disk
bits were already set.

The volume therefore sets `VCB_BITMAP_WRITE_FAILED` while holding
`BitmapResource` after any bitmap I/O failure or short transfer. Subsequent
cluster allocation and `FreeClusters()` metadata release attempts fail with
`STATUS_DEVICE_DATA_ERROR` until the volume is dismounted/remounted and the
bitmap is loaded again. This is deliberately fail-closed; it does not attempt
an unreliable compensating write or claim transaction/crash atomicity.
The volume reports zero free clusters while this latch is set, so filesystem
information queries do not advertise space whose allocation state is uncertain. A successful complete bitmap reload reconstructs the in-memory
bitmap and free count before clearing the latch; failed or partial reloads do
not clear it.

For a failed `FreeClusters()` bitmap write, the shortened MFT record remains
authoritative and the in-memory free-cluster counter is not incremented.
The in-memory bits may already have been cleared, but the sticky failure
state prevents their reuse. For a failed allocation bitmap write, the
allocation path rolls back its in-memory reservation as before, while the
sticky failure state prevents that range from being reallocated in this
mount. A remount reloads the actual on-disk bitmap and naturally accounts for
any clusters that became allocated despite the failed operation.
