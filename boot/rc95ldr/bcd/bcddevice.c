#include "rc95bcddevice.h"
#include <string.h>

RC95LDR_STATUS
Rc95BcdDecodePartitionDevice(
    const RC95BCD_ELEMENT_VIEW *Element,
    RC95BCD_PARTITION_DEVICE *Device)
{
    const uint8_t *Data;
    uint32_t Length;

    if (Element == NULL || Device == NULL ||
        Element->Kind != Rc95BcdElementDevice ||
        (Element->DeviceData == NULL && Element->DeviceDataLength != 0))
        return Rc95LdrInvalidParameter;

    if (Element->DeviceType != RC95BCD_DEVICE_TYPE_PARTITION)
        return Rc95LdrUnsupported;

    Data = Element->DeviceData;
    Length = Element->DeviceDataLength;
    memset(Device, 0, sizeof(*Device));

    if (Length == 12)
    {
        Device->Encoding = Rc95BcdPartitionMbr;
        Device->Identity.Kind = Rc95LdrPartitionIdMbr;
        Device->Identity.MbrDiskSignature = Rc95LdrReadU32(Data);
        Device->Identity.MbrPartitionOffsetBytes = Rc95LdrReadU64(Data + 4);
        if (Device->Identity.MbrDiskSignature == 0 ||
            Device->Identity.MbrPartitionOffsetBytes == 0)
            return Rc95LdrCorruptData;
        return Rc95LdrSuccess;
    }

    if (Length == 32)
    {
        Device->Encoding = Rc95BcdPartitionGpt;
        Device->Identity.Kind = Rc95LdrPartitionIdGpt;
        memcpy(Device->GptDiskGuid, Data, 16);
        memcpy(Device->Identity.GptDiskGuid, Data, 16);
        memcpy(Device->Identity.GptUniqueGuid, Data + 16, 16);
        if (memcmp(Device->GptDiskGuid, "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 16) == 0 ||
            memcmp(Device->Identity.GptUniqueGuid, "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 16) == 0)
            return Rc95LdrCorruptData;
        return Rc95LdrSuccess;
    }

    return Rc95LdrUnsupported;
}
