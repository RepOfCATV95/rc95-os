#include "rc95ldrpolicy.h"
#include "rc95ldrwinload.h"
#include <stddef.h>
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
    if (Candidate->GraphicsModeDisabled > 1u)
        return Rc95LdrCorruptData;

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
        &OsDevice.Identity, Rc95LdrFsUnknown, &OsIndex);
    if (Status != Rc95LdrSuccess) return Status;
    /* Preserve a Btrfs osdevice as an explicit candidate. UEFI binds the
       native read-only VENEER provider; firmware paths without that provider
       must stop with Unsupported rather than substitute another partition. */
    if (Partitions[OsIndex].FileSystem != Rc95LdrFsNtfs &&
        Partitions[OsIndex].FileSystem != Rc95LdrFsBtrfs)
        return Rc95LdrUnsupported;

    if (Candidate->SystemRootCharacters == 0 ||
        Candidate->SystemRootCharacters >= RC95LDR_MAX_SYSTEM_ROOT_CHARS ||
        Candidate->SystemRootUtf16 == NULL ||
        Candidate->SystemRootUtf16[Candidate->SystemRootCharacters] != 0 ||
        Candidate->SystemRootUtf16[0] != '\\' ||
        Candidate->LoaderPathCharacters == 0 ||
        Candidate->LoaderPathCharacters >= RC95LDR_MAX_SYSTEM_ROOT_CHARS ||
        Candidate->LoaderPathUtf16 == NULL ||
        Candidate->LoaderPathUtf16[Candidate->LoaderPathCharacters] != 0 ||
        Candidate->LoaderPathUtf16[0] != '\\' ||
        Candidate->BootOptionsCharacters >= RC95LDR_MAX_BCD_STRING_CHARS ||
        Candidate->BootOptionsStorage[Candidate->BootOptionsCharacters] != 0)
        return Rc95LdrCorruptData;

    /* The selected BCD object is the sole source of all loader policy data.
       Copy options into the resolved installation so no later caller needs
       to consult a different/default candidate or retain the hive image. */
    {
        uint32_t OptionIndex;
        for (OptionIndex = 0;
             OptionIndex < Candidate->BootOptionsCharacters;
             ++OptionIndex)
        {
            if (Candidate->BootOptionsStorage[OptionIndex] == 0)
                return Rc95LdrCorruptData;
        }
    }

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
    Result->BootOptionsCharacters = Candidate->BootOptionsCharacters;
    memcpy(Result->BootOptionsUtf16, Candidate->BootOptionsStorage,
        (Candidate->BootOptionsCharacters + 1u) * sizeof(uint16_t));
    Result->GraphicsModeDisabled = Candidate->GraphicsModeDisabled;
    return Rc95LdrSuccess;
}


RC95LDR_STATUS
Rc95LdrResolveWindowsBootCandidate(
    const RC95LDR_PARTITION_INFO *Partitions,
    size_t PartitionCount,
    const RC95LDR_BCD_BOOT_CANDIDATE *Candidate,
    RC95LDR_WINDOWS_INSTALLATION *Installation)
{
    if ((!Partitions && PartitionCount) || !Candidate || !Installation)
        return Rc95LdrInvalidParameter;
    memset(Installation, 0, sizeof(*Installation));
    return Rc95LdrResolveCandidate(Partitions, PartitionCount,
                                   Candidate, Installation);
}

RC95LDR_STATUS
Rc95LdrFilterWindowsBootCandidates(
    const RC95LDR_PARTITION_INFO *Partitions,
    size_t PartitionCount,
    RC95LDR_BCD_BOOT_CANDIDATE *Candidates,
    size_t *CandidateCount)
{
    size_t ReadIndex, WriteIndex = 0, OriginalCount;
    if ((!Partitions && PartitionCount) || !Candidates || !CandidateCount)
        return Rc95LdrInvalidParameter;
    OriginalCount = *CandidateCount;
    for (ReadIndex = 0; ReadIndex < OriginalCount; ++ReadIndex)
    {
        RC95LDR_WINDOWS_INSTALLATION Trial;
        RC95LDR_BCD_BOOT_CANDIDATE *Source = &Candidates[ReadIndex];
        RC95LDR_STATUS Status = Rc95LdrResolveCandidate(
            Partitions, PartitionCount, Source, &Trial);
        if (Status == Rc95LdrNotFound || Status == Rc95LdrUnsupported ||
            Status == Rc95LdrCorruptData || Status == Rc95LdrOutOfBounds)
            continue;
        if (Status != Rc95LdrSuccess)
            return Status;
        if (WriteIndex != ReadIndex)
            Candidates[WriteIndex] = *Source;
        Candidates[WriteIndex].DeviceElement =
            &Candidates[WriteIndex].DeviceElementStorage;
        Candidates[WriteIndex].OsDeviceElement =
            &Candidates[WriteIndex].OsDeviceElementStorage;
        Candidates[WriteIndex].LoaderPathUtf16 =
            Candidates[WriteIndex].LoaderPathStorage;
        Candidates[WriteIndex].SystemRootUtf16 =
            Candidates[WriteIndex].SystemRootStorage;
        ++WriteIndex;
    }
    *CandidateCount = WriteIndex;
    return WriteIndex ? Rc95LdrSuccess : Rc95LdrNotFound;
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

RC95LDR_STATUS
Rc95LdrApplyInstallationGraphicsPolicy(
    const RC95LDR_WINDOWS_INSTALLATION *Installation,
    RC95LDR_WINLOAD_REQUEST *Request)
{
    if (!Installation || !Request ||
        Installation->GraphicsModeDisabled > 1u)
        return Rc95LdrInvalidParameter;
    if (Request->StructureSize <
        offsetof(RC95LDR_WINLOAD_REQUEST, GraphicsModePolicyValid) +
        sizeof(Request->GraphicsModePolicyValid))
        return Rc95LdrBufferTooSmall;
    Request->GraphicsModeDisabled = Installation->GraphicsModeDisabled;
    Request->GraphicsModePolicyValid = 1u;
    return Rc95LdrSuccess;
}

/*
 * A resource-only bootres.dll is admitted only for RC95-OS and only when the
 * selected BCD object explicitly supplied an enabled-graphics policy. An
 * absent/invalid policy is fail-closed for optional graphics resources.
 */
int
Rc95LdrShouldLoadBootResource(uint32_t OsFamily,
                              uint32_t GraphicsModePolicyValid,
                              uint32_t GraphicsModeDisabled)
{
    return OsFamily == Rc95LdrOsRc95 &&
           GraphicsModePolicyValid == 1u &&
           GraphicsModeDisabled == 0u;
}
