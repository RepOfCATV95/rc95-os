/*
 * PROJECT:    RC95-OS libwim
 * LICENSE:    GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:    Public interface for writing uncompressed single-part WIM files.
 * COPYRIGHT:  Copyright 1997-2028 Republic of CATV 95
 */

#ifndef _LIBWIM_WIM_WRITE_H_
#define _LIBWIM_WIM_WRITE_H_

#include <stddef.h>
#include <stdint.h>
#include <libwim/wim_format.h>
#include <libwim/wim_io.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum _WIM_WRITER_STATUS {
    WIM_WRITER_OK = 0,
    WIM_WRITER_INVALID_ARGUMENT,
    WIM_WRITER_OUT_OF_MEMORY,
    WIM_WRITER_IO_ERROR,
    WIM_WRITER_UNSUPPORTED,
    WIM_WRITER_SIZE_OVERFLOW
} WIM_WRITER_STATUS;

/* Each blob becomes one lookup-table resource; XPRESS chunking is optional. */
typedef struct _WIM_WRITE_BLOB {
    const void *Data;
    uint64_t Size;
    uint32_t ReferenceCount;
    uint8_t Flags; /* WIM_RESOURCE_FLAG_METADATA is the only accepted input flag. */
} WIM_WRITE_BLOB;

typedef struct _WIM_WRITER_OPTIONS {
    uint32_t StructureSize;
    uint32_t Version;
    uint32_t Flags; /* Zero for raw resources, or WIM_HEADER_FLAG_COMPRESS_XPRESS. */
    uint32_t ChunkSize; /* XPRESS chunk size; zero selects 32768 bytes. */
    uint32_t ImageCount;
    uint32_t BootIndex;
    const uint8_t *Guid; /* Optional 16-byte WIM GUID; NULL writes an all-zero GUID. */
    const WIM_WRITE_BLOB *Blobs;
    size_t BlobCount;
    const void *XmlData;
    size_t XmlSize;
    const void *BootMetadata;
    size_t BootMetadataSize;
    const void *IntegrityTable;
    size_t IntegrityTableSize;
} WIM_WRITER_OPTIONS;

/*
 * Write a new, single-part WIM to an empty WIM_IO stream.
 * The caller supplies already-serialized image metadata blobs and the WIM XML
 * catalogue. The writer creates lookup entries and computes each blob SHA-1.
 * It does not capture a filesystem tree. XPRESS is emitted as independent WIM chunks; chunks that do not shrink are stored raw.
 */
WIM_WRITER_STATUS WimWriterWrite(WIM_IO *Output,
                                 const WIM_WRITER_OPTIONS *Options);

#ifdef __cplusplus
}
#endif
#endif
