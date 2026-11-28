#ifndef _RC95LDR_WIM_PROVIDER_H_
#define _RC95LDR_WIM_PROVIDER_H_
#include "rc95ldr.h"
#include <libwim/wim_codec.h>
#include <libwim/wim_read.h>
#include <libwim/wim_image.h>
#ifdef __cplusplus
extern "C" {
#endif
#define RC95LDR_WIM_OPEN_SLOTS 8u
#define RC95LDR_WIM_DEFAULT_MAX_IMAGE_SIZE (UINT64_C(1024) * 1024u * 1024u)
#define RC95LDR_WIM_DEFAULT_MAX_RESOURCE_SIZE (UINT64_C(512) * 1024u * 1024u)
#define RC95LDR_WIM_DEFAULT_MAX_FILE_SIZE (UINT64_C(256) * 1024u * 1024u)
typedef struct _RC95LDR_WIM_ALLOCATOR {
    void *Context;
    void *(*Allocate)(void *Context, size_t Size);
    void (*Free)(void *Context, void *Allocation);
} RC95LDR_WIM_ALLOCATOR;
typedef struct _RC95LDR_WIM_FILE_SLOT {
    uint8_t *Data;
    uint64_t Size;
    uintptr_t Generation;
    uint8_t InUse;
} RC95LDR_WIM_FILE_SLOT;
typedef struct _RC95LDR_WIM_CONTEXT {
    const uint8_t *Image;
    size_t ImageSize;
    WIM_READER *Reader;
    WIM_DIRECTORY_NODE *Root;
    RC95LDR_WIM_ALLOCATOR Allocator;
    RC95LDR_FILESYSTEM FileSystem;
    RC95LDR_WIM_FILE_SLOT Slots[RC95LDR_WIM_OPEN_SLOTS];
    uint64_t MaximumFileSize;
    uint32_t Mounted;
    uint32_t ImageOwned;
} RC95LDR_WIM_CONTEXT;
/* Open and parse a WIM without selecting an image; enumerate before selection. */
RC95LDR_STATUS Rc95LdrWimOpenMemory(
    RC95LDR_WIM_CONTEXT *Context, const void *ImageBytes, size_t ImageSize,
    uint64_t MaximumResourceSize, uint64_t MaximumFileSize,
    const RC95LDR_WIM_ALLOCATOR *Allocator, WIM_HEADER_INFO *Header);
RC95LDR_STATUS Rc95LdrWimSelectImage(RC95LDR_WIM_CONTEXT *Context,
    size_t ImageIndex, RC95LDR_VOLUME *Volume, WIM_IMAGE_INFO *ImageInfo);
/* Convenience open + select; ImageBytes remains caller-owned until Unmount. */
RC95LDR_STATUS Rc95LdrWimMountMemory(
    RC95LDR_WIM_CONTEXT *Context, const void *ImageBytes, size_t ImageSize,
    size_t ImageIndex, uint64_t MaximumResourceSize,
    uint64_t MaximumFileSize, const RC95LDR_WIM_ALLOCATOR *Allocator,
    RC95LDR_VOLUME *Volume, WIM_HEADER_INFO *Header,
    WIM_IMAGE_INFO *ImageInfo);
/* Open a WIM file from an existing volume; enumerate before selecting. */
RC95LDR_STATUS Rc95LdrWimOpenFile(
    RC95LDR_WIM_CONTEXT *Context, const RC95LDR_VOLUME *SourceVolume,
    const char *WimPath, uint64_t MaximumImageSize,
    uint64_t MaximumResourceSize, uint64_t MaximumFileSize,
    const RC95LDR_WIM_ALLOCATOR *Allocator, WIM_HEADER_INFO *Header);
/* Convenience open + select; the bounded image copy is owned. */
RC95LDR_STATUS Rc95LdrWimMountFile(
    RC95LDR_WIM_CONTEXT *Context, const RC95LDR_VOLUME *SourceVolume,
    const char *WimPath, size_t ImageIndex, uint64_t MaximumImageSize,
    uint64_t MaximumResourceSize, uint64_t MaximumFileSize,
    const RC95LDR_WIM_ALLOCATOR *Allocator, RC95LDR_VOLUME *Volume,
    WIM_HEADER_INFO *Header, WIM_IMAGE_INFO *ImageInfo);
void Rc95LdrWimUnmount(RC95LDR_WIM_CONTEXT *Context);
WIM_READER_STATUS Rc95LdrWimGetImageCount(RC95LDR_WIM_CONTEXT *Context,
                                           size_t *ImageCount);
WIM_READER_STATUS Rc95LdrWimGetImageInfo(RC95LDR_WIM_CONTEXT *Context,
                                          size_t ImageIndex,
                                          WIM_IMAGE_INFO *ImageInfo);
#ifdef __cplusplus
}
#endif
#endif
