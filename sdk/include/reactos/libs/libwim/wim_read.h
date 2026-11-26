#ifndef _LIBWIM_WIM_READ_H_
#define _LIBWIM_WIM_READ_H_

#include <stddef.h>
#include <stdint.h>
#include <libwim/wim_codec.h>
#include <libwim/wim_format.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum _WIM_READER_STATUS {
    WIM_READER_OK = 0,
    WIM_READER_INVALID_ARGUMENT,
    WIM_READER_BAD_SIGNATURE,
    WIM_READER_BAD_HEADER,
    WIM_READER_BAD_RESOURCE,
    WIM_READER_UNSUPPORTED,
    WIM_READER_BUFFER_TOO_SMALL,
    WIM_READER_CORRUPT_DATA,
    WIM_READER_OUT_OF_MEMORY
} WIM_READER_STATUS;

typedef struct _WIM_READER_OPTIONS {
    uint32_t StructureSize;
    uint64_t MaximumResourceSize;
    WIM_CODEC_ALLOCATOR Allocator;
} WIM_READER_OPTIONS;

typedef struct _WIM_READER WIM_READER;
struct _WIM_IMAGE_CATALOGUE;

struct _WIM_READER {
    const uint8_t *Image;
    size_t ImageSize;
    WIM_HEADER_INFO Header;
    WIM_RESOURCE_DESCRIPTOR Resources[WIM_RESOURCE_KIND_COUNT];
    WIM_LOOKUP_ENTRY *LookupEntries;
    size_t LookupEntryCount;
    WIM_READER_OPTIONS Options;
    struct _WIM_IMAGE_CATALOGUE *Catalogue;
};

/* The caller owns ImageBytes and must retain it until WimReaderClose(). */
WIM_READER_STATUS WimReaderOpenMemory(
    WIM_READER **Reader,
    const void *ImageBytes,
    size_t ImageSize,
    const WIM_READER_OPTIONS *Options);
void WimReaderClose(WIM_READER *Reader);
WIM_READER_STATUS WimReaderGetHeaderInfo(
    const WIM_READER *Reader,
    WIM_HEADER_INFO *Info);
WIM_READER_STATUS WimReaderGetResource(
    const WIM_READER *Reader,
    WIM_RESOURCE_KIND Kind,
    WIM_RESOURCE_DESCRIPTOR *Descriptor);

/* Load and enumerate the WIM lookup table (50-byte on-disk entries). */
WIM_READER_STATUS WimReaderLoadLookupTable(WIM_READER *Reader);
WIM_READER_STATUS WimReaderGetLookupEntryCount(
    WIM_READER *Reader,
    size_t *EntryCount);
WIM_READER_STATUS WimReaderGetLookupEntry(
    WIM_READER *Reader,
    size_t Index,
    WIM_LOOKUP_ENTRY *Entry);
WIM_READER_STATUS WimReaderReadLookupResource(
    WIM_READER *Reader,
    size_t Index,
    uint8_t *Output,
    size_t OutputCapacity,
    size_t *OutputUsed);

/*
 * Read a complete resource into a caller-owned buffer. The capacity must be
 * at least OriginalSize. Compressed chunks are dispatched through libwim's
 * codec registry. This first container pass reads resources present in the current WIM part
 * and supports LZMS; other algorithms remain explicitly unsupported until their
 * providers implement WIM chunk semantics.
 */
WIM_READER_STATUS WimReaderReadResource(
    const WIM_READER *Reader,
    const WIM_RESOURCE_DESCRIPTOR *Descriptor,
    uint8_t *Output,
    size_t OutputCapacity,
    size_t *OutputUsed);

#ifdef __cplusplus
}
#endif
#endif
