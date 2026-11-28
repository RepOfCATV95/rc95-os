#ifndef _RC95LDR_RAMDISK_H_
#define _RC95LDR_RAMDISK_H_

#include "rc95ldr.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * A RAM disk is a caller-owned, contiguous memory image exposed as an ordinary
 * RC95LDR block device. The image remains owned by the caller and must outlive
 * the device. Writes update this memory immediately; they are volatile and do
 * not modify the source image file or provide persistence across reboot.
 */
typedef struct _RC95LDR_RAMDISK_CONTEXT
{
    uint8_t *Image;
    size_t ImageSize;
    uint32_t BlockSize;
    uint64_t BlockCount;
    uint32_t Writable;
} RC95LDR_RAMDISK_CONTEXT;

RC95LDR_STATUS
Rc95LdrInitializeRamDisk(void *Image,
                         size_t ImageSize,
                         uint32_t BlockSize,
                         int Writable,
                         RC95LDR_RAMDISK_CONTEXT *Context,
                         RC95LDR_BLOCK_DEVICE *Device);

#ifdef __cplusplus
}
#endif

#endif
