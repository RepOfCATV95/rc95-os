#ifndef _RC95LDR_VOLUME_PROVIDER_H_
#define _RC95LDR_VOLUME_PROVIDER_H_
#include "rc95ldr.h"
#ifdef __cplusplus
extern "C" {
#endif
#define RC95LDR_VOLUME_PROVIDER_SLOTS 8u
typedef struct _RC95LDR_VOLUME_PROVIDER_SLOT {
    void *FileHandle;
    uint64_t FileSize;
    uint64_t Offset;
    uintptr_t Generation;
    uint8_t InUse;
} RC95LDR_VOLUME_PROVIDER_SLOT;
typedef struct _RC95LDR_VOLUME_PROVIDER_CONTEXT {
    const RC95LDR_VOLUME *Volume;
    RC95LDR_VOLUME_PROVIDER_SLOT Slots[RC95LDR_VOLUME_PROVIDER_SLOTS];
} RC95LDR_VOLUME_PROVIDER_CONTEXT;
/* Bind any RC95LDR read-only volume to the loader-scoped file-provider ABI.
 * The context and volume must outlive the provider scope and all open handles. */
RC95LDR_STATUS Rc95LdrInitializeVolumeFileProvider(
    const RC95LDR_VOLUME *Volume,
    RC95LDR_VOLUME_PROVIDER_CONTEXT *Context,
    RC95LDR_FILE_PROVIDER *Provider);
#ifdef __cplusplus
}
#endif
#endif
