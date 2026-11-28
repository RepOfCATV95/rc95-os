#ifndef _RC95LDR_ISO9660_H_
#define _RC95LDR_ISO9660_H_
#include "rc95ldr.h"
#ifdef __cplusplus
extern "C" {
#endif

#define RC95LDR_ISO9660_LOGICAL_BLOCK_SIZE 2048u
#define RC95LDR_ISO9660_MAX_OPEN_FILES 8u
#define RC95LDR_ISO9660_MAX_PATH 260u

typedef struct _RC95LDR_ISO9660_FILE {
    uint64_t ExtentLba;
    uint64_t Size;
    uint64_t Position;
    uint8_t InUse;
    uint8_t IsDirectory;
} RC95LDR_ISO9660_FILE;

typedef struct _RC95LDR_ISO9660 {
    const RC95LDR_BLOCK_DEVICE *Device;
    uint64_t VolumeBlockCount;
    uint64_t RootExtentLba;
    uint64_t RootSize;
    uint8_t Joliet;
    uint8_t Mounted;
    RC95LDR_ISO9660_FILE Files[RC95LDR_ISO9660_MAX_OPEN_FILES];
    RC95LDR_FILESYSTEM Interface;
    RC95LDR_FILE_PROVIDER FileProvider;
} RC95LDR_ISO9660;

/* Mounts a read-only ISO 9660 volume (2048-byte logical blocks). Primary
 * Volume Descriptors are required; a valid Joliet supplementary descriptor
 * is preferred for directory names. Rock Ridge and multi-extent files are
 * intentionally not interpreted by this boot-time provider. */
RC95LDR_STATUS Rc95LdrIso9660Mount(
    RC95LDR_ISO9660 *Volume, const RC95LDR_BLOCK_DEVICE *Device);
void Rc95LdrIso9660Unmount(RC95LDR_ISO9660 *Volume);
const RC95LDR_FILESYSTEM *Rc95LdrIso9660GetInterface(RC95LDR_ISO9660 *Volume);
const RC95LDR_FILE_PROVIDER *Rc95LdrIso9660GetFileProvider(RC95LDR_ISO9660 *Volume);
RC95LDR_STATUS Rc95LdrIso9660BindVolume(
    RC95LDR_ISO9660 *Iso, RC95LDR_VOLUME *Volume);
#ifdef __cplusplus
}
#endif
#endif
