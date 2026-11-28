#include "rc95ldrdiscover.h"
#include <string.h>

/* EFI System Partition type GUID, serialized in GPT/on-disk byte order. */
static const uint8_t Rc95LdrEspTypeGuid[16] = {
    0x28, 0x73, 0x2a, 0xc1, 0xf8, 0xf1, 0xd2, 0x11,
    0xba, 0x4b, 0x00, 0xa0, 0xc9, 0x3e, 0xc9, 0x3b
};

RC95LDR_STATUS
Rc95LdrFindEsp(
    const RC95LDR_PARTITION_INFO *Partitions,
    size_t PartitionCount,
    size_t *PartitionIndex)
{
    size_t Index, Found = 0, FoundIndex = 0;

    if ((!Partitions && PartitionCount) || !PartitionIndex)
        return Rc95LdrInvalidParameter;
    *PartitionIndex = (size_t)-1;

    for (Index = 0; Index < PartitionCount; ++Index)
    {
        const RC95LDR_PARTITION_INFO *Part = &Partitions[Index];
        if (Part->Style != Rc95LdrPartitionGpt ||
            memcmp(Part->TypeGuid, Rc95LdrEspTypeGuid, 16) != 0)
            continue;
        if (!Part->BlockSize || !Part->BlockCount ||
            Part->StartLba > UINT64_MAX - Part->BlockCount)
            return Rc95LdrCorruptData;
        if (Part->FileSystem != Rc95LdrFsFat32)
            continue;
        FoundIndex = Index;
        ++Found;
    }

    if (!Found)
        return Rc95LdrNotFound;
    if (Found != 1)
        return Rc95LdrUnsupported; /* Caller applies deterministic multi-ESP policy. */
    *PartitionIndex = FoundIndex;
    return Rc95LdrSuccess;
}

RC95LDR_STATUS
Rc95LdrResolvePartition(
    const RC95LDR_PARTITION_INFO *Partitions,
    size_t PartitionCount,
    const RC95LDR_PARTITION_IDENTITY *Identity,
    RC95LDR_FILESYSTEM_KIND RequiredFs,
    size_t *PartitionIndex)
{
    size_t Index, Found = 0, FoundIndex = 0;

    if ((!Partitions && PartitionCount) || !Identity || !PartitionIndex)
        return Rc95LdrInvalidParameter;
    *PartitionIndex = (size_t)-1;

    if (Identity->Kind != Rc95LdrPartitionIdGpt &&
        Identity->Kind != Rc95LdrPartitionIdMbr)
        return Rc95LdrUnsupported;

    for (Index = 0; Index < PartitionCount; ++Index)
    {
        const RC95LDR_PARTITION_INFO *Part = &Partitions[Index];
        int Match = 0;

        if (!Part->BlockSize || !Part->BlockCount ||
            Part->StartLba > UINT64_MAX - Part->BlockCount)
            return Rc95LdrCorruptData;
        if (RequiredFs != Rc95LdrFsUnknown &&
            Part->FileSystem != RequiredFs)
            continue;

        if (Identity->Kind == Rc95LdrPartitionIdGpt &&
            Part->Style == Rc95LdrPartitionGpt)
        {
            Match = memcmp(Part->DiskGuid, Identity->GptDiskGuid, 16) == 0 &&
                    memcmp(Part->UniqueGuid, Identity->GptUniqueGuid, 16) == 0;
        }
        else if (Identity->Kind == Rc95LdrPartitionIdMbr &&
                 Part->Style == Rc95LdrPartitionMbr &&
                 Part->MbrDiskSignature == Identity->MbrDiskSignature)
        {
            uint64_t Offset;
            if (Part->StartLba > UINT64_MAX / Part->BlockSize)
                return Rc95LdrIntegerOverflow;
            Offset = Part->StartLba * Part->BlockSize;
            Match = Offset == Identity->MbrPartitionOffsetBytes;
        }

        if (Match)
        {
            FoundIndex = Index;
            ++Found;
        }
    }

    if (!Found)
        return Rc95LdrNotFound;
    if (Found != 1)
        return Rc95LdrCorruptData;
    *PartitionIndex = FoundIndex;
    return Rc95LdrSuccess;
}
