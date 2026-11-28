#include "rc95ldr_ramdisk.h"
#include <string.h>

int main(void)
{
    uint8_t Image[4096], Input[1024], Output[1024];
    RC95LDR_RAMDISK_CONTEXT Context, ReadOnlyContext;
    RC95LDR_BLOCK_DEVICE Device, ReadOnly;

    memset(Image, 0x11, sizeof(Image));
    memset(Input, 0xa5, sizeof(Input));
    memset(Output, 0, sizeof(Output));

    if (Rc95LdrInitializeRamDisk(Image, sizeof(Image), 512u, 1,
                                 &Context, &Device) != Rc95LdrSuccess)
        return 1;
    if (Device.BlockCount != 8u || Device.BlockSize != 512u ||
        Device.Read == NULL || Device.Write == NULL)
        return 2;

    /* Writes mutate the in-memory image and are visible to subsequent reads. */
    if (Rc95LdrWriteBlocks(&Device, 2u, 2u, Input, sizeof(Input)) != Rc95LdrSuccess)
        return 3;
    if (memcmp(Image + 1024u, Input, sizeof(Input)) != 0)
        return 4;
    if (Rc95LdrReadBlocks(&Device, 2u, 2u, Output, sizeof(Output)) != Rc95LdrSuccess)
        return 5;
    if (memcmp(Output, Input, sizeof(Input)) != 0)
        return 6;

    /* The same image can be exposed read-only without a write callback. */
    if (Rc95LdrInitializeRamDisk(Image, sizeof(Image), 512u, 0,
                                 &ReadOnlyContext, &ReadOnly) != Rc95LdrSuccess)
        return 7;
    if (ReadOnly.Write != NULL ||
        Rc95LdrWriteBlocks(&ReadOnly, 0u, 1u, Input, 512u) != Rc95LdrUnsupported)
        return 8;

    if (Rc95LdrWriteBlocks(&Device, 8u, 1u, Input, 512u) != Rc95LdrOutOfBounds)
        return 9;
    if (Rc95LdrWriteBlocks(&Device, UINT64_MAX, 1u, Input, 512u) != Rc95LdrOutOfBounds)
        return 10;
    if (Rc95LdrReadBlocks(&Device, 0u, 1u, Output, 511u) != Rc95LdrBufferTooSmall)
        return 11;
    if (Rc95LdrInitializeRamDisk(Image, sizeof(Image) - 1u, 512u, 1,
                                 &Context, &Device) != Rc95LdrInvalidImage)
        return 12;
    if (Rc95LdrInitializeRamDisk(NULL, sizeof(Image), 512u, 1,
                                 &Context, &Device) != Rc95LdrInvalidParameter)
        return 13;

    return 0;
}
