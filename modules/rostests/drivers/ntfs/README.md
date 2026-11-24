# NTFS regression-test infrastructure

This directory is the NTFS-specific test entry under the ReactOS/RC95-OS
`modules/rostests/drivers` umbrella. It is enabled with the normal
`ENABLE_ROSTESTS` CMake option and packages `ntfs_drvtest` through
`add_rostests_file`.

## Current test

- `VolumeInfo` enumerates accessible mounted volumes, selects those whose
  reported filesystem name is NTFS, and checks basic volume-information
  invariants. For each accessible NTFS volume it also creates a temporary
  file, writes a fixed byte sequence, flushes, closes and reopens the file,
  reads it back, compares the bytes, and deletes the file.
- The same integration test creates a second temporary file, writes an initial
  payload, appends a second payload by seeking to EOF, flushes and verifies the
  complete byte sequence, truncates the stream with `SetEndOfFile`, flushes
  again, and verifies the retained prefix before cleanup. Each dependent step
  is gated on the preceding operation succeeding, so a later successful flush
  cannot mask an earlier write or seek failure.
- Both tests skip cleanly when no accessible NTFS volume is mounted.

This smoke test validates test discovery, execution, and the OS-facing NTFS
volume-information path. It is not evidence that private NTFS metadata paths
or bitmap persistence have been tested.

## Existing FsRtl fixture

The kernel-mode `FsRtlMcb` rostest now includes an NTFS-tail-trim fixture. It
builds a real `LARGE_MCB` with physical extents separated by a sparse gap,
prepares a shortened mapping, and checks that the sparse range is preserved,
the retained physical extent is clipped correctly, the trimmed tail is absent,
and the source MCB remains unchanged. This exercises the platform FsRtl MCB
implementation; it does not yet invoke `FreeClusters()` itself. The fixture
also verifies that an implicit sparse tail beyond the final physical MCB entry
remains outside FsRtl's enumerated physical runs; NTFS supplies the logical VCN
count separately when encoding that tail.

## NTFS_TEST_BUILD kernel runner

The rostest directory now defines a separate `ntfs_test` kernel module. It
compiles the production NTFS implementation sources with
`NTFS_TEST_BUILD`, while deliberately excluding `drivers/filesystems/ntfs/ntfs.c`
(the production filesystem `DriverEntry`). Its private entry point runs
`NtfsTestRunAll()` and does not create a filesystem device, register NTFS, or
modify the production driver's `DEVICE_EXTENSION`.

The private kernel module also compiles the clean-room LFS parser from
`sdk/lib/fslib/ntfslib/logfile.c` in a kernel-compatible configuration and
executes `NtfsLogfileRunRecordFixtures()` before the NTFS VCB tests. The
fixtures cover restart-page USA restoration and version rejection,
restart-area length/client-offset/list-head/sequence-width/page-data bounds,
wrap-aware selection between two validated restart areas, restart USA overlap
rejection, caller-tampered iterator-state rejection, native 0xA0-byte restart-client
record decoding with inline UTF-16 name bounds and all-or-nothing output
validation, free/in-use client-list topology and complete membership checks,
transaction Begin/Update/Commit/Abort state analysis keyed by client and
transaction ID, distinct-transaction capacity accounting, failure-output
clearing, duplicate-begin/invalid-transition/client/capacity rejection,
reciprocal restart-client link validation, empty-table and sentinel handling,
native NTFS operation-envelope decoding (raw redo/undo opcodes, bounded payload
pointers, target VCN, and LCN list access), malformed/overlapping operation
range rejection and failure-atomic output, self/two-node transaction cycle
rejection, single- and multi-sector RCRD USA fixups, record framing and
multi-page assembly, malformed fragments, LSN wrap ordering, circular log
offsets, and transaction predecessor chains (including missing links,
cross-client links, duplicate LSNs, and cycles).
These are parser and structural-validation tests only: they do not apply redo
or undo operations, write recovered metadata, or establish crash consistency.

The in-module tests exercise the real production run-list, attribute,
bitmap allocator, bitmap writer, MFT record writer, sparse-range allocator,
and attribute data-write paths. They verify physical-run coalescing, physical
discontinuities, sparse-run coalescing, buffer offsets, total length,
zero-length append behavior, malformed descriptor and overflow rejection,
stack-to-pool run-list growth, MFT attribute-envelope bounds, update-sequence
fixups, physical/sparse mapping-pair round trips, interior sparse-hole
materialization, implicit sparse tails, exact storage offsets and bytes,
zero-filled sparse reads, and low-level rejection of unmaterialized holes.
The `NtfsCalculateVolumeBitmapRange()` helper is also exercised independently of the IRP plumbing. Its regression cases cover negative and beyond-end LCNs, output-parameter preservation on invalid input, exact-end requests, non-byte-aligned volume sizes, final partial bitmap bytes, short output buffers, aligned nonzero starts, very large logical cluster counts, and empty volumes. The production `FSCTL_GET_VOLUME_BITMAP` path uses this same checked helper and snapshots the user input while it is inside the exception-handling region.

The VCB-backed cases also exercise `NtfsGetFreeClusters()` with a valid bitmap, a null VCB, a sticky bitmap-write failure, missing bitmap storage, a detached or zero-sized RTL bitmap, a bitmap extent larger than its backing buffer, and an impossible cached free count. The accessor must report zero for every untrustworthy state and preserve the cached count only for a valid bitmap. The VCB-backed cases cover contiguous and fragmented bitmap allocation,
including the fallback to a free run before a high allocation hint, normalization
of hints at the bitmap end, and rejection of zero-length/oversized allocation
requests without I/O or accounting changes. Direct bitmap writes also reject
zero-length and out-of-range requests before lower-device I/O.
Persistent bitmap bytes include a two-sector update crossing bitmap bits
4095-4096, allocation rollback after hard and short bitmap writes, sticky
bitmap-failure state (including proof that subsequent allocations are rejected
without another lower-device request after both hard errors and successful short
writes), and production `NtfsFreeVolumeBitmap()` teardown checks
for stale RTL descriptor clearing, cached-size/free-count reset, idempotent
repeated teardown, sticky-failure preservation, and clean teardown without
manufacturing a failure latch. MFT record persistence and USA encoding, MFT
hard-error and short-write handling, and a sparse-hole sequence that
injects both zeroing and MFT-commit errors, verifies that uncertain clusters
remain allocated while uncommitted mappings roll back, retries on a different
cluster, and then performs the user-data write. The successful path asserts
the exact lower-device order: bitmap sector, zeroed cluster, MFT record, data.
The fake device is a private unnamed device created by the test driver;
it uses a bounded nonpaged memory image and injects faults by request index.
Its dispatch is installed only during the serialized test and restored before
the device is deleted.

Build integration is under `modules/rostests/drivers/ntfs/CMakeLists.txt`.
The target is packaged with `add_rostests_file(TARGET ntfs_test)` when
`ENABLE_ROSTESTS` enables the rostests tree. Build the `ntfs_test` target
alongside `ntfs_drvtest`. Loading the test module executes its private
self-tests and returns their status from its test-only `DriverEntry`; it
never registers a usable filesystem.

NTFS_TEST_BUILD identifies and isolates the test module. In addition to
generic lower-device tests, the fake device backs a private VCB whose real
bitmap allocator, nonresident bitmap writer, MFT record writer, and sparse
range allocator execute against the in-memory disk. The fixture does not
mount a volume or register the filesystem. Its sparse-stream scenario also
calls the production FreeClusters() routine against the fake disk, covering
MFT-before-bitmap release ordering, MFT hard-error/short-write rollback,
bitmap failure after the shortened MFT record is durable, and the resulting
allocated-cluster leak. A separate physical/sparse/physical tail-trim case
proves that two logical VCNs release only one physical cluster. That fixture
also verifies that a stale physical extent whose bitmap bit is already clear
is rejected before any storage write or MCB mutation. It gives the attribute
two implicit sparse-tail VCNs beyond the last MCB entry and verifies trimming
them persists only the shorter MFT mapping, leaves both physical bitmap bits
allocated, and does not change the free-cluster count. Full remount/reload behavior remains outstanding.

## Sparse-write allocation path

The production FCB-aware write paths now materialize sparse holes before data
I/O. Non-paging and cached writes serialize against paging I/O, reserve physical
clusters through the mounted volume's `$Bitmap`, zero each newly allocated
cluster before publishing it, insert the new physical extent at its original
VCN, and persist the updated mapping pairs and allocation size in the owning
MFT record. Cached writes perform this preparation before dirtying cache pages;
paging writes do not allocate and must encounter only already-backed clusters.
A failed zero or MFT update can leave an allocated-but-unreferenced cluster
(leak), deliberately preferring a leak to exposing stale data or freeing a
cluster that may have become referenced.

The MCB regression still verifies that inserting a physical extent into an
interior sparse gap does not shift later extents. The VCB-backed fake-storage
fixture exercises sparse allocation through bitmap persistence,
zero-before-publish, mapping-pair insertion, MFT persistence, and a subsequent
data write. It injects zeroing and MFT-commit failures, checks safe allocation
leaks and in-memory rollback, and verifies retries select different clusters.
The same fixture exercises FreeClusters() against a sparse nonresident stream:
zero-length no-op, oversized release rejection, sticky bitmap-failure blocking,
and stale/already-free bitmap rejection without I/O,
MFT hard-error/short-write failure with complete caller-record restoration
and no bitmap mutation, successful tail release
with exact MFT-then-bitmap ordering plus the caller-side AllocatedSize commit,
and bitmap-write failure after the shortened MFT record is durable. The last
case verifies that the cluster remains allocated on disk, is not credited to
FreeClusterCount, and latches VCB_BITMAP_WRITE_FAILED. A separate isolated
case trims two logical VCNs across a physical/sparse/physical tail and proves
that only the one physical cluster is released and counted. Full recovery
after remount remains uncovered.
Compressed sparse streams and memory-mapped writes that bypass the cached
write preparation path are not yet covered.

## Production persistence seam

The bitmap and MFT write paths share a lower-level I/O boundary:

- `NtfsWriteVolumeBitmap()` submits the changed non-resident `$Bitmap` range
  through `WriteAttribute()`.
- `UpdateFileRecord()` submits an MFT record through `WriteAttribute()`.
- For non-resident attributes, `WriteAttribute()` maps the requested VCN
  range and calls `NtfsPerformIrpIoRuns()` to issue the physical run-list I/O.

`NtfsPerformIrpIoRuns()` and its internal run-list submission helpers in
`blockdev.c` form the shared storage boundary. The seam should sit at the final
lower-device submission/completion boundary (or use a fake lower storage
device), not replace `NtfsPerformIrpIoRuns()` itself. That preserves the real
run-list validation, IRP construction, alignment handling, and transfer
accounting while allowing the fixture to capture the actual disk offsets and
bytes.

The NTFS-owned test runner now creates a private fake lower device and
assigns it to each test VCB's StorageDevice. Its read/write dispatch obtains
the byte offset and length from the IRP stack, maps the IRP MDL, and services
the request against a bounded in-memory image. It completes inline and reports
the exact byte count through IoStatus.Information. Fault injection is selected
by request index and supports full completion, a successful short completion,
and an I/O error. The production mapping, range validation, bitmap logic, MFT
fixups, and sparse allocation ordering therefore remain executed by the real
NTFS code.

Keep the seam behind a dedicated NTFS test-build definition. Do not add a
callback or test-state field to the production `DEVICE_EXTENSION`, add a
public filesystem dispatch/IOCTL contract, or change the on-disk format.
Normal driver builds must compile out the backend and all test registration.
The test fixture should register a private backend only for the duration of a
serialized test and restore the default I/O path before teardown.

The existing kmtest_drv is a separate kernel test driver: its source
list includes ntos_fsrtl/FsRtlMcb.c, but it does not compile or link the NTFS
driver's private implementation. Consequently, the FsRtl fixture cannot call
FreeClusters() merely by adding more MCB cases. The NTFS-owned test-only module
is kept isolated under this rostest directory; its private DriverEntry runs
the tests and does not register the filesystem. Continue extending this
module's private VCB and fake-storage fixtures rather than linking the full
NTFS implementation into generic kmtest_drv, which would mix driver entry
points and unrelated initialization dependencies.

## Planned NTFS-private test layers

The bitmap, MFT-record, attribute, and LARGE_MCB regression cases must execute
the production NTFS implementation. Do not create a second implementation of
the range arithmetic or use source-text matching as a substitute.

The production run-list builder, bitmap allocator, bitmap persistence,
MFT record persistence, and one complete sparse-hole materialization path are
now covered by the in-module runner. The storage I/O tests also reject
non-power-of-two sector geometry before issuing requests and exercise the
single aligned run's original-IRP/MDL forwarding path, checking that data
reaches the expected backing-image offset without a bounce-buffer copy. Extend the VCB fixture next to cover:

- Exact bitmap byte ranges at first/last bits and sector-boundary crossings.
- FreeClusters() with physical extents, sparse gaps, implicit sparse tails,
  malformed ranges, and failures before and after the MFT update. The current
  fixture covers an allocated physical tail, a physical/sparse/physical tail
  trim, already-free bitmap rejection, MFT hard-error/short-write rollback,
  and a bitmap failure after MFT
  commit; implicit sparse tails and
  exhaustive fragmented-release layouts remain open.
- Sparse-allocation failures during bitmap persistence, zeroing, and
  MFT commit, including bitmap short writes and MFT short writes; verify
  in-memory rollback and the deliberate allocation leak when disk state is
  uncertain.
- Retry/remount behavior, proving that a sticky bitmap failure blocks further
  allocation/free operations until the bitmap is reloaded.
- Cached-write and paging-I/O ordering, plus memory-mapped write integration.

The MFT and bitmap persistence operations are not transactional. Tests must
assert the documented safety bias: do not free clusters before the shortened
MFT record is persisted, and do not increment the free-cluster count after a
bitmap persistence failure. A failed or partial MFT write must not be reported
as a successful release.

Keep the existing FsRtl tests for platform MCB semantics separate from these
NTFS-private tests.

## Build and run

Configure the tree with `ENABLE_ROSTESTS=1`. Build `ntfs_drvtest` for the
user-mode volume integration checks and build `ntfs_test` for the isolated
kernel-mode private test module. The latter runs its self-tests when loaded;
it does not register the NTFS filesystem. The user-mode integration test
requires a mounted, accessible NTFS volume and skips when none is available.

The NTFS_TEST_BUILD target now includes production run-list tests, a fake
lower storage device, bitmap allocation/writeback tests, MFT update-sequence
persistence tests, sparse-allocation-to-data-write integration, and a
FreeClusters() MFT/bitmap release fixture with injected failures and a
physical/sparse/physical tail-trim case. First/last bitmap bits and the
4095/4096 sector crossing are covered; implicit sparse tails, exhaustive
allocation/release
failure permutations, and remount/reload tests remain unfinished. Do not
interpret successful execution as coverage of those remaining paths.

## Fragmented I/O run-list tuning

The inline run-list capacity is 16 entries (up from 8) to keep moderately
fragmented requests on the caller's stack and avoid a nonpaged-pool growth
allocation. More heavily fragmented requests retain the existing checked
doubling/spill path. The run-list regression verifies that all 16 entries stay
inline, the 17th entry spills successfully, and freeing the grown list resets
its descriptor. It also checks NULL/corrupt descriptors and total-length
overflow rejection without mutation.

The fake-storage I/O tests additionally corrupt a run's buffer offset and the
aggregate transfer length independently. Both malformed tilings must be
rejected before any lower-device request is observed.


Transaction lifecycle analysis still consumes caller-classified events. The NTFS client-record decoder currently validates and exposes the native operation envelope, but does not interpret operation-specific semantics or perform redo/undo. This remains parser/validation work, not a recovery engine.
