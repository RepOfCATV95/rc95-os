#ifndef _LIBWIM_WIM_FORMAT_H_
#define _LIBWIM_WIM_FORMAT_H_

#include <stdint.h>

/* On-disk WIM structures are decoded field-by-field as little endian. */
#define WIM_HEADER_SIGNATURE "MSWIM\0\0\0"
#define WIM_HEADER_MIN_SIZE 208u

#define WIM_HEADER_FLAG_COMPRESS_XPRESS 0x00020000u
#define WIM_HEADER_FLAG_COMPRESS_LZX    0x00040000u
#define WIM_HEADER_FLAG_COMPRESS_LZMS   0x00080000u
#define WIM_HEADER_FLAG_COMPRESS_XPRESS_2 0x00200000u
#define WIM_HEADER_FLAG_COMPRESSION_MASK 0x002E0000u

#define WIM_RESOURCE_FLAG_FREE       0x01u
#define WIM_RESOURCE_FLAG_METADATA   0x02u
#define WIM_RESOURCE_FLAG_COMPRESSED 0x04u
#define WIM_RESOURCE_FLAG_SPANNED    0x08u
#define WIM_RESOURCE_FLAG_SOLID      0x10u

#define WIM_RESOURCE_SIZE_MASK UINT64_C(0x00FFFFFFFFFFFFFF)

typedef struct _WIM_RESOURCE_DESCRIPTOR {
    uint64_t CompressedSize;
    uint64_t Offset;
    uint64_t OriginalSize;
    uint8_t Flags;
} WIM_RESOURCE_DESCRIPTOR;

typedef enum _WIM_RESOURCE_KIND {
    WIM_RESOURCE_LOOKUP_TABLE = 0,
    WIM_RESOURCE_XML_DATA,
    WIM_RESOURCE_BOOT_METADATA,
    WIM_RESOURCE_INTEGRITY_TABLE,
    WIM_RESOURCE_KIND_COUNT
} WIM_RESOURCE_KIND;

typedef struct _WIM_LOOKUP_ENTRY {
    WIM_RESOURCE_DESCRIPTOR Resource;
    uint16_t PartNumber;
    uint32_t ReferenceCount;
    uint8_t Sha1[20];
} WIM_LOOKUP_ENTRY;

typedef struct _WIM_HEADER_INFO {
    uint32_t HeaderSize;
    uint32_t Version;
    uint32_t Flags;
    uint32_t ChunkSize;
    uint16_t PartNumber;
    uint16_t TotalParts;
    uint32_t ImageCount;
    uint32_t BootIndex;
} WIM_HEADER_INFO;

#endif
