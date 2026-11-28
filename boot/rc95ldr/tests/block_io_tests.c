
#include "rc95ldr.h"
#include <string.h>

typedef struct _TEST_DISK
{
    uint8_t Bytes[4096];
    unsigned ReadCalls;
    unsigned WriteCalls;
    RC95LDR_STATUS WriteStatus;
} TEST_DISK;

static RC95LDR_STATUS
TestRead(void *Context, uint64_t Lba, uint32_t Blocks,
         void *Buffer, size_t Length)
{
    TEST_DISK *Disk = (TEST_DISK *)Context;
    size_t Offset = (size_t)Lba * 512u;
    if (Disk == NULL || Buffer == NULL || Offset > sizeof(Disk->Bytes) ||
        Length > sizeof(Disk->Bytes) - Offset ||
        Length != (size_t)Blocks * 512u)
        return Rc95LdrOutOfBounds;
    ++Disk->ReadCalls;
    memcpy(Buffer, Disk->Bytes + Offset, Length);
    return Rc95LdrSuccess;
}

static RC95LDR_STATUS
TestWrite(void *Context, uint64_t Lba, uint32_t Blocks,
          const void *Buffer, size_t Length)
{
    TEST_DISK *Disk = (TEST_DISK *)Context;
    size_t Offset = (size_t)Lba * 512u;
    if (Disk == NULL || Buffer == NULL || Offset > sizeof(Disk->Bytes) ||
        Length > sizeof(Disk->Bytes) - Offset ||
        Length != (size_t)Blocks * 512u)
        return Rc95LdrOutOfBounds;
    ++Disk->WriteCalls;
    if (Disk->WriteStatus != Rc95LdrSuccess)
        return Disk->WriteStatus;
    memcpy(Disk->Bytes + Offset, Buffer, Length);
    return Rc95LdrSuccess;
}

int main(void)
{
    TEST_DISK Disk;
    RC95LDR_BLOCK_DEVICE Device;
    RC95LDR_BLOCK_DEVICE ReadOnly;
    uint8_t Input[1024], Output[1024];

    memset(&Disk, 0, sizeof(Disk));
    memset(&Device, 0, sizeof(Device));
    memset(Input, 0x5a, sizeof(Input));
    memset(Output, 0, sizeof(Output));
    Device.DeviceContext = &Disk;
    Device.BlockCount = 8u;
    Device.BlockSize = 512u;
    Device.Read = TestRead;
    Device.Write = TestWrite;

    /* A valid multi-block write dispatches exactly the requested byte count. */
    if (Rc95LdrWriteBlocks(&Device, 2u, 2u, Input, sizeof(Input)) != Rc95LdrSuccess)
        return 1;
    if (Disk.WriteCalls != 1u ||
        memcmp(Disk.Bytes + 1024u, Input, sizeof(Input)) != 0)
        return 2;

    /* Reads and writes share the same checked geometry contract. */
    if (Rc95LdrReadBlocks(&Device, 2u, 2u, Output, sizeof(Output)) != Rc95LdrSuccess)
        return 3;
    if (Disk.ReadCalls != 1u || memcmp(Output, Input, sizeof(Input)) != 0)
        return 4;

    /* No write callback means read-only, and must not invoke a callback. */
    ReadOnly = Device;
    ReadOnly.Write = NULL;
    if (Rc95LdrWriteBlocks(&ReadOnly, 0u, 1u, Input, 512u) != Rc95LdrUnsupported)
        return 5;
    if (Disk.WriteCalls != 1u)
        return 6;

    if (Rc95LdrWriteBlocks(&Device, 8u, 1u, Input, 512u) != Rc95LdrOutOfBounds)
        return 7;
    if (Rc95LdrWriteBlocks(&Device, UINT64_MAX, 1u, Input, 512u) != Rc95LdrOutOfBounds)
        return 8;
    if (Rc95LdrWriteBlocks(&Device, 0u, 2u, Input, 511u) != Rc95LdrBufferTooSmall)
        return 9;
    if (Rc95LdrWriteBlocks(&Device, 0u, 0u, Input, sizeof(Input)) != Rc95LdrInvalidParameter)
        return 10;
    if (Rc95LdrWriteBlocks(NULL, 0u, 1u, Input, 512u) != Rc95LdrInvalidParameter)
        return 11;
    if (Rc95LdrWriteBlocks(&Device, 0u, 1u, NULL, 512u) != Rc95LdrInvalidParameter)
        return 12;

    /* Preserve lower-layer write errors instead of reporting false success. */
    Disk.WriteStatus = Rc95LdrIoError;
    if (Rc95LdrWriteBlocks(&Device, 0u, 1u, Input, 512u) != Rc95LdrIoError)
        return 13;
    if (Disk.WriteCalls != 2u)
        return 14;

    /* A device with no read method can still be write-capable. */
    Device.Read = NULL;
    Disk.WriteStatus = Rc95LdrSuccess;
    if (Rc95LdrWriteBlocks(&Device, 0u, 1u, Input, 512u) != Rc95LdrSuccess)
        return 15;

    return 0;
}
