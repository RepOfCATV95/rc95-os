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
    size_t *CandidateCountOut, void *HiveBuffer, size_t HiveCapacity,
    RC95LDR_WINDOWS_INSTALLATION *Installation,
    RC95LDR_VOLUME *WindowsVolume, size_t *EspPartitionIndex)
{
    size_t PartitionCount = 0, Index, CandidateCount, ImageSize;
    RC95LDR_STATUS Status, LastStatus = Rc95LdrNotFound;
    int BcdStoreFound = 0, CorruptBcdStoreFound = 0;
    RC95LDR_VOLUME EspVolume;
    RC95BCD_HIVE Hive;

    if (!Ops || !Ops->EnumeratePartitions || !Ops->MountVolume ||
        !Ops->UnmountVolume || !Ops->ValidateWindowsVolume || !Partitions || !PartitionCapacity ||
        !Candidates || !CandidateCapacity || !CandidateCountOut || !HiveBuffer || !HiveCapacity ||
        !Installation || !WindowsVolume || !EspPartitionIndex)
        return Rc95LdrInvalidParameter;

    memset(Installation, 0, sizeof(*Installation));
    memset(WindowsVolume, 0, sizeof(*WindowsVolume));
    *EspPartitionIndex = (size_t)-1;
    *CandidateCountOut = 0;
    Status = Ops->EnumeratePartitions(Ops->Context, Partitions,
                                      PartitionCapacity, &PartitionCount);
    if (Status != Rc95LdrSuccess) return Status;
    if (PartitionCount > PartitionCapacity) return Rc95LdrCorruptData;

    for (Index = 0; Index < PartitionCount; ++Index)
    {
        RC95LDR_WINDOWS_INSTALLATION Trial;
        RC95LDR_VOLUME OsVolume;
        if (Partitions[Index].Style != Rc95LdrPartitionGpt ||
            Partitions[Index].FileSystem != Rc95LdrFsFat32 ||
            memcmp(Partitions[Index].TypeGuid, EspGuid, 16) != 0)
            continue;

        memset(&EspVolume, 0, sizeof(EspVolume));
        memset(&OsVolume, 0, sizeof(OsVolume));
        Status = Ops->MountVolume(Ops->Context, &Partitions[Index],
                                  Rc95LdrFsFat32, &EspVolume);
        if (Status != Rc95LdrSuccess) { LastStatus = Status; continue; }

        ImageSize = 0;
        Status = Rc95BcdLoadFromVolume(&EspVolume,
            "\\EFI\\Microsoft\\Boot\\BCD",
            HiveBuffer, HiveCapacity, &ImageSize, &Hive);
        if (Status == Rc95LdrSuccess)
        {
            BcdStoreFound = 1;
            CandidateCount = 0;
            if (Ops->CollectBootCandidates)
                Status = Ops->CollectBootCandidates(Ops->Context, &Hive,
                    Candidates, CandidateCapacity, &CandidateCount);
            else
                Status = Rc95BcdCollectWindowsBootCandidates(&Hive,
                    Candidates, CandidateCapacity, &CandidateCount);
            if (Status == Rc95LdrInvalidImage || Status == Rc95LdrCorruptData)
                CorruptBcdStoreFound = 1;
            if (Status == Rc95LdrSuccess && CandidateCount <= CandidateCapacity)
            {
                Status = Rc95LdrFilterWindowsBootCandidates(
                    Partitions, PartitionCount, Candidates, &CandidateCount);
                if (Status == Rc95LdrSuccess && Ops->SelectBootCandidate)
                {
                    const RC95LDR_BCD_BOOT_CANDIDATE *Selected = NULL;
                    size_t SelectedIndex;
                    Status = Ops->SelectBootCandidate(
                        Ops->SelectionContext ? Ops->SelectionContext : Ops->Context,
                        &Hive,
                        Candidates, CandidateCount, &Selected);
                    if (Status == Rc95LdrSuccess)
                    {
                        for (SelectedIndex = 0; SelectedIndex < CandidateCount;
                             ++SelectedIndex)
                            if (Selected == &Candidates[SelectedIndex]) break;
                        if (!Selected || SelectedIndex == CandidateCount)
                            Status = Rc95LdrCorruptData;
                        else
                        {
                            Status = Rc95LdrResolveWindowsBootCandidate(
                                Partitions, PartitionCount, Selected, &Trial);
                            if (Status == Rc95LdrSuccess)
                                Trial.GraphicsModeDisabled =
                                    Selected->GraphicsModeDisabled;
                        }
                    }
                }
                else if (Status == Rc95LdrSuccess)
                    Status = Rc95LdrSelectWindowsInstallation(Partitions,
                        PartitionCount, Candidates, CandidateCount, &Trial);
                if (Status == Rc95LdrInvalidImage ||
                    Status == Rc95LdrCorruptData)
                    CorruptBcdStoreFound = 1;
            }
            else if (Status == Rc95LdrSuccess)
            {
                Status = Rc95LdrCorruptData;
                CorruptBcdStoreFound = 1;
            }
        }
        else if (Status == Rc95LdrInvalidImage ||
                 Status == Rc95LdrCorruptData)
            CorruptBcdStoreFound = 1;

        if (Status == Rc95LdrSuccess)
        {
            Status = Ops->MountVolume(Ops->Context, &Trial.OsPartition,
                                      Trial.OsPartition.FileSystem, &OsVolume);
            if (Status == Rc95LdrSuccess)
            {
                Status = Ops->ValidateWindowsVolume(Ops->Context,
                    &OsVolume, &Trial);
                if (Status == Rc95LdrSuccess)
                {
                    *Installation = Trial;
                    *WindowsVolume = OsVolume;
                    *CandidateCountOut = CandidateCount;
                    *EspPartitionIndex = Index;
                    Ops->UnmountVolume(Ops->Context, &EspVolume);
                    return Rc95LdrSuccess;
                }
                Ops->UnmountVolume(Ops->Context, &OsVolume);
            }
        }

        Ops->UnmountVolume(Ops->Context, &EspVolume);
        LastStatus = Status;
    }

    /* Distinguish an absent BCD file from a present but malformed hive.
       A valid hive with no usable Windows entry retains its policy status. */
    if (Ops->TextUi)
    {
        if (CorruptBcdStoreFound)
            (void)Rc95LdrTextUiShowBcdError(Ops->TextUi,
                Rc95LdrCorruptData, 0xBCD00002u);
        else if (!BcdStoreFound && LastStatus == Rc95LdrNotFound)
            (void)Rc95LdrTextUiShowBcdError(Ops->TextUi,
                Rc95LdrNotFound, 0xBCD00001u);
    }
    return CorruptBcdStoreFound ? Rc95LdrCorruptData : LastStatus;
}
