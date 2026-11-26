# WIM filesystem capture and extraction

`wimage capture <source-directory> <output.wim> [image-name]` creates a
single-image, single-part WIM using the in-tree `libwim` capture and writer
APIs. Capture uses XPRESS LZ77 compression with 32 KiB independent chunks.
Each chunk is compressed independently; if XPRESS output is not smaller than
the source chunk, that chunk is stored raw inside the compressed resource.
Resources containing no compressed chunks remain raw.

The capture path:

1. Enumerates regular files and directories recursively (maximum depth 256).
2. Skips symbolic links, Windows reparse points, devices, sockets and other
   non-regular filesystem objects.
3. Reads file contents and (on Windows) NTFS named streams with a default
   1 GiB aggregate uncompressed-data limit. `WIM_CAPTURE_OPTIONS.MaximumTotalBytes`
   can override that limit.
4. Computes SHA-1 for non-empty file streams and deduplicates equal resources
   using both the digest and a byte-for-byte comparison.
5. On Windows, captures owner/group/DACL security descriptors into the WIM
   security table, NTFS named streams into extra-stream records, and hard-link
   group IDs from volume/file identities. Sparse attributes are retained.
6. Serializes directory entries, UTF-16LE names, file timestamps, stream hashes,
   security IDs, hard-link group IDs, and the image XML catalogue.
7. Writes lookup resources using chunk-framed XPRESS. The writer bounds chunk
   sizes, checks codec input/output accounting, and falls back to raw chunks
   whenever compression does not reduce the stored size.

On extraction, `wimage extract <image.wim> <image-index> <existing-directory>`
recreates sparse allocation for entries carrying `FILE_ATTRIBUTE_SPARSE_FILE`.
Runs of at least 4096 consecutive zero bytes are represented as filesystem
holes; shorter zero runs are written normally. The destination file's logical
length is explicitly set after the final hole, so reads return the same
zero-filled byte stream as the WIM. Windows uses `FSCTL_SET_SPARSE`; POSIX
uses seek-over-holes followed by `ftruncate`. This is a content-based
reconstruction: the original NTFS/POSIX extent map and exact allocation
granularity are not stored in the WIM and cannot be reproduced exactly.

On Windows, owner/group/DACL security descriptors, NTFS alternate data streams,
and hard-link group IDs are preserved in metadata. SACLs are not captured.
Symbolic links and reparse-point payloads are not preserved. On non-Windows
hosts, security descriptors, named streams and hard-link IDs are not available
through this capture backend. Empty files are represented as zero-length files
without a main resource.

Keep the output WIM outside the source directory. The capture API creates or
truncates the output path after enumeration. Existing files inside the source
tree are captured as ordinary source files.

The `libwim_capture_unittest` test creates a nested directory tree with
duplicate file contents, captures it, reopens the WIM with the independent
reader, checks the image catalogue and directory tree, verifies resource
deduplication, and reads back file and named-stream data. Windows coverage also
creates a hard link, an NTFS named stream, and a sparse file. The XPRESS writer
is verified by reopening and decoding its compressed resources through the
reader.
