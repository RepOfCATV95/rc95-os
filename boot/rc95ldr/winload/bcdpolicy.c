#include "rc95ldrpolicy.h"
#include <string.h>

static RC95LDR_STATUS
Rc95LdrResolveCandidate(
    const RC95LDR_PARTITION_INFO *Partitions,
    size_t PartitionCount,
    const RC95LDR_BCD_BOOT_CANDIDATE *Candidate,
    RC95LDR_WINDOWS_INSTALLATION *Result)
{
    RC95BCD_PARTITION_DEVICE BootDevice, OsDevice;
    size_t BootIndex, OsIndex;
    RC95LDR_STATUS Status;

    if (!Candidate->DeviceElement || !Candidate->OsDeviceElement ||
        !Candidate->IsWindowsLoader || !Candidate->IsEnabled)
        return Rc95LdrNotFound;

    Status = Rc95BcdDecodePartitionDevice(Candidate->DeviceElement, &BootDevice);
    if (Status != Rc95LdrSuccess) return Status;
    Status = Rc95BcdDecodePartitionDevice(Candidate->OsDeviceElement, &OsDevice);
    if (Status != Rc95LdrSuccess) return Status;

    /* BCD device identifies the loader/boot-files volume and can legitimately
     * be the ESP; only osdevice is required to be the Windows NTFS volume. */
    Status = Rc95LdrResolvePartition(Partitions, PartitionCount,
        &BootDevice.Identity, Rc95LdrFsUnknown, &BootIndex);
    if (Status != Rc95LdrSuccess) return Status;
    Status = Rc95LdrResolvePartition(Partitions, PartitionCount,
        &OsDevice.Identity, Rc95LdrFsNtfs, &OsIndex);
    if (Status != Rc95LdrSuccess) return Status;

    if (Candidate->SystemRootCharacters == 0 ||
        Candidate->SystemRootCharacters >= RC95LDR_MAX_SYSTEM_ROOT_CHARS ||
        Candidate->SystemRootUtf16 == NULL ||
        Candidate->LoaderPathCharacters == 0 ||
        Candidate->LoaderPathCharacters >= RC95LDR_MAX_SYSTEM_ROOT_CHARS ||
        Candidate->LoaderPathUtf16 == NULL)
        return Rc95LdrCorruptData;

    memset(Result, 0, sizeof(*Result));
    Result->BootEntryId = Candidate->ObjectId;
    Result->PartitionIndex = BootIndex;
    Result->OsPartitionIndex = OsIndex;
    Result->BootPartition = Partitions[BootIndex];
    Result->OsPartition = Partitions[OsIndex];
    Result->LoaderPathCharacters = Candidate->LoaderPathCharacters;
    memcpy(Result->LoaderPathUtf16, Candidate->LoaderPathUtf16,
        Candidate->LoaderPathCharacters * sizeof(uint16_t));
    Result->LoaderPathUtf16[Candidate->LoaderPathCharacters] = 0;
    Result->SystemRootCharacters = Candidate->SystemRootCharacters;
    memcpy(Result->SystemRootUtf16, Candidate->SystemRootUtf16,
        Candidate->SystemRootCharacters * sizeof(uint16_t));
    Result->SystemRootUtf16[Candidate->SystemRootCharacters] = 0;
    return Rc95LdrSuccess;
}

RC95LDR_STATUS
Rc95LdrSelectWindowsInstallation(
    const RC95LDR_PARTITION_INFO *Partitions,
    size_t PartitionCount,
    const RC95LDR_BCD_BOOT_CANDIDATE *Candidates,
    size_t CandidateCount,
    RC95LDR_WINDOWS_INSTALLATION *Installation)
{
    size_t Index, Pass, Best;
    RC95LDR_STATUS Status;

    if ((!Partitions && PartitionCount) || (!Candidates && CandidateCount) ||
        !Installation)
        return Rc95LdrInvalidParameter;
    memset(Installation, 0, sizeof(*Installation));

    /* Pass 0: valid default. Pass 1: lowest display-order enabled Windows
     * entry. Invalid defaults do not prevent trying another valid entry. */
    for (Pass = 0; Pass != 2; ++Pass)
    {
        Best = (size_t)-1;
        for (Index = 0; Index < CandidateCount; ++Index)
        {
            const RC95LDR_BCD_BOOT_CANDIDATE *Candidate = &Candidates[Index];
            RC95LDR_WINDOWS_INSTALLATION Trial;
            if (!Candidate->IsWindowsLoader || !Candidate->IsEnabled ||
                (Pass == 0 && !Candidate->IsDefault) ||
                (Pass == 1 && Candidate->IsDefault))
                continue;
            if (Best != (size_t)-1 &&
                Candidate->DisplayOrder >= Candidates[Best].DisplayOrder)
                continue;
            Status = Rc95LdrResolveCandidate(Partitions, PartitionCount,
                                              Candidate, &Trial);
            if (Status == Rc95LdrSuccess)
            {
                Best = Index;
                *Installation = Trial;
            }
            else if (Status != Rc95LdrNotFound && Status != Rc95LdrUnsupported &&
                     Status != Rc95LdrCorruptData && Status != Rc95LdrOutOfBounds)
                return Status;
        }
        if (Best != (size_t)-1)
            return Rc95LdrSuccess;
    }
    return Rc95LdrNotFound;
}
