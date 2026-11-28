#ifndef _RC95BCDDEVICE_H_
#define _RC95BCDDEVICE_H_
#include "rc95bcd.h"
#include "rc95ldrdiscover.h"
#ifdef __cplusplus
extern "C" {
#endif

#define RC95BCD_DEVICE_TYPE_PARTITION 2u

/* Partition device payloads used by BCD device descriptors.
 * MBR: DWORD disk signature + QWORD partition byte offset (12 bytes).
 * GPT: 16-byte disk GUID + 16-byte partition unique GUID (32 bytes).
 * The descriptor's DeviceType is validated by the caller against its BCD
 * element semantics; this decoder only accepts these exact partition layouts.
 */
typedef enum _RC95BCD_PARTITION_ENCODING {
    Rc95BcdPartitionMbr = 1,
    Rc95BcdPartitionGpt = 2
} RC95BCD_PARTITION_ENCODING;

typedef struct _RC95BCD_PARTITION_DEVICE {
    RC95BCD_PARTITION_ENCODING Encoding;
    RC95LDR_PARTITION_IDENTITY Identity;
    uint8_t GptDiskGuid[16];
} RC95BCD_PARTITION_DEVICE;

RC95LDR_STATUS Rc95BcdDecodePartitionDevice(
    const RC95BCD_ELEMENT_VIEW *Element,
    RC95BCD_PARTITION_DEVICE *Device);

#ifdef __cplusplus
}
#endif
#endif
