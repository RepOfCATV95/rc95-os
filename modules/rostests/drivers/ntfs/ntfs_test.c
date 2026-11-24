/*
 * PROJECT:    RC95-OS NTFS regression tests
 * LICENSE:    GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:    Exercise production NTFS metadata, sparse mapping, and I/O paths.
 * COPYRIGHT:  Copyright 1997-2028 RepOfCATV95
 */

#include "ntfs.h"

#ifndef NTFS_TEST_BUILD
#error This source belongs only to the NTFS_TEST_BUILD target
#endif

#define NDEBUG
#include <debug.h>

NTSTATUS NtfsTestRunStorageIo(PDRIVER_OBJECT DriverObject);
NTSTATUS NTAPI NtfsLogfileRunRecordFixtures(VOID);

NTSTATUS
NtfsTestRunIoRunList(VOID)
{
    NTFS_IO_RUN_LIST RunList;
    NTSTATUS Status;

    NtfsInitIoRunList(&RunList);

    Status = NtfsAddIoRun(&RunList, 0x1000, 512);
    if (!NT_SUCCESS(Status))
        goto Failure;

    /* Adjacent physical ranges must coalesce into one run. */
    Status = NtfsAddIoRun(&RunList, 0x1200, 256);
    if (!NT_SUCCESS(Status))
        goto Failure;

    /* A physical discontinuity starts a new run. */
    Status = NtfsAddIoRun(&RunList, 0x2000, 128);
    if (!NT_SUCCESS(Status))
        goto Failure;

    /* Sparse ranges coalesce, but remain separate from physical runs. */
    Status = NtfsAddIoRun(&RunList, NTFS_SPARSE_LBO, 32);
    if (!NT_SUCCESS(Status))
        goto Failure;

    Status = NtfsAddIoRun(&RunList, NTFS_SPARSE_LBO, 32);
    if (!NT_SUCCESS(Status))
        goto Failure;

    if (RunList.Count != 3 ||
        RunList.TotalLength != 960 ||
        RunList.Runs[0].Lbo != 0x1000 ||
        RunList.Runs[0].BufferOffset != 0 ||
        RunList.Runs[0].ByteCount != 768 ||
        RunList.Runs[1].Lbo != 0x2000 ||
        RunList.Runs[1].BufferOffset != 768 ||
        RunList.Runs[1].ByteCount != 128 ||
        RunList.Runs[2].Lbo != NTFS_SPARSE_LBO ||
        RunList.Runs[2].BufferOffset != 896 ||
        RunList.Runs[2].ByteCount != 64)
    {
        Status = STATUS_DATA_ERROR;
        goto Failure;
    }

    /* A zero-length append is harmless, but a NULL descriptor is not valid. */
    Status = NtfsAddIoRun(&RunList, 0x3000, 0);
    if (!NT_SUCCESS(Status) ||
        NtfsAddIoRun(NULL, 0x3000, 0) != STATUS_INVALID_PARAMETER)
    {
        Status = STATUS_DATA_ERROR;
        goto Failure;
    }

    /*
     * Invalid physical offsets must be rejected without changing the
     * accumulated run list. -1 is the only reserved sparse LBO.
     */
    {
        ULONG OldCount = RunList.Count;
        ULONG OldLength = RunList.TotalLength;

        if (NtfsAddIoRun(&RunList, -2, 16) != STATUS_INTEGER_OVERFLOW ||
            NtfsAddIoRun(&RunList, MAXLONGLONG - 1, 4) != STATUS_INTEGER_OVERFLOW ||
            RunList.Count != OldCount ||
            RunList.TotalLength != OldLength)
        {
            Status = STATUS_DATA_ERROR;
            goto Failure;
        }
    }

    /* A corrupt descriptor must be rejected before its run array is read. */
    {
        NTFS_IO_RUN_LIST InvalidRunList = RunList;

        InvalidRunList.Count = InvalidRunList.Capacity + 1;
        if (NtfsAddIoRun(&InvalidRunList, 0x4000, 16) !=
            STATUS_INVALID_PARAMETER)
        {
            Status = STATUS_DATA_ERROR;
            goto Failure;
        }

        InvalidRunList = RunList;
        InvalidRunList.Runs = NULL;
        if (NtfsAddIoRun(&InvalidRunList, 0x4000, 16) !=
            STATUS_INVALID_PARAMETER)
        {
            Status = STATUS_DATA_ERROR;
            goto Failure;
        }
    }

    /* Reject a total-length overflow without changing the existing runs. */
    {
        NTFS_IO_RUN_LIST OverflowRunList = RunList;
        ULONG OldCount = OverflowRunList.Count;
        ULONG OldRunLength = OverflowRunList.Runs[0].ByteCount;

        OverflowRunList.TotalLength = MAXULONG - 3;
        if (NtfsAddIoRun(&OverflowRunList, 0x5000, 4) !=
                STATUS_INTEGER_OVERFLOW ||
            OverflowRunList.Count != OldCount ||
            OverflowRunList.Runs[0].ByteCount != OldRunLength ||
            OverflowRunList.TotalLength != MAXULONG - 3)
        {
            Status = STATUS_DATA_ERROR;
            goto Failure;
        }
    }

    /*
     * Fill the enlarged inline run array first. Moderately fragmented
     * requests should avoid a nonpaged-pool allocation.
     */
    while (RunList.Count < NTFS_MAX_IO_RUNS_ON_STACK)
    {
        LONGLONG Lbo = 0x4000 + ((LONGLONG)RunList.Count * 0x200);

        Status = NtfsAddIoRun(&RunList, Lbo, 16);
        if (!NT_SUCCESS(Status))
            goto Failure;
    }

    if (RunList.Runs != RunList.StackRuns ||
        RunList.Capacity != NTFS_MAX_IO_RUNS_ON_STACK)
    {
        Status = STATUS_DATA_ERROR;
        goto Failure;
    }

    /* The first run beyond inline capacity must spill safely into pool. */
    Status = NtfsAddIoRun(&RunList,
                          0x4000 + ((LONGLONG)RunList.Count * 0x200),
                          16);
    if (!NT_SUCCESS(Status))
        goto Failure;

    if (RunList.Runs == RunList.StackRuns ||
        RunList.Capacity <= NTFS_MAX_IO_RUNS_ON_STACK ||
        RunList.Count != NTFS_MAX_IO_RUNS_ON_STACK + 1)
    {
        Status = STATUS_DATA_ERROR;
        goto Failure;
    }

    NtfsFreeIoRunList(&RunList);
    if (RunList.Runs != NULL ||
        RunList.Count != 0 ||
        RunList.Capacity != 0 ||
        RunList.TotalLength != 0)
    {
        return STATUS_DATA_ERROR;
    }

    DPRINT1("NTFS_TEST_BUILD: I/O run-list tests passed\n");
    return STATUS_SUCCESS;

Failure:
    NtfsFreeIoRunList(&RunList);
    DPRINT1("NTFS_TEST_BUILD: I/O run-list test failed (0x%lx)\n", Status);
    return Status;
}


static
NTSTATUS
NtfsTestRunAttributeEnvelope(VOID)
{
    DEVICE_EXTENSION Vcb;
    UCHAR RecordBuffer[1024];
    PFILE_RECORD_HEADER Record;
    PNTFS_ATTR_RECORD Attribute;
    PNTFS_ATTR_RECORD FoundAttribute;
    FIND_ATTR_CONTXT Context;
    NTSTATUS Status;

    RtlZeroMemory(&Vcb, sizeof(Vcb));
    RtlZeroMemory(RecordBuffer, sizeof(RecordBuffer));
    Vcb.NtfsInfo.BytesPerFileRecord = sizeof(RecordBuffer);

    Record = (PFILE_RECORD_HEADER)RecordBuffer;
    Record->BytesAllocated = sizeof(RecordBuffer);
    Record->BytesInUse = sizeof(FILE_RECORD_HEADER) + 24 + sizeof(ULONG);
    Record->AttributeOffset = sizeof(FILE_RECORD_HEADER);

    Attribute = (PNTFS_ATTR_RECORD)(RecordBuffer + Record->AttributeOffset);
    Attribute->Type = AttributeData;
    Attribute->Length = 24;
    Attribute->IsNonResident = FALSE;
    Attribute->Resident.ValueOffset = 24;
    Attribute->Resident.ValueLength = 0;

    Status = FindFirstAttribute(&Context, &Vcb, Record, TRUE, &FoundAttribute);
    if (!NT_SUCCESS(Status))
        return STATUS_DATA_ERROR;
    FindCloseAttribute(&Context);

    /* A resident value may not escape its own attribute, even inside the MFT record. */
    Attribute->Resident.ValueLength = 1;
    Status = FindFirstAttribute(&Context, &Vcb, Record, TRUE, &FoundAttribute);
    if (Status != STATUS_FILE_CORRUPT_ERROR)
        return STATUS_DATA_ERROR;

    Attribute->Resident.ValueLength = 0;
    Attribute->NameLength = 1;
    Attribute->NameOffset = 24;
    Status = FindFirstAttribute(&Context, &Vcb, Record, TRUE, &FoundAttribute);
    if (Status != STATUS_FILE_CORRUPT_ERROR)
        return STATUS_DATA_ERROR;

    Attribute->NameLength = 0;
    Attribute->NameOffset = 0;
    Attribute->Length = 25;
    Status = FindFirstAttribute(&Context, &Vcb, Record, TRUE, &FoundAttribute);
    if (Status != STATUS_FILE_CORRUPT_ERROR)
        return STATUS_DATA_ERROR;

    /* Ordinary nonresident attributes may omit the optional CompressedSize field. */
    Attribute->Length = 72;
    Attribute->IsNonResident = TRUE;
    Attribute->NonResident.LowestVCN = 0;
    Attribute->NonResident.HighestVCN = 0;
    Attribute->NonResident.MappingPairsOffset = 64;
    Record->BytesInUse = Record->AttributeOffset + Attribute->Length + sizeof(ULONG);
    Status = FindFirstAttribute(&Context, &Vcb, Record, TRUE, &FoundAttribute);
    if (!NT_SUCCESS(Status))
        return STATUS_DATA_ERROR;
    FindCloseAttribute(&Context);

    /* Sparse attributes carry the extended header including CompressedSize. */
    Attribute->Flags = NTFS_ATTR_SPARSE;
    Attribute->Length = 80;
    Attribute->NonResident.MappingPairsOffset = 64;
    Record->BytesInUse = Record->AttributeOffset + Attribute->Length + sizeof(ULONG);
    Status = FindFirstAttribute(&Context, &Vcb, Record, TRUE, &FoundAttribute);
    if (Status != STATUS_FILE_CORRUPT_ERROR)
        return STATUS_DATA_ERROR;

    Attribute->NonResident.MappingPairsOffset = 72;
    Status = FindFirstAttribute(&Context, &Vcb, Record, TRUE, &FoundAttribute);
    if (!NT_SUCCESS(Status))
        return STATUS_DATA_ERROR;
    FindCloseAttribute(&Context);

    DPRINT1("NTFS_TEST_BUILD: MFT attribute-envelope tests passed\\n");
    return STATUS_SUCCESS;
}

static
NTSTATUS
NtfsTestRunMftFixups(VOID)
{
    DEVICE_EXTENSION Vcb;
    UCHAR RecordBuffer[1024];
    PNTFS_RECORD_HEADER Header;
    PUSHORT Usa;
    PUSHORT FirstTrailer;
    PUSHORT SecondTrailer;
    NTSTATUS Status;

    RtlZeroMemory(&Vcb, sizeof(Vcb));
    Vcb.NtfsInfo.BytesPerSector = 512;
    Vcb.NtfsInfo.BytesPerFileRecord = sizeof(RecordBuffer);
    Vcb.NtfsInfo.BytesPerIndexRecord = sizeof(RecordBuffer);

    RtlZeroMemory(RecordBuffer, sizeof(RecordBuffer));
    Header = (PNTFS_RECORD_HEADER)RecordBuffer;
    Header->Type = NRH_FILE_TYPE;
    Header->UsaOffset = 48;
    Header->UsaCount = 3;
    Usa = (PUSHORT)(RecordBuffer + Header->UsaOffset);
    Usa[0] = 0x1234;
    FirstTrailer = (PUSHORT)(RecordBuffer + 510);
    SecondTrailer = (PUSHORT)(RecordBuffer + 1022);
    *FirstTrailer = 0x1111;
    *SecondTrailer = 0x2222;

    Status = AddFixupArray(&Vcb, Header);
    if (!NT_SUCCESS(Status) || *FirstTrailer != Usa[0] ||
        *SecondTrailer != Usa[0])
        return STATUS_DATA_ERROR;

    Status = FixupUpdateSequenceArray(&Vcb, Header);
    if (!NT_SUCCESS(Status) || *FirstTrailer != 0x1111 ||
        *SecondTrailer != 0x2222)
        return STATUS_DATA_ERROR;

    /* A bad later trailer must not restore an earlier sector partially. */
    Status = AddFixupArray(&Vcb, Header);
    if (!NT_SUCCESS(Status))
        return Status;
    *SecondTrailer = 0x7777;
    Status = FixupUpdateSequenceArray(&Vcb, Header);
    if (Status != STATUS_FILE_CORRUPT_ERROR ||
        *FirstTrailer != Usa[0] || *SecondTrailer != 0x7777)
        return STATUS_DATA_ERROR;

    Header->UsaCount = 2;
    Status = FixupUpdateSequenceArray(&Vcb, Header);
    if (Status != STATUS_FILE_CORRUPT_ERROR)
        return STATUS_DATA_ERROR;

    DPRINT1("NTFS_TEST_BUILD: MFT update-sequence tests passed\\n");
    return STATUS_SUCCESS;
}

static
NTSTATUS
NtfsTestRunSparseMappingPairs(VOID)
{
    UCHAR MappingPairs[] =
    {
        0x11, 3, 10,       /* VCN 0..2 -> LCN 10..12 */
        0x01, 2,           /* VCN 3..4 are sparse */
        0x11, 2, 20,       /* VCN 5..6 -> LCN 30..31 */
        0
    };
    UCHAR TailPairs[] =
    {
        0x11, 2, 10,
        0x01, 3,
        0
    };
    UCHAR Output[64];
    LARGE_MCB Mcb;
    ULONGLONG NextVbn;
    ULONG OutputLength;
    LONGLONG Vbn, Lbn, Count;
    NTSTATUS Status;

    NextVbn = 0;
    Status = ConvertDataRunsToLargeMCB(MappingPairs,
                                       sizeof(MappingPairs),
                                       0,
                                       &Mcb,
                                       &NextVbn);
    if (!NT_SUCCESS(Status) || NextVbn != 7 ||
        FsRtlNumberOfRunsInLargeMcb(&Mcb) != 2)
        return STATUS_DATA_ERROR;

    if (!FsRtlGetNextLargeMcbEntry(&Mcb, 0, &Vbn, &Lbn, &Count) ||
        Vbn != 0 || Lbn != 10 || Count != 3 ||
        !FsRtlGetNextLargeMcbEntry(&Mcb, 1, &Vbn, &Lbn, &Count) ||
        Vbn != 5 || Lbn != 30 || Count != 2)
    {
        FsRtlUninitializeLargeMcb(&Mcb);
        return STATUS_DATA_ERROR;
    }

    RtlZeroMemory(Output, sizeof(Output));
    Status = ConvertLargeMCBToDataRuns(&Mcb, 7, Output,
                                       sizeof(Output), &OutputLength);
    if (!NT_SUCCESS(Status) || OutputLength != sizeof(MappingPairs) ||
        RtlCompareMemory(Output, MappingPairs, sizeof(MappingPairs)) !=
            sizeof(MappingPairs))
    {
        FsRtlUninitializeLargeMcb(&Mcb);
        return STATUS_DATA_ERROR;
    }

    /*
     * Materialize the two-cluster hole between the original extents. The
     * resulting mapping-pair stream must retain the later physical extent.
     */
    _SEH2_TRY
    {
        if (!FsRtlAddLargeMcbEntry(&Mcb, 3, 40, 2))
            ExRaiseStatus(STATUS_UNSUCCESSFUL);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        FsRtlUninitializeLargeMcb(&Mcb);
        return _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (FsRtlNumberOfRunsInLargeMcb(&Mcb) != 3 ||
        !FsRtlGetNextLargeMcbEntry(&Mcb, 1, &Vbn, &Lbn, &Count) ||
        Vbn != 3 || Lbn != 40 || Count != 2 ||
        !FsRtlGetNextLargeMcbEntry(&Mcb, 2, &Vbn, &Lbn, &Count) ||
        Vbn != 5 || Lbn != 30 || Count != 2)
    {
        FsRtlUninitializeLargeMcb(&Mcb);
        return STATUS_DATA_ERROR;
    }

    RtlZeroMemory(Output, sizeof(Output));
    Status = ConvertLargeMCBToDataRuns(&Mcb, 7, Output,
                                       sizeof(Output), &OutputLength);
    FsRtlUninitializeLargeMcb(&Mcb);
    if (!NT_SUCCESS(Status) || OutputLength == 0 ||
        OutputLength >= sizeof(Output))
        return STATUS_DATA_ERROR;

    /* Decode the updated mapping pairs and verify all seven VCNs are backed. */
    NextVbn = 0;
    Status = ConvertDataRunsToLargeMCB(Output, OutputLength, 0, &Mcb, &NextVbn);
    if (!NT_SUCCESS(Status) || NextVbn != 7 ||
        FsRtlNumberOfRunsInLargeMcb(&Mcb) != 3)
    {
        if (NT_SUCCESS(Status))
            FsRtlUninitializeLargeMcb(&Mcb);
        return STATUS_DATA_ERROR;
    }
    FsRtlUninitializeLargeMcb(&Mcb);

    /* An implicit sparse tail must be materialized when encoding the MCB. */
    NextVbn = 0;
    Status = ConvertDataRunsToLargeMCB(TailPairs, sizeof(TailPairs), 0,
                                       &Mcb, &NextVbn);
    if (!NT_SUCCESS(Status) || NextVbn != 5)
        return STATUS_DATA_ERROR;

    RtlZeroMemory(Output, sizeof(Output));
    Status = ConvertLargeMCBToDataRuns(&Mcb, 5, Output,
                                       sizeof(Output), &OutputLength);
    FsRtlUninitializeLargeMcb(&Mcb);
    if (!NT_SUCCESS(Status) || OutputLength != sizeof(TailPairs) ||
        RtlCompareMemory(Output, TailPairs, sizeof(TailPairs)) !=
            sizeof(TailPairs))
        return STATUS_DATA_ERROR;

    /* Reject truncated, zero-length, and underflowing physical mapping pairs. */
    {
        UCHAR Truncated[] = { 0x11, 3 };
        UCHAR ZeroLength[] = { 0x01, 0, 0 };
        UCHAR Underflow[] = { 0x11, 1, 0xFF, 0 };

        NextVbn = 0;
        if (NT_SUCCESS(ConvertDataRunsToLargeMCB(Truncated,
                                                  sizeof(Truncated), 0,
                                                  &Mcb, &NextVbn)))
        {
            FsRtlUninitializeLargeMcb(&Mcb);
            return STATUS_DATA_ERROR;
        }

        NextVbn = 0;
        if (NT_SUCCESS(ConvertDataRunsToLargeMCB(ZeroLength,
                                                  sizeof(ZeroLength), 0,
                                                  &Mcb, &NextVbn)))
        {
            FsRtlUninitializeLargeMcb(&Mcb);
            return STATUS_DATA_ERROR;
        }

        NextVbn = 0;
        if (NT_SUCCESS(ConvertDataRunsToLargeMCB(Underflow,
                                                  sizeof(Underflow), 0,
                                                  &Mcb, &NextVbn)))
        {
            FsRtlUninitializeLargeMcb(&Mcb);
            return STATUS_DATA_ERROR;
        }
    }

    DPRINT1("NTFS_TEST_BUILD: sparse mapping-pairs tests passed\\n");
    return STATUS_SUCCESS;
}

static
NTSTATUS
NtfsTestRunBitmapRange(VOID)
{
    ULONGLONG Start, Bits;
    ULONG Bytes;
    BOOLEAN Overflow;
    ULONG HeaderSize = FIELD_OFFSET(VOLUME_BITMAP_BUFFER, Buffer);
    NTSTATUS Status;

    /* Invalid calls must not partially overwrite the caller's outputs. */
    Start = 0x1111111111111111ULL;
    Bytes = 0x22222222;
    Bits = 0x3333333333333333ULL;
    Overflow = TRUE;
    if (NtfsCalculateVolumeBitmapRange(-1, 17, HeaderSize + 8,
                                       &Start, &Bytes, &Bits, &Overflow) !=
            STATUS_INVALID_PARAMETER ||
        Start != 0x1111111111111111ULL || Bytes != 0x22222222 ||
        Bits != 0x3333333333333333ULL || !Overflow)
        return STATUS_DATA_ERROR;

    /* Negative starts and starts beyond the volume must fail closed. */
    if (NtfsCalculateVolumeBitmapRange(18, 17, HeaderSize + 8,
                                       &Start, &Bytes, &Bits, &Overflow) !=
            STATUS_INVALID_PARAMETER ||
        NtfsCalculateVolumeBitmapRange(0, 17, HeaderSize - 1,
                                       &Start, &Bytes, &Bits, &Overflow) !=
            STATUS_INVALID_PARAMETER ||
        NtfsCalculateVolumeBitmapRange(0, 17, HeaderSize + 8,
                                       NULL, &Bytes, &Bits, &Overflow) !=
            STATUS_INVALID_PARAMETER)
        return STATUS_DATA_ERROR;

    /* A request at EOF is valid and returns an empty bitmap. */
    Status = NtfsCalculateVolumeBitmapRange(17, 17, HeaderSize + 8,
                                            &Start, &Bytes, &Bits, &Overflow);
    if (!NT_SUCCESS(Status) || Start != 16 || Bytes != 1 ||
        Bits != 1 || Overflow)
        return STATUS_DATA_ERROR;

    /* A non-byte-aligned volume includes its final partial bitmap byte. */
    Status = NtfsCalculateVolumeBitmapRange(0, 17, HeaderSize + 8,
                                            &Start, &Bytes, &Bits, &Overflow);
    if (!NT_SUCCESS(Status) || Start != 0 || Bytes != 3 ||
        Bits != 17 || Overflow)
        return STATUS_DATA_ERROR;

    /* A short caller buffer reports only the bits that fit. */
    Status = NtfsCalculateVolumeBitmapRange(0, 65, HeaderSize + 1,
                                            &Start, &Bytes, &Bits, &Overflow);
    if (!NT_SUCCESS(Status) || Start != 0 || Bytes != 1 ||
        Bits != 8 || !Overflow)
        return STATUS_DATA_ERROR;

    /* Alignment rounds down while preserving the requested containing byte. */
    Status = NtfsCalculateVolumeBitmapRange(9, 65, HeaderSize + 8,
                                            &Start, &Bytes, &Bits, &Overflow);
    if (!NT_SUCCESS(Status) || Start != 8 || Bytes != 8 ||
        Bits != 57 || Overflow)
        return STATUS_DATA_ERROR;

    /* Large logical volumes must clamp to the caller's bounded buffer. */
    Status = NtfsCalculateVolumeBitmapRange(0, (~(ULONGLONG)0),
                                            HeaderSize + 2,
                                            &Start, &Bytes, &Bits, &Overflow);
    if (!NT_SUCCESS(Status) || Start != 0 || Bytes != 2 ||
        Bits != 16 || !Overflow)
        return STATUS_DATA_ERROR;

    /* Empty volumes still produce a valid zero-bit result. */
    Status = NtfsCalculateVolumeBitmapRange(0, 0, HeaderSize + 1,
                                            &Start, &Bytes, &Bits, &Overflow);
    if (!NT_SUCCESS(Status) || Start != 0 || Bytes != 0 ||
        Bits != 0 || Overflow)
        return STATUS_DATA_ERROR;

    DPRINT1("NTFS_TEST_BUILD: volume bitmap range tests passed\\n");
    return STATUS_SUCCESS;
}


static
NTSTATUS
NtfsTestRunBitmapStorageRange(VOID)
{
    ULONG Offset = 0xA5A5A5A5;
    NTSTATUS Status;

    Status = NtfsCalculateBitmapStorageRange(0, 1, 1, &Offset);
    if (!NT_SUCCESS(Status) || Offset != 0)
        return STATUS_DATA_ERROR;

    /* Exact-end, zero-byte reads are valid. */
    Status = NtfsCalculateBitmapStorageRange(8, 0, 1, &Offset);
    if (!NT_SUCCESS(Status) || Offset != 1)
        return STATUS_DATA_ERROR;

    /* A range ending exactly at the cached bitmap boundary is valid. */
    Status = NtfsCalculateBitmapStorageRange(8, 2, 3, &Offset);
    if (!NT_SUCCESS(Status) || Offset != 1)
        return STATUS_DATA_ERROR;

    /* Reject ranges that would read beyond the cached $Bitmap allocation. */
    Offset = 0xA5A5A5A5;
    Status = NtfsCalculateBitmapStorageRange(8, 3, 3, &Offset);
    if (Status != STATUS_FILE_CORRUPT_ERROR || Offset != 0xA5A5A5A5)
        return STATUS_DATA_ERROR;

    Status = NtfsCalculateBitmapStorageRange(32, 1, 3, &Offset);
    if (Status != STATUS_FILE_CORRUPT_ERROR)
        return STATUS_DATA_ERROR;

    /* Reject non-byte-aligned LCNs and null output pointers. */
    if (NtfsCalculateBitmapStorageRange(1, 1, 3, &Offset) !=
            STATUS_INVALID_PARAMETER ||
        NtfsCalculateBitmapStorageRange(0, 1, 3, NULL) !=
            STATUS_INVALID_PARAMETER)
        return STATUS_DATA_ERROR;

    DPRINT1("NTFS_TEST_BUILD: bitmap backing-range tests passed\\n");
    return STATUS_SUCCESS;
}


NTSTATUS
NtfsTestRunAll(PDRIVER_OBJECT DriverObject)
{
    NTSTATUS Status;

    Status = NtfsLogfileRunRecordFixtures();
    if (!NT_SUCCESS(Status))
        return Status;

    Status = NtfsTestRunIoRunList();
    if (!NT_SUCCESS(Status))
        return Status;

    Status = NtfsTestRunAttributeEnvelope();
    if (!NT_SUCCESS(Status))
        return Status;

    Status = NtfsTestRunMftFixups();
    if (!NT_SUCCESS(Status))
        return Status;

    Status = NtfsTestRunSparseMappingPairs();
    if (!NT_SUCCESS(Status))
        return Status;

    Status = NtfsTestRunBitmapRange();
    if (!NT_SUCCESS(Status))
        return Status;

    Status = NtfsTestRunBitmapStorageRange();
    if (!NT_SUCCESS(Status))
        return Status;

    return NtfsTestRunStorageIo(DriverObject);
}
