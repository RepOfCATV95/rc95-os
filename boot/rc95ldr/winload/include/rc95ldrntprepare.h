#ifndef _RC95LDR_NTPREPARE_H_
#define _RC95LDR_NTPREPARE_H_
#include "rc95ldr.h"
#ifdef __cplusplus
extern "C" {
#endif
#define RC95LDR_MAX_BOOT_DRIVERS 64u
#define RC95LDR_REGF_HEADER_SIZE 0x1000u
typedef struct _RC95LDR_BOOT_DRIVER_NODE {
    uint64_t Dependencies; /* bit N means this driver requires node N */
    uint32_t OriginalIndex;
} RC95LDR_BOOT_DRIVER_NODE;
RC95LDR_STATUS Rc95LdrOrderBootDrivers(const RC95LDR_BOOT_DRIVER_NODE *Nodes,
    size_t Count, uint32_t *OrderedIndices, size_t OrderedCapacity);
RC95LDR_STATUS Rc95LdrValidateSystemHive(const void *HiveBytes, size_t HiveSize,
    uint32_t *HiveDataLength);
#ifdef __cplusplus
}
#endif
#endif
