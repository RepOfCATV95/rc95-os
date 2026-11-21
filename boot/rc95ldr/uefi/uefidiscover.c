#include "rc95ldrdiscoveryuefi.h"
#include "rc95bcdwalk.h"
#include <string.h>

static const uint8_t EspGuid[16] = {
    0x28,0x73,0x2a,0xc1,0xf8,0xf1,0xd2,0x11,
    0xba,0x4b,0x00,0xa0,0xc9,0x3e,0xc9,0x3b
};

RC95LDR_STATUS
Rc95LdrDiscoverUefiWindows(
    const RC95LDR_UEFI_DISCOVERY_OPS *Ops,
    RC95LDR_PARTITION_INFO *Partitions, size_t PartitionCapacity,
    RC95LDR_BCD_BOOT_CANDIDATE *Candidates, size_t CandidateCapacity,
    void *HiveBuffer, size_t HiveCapacity,
    RC95LDR_WINDOWS_INSTALLATION *Installation,
    RC95LDR_VOLUME *WindowsVolume, size_t *EspPartitionIndex)
{
    size_t PartitionCount = 0, Index, CandidateCount, ImageSize;
    RC95LDR_STATUS Status, LastStatus = Rc95LdrNotFound;
    RC95LDR_VOLUME EspVolume;
    RC95BCD_HIVE Hive;

    if (!Ops || !Ops->EnumeratePartitions || !Ops->MountVolume ||
        !Ops->UnmountVolume || !Ops->ValidateWindowsVolume || !Partitions || !PartitionCapacity ||
        !Candidates || !CandidateCapacity || !HiveBuffer || !HiveCapacity ||
        !Installation || !WindowsVolume || !EspPartitionIndex)
        return Rc95LdrInvalidParameter;

    memset(Installation, 0, sizeof(*Installation));
    memset(WindowsVolume, 0, sizeof(*WindowsVolume));
    *EspPartitionIndex = (size_t)-1;
    Status = Ops->EnumeratePartitions(Ops->Context, Partitions,
                                      PartitionCapacity, &PartitionCount);
    if (Status != Rc95LdrSuccess) return Status;
    if (PartitionCount > PartitionCapacity) return Rc95LdrCorruptData;

    for (Index = 0; Index < PartitionCount; ++Index)
    {
        RC95LDR_WINDOWS_INSTALLATION Trial;
        RC95LDR_VOLUME NtfsVolume;
        if (Partitions[Index].Style != Rc95LdrPartitionGpt ||
            Partitions[Index].FileSystem != Rc95LdrFsFat32 ||
            memcmp(Partitions[Index].TypeGuid, EspGuid, 16) != 0)
            continue;

        memset(&EspVolume, 0, sizeof(EspVolume));
        memset(&NtfsVolume, 0, sizeof(NtfsVolume));
        Status = Ops->MountVolume(Ops->Context, &Partitions[Index],
                                  Rc95LdrFsFat32, &EspVolume);
        if (Status != Rc95LdrSuccess) { LastStatus = Status; continue; }

        ImageSize = 0;
        Status = Rc95BcdLoadFromVolume(&EspVolume,
            "\\EFI\\Microsoft\\Boot\\BCD",
            HiveBuffer, HiveCapacity, &ImageSize, &Hive);
        if (Status == Rc95LdrSuccess)
        {
            CandidateCount = 0;
            if (Ops->CollectBootCandidates)
                Status = Ops->CollectBootCandidates(Ops->Context, &Hive,
                    Candidates, CandidateCapacity, &CandidateCount);
            else
                Status = Rc95BcdCollectWindowsBootCandidates(&Hive,
                    Candidates, CandidateCapacity, &CandidateCount);
            if (Status == Rc95LdrSuccess && CandidateCount <= CandidateCapacity)
                Status = Rc95LdrSelectWindowsInstallation(Partitions,
                    PartitionCount, Candidates, CandidateCount, &Trial);
            else if (Status == Rc95LdrSuccess)
                Status = Rc95LdrCorruptData;
        }

        if (Status == Rc95LdrSuccess)
        {
            Status = Ops->MountVolume(Ops->Context, &Trial.OsPartition,
                                      Rc95LdrFsNtfs, &NtfsVolume);
            if (Status == Rc95LdrSuccess)
            {
                Status = Ops->ValidateWindowsVolume(Ops->Context,
                    &NtfsVolume, &Trial);
                if (Status == Rc95LdrSuccess)
                {
                    *Installation = Trial;
                    *WindowsVolume = NtfsVolume;
                    *EspPartitionIndex = Index;
                    Ops->UnmountVolume(Ops->Context, &EspVolume);
                    return Rc95LdrSuccess;
                }
                Ops->UnmountVolume(Ops->Context, &NtfsVolume);
            }
        }
        Ops->UnmountVolume(Ops->Context, &EspVolume);
        LastStatus = Status;
    }
    return LastStatus;
}
