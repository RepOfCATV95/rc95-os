# libwim raw WIM writer

`WimWriterWrite` writes a new, uncompressed, single-part WIM to an empty `WIM_IO` stream.

The caller supplies:
- The serialized metadata resource for each image, marked with `WIM_RESOURCE_FLAG_METADATA`.
- The XML image catalogue.
- Optional boot-metadata and integrity-table byte ranges.

The writer lays out raw resources on 8-byte boundaries, creates the 50-byte lookup-table entries, writes the single-part identifiers and resource descriptors, and computes a SHA-1 digest for each lookup resource. It emits a 208-byte WIM header and its CRC-32 header checksum. The output is deliberately limited to uncompressed resources; compression, filesystem capture, resource deduplication, split WIMs, and solid resources are not implemented by this writer pass.

`libwim_writer_unittest` writes a one-image container to a bounded memory stream, opens it with the independent reader, enumerates the lookup table, and reads the metadata resource back through the reader's SHA-1 verification path.
