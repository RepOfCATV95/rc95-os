# libwim container reader

The first reader implementation is memory-backed: the caller supplies an immutable WIM byte range and an allocator. The reader does not take ownership of the image bytes.

## Container structures

- Validates the 8-byte `MSWIM\0\0\0` signature and the 208-byte minimum header.
- Decodes the fixed little-endian header fields, including version, compression flags, chunk size, part numbers, image count, and boot index.
- Parses the four resource descriptors in their actual header order: lookup table (offset 48), XML data (72), boot metadata (96), and integrity table (124).
- Parses the lookup table as 50-byte entries: 24-byte resource descriptor, 16-bit part number, 32-bit reference count, and 20-byte SHA-1 digest. Empty entries are retained; the digest is exposed but not yet verified.
- Exposes lookup-entry metadata and a helper to read a referenced resource from the current WIM part.

## Resource decoding

Uncompressed resources are copied after exact size and file-range checks.

For ordinary compressed WIM resources, the reader implements the original chunk-table layout:

1. The chunk table precedes the compressed chunk payload.
2. The first chunk begins at offset zero after the table.
3. Subsequent chunk offsets are stored in the table, relative to the end of the table.
4. Entries are 32-bit when the uncompressed resource is smaller than 4 GiB and 64-bit otherwise.
5. A chunk stored at exactly its uncompressed size is copied as raw data; smaller chunks are sent to the selected codec provider.
6. Every chunk must produce its exact expected output, and all resource payload bytes must be accounted for.

XPRESS and LZMS chunks are dispatched through `WimCodecOpen` and `WimCodecProcess`. The XPRESS provider implements bounded, whole-chunk LZ77 decoding and a greedy whole-buffer encoder (literal indicator groups, 8 KiB backreference window, and byte/word/dword match-length extensions). The encoder uses bounded hash-chain search and reports a literal-only maximum compressed-size bound; streaming encode/decode are not implemented. XPRESS-Huffman remains a separate one-shot decoder and is not selected by WIM compression flags. WIM LZX chunks are dispatched to the bounded 32 KiB WIM-specific decoder, which resets state per chunk and supports verbatim, aligned, and uncompressed blocks plus E8 postprocessing. The LZX tests cover a hand-built uncompressed block, a complete-tree verbatim block, truncated input, two uncompressed blocks in one chunk, and WIM E8 address translation. The aligned-offset path and differential comparison against independently decoded compressed vectors remain pending. Chunks stored raw can still be copied without invoking a codec. Solid resources, spanned resources, and resources stored in another WIM part are not decoded by this first pass. Pipable WIMs use a different magic and are rejected rather than being misparsed as the original resource layout.

## CLI

- `wimage inspect image.wim` parses the header, displays resource descriptors, and attempts to enumerate lookup-table entries.
- `wimage verify image.wim` reads every non-free lookup resource in the current WIM part through the normal resource decoder and SHA-1 verification path. It reports resources belonging to other parts as skipped and returns exit status 3 rather than claiming a complete verification.
- `wimage extract-xml image.wim output.xml` reads the XML resource through the same resource reader.

The image layer parses the XML IMAGE catalogue, checks contiguous 1-based image indices, and associates metadata lookup entries with images in their on-disk lookup-table order (the WIM convention). `WimReaderGetImageInfo` returns caller-owned strings. `WimReaderLoadImageDirectoryTree` reads the selected metadata resource, validates the security-descriptor table, and builds a parent/child tree from the root dentry and each directory's absolute subdirectory offset. Dentry names are decoded from UTF-16LE; file attributes, security IDs, timestamps, and main-stream SHA-1 values are retained. `wimage list-images` and `wimage list-tree image.wim INDEX` expose these structures.

The tree pass resolves each file's unnamed main data stream to a lookup-table entry; `WimImageReadMainStream` reads and SHA-1 verifies its bytes. `wimage list-tree` displays the resolved uncompressed size. It still skips named alternate data stream descriptors after validating their bounds, does not extract files to the host filesystem, and verifies nonzero lookup-table SHA-1 digests when reading blobs, but does not yet validate the WIM integrity table. Successful decompression of independently captured LZMS WIM chunks still requires external test vectors.



## Named streams and extraction

Metadata dentries retain each additional stream descriptor (UTF-16LE name, SHA-1, and uncompressed size). Non-empty named streams are resolved against non-metadata lookup entries matching both digest and size. `WimImageReadNamedStream` reads a selected stream through the lookup-resource reader, preserving the existing nonzero lookup SHA-1 verification. A zero-digest, zero-size descriptor is retained without a payload.

The `wimage extract` command writes the selected image beneath an existing destination directory. It rejects rooted names, traversal components, drive/alternate-stream syntax, control characters, and platform-reserved path characters; refuses to overwrite files; and checks existing path components for symlinks/reparse points. Main unnamed streams become files. Named streams are emitted as separate `.wimstream.*` sidecars, with unsafe characters in stream names replaced by underscores. Reparse-point payloads are not followed or materialized as links. Extraction stops on the first unsafe path or failed stream read/write.
