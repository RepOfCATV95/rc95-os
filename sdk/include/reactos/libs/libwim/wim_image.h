#ifndef _LIBWIM_WIM_IMAGE_H_
#define _LIBWIM_WIM_IMAGE_H_

#include <stddef.h>
#include <stdint.h>
#include <libwim/wim_read.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _WIM_IMAGE_INFO {
    uint32_t Index;
    char *Name;
    char *Description;
    char *EditionId;
    uint64_t DirectoryCount;
    uint64_t FileCount;
    uint64_t TotalBytes;
} WIM_IMAGE_INFO;

typedef struct _WIM_NAMED_STREAM {
    char *Name;
    uint8_t Sha1[20];
    size_t LookupIndex;
    uint64_t Size;
    uint8_t HasLookup;
} WIM_NAMED_STREAM;

typedef struct _WIM_DIRECTORY_NODE {
    char *Name;
    uint32_t Attributes;
    uint32_t SecurityId;
    uint64_t HardLinkGroupId;
    uint64_t SubdirectoryOffset;
    uint64_t CreationTime;
    uint64_t LastAccessTime;
    uint64_t LastWriteTime;
    uint8_t MainSha1[20];
    size_t MainStreamLookupIndex;
    uint64_t MainStreamSize;
    uint8_t HasMainStream;
    WIM_NAMED_STREAM *NamedStreams;
    size_t NamedStreamCount;
    struct _WIM_DIRECTORY_NODE *Parent;
    struct _WIM_DIRECTORY_NODE **Children;
    size_t ChildCount;
    size_t ChildCapacity;
    uint8_t IsDirectory;
} WIM_DIRECTORY_NODE;

typedef struct _WIM_IMAGE_CATALOGUE WIM_IMAGE_CATALOGUE;

/* Parse the WIM XML resource and associate metadata lookup resources by image order. */
WIM_READER_STATUS WimReaderLoadImageCatalogue(WIM_READER *Reader);
WIM_READER_STATUS WimReaderGetImageCount(WIM_READER *Reader, size_t *Count);
WIM_READER_STATUS WimReaderGetImageInfo(WIM_READER *Reader, size_t ImageIndex,
                                         WIM_IMAGE_INFO *Info);
WIM_READER_STATUS WimReaderLoadImageDirectoryTree(WIM_READER *Reader,
                                                   size_t ImageIndex,
                                                   WIM_DIRECTORY_NODE **Root);
void WimImageDirectoryTreeFree(WIM_READER *Reader, WIM_DIRECTORY_NODE *Root);
WIM_READER_STATUS WimImageReadNamedStream(WIM_READER *Reader,
                                           const WIM_DIRECTORY_NODE *Node,
                                           size_t StreamIndex,
                                           uint8_t *Output,
                                           size_t OutputCapacity,
                                           size_t *OutputUsed);
WIM_READER_STATUS WimImageReadMainStream(WIM_READER *Reader,
                                          const WIM_DIRECTORY_NODE *Node,
                                          uint8_t *Output,
                                          size_t OutputCapacity,
                                          size_t *OutputUsed);
void WimImageInfoFree(WIM_READER *Reader, WIM_IMAGE_INFO *Info);

#ifdef __cplusplus
}
#endif
#endif
