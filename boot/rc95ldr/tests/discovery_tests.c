#include "rc95ldrdiscover.h"
#include <stdio.h>
#include <string.h>

static int TestDiscovery(void)
{
    static const uint8_t EspGuid[16] = {
        0x28,0x73,0x2a,0xc1,0xf8,0xf1,0xd2,0x11,
        0xba,0x4b,0x00,0xa0,0xc9,0x3e,0xc9,0x3b
    };
    RC95LDR_PARTITION_INFO p[3];
    RC95LDR_PARTITION_IDENTITY id;
    size_t index = 99;

    memset(p, 0, sizeof(p));
    p[0].Style = Rc95LdrPartitionGpt;
    p[0].FileSystem = Rc95LdrFsFat32;
    memcpy(p[0].TypeGuid, EspGuid, 16);
    p[0].BlockSize = 512; p[0].BlockCount = 2048;
    p[1].Style = Rc95LdrPartitionGpt;
    p[1].FileSystem = Rc95LdrFsNtfs;
    p[1].BlockSize = 4096; p[1].BlockCount = 100000;
    p[1].StartLba = 2048;
    p[1].DiskGuid[0] = 0x24;
    p[1].UniqueGuid[0] = 0x42;

    if (Rc95LdrFindEsp(p, 2, &index) != Rc95LdrSuccess || index != 0)
        return 0;
    memset(&id, 0, sizeof(id));
    id.Kind = Rc95LdrPartitionIdGpt;
    id.GptDiskGuid[0] = 0x24;
    id.GptUniqueGuid[0] = 0x42;
    if (Rc95LdrResolvePartition(p, 2, &id, Rc95LdrFsNtfs, &index) != Rc95LdrSuccess || index != 1)
        return 0;
    id.GptDiskGuid[0] = 0x25;
    if (Rc95LdrResolvePartition(p, 2, &id, Rc95LdrFsNtfs, &index) != Rc95LdrNotFound)
        return 0;
    id.GptDiskGuid[0] = 0x24;
    p[2] = p[1];
    if (Rc95LdrResolvePartition(p, 3, &id, Rc95LdrFsNtfs, &index) != Rc95LdrCorruptData)
        return 0;
    p[2].Style = Rc95LdrPartitionMbr;
    p[2].MbrDiskSignature = 0x12345678;
    p[2].StartLba = 63;
    p[2].BlockSize = 512;
    id.Kind = Rc95LdrPartitionIdMbr;
    id.MbrDiskSignature = 0x12345678;
    id.MbrPartitionOffsetBytes = 63u * 512u;
    if (Rc95LdrResolvePartition(p, 3, &id, Rc95LdrFsUnknown, &index) != Rc95LdrSuccess || index != 2)
        return 0;
    id.MbrPartitionOffsetBytes++;
    if (Rc95LdrResolvePartition(p, 3, &id, Rc95LdrFsUnknown, &index) != Rc95LdrNotFound)
        return 0;
    return 1;
}

int main(void)
{
    if (!TestDiscovery())
    {
        fputs("RC95LDR partition discovery tests failed\n", stderr);
        return 1;
    }
    puts("RC95LDR partition discovery tests passed");
    return 0;
}
