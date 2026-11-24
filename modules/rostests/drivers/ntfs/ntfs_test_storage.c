/*
 * PROJECT:    RC95-OS NTFS regression tests
 * LICENSE:    GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:    In-memory lower storage device with deterministic I/O faults.
 * COPYRIGHT:  Copyright 1997-2028 RepOfCATV95
 */

#include "ntfs.h"

#ifndef NTFS_TEST_BUILD
#error This source belongs only to the NTFS_TEST_BUILD target
#endif

#define NDEBUG
#include <debug.h>

#define NTFS_TEST_STORAGE_SIZE 65536

#define NTFS_TEST_FAULT_NONE   0
#define NTFS_TEST_FAULT_SHORT  1
#define NTFS_TEST_FAULT_ERROR  2

typedef struct _NTFS_TEST_STORAGE
{
    PUCHAR Image;
    ULONG ImageLength;
    volatile LONG RequestCount;
    volatile LONG FaultRequest;
    volatile LONG FaultMode;
    LONGLONG RequestOffset[256];
    ULONG RequestLength[256];
    UCHAR RequestMajor[256];
} NTFS_TEST_STORAGE, *PNTFS_TEST_STORAGE;

static
NTSTATUS
NTAPI
NtfsTestStorageDispatch(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PNTFS_TEST_STORAGE Storage;
    PIO_STACK_LOCATION Stack;
    PUCHAR MdlBuffer;
    LONGLONG Offset;
    ULONG Length;
    ULONG TransferLength;
    ULONG_PTR Information = 0;
    LONG RequestIndex;
    LONG FaultMode;
    NTSTATUS Status = STATUS_SUCCESS;

    Storage = (PNTFS_TEST_STORAGE)DeviceObject->DeviceExtension;
    Stack = IoGetCurrentIrpStackLocation(Irp);
    Offset = Stack->Parameters.Read.ByteOffset.QuadPart;
    Length = Stack->Parameters.Read.Length;
    RequestIndex = InterlockedIncrement(&Storage->RequestCount) - 1;
    if (RequestIndex >= 0 && (ULONG)RequestIndex < RTL_NUMBER_OF(Storage->RequestOffset))
    {
        Storage->RequestOffset[RequestIndex] = Offset;
        Storage->RequestLength[RequestIndex] = Length;
        Storage->RequestMajor[RequestIndex] = Stack->MajorFunction;
    }
    FaultMode = (RequestIndex == Storage->FaultRequest) ?
                Storage->FaultMode : NTFS_TEST_FAULT_NONE;

    if (FaultMode == NTFS_TEST_FAULT_ERROR)
    {
        Status = STATUS_IO_DEVICE_ERROR;
        goto Complete;
    }

    if (Offset < 0 ||
        (ULONGLONG)Offset > Storage->ImageLength ||
        Length > Storage->ImageLength - (ULONG)Offset ||
        Irp->MdlAddress == NULL)
    {
        Status = STATUS_INVALID_PARAMETER;
        goto Complete;
    }

    MdlBuffer = MmGetSystemAddressForMdlSafe(Irp->MdlAddress,
                                             NormalPagePriority);
    if (MdlBuffer == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Complete;
    }

    TransferLength = Length;
    if (FaultMode == NTFS_TEST_FAULT_SHORT && TransferLength != 0)
        TransferLength--;

    if (Stack->MajorFunction == IRP_MJ_READ)
    {
        RtlCopyMemory(MdlBuffer,
                      Storage->Image + (ULONG)Offset,
                      TransferLength);
    }
    else if (Stack->MajorFunction == IRP_MJ_WRITE)
    {
        RtlCopyMemory(Storage->Image + (ULONG)Offset,
                      MdlBuffer,
                      TransferLength);
    }
    else
    {
        Status = STATUS_INVALID_DEVICE_REQUEST;
        goto Complete;
    }

    Information = TransferLength;

Complete:
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = Information;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

/*
 * Exercise the real volume-bitmap allocator and its $Bitmap writeback
 * against the fake lower device. This fixture intentionally builds only the
 * minimum VCB/attribute state required by the production allocation path.
 */
static
NTSTATUS
NtfsTestRunBitmapAllocation(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PNTFS_TEST_STORAGE Storage)
{
    DEVICE_EXTENSION Vcb;
    NTFS_ATTR_CONTEXT BitmapContext;
    NTFS_ATTR_RECORD BitmapAttribute;
    UCHAR BitmapMappingPairs[] = { 0x11, 2, 2, 0 };
    ULONGLONG NextVbn = 0;
    ULONG FirstCluster;
    ULONG AssignedClusters;
    LONG RequestsBefore;
    NTSTATUS Status;

    RtlZeroMemory(&Vcb, sizeof(Vcb));
    RtlZeroMemory(&BitmapContext, sizeof(BitmapContext));
    RtlZeroMemory(&BitmapAttribute, sizeof(BitmapAttribute));

    Vcb.StorageDevice = DeviceObject;
    Vcb.NtfsInfo.BytesPerSector = 512;
    Vcb.NtfsInfo.BytesPerCluster = 512;
    Vcb.NtfsInfo.SectorsPerCluster = 1;
    Vcb.NtfsInfo.BytesPerFileRecord = 1024;
    Vcb.NtfsInfo.ClusterCount = 8192;
    Vcb.BitmapSize = 1024;
    Vcb.BitmapBuffer = ExAllocatePoolWithTag(NonPagedPool, 1024, TAG_NTFS);
    if (Vcb.BitmapBuffer == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Vcb.BitmapBuffer, 1024);
    RtlInitializeBitMap(&Vcb.ClusterBitmap, Vcb.BitmapBuffer, 8192);
    RtlSetBits(&Vcb.ClusterBitmap, 0, 8); /* Reserved low clusters. */
    Vcb.FreeClusterCount = 8182;

    Status = ExInitializeResourceLite(&Vcb.BitmapResource);
    if (!NT_SUCCESS(Status))
        goto CleanupBitmap;

    BitmapAttribute.Type = AttributeData;
    BitmapAttribute.Length = sizeof(BitmapAttribute);
    BitmapAttribute.IsNonResident = TRUE;
    BitmapAttribute.NonResident.LowestVCN = 0;
    BitmapAttribute.NonResident.HighestVCN = 1;
    BitmapAttribute.NonResident.MappingPairsOffset = 72;
    BitmapAttribute.NonResident.AllocatedSize = 1024;
    BitmapAttribute.NonResident.DataSize = 1024;
    BitmapContext.pRecord = &BitmapAttribute;

    Status = ConvertDataRunsToLargeMCB(BitmapMappingPairs,
                                       sizeof(BitmapMappingPairs),
                                       0,
                                       &BitmapContext.DataRunsMCB,
                                       &NextVbn);
    if (!NT_SUCCESS(Status) || NextVbn != 2)
    {
        if (NT_SUCCESS(Status))
        {
            FsRtlUninitializeLargeMcb(&BitmapContext.DataRunsMCB);
            Status = STATUS_DATA_ERROR;
        }
        goto CleanupResource;
    }

    Vcb.BitmapContext = &BitmapContext;

    /*
     * Query the production free-space accessor with a valid bitmap and with
     * each class of stale/corrupt state it must fail closed on. Keep the
     * resource initialized: NtfsGetFreeClusters() always takes it.
     */
    if (NtfsGetFreeClusters(NULL) != 0 ||
        NtfsGetFreeClusters(&Vcb) != Vcb.FreeClusterCount)
    {
        Status = STATUS_DATA_ERROR;
        goto CleanupMcb;
    }

    Vcb.Flags |= VCB_BITMAP_WRITE_FAILED;
    if (NtfsGetFreeClusters(&Vcb) != 0)
    {
        Status = STATUS_DATA_ERROR;
        goto CleanupMcb;
    }
    Vcb.Flags &= ~VCB_BITMAP_WRITE_FAILED;

    Vcb.BitmapSize = 0;
    if (NtfsGetFreeClusters(&Vcb) != 0)
    {
        Status = STATUS_DATA_ERROR;
        goto CleanupMcb;
    }
    Vcb.BitmapSize = 1024;

    Vcb.ClusterBitmap.Buffer = NULL;
    if (NtfsGetFreeClusters(&Vcb) != 0)
    {
        Status = STATUS_DATA_ERROR;
        goto CleanupMcb;
    }
    Vcb.ClusterBitmap.Buffer = Vcb.BitmapBuffer;

    Vcb.ClusterBitmap.SizeOfBitMap = 0;
    if (NtfsGetFreeClusters(&Vcb) != 0)
    {
        Status = STATUS_DATA_ERROR;
        goto CleanupMcb;
    }
    Vcb.ClusterBitmap.SizeOfBitMap = 8192;

    Vcb.ClusterBitmap.SizeOfBitMap = 8193;
    if (NtfsGetFreeClusters(&Vcb) != 0)
    {
        Status = STATUS_DATA_ERROR;
        goto CleanupMcb;
    }
    Vcb.ClusterBitmap.SizeOfBitMap = 8192;

    Vcb.FreeClusterCount = 8193;
    if (NtfsGetFreeClusters(&Vcb) != 0)
    {
        Status = STATUS_DATA_ERROR;
        goto CleanupMcb;
    }
    Vcb.FreeClusterCount = 8182;

    /*
     * Impossible allocation requests must fail without changing the bitmap,
     * free-space accounting, or issuing lower-device I/O.
     */
    RequestsBefore = Storage->RequestCount;
    FirstCluster = 0xFFFFFFFF;
    AssignedClusters = 0xFFFFFFFF;
    Status = NtfsAllocateClusters(&Vcb, 8, 0,
                                  &FirstCluster, &AssignedClusters);
    if (Status != STATUS_INVALID_PARAMETER || FirstCluster != 0 ||
        AssignedClusters != 0 || Vcb.FreeClusterCount != 8182 ||
        Storage->RequestCount != RequestsBefore)
    {
        Status = STATUS_DATA_ERROR;
        goto CleanupMcb;
    }

    FirstCluster = 0xFFFFFFFF;
    AssignedClusters = 0xFFFFFFFF;
    Status = NtfsAllocateClusters(&Vcb, 8, 8193,
                                  &FirstCluster, &AssignedClusters);
    if (Status != STATUS_DISK_FULL || FirstCluster != 0 ||
        AssignedClusters != 0 || Vcb.FreeClusterCount != 8182 ||
        Storage->RequestCount != RequestsBefore)
    {
        Status = STATUS_DATA_ERROR;
        goto CleanupMcb;
    }

    /*
     * A hint at or beyond the bitmap's logical end is normalized to zero.
     * Verify the allocator chooses the first valid free cluster in that case.
     */
    RtlSetAllBits(&Vcb.ClusterBitmap);
    RtlClearBits(&Vcb.ClusterBitmap, 8, 1);
    Vcb.FreeClusterCount = 1;
    Status = NtfsAllocateClusters(&Vcb, 8192, 1,
                                  &FirstCluster, &AssignedClusters);
    if (!NT_SUCCESS(Status) || FirstCluster != 8 ||
        AssignedClusters != 1 || Vcb.FreeClusterCount != 0)
    {
        Status = NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
        goto CleanupMcb;
    }

    /* A contiguous allocation must persist the corresponding $Bitmap bits. */
    RtlSetAllBits(&Vcb.ClusterBitmap);
    RtlClearBits(&Vcb.ClusterBitmap, 8, 3);
    Vcb.FreeClusterCount = 8182;
    Status = NtfsAllocateClusters(&Vcb, 8, 3,
                                  &FirstCluster, &AssignedClusters);
    if (!NT_SUCCESS(Status) || FirstCluster != 8 ||
        AssignedClusters != 3 || Vcb.FreeClusterCount != 8179 ||
        !RtlAreBitsSet(&Vcb.ClusterBitmap, 8, 3) ||
        Storage->Image[1024] != 0xFF ||
        Storage->Image[1025] != 0x07)
    {
        Status = NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
        goto CleanupMcb;
    }

    /*
     * Force fragmentation: only VCN-independent cluster numbers 8 and 10
     * are free, so a two-cluster request must be returned as two persisted
     * one-cluster allocations rather than claiming an adjacent run.
     */
    RtlSetAllBits(&Vcb.ClusterBitmap);
    RtlClearBits(&Vcb.ClusterBitmap, 8, 1);
    RtlClearBits(&Vcb.ClusterBitmap, 10, 1);
    Vcb.FreeClusterCount = 2;

    Status = NtfsAllocateClusters(&Vcb, 8, 2,
                                  &FirstCluster, &AssignedClusters);
    if (!NT_SUCCESS(Status) || FirstCluster != 8 || AssignedClusters != 1 ||
        Vcb.FreeClusterCount != 1 ||
        !RtlAreBitsSet(&Vcb.ClusterBitmap, 8, 1) ||
        RtlAreBitsSet(&Vcb.ClusterBitmap, 10, 1))
    {
        Status = NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
        goto CleanupMcb;
    }

    Status = NtfsAllocateClusters(&Vcb, 10, 1,
                                  &FirstCluster, &AssignedClusters);
    if (!NT_SUCCESS(Status) || FirstCluster != 10 || AssignedClusters != 1 ||
        Vcb.FreeClusterCount != 0 ||
        !RtlAreBitsSet(&Vcb.ClusterBitmap, 8, 1) ||
        !RtlAreBitsSet(&Vcb.ClusterBitmap, 10, 1))
    {
        Status = NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
        goto CleanupMcb;
    }

    /*
     * A lower-device error leaves the in-memory reservation rolled back,
     * preserves the free count, and marks the bitmap write state uncertain.
     */
    RtlSetAllBits(&Vcb.ClusterBitmap);
    RtlClearBits(&Vcb.ClusterBitmap, 16, 2);
    Vcb.FreeClusterCount = 2;
    RequestsBefore = Storage->RequestCount;
    Storage->FaultRequest = RequestsBefore;
    Storage->FaultMode = NTFS_TEST_FAULT_ERROR;
    Status = NtfsAllocateClusters(&Vcb, 16, 2,
                                  &FirstCluster, &AssignedClusters);
    if (Status != STATUS_IO_DEVICE_ERROR ||
        AssignedClusters != 0 ||
        Vcb.FreeClusterCount != 2 ||
        RtlAreBitsSet(&Vcb.ClusterBitmap, 16, 2) ||
        !(Vcb.Flags & VCB_BITMAP_WRITE_FAILED) ||
        NtfsGetFreeClusters(&Vcb) != 0)
    {
        Status = STATUS_DATA_ERROR;
        goto CleanupMcb;
    }

    /*
     * Once a bitmap write is uncertain, allocation must fail before another
     * lower-device request. The failed reservation above was rolled back in
     * memory, but the disk may contain a partially persisted bitmap update.
     */
    RequestsBefore = Storage->RequestCount;
    Status = NtfsAllocateClusters(&Vcb, 16, 1,
                                  &FirstCluster, &AssignedClusters);
    if (Status != STATUS_DEVICE_DATA_ERROR ||
        AssignedClusters != 0 ||
        FirstCluster != 0 ||
        Vcb.FreeClusterCount != 2 ||
        RtlAreBitsSet(&Vcb.ClusterBitmap, 16, 2) ||
        Storage->RequestCount != RequestsBefore)
    {
        Status = STATUS_DATA_ERROR;
        goto CleanupMcb;
    }

    /*
     * If the allocation hint is beyond all free clusters, the fragmented
     * fallback must still find a run in the wrapped portion of the bitmap.
     * Only cluster 4 is free, so a two-cluster request is reduced to one.
     */
    RtlSetAllBits(&Vcb.ClusterBitmap);
    RtlClearBits(&Vcb.ClusterBitmap, 4, 1);
    Vcb.FreeClusterCount = 1;
    RequestsBefore = Storage->RequestCount;
    Status = NtfsAllocateClusters(&Vcb, 100, 2,
                                  &FirstCluster, &AssignedClusters);
    if (!NT_SUCCESS(Status) || FirstCluster != 4 ||
        AssignedClusters != 1 || Vcb.FreeClusterCount != 0 ||
        !RtlAreBitsSet(&Vcb.ClusterBitmap, 4, 1) ||
        Storage->RequestCount != RequestsBefore + 1)
    {
        Status = NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
        goto CleanupMcb;
    }

    /* Reset the fixture to model a remount before injecting a short write. */
    Storage->FaultMode = NTFS_TEST_FAULT_NONE;
    Storage->FaultRequest = -1;
    Vcb.Flags &= ~VCB_BITMAP_WRITE_FAILED;
    RtlSetAllBits(&Vcb.ClusterBitmap);
    RtlClearBits(&Vcb.ClusterBitmap, 8, 3);
    Vcb.FreeClusterCount = 3;

    RequestsBefore = Storage->RequestCount;
    Storage->FaultRequest = RequestsBefore;
    Storage->FaultMode = NTFS_TEST_FAULT_SHORT;
    Status = NtfsAllocateClusters(&Vcb, 8, 3,
                                  &FirstCluster, &AssignedClusters);
    if (Status != STATUS_UNEXPECTED_IO_ERROR ||
        AssignedClusters != 0 ||
        Vcb.FreeClusterCount != 3 ||
        RtlAreBitsSet(&Vcb.ClusterBitmap, 8, 3) ||
        !(Vcb.Flags & VCB_BITMAP_WRITE_FAILED) ||
        NtfsGetFreeClusters(&Vcb) != 0)
    {
        Status = STATUS_DATA_ERROR;
        goto CleanupMcb;
    }

    /* A successful short write is equally uncertain and must block retries. */
    RequestsBefore = Storage->RequestCount;
    Status = NtfsAllocateClusters(&Vcb, 8, 1,
                                  &FirstCluster, &AssignedClusters);
    if (Status != STATUS_DEVICE_DATA_ERROR ||
        AssignedClusters != 0 ||
        FirstCluster != 0 ||
        Vcb.FreeClusterCount != 3 ||
        RtlAreBitsSet(&Vcb.ClusterBitmap, 8, 3) ||
        Storage->RequestCount != RequestsBefore)
    {
        Status = STATUS_DATA_ERROR;
        goto CleanupMcb;
    }

    /*
     * Invalid direct bitmap writes must be rejected before reaching the
     * lower device. Exercise zero-length and out-of-range requests while
     * retaining a valid attribute context and bitmap descriptor.
     */
    RequestsBefore = Storage->RequestCount;
    Status = NtfsWriteVolumeBitmap(&Vcb, 4, 0);
    if (Status != STATUS_INVALID_PARAMETER ||
        Storage->RequestCount != RequestsBefore)
    {
        Status = STATUS_DATA_ERROR;
        goto CleanupMcb;
    }
    Status = NtfsWriteVolumeBitmap(&Vcb, 8191, 2);
    if (Status != STATUS_INVALID_PARAMETER ||
        Storage->RequestCount != RequestsBefore)
    {
        Status = STATUS_DATA_ERROR;
        goto CleanupMcb;
    }

    /*
     * Allocate the two clusters straddling a bitmap-sector boundary. The
     * persistence writer must issue one aligned two-sector update.
     */
    Storage->FaultMode = NTFS_TEST_FAULT_NONE;
    Storage->FaultRequest = -1;
    Vcb.Flags &= ~VCB_BITMAP_WRITE_FAILED;
    RtlSetAllBits(&Vcb.ClusterBitmap);
    RtlClearBits(&Vcb.ClusterBitmap, 4095, 2);
    Vcb.FreeClusterCount = 2;
    RequestsBefore = Storage->RequestCount;
    Status = NtfsAllocateClusters(&Vcb, 4095, 2,
                                  &FirstCluster, &AssignedClusters);
    if (!NT_SUCCESS(Status) || FirstCluster != 4095 ||
        AssignedClusters != 2 || Vcb.FreeClusterCount != 0 ||
        Storage->RequestCount != RequestsBefore + 1 ||
        Storage->RequestOffset[RequestsBefore] != 1024 ||
        Storage->RequestLength[RequestsBefore] != 1024 ||
        !RtlAreBitsSet(&Vcb.ClusterBitmap, 4095, 2) ||
        RtlCompareMemory(Storage->Image + 1024, Vcb.BitmapBuffer, 1024) != 1024)
    {
        Status = NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
        goto CleanupMcb;
    }

    /*
     * Exercise the first and last valid bitmap bits. They reside in
     * different $Bitmap sectors and must persist to the corresponding LBOs.
     */
    RtlSetAllBits(&Vcb.ClusterBitmap);
    RtlClearBits(&Vcb.ClusterBitmap, 0, 1);
    RtlClearBits(&Vcb.ClusterBitmap, 8191, 1);
    Vcb.FreeClusterCount = 2;

    RequestsBefore = Storage->RequestCount;
    Status = NtfsAllocateClusters(&Vcb, 0, 1,
                                  &FirstCluster, &AssignedClusters);
    if (!NT_SUCCESS(Status) || FirstCluster != 0 || AssignedClusters != 1 ||
        Vcb.FreeClusterCount != 1 ||
        Storage->RequestCount != RequestsBefore + 1 ||
        Storage->RequestOffset[RequestsBefore] != 1024 ||
        Storage->RequestLength[RequestsBefore] != 512 ||
        !RtlAreBitsSet(&Vcb.ClusterBitmap, 0, 1))
    {
        Status = NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
        goto CleanupMcb;
    }

    RequestsBefore = Storage->RequestCount;
    Status = NtfsAllocateClusters(&Vcb, 8191, 1,
                                  &FirstCluster, &AssignedClusters);
    if (!NT_SUCCESS(Status) || FirstCluster != 8191 || AssignedClusters != 1 ||
        Vcb.FreeClusterCount != 0 ||
        Storage->RequestCount != RequestsBefore + 1 ||
        Storage->RequestOffset[RequestsBefore] != 1536 ||
        Storage->RequestLength[RequestsBefore] != 512 ||
        !RtlAreBitsSet(&Vcb.ClusterBitmap, 8191, 1))
    {
        Status = NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
        goto CleanupMcb;
    }

    /*
     * Freeing a partially initialized bitmap must detach every cached
     * descriptor, reset the cached count, and preserve the sticky write
     * failure. A second free must be harmless (including after a failed
     * reload has already discarded the old state).
     *
     * This fixture owns BitmapContext on its stack, so detach it explicitly
     * rather than passing stack storage to the production context destructor.
     */
    Vcb.BitmapContext = NULL;
    Vcb.Flags |= VCB_BITMAP_WRITE_FAILED;
    NtfsFreeVolumeBitmap(&Vcb);
    if (Vcb.BitmapBuffer != NULL ||
        Vcb.BitmapSize != 0 ||
        Vcb.FreeClusterCount != 0 ||
        Vcb.ClusterBitmap.Buffer != NULL ||
        Vcb.ClusterBitmap.SizeOfBitMap != 0 ||
        !(Vcb.Flags & VCB_BITMAP_WRITE_FAILED))
    {
        Status = STATUS_DATA_ERROR;
        goto CleanupMcb;
    }

    NtfsFreeVolumeBitmap(&Vcb);
    if (Vcb.BitmapBuffer != NULL ||
        Vcb.BitmapSize != 0 ||
        Vcb.FreeClusterCount != 0 ||
        Vcb.ClusterBitmap.Buffer != NULL ||
        Vcb.ClusterBitmap.SizeOfBitMap != 0 ||
        !(Vcb.Flags & VCB_BITMAP_WRITE_FAILED))
    {
        Status = STATUS_DATA_ERROR;
        goto CleanupMcb;
    }

    /* A clean teardown must not manufacture a sticky write failure. */
    Vcb.Flags &= ~VCB_BITMAP_WRITE_FAILED;
    Vcb.BitmapBuffer = ExAllocatePoolWithTag(PagedPool, 512, TAG_NTFS);
    if (Vcb.BitmapBuffer == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto CleanupMcb;
    }
    Vcb.BitmapSize = 512;
    RtlInitializeBitMap(&Vcb.ClusterBitmap, Vcb.BitmapBuffer, 4096);
    Vcb.FreeClusterCount = 4096;
    NtfsFreeVolumeBitmap(&Vcb);
    if (Vcb.BitmapBuffer != NULL ||
        Vcb.BitmapSize != 0 ||
        Vcb.FreeClusterCount != 0 ||
        Vcb.ClusterBitmap.Buffer != NULL ||
        (Vcb.Flags & VCB_BITMAP_WRITE_FAILED))
    {
        Status = STATUS_DATA_ERROR;
        goto CleanupMcb;
    }

    Status = STATUS_SUCCESS;
    DPRINT1("NTFS_TEST_BUILD: $Bitmap allocation and teardown tests passed\\n");


CleanupMcb:
    Storage->FaultMode = NTFS_TEST_FAULT_NONE;
    Storage->FaultRequest = -1;
    FsRtlUninitializeLargeMcb(&BitmapContext.DataRunsMCB);
CleanupResource:
    ExDeleteResourceLite(&Vcb.BitmapResource);
CleanupBitmap:
    if (Vcb.BitmapBuffer != NULL)
        ExFreePoolWithTag(Vcb.BitmapBuffer, TAG_NTFS);
    return Status;
}

/*
 * Run a real UpdateFileRecord() against a fake nonresident $MFT stream.
 * Verify the on-disk USA encoding and that success/error/short-write paths
 * all restore the caller's in-memory record.
 */
static
NTSTATUS
NtfsTestRunMftPersistence(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PNTFS_TEST_STORAGE Storage)
{
    DEVICE_EXTENSION Vcb;
    NTFS_ATTR_CONTEXT MftContext;
    NTFS_ATTR_RECORD MftAttribute;
    UCHAR MftMappingPairs[] = { 0x11, 2, 4, 0 };
    UCHAR FileRecordBuffer[1024];
    UCHAR OriginalRecord[1024];
    UCHAR ExpectedDisk[1024];
    UCHAR ExpectedRestored[1024];
    PFILE_RECORD_HEADER FileRecord;
    PUSHORT Usa;
    PUSHORT FirstTrailer;
    PUSHORT SecondTrailer;
    ULONGLONG NextVbn = 0;
    ULONG RequestsBefore;
    NTSTATUS Status;

    RtlZeroMemory(&Vcb, sizeof(Vcb));
    RtlZeroMemory(&MftContext, sizeof(MftContext));
    RtlZeroMemory(&MftAttribute, sizeof(MftAttribute));
    RtlZeroMemory(FileRecordBuffer, sizeof(FileRecordBuffer));

    Vcb.StorageDevice = DeviceObject;
    Vcb.NtfsInfo.BytesPerSector = 512;
    Vcb.NtfsInfo.BytesPerCluster = 512;
    Vcb.NtfsInfo.SectorsPerCluster = 1;
    Vcb.NtfsInfo.BytesPerFileRecord = sizeof(FileRecordBuffer);
    Vcb.NtfsInfo.ClusterCount = 128;

    MftAttribute.Type = AttributeData;
    MftAttribute.Length = sizeof(MftAttribute);
    MftAttribute.IsNonResident = TRUE;
    MftAttribute.NonResident.LowestVCN = 0;
    MftAttribute.NonResident.HighestVCN = 1;
    MftAttribute.NonResident.MappingPairsOffset = 72;
    MftAttribute.NonResident.AllocatedSize = sizeof(FileRecordBuffer);
    MftAttribute.NonResident.DataSize = sizeof(FileRecordBuffer);
    MftContext.pRecord = &MftAttribute;

    Status = ConvertDataRunsToLargeMCB(MftMappingPairs,
                                       sizeof(MftMappingPairs),
                                       0,
                                       &MftContext.DataRunsMCB,
                                       &NextVbn);
    if (!NT_SUCCESS(Status) || NextVbn != 2)
    {
        if (NT_SUCCESS(Status))
        {
            FsRtlUninitializeLargeMcb(&MftContext.DataRunsMCB);
            Status = STATUS_DATA_ERROR;
        }
        return Status;
    }

    Vcb.MFTContext = &MftContext;
    FileRecord = (PFILE_RECORD_HEADER)FileRecordBuffer;
    FileRecord->Ntfs.Type = NRH_FILE_TYPE;
    FileRecord->Ntfs.UsaOffset = 48;
    FileRecord->Ntfs.UsaCount = 3;
    FileRecord->BytesAllocated = sizeof(FileRecordBuffer);
    FileRecord->BytesInUse = sizeof(FILE_RECORD_HEADER);
    Usa = (PUSHORT)(FileRecordBuffer + FileRecord->Ntfs.UsaOffset);
    Usa[0] = 0x1234;
    FirstTrailer = (PUSHORT)(FileRecordBuffer + 510);
    SecondTrailer = (PUSHORT)(FileRecordBuffer + 1022);
    *FirstTrailer = 0x1111;
    *SecondTrailer = 0x2222;
    RtlCopyMemory(OriginalRecord, FileRecordBuffer, sizeof(OriginalRecord));

    /* A successful MFT write must persist the incremented USA and trailers. */
    Status = UpdateFileRecord(&Vcb, 0, FileRecord);
    if (!NT_SUCCESS(Status))
        goto CleanupMcb;

    RtlCopyMemory(ExpectedDisk, OriginalRecord, sizeof(ExpectedDisk));
    Usa = (PUSHORT)(ExpectedDisk + 48);
    Usa[0] = 0x1235;
    Usa[1] = 0x1111;
    Usa[2] = 0x2222;
    *(PUSHORT)(ExpectedDisk + 510) = 0x1235;
    *(PUSHORT)(ExpectedDisk + 1022) = 0x1235;

    RtlCopyMemory(ExpectedRestored, OriginalRecord, sizeof(ExpectedRestored));
    *(PUSHORT)(ExpectedRestored + 48) = 0x1235;
    *(PUSHORT)(ExpectedRestored + 50) = 0x1111;
    *(PUSHORT)(ExpectedRestored + 52) = 0x2222;
    if (RtlCompareMemory(FileRecordBuffer, ExpectedRestored,
                         sizeof(ExpectedRestored)) != sizeof(ExpectedRestored) ||
        RtlCompareMemory(Storage->Image + 2048, ExpectedDisk,
                         sizeof(ExpectedDisk)) != sizeof(ExpectedDisk))
    {
        Status = STATUS_DATA_ERROR;
        goto CleanupMcb;
    }

    /* AddFixupArray advances the in-memory USN; the trailers are restored. */
    RtlCopyMemory(OriginalRecord, FileRecordBuffer, sizeof(OriginalRecord));
    RtlCopyMemory(ExpectedRestored, OriginalRecord, sizeof(ExpectedRestored));
    *(PUSHORT)(ExpectedRestored + 48) = 0x1236;
    RequestsBefore = Storage->RequestCount;
    Storage->FaultRequest = RequestsBefore;
    Storage->FaultMode = NTFS_TEST_FAULT_ERROR;
    Status = UpdateFileRecord(&Vcb, 0, FileRecord);
    Storage->FaultMode = NTFS_TEST_FAULT_NONE;
    Storage->FaultRequest = -1;
    if (Status != STATUS_IO_DEVICE_ERROR ||
        RtlCompareMemory(FileRecordBuffer, ExpectedRestored,
                         sizeof(ExpectedRestored)) != sizeof(ExpectedRestored))
    {
        Status = STATUS_DATA_ERROR;
        goto CleanupMcb;
    }

    /* A successful short completion is not a committed MFT record. */
    RtlCopyMemory(OriginalRecord, FileRecordBuffer, sizeof(OriginalRecord));
    RtlCopyMemory(ExpectedRestored, OriginalRecord, sizeof(ExpectedRestored));
    *(PUSHORT)(ExpectedRestored + 48) = 0x1237;
    RequestsBefore = Storage->RequestCount;
    Storage->FaultRequest = RequestsBefore;
    Storage->FaultMode = NTFS_TEST_FAULT_SHORT;
    Status = UpdateFileRecord(&Vcb, 0, FileRecord);
    Storage->FaultMode = NTFS_TEST_FAULT_NONE;
    Storage->FaultRequest = -1;
    if (Status != STATUS_UNEXPECTED_IO_ERROR ||
        RtlCompareMemory(FileRecordBuffer, ExpectedRestored,
                         sizeof(ExpectedRestored)) != sizeof(ExpectedRestored))
    {
        Status = STATUS_DATA_ERROR;
        goto CleanupMcb;
    }

    Status = STATUS_SUCCESS;
    DPRINT1("NTFS_TEST_BUILD: MFT persistence/fixup fault tests passed\n");

CleanupMcb:
    Storage->FaultMode = NTFS_TEST_FAULT_NONE;
    Storage->FaultRequest = -1;
    FsRtlUninitializeLargeMcb(&MftContext.DataRunsMCB);
    return Status;
}

/*
 * Free the physical tail of a physical/sparse/physical stream. Two logical
 * VCNs are removed, but only the physical tail owns a bitmap bit and should
 * increase FreeClusterCount.
 */
static
NTSTATUS
NtfsTestRunFreeSparseTail(
    _In_ PDEVICE_EXTENSION Vcb,
    _In_ PNTFS_TEST_STORAGE Storage,
    _In_ PFILE_RECORD_HEADER SourceRecord,
    _In_reads_bytes_(9) PUCHAR SparseMappingPairs)
{
    NTFS_ATTR_CONTEXT AttrContext;
    UCHAR RecordBuffer[1024];
    UCHAR SavedMftRecord[1024];
    UCHAR SavedBitmapSector[512];
    UCHAR SavedBitmapBuffer[512];
    PFILE_RECORD_HEADER FileRecord;
    PNTFS_ATTR_RECORD Attribute;
    PFILE_RECORD_HEADER DiskRecord;
    PNTFS_ATTR_RECORD DiskAttribute;
    ULONGLONG NextVbn = 0;
    ULONG SavedFreeClusterCount;
    ULONG SavedFlags;
    ULONG RequestsBefore;
    LONGLONG Lcn;
    NTSTATUS Status;
    BOOLEAN McbInitialized = FALSE;

    RtlZeroMemory(&AttrContext, sizeof(AttrContext));
    RtlCopyMemory(RecordBuffer, SourceRecord, sizeof(RecordBuffer));
    FileRecord = (PFILE_RECORD_HEADER)RecordBuffer;
    Attribute = (PNTFS_ATTR_RECORD)(RecordBuffer + FileRecord->AttributeOffset);

    Attribute->Length = 88;
    Attribute->Flags = NTFS_ATTR_SPARSE;
    Attribute->IsNonResident = TRUE;
    Attribute->NonResident.LowestVCN = 0;
    Attribute->NonResident.HighestVCN = 2;
    Attribute->NonResident.MappingPairsOffset = 72;
    Attribute->NonResident.AllocatedSize = 1024;
    Attribute->NonResident.CompressedSize = 1024;
    RtlCopyMemory((PUCHAR)Attribute + 72, SparseMappingPairs, 9);
    *(PULONG)(RecordBuffer + FileRecord->AttributeOffset + 88) = AttributeEnd;
    FileRecord->BytesInUse = FileRecord->AttributeOffset + 88 + sizeof(ULONG);

    AttrContext.pRecord = ExAllocatePoolWithTag(NonPagedPool,
                                                 Attribute->Length,
                                                 TAG_NTFS);
    if (AttrContext.pRecord == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlCopyMemory(AttrContext.pRecord, Attribute, Attribute->Length);
    AttrContext.FileMFTIndex = 0;

    Status = ConvertDataRunsToLargeMCB(SparseMappingPairs,
                                       9,
                                       0,
                                       &AttrContext.DataRunsMCB,
                                       &NextVbn);
    if (NT_SUCCESS(Status))
        McbInitialized = TRUE;
    if (!NT_SUCCESS(Status) || NextVbn != 3)
    {
        if (NT_SUCCESS(Status))
            Status = STATUS_DATA_ERROR;
        goto Cleanup;
    }

    RtlCopyMemory(SavedMftRecord, Storage->Image + 2048,
                  sizeof(SavedMftRecord));
    RtlCopyMemory(SavedBitmapSector, Storage->Image + 1024,
                  sizeof(SavedBitmapSector));
    RtlCopyMemory(SavedBitmapBuffer, Vcb->BitmapBuffer,
                  sizeof(SavedBitmapBuffer));
    SavedFreeClusterCount = Vcb->FreeClusterCount;
    SavedFlags = Vcb->Flags;

    /*
     * A zero-length release is a strict no-op: it must not submit an MFT
     * write, change the mapping, or touch the free-space accounting.
     */
    RequestsBefore = Storage->RequestCount;
    Status = FreeClusters(Vcb, &AttrContext, FileRecord->AttributeOffset,
                          FileRecord, 0);
    if (!NT_SUCCESS(Status) ||
        Storage->RequestCount != RequestsBefore ||
        Vcb->FreeClusterCount != SavedFreeClusterCount ||
        AttrContext.pRecord->NonResident.HighestVCN != 2 ||
        !RtlAreBitsSet(&Vcb->ClusterBitmap, 8, 1) ||
        !RtlAreBitsSet(&Vcb->ClusterBitmap, 10, 1))
    {
        Status = NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
        goto RestoreImage;
    }

    /* Reject a release larger than the complete logical stream extent. */
    RequestsBefore = Storage->RequestCount;
    Status = FreeClusters(Vcb, &AttrContext, FileRecord->AttributeOffset,
                          FileRecord, 4);
    if (Status != STATUS_INVALID_PARAMETER ||
        Storage->RequestCount != RequestsBefore ||
        Vcb->FreeClusterCount != SavedFreeClusterCount ||
        AttrContext.pRecord->NonResident.HighestVCN != 2 ||
        !RtlAreBitsSet(&Vcb->ClusterBitmap, 8, 1) ||
        !RtlAreBitsSet(&Vcb->ClusterBitmap, 10, 1))
    {
        Status = STATUS_DATA_ERROR;
        goto RestoreImage;
    }

    /*
     * Once bitmap persistence is uncertain, even a valid release must be
     * blocked before the shortened MFT record can be written.
     */
    Vcb->Flags |= VCB_BITMAP_WRITE_FAILED;
    RequestsBefore = Storage->RequestCount;
    Status = FreeClusters(Vcb, &AttrContext, FileRecord->AttributeOffset,
                          FileRecord, 1);
    Vcb->Flags = SavedFlags;
    if (Status != STATUS_DEVICE_DATA_ERROR ||
        Storage->RequestCount != RequestsBefore ||
        Vcb->FreeClusterCount != SavedFreeClusterCount ||
        AttrContext.pRecord->NonResident.HighestVCN != 2 ||
        !RtlAreBitsSet(&Vcb->ClusterBitmap, 8, 1) ||
        !RtlAreBitsSet(&Vcb->ClusterBitmap, 10, 1))
    {
        Status = STATUS_DATA_ERROR;
        goto RestoreImage;
    }

    /*
     * A physical run whose bitmap bit is already clear is stale/corrupt.
     * FreeClusters must reject it before writing the MFT or bitmap and must
     * leave the live MCB and attribute extent unchanged.
     */
    RtlClearBits(&Vcb->ClusterBitmap, 10, 1);
    RequestsBefore = Storage->RequestCount;
    Status = FreeClusters(Vcb, &AttrContext, FileRecord->AttributeOffset,
                          FileRecord, 1);
    RtlSetBits(&Vcb->ClusterBitmap, 10, 1);
    if (Status != STATUS_FILE_CORRUPT_ERROR ||
        Storage->RequestCount != RequestsBefore ||
        Vcb->FreeClusterCount != SavedFreeClusterCount ||
        AttrContext.pRecord->NonResident.HighestVCN != 2 ||
        !RtlAreBitsSet(&Vcb->ClusterBitmap, 10, 1) ||
        !FsRtlLookupLargeMcbEntry(&AttrContext.DataRunsMCB, 2,
                                  &Lcn, NULL, NULL, NULL, NULL) ||
        Lcn != 10)
    {
        Status = STATUS_DATA_ERROR;
        goto RestoreImage;
    }

    /*
     * First trim two implicit sparse-tail VCNs. The MCB ends at VCN 2,
     * while the attribute's HighestVCN is 4. VCNs 3 and 4 therefore have
     * no physical MCB entries and must not release any bitmap clusters.
     */
    Attribute->NonResident.HighestVCN = 4;
    AttrContext.pRecord->NonResident.HighestVCN = 4;
    RequestsBefore = Storage->RequestCount;
    Status = FreeClusters(Vcb, &AttrContext, FileRecord->AttributeOffset,
                          FileRecord, 2);
    if (!NT_SUCCESS(Status) ||
        Storage->RequestCount != RequestsBefore + 1 ||
        Storage->RequestOffset[RequestsBefore] != 2048 ||
        Storage->RequestLength[RequestsBefore] != 1024 ||
        Vcb->FreeClusterCount != SavedFreeClusterCount ||
        !RtlAreBitsSet(&Vcb->ClusterBitmap, 8, 1) ||
        !RtlAreBitsSet(&Vcb->ClusterBitmap, 10, 1) ||
        AttrContext.pRecord->NonResident.HighestVCN != 2 ||
        AttrContext.pRecord->NonResident.CompressedSize != 1024 ||
        !FsRtlLookupLargeMcbEntry(&AttrContext.DataRunsMCB, 2,
                                  &Lcn, NULL, NULL, NULL, NULL) ||
        Lcn != 10)
    {
        Status = NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
        goto RestoreImage;
    }

    /*
     * Now free VCNs 2 and 1. VCN 2 maps to LCN 10; VCN 1 is sparse and must
     * not be counted as a released physical cluster.
     */
    RequestsBefore = Storage->RequestCount;
    Status = FreeClusters(Vcb, &AttrContext, FileRecord->AttributeOffset,
                          FileRecord, 2);
    if (!NT_SUCCESS(Status) ||
        Storage->RequestCount != RequestsBefore + 2 ||
        Storage->RequestOffset[RequestsBefore] != 2048 ||
        Storage->RequestLength[RequestsBefore] != 1024 ||
        Storage->RequestOffset[RequestsBefore + 1] != 1024 ||
        Storage->RequestLength[RequestsBefore + 1] != 512 ||
        Vcb->FreeClusterCount != SavedFreeClusterCount + 1 ||
        !RtlAreBitsClear(&Vcb->ClusterBitmap, 10, 1) ||
        !RtlAreBitsSet(&Vcb->ClusterBitmap, 12, 1) ||
        AttrContext.pRecord->NonResident.HighestVCN != 0 ||
        AttrContext.pRecord->NonResident.CompressedSize != 512 ||
        !FsRtlLookupLargeMcbEntry(&AttrContext.DataRunsMCB, 0,
                                  &Lcn, NULL, NULL, NULL, NULL) ||
        Lcn != 8 ||
        FsRtlLookupLargeMcbEntry(&AttrContext.DataRunsMCB, 1,
                                 &Lcn, NULL, NULL, NULL, NULL))
    {
        Status = NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
        goto RestoreImage;
    }

    /*
     * Apply the allocation-size update that the successful resize caller
     * performs after FreeClusters() has persisted the shorter runlist.
     */
    Attribute->NonResident.AllocatedSize = 512;
    AttrContext.pRecord->NonResident.AllocatedSize = 512;
    Status = UpdateFileRecord(Vcb, 0, FileRecord);
    if (!NT_SUCCESS(Status) ||
        Storage->RequestCount != RequestsBefore + 3 ||
        Storage->RequestOffset[RequestsBefore + 2] != 2048 ||
        Storage->RequestLength[RequestsBefore + 2] != 1024)
    {
        Status = NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
        goto RestoreImage;
    }

    DiskRecord = (PFILE_RECORD_HEADER)(Storage->Image + 2048);
    DiskAttribute = (PNTFS_ATTR_RECORD)
        ((PUCHAR)DiskRecord + DiskRecord->AttributeOffset);
    if (DiskAttribute->NonResident.HighestVCN != 0 ||
        DiskAttribute->NonResident.AllocatedSize != 512 ||
        DiskAttribute->NonResident.CompressedSize != 512 ||
        !RtlAreBitsClear(&Vcb->ClusterBitmap, 10, 1))
    {
        Status = STATUS_DATA_ERROR;
        goto RestoreImage;
    }

    Status = STATUS_SUCCESS;

RestoreImage:
    /*
     * This isolated case shares the enclosing fake volume with subsequent
     * scenarios. Restore its MFT and bitmap image after assertions while
     * retaining the captured request log.
     */
    RtlCopyMemory(Storage->Image + 2048, SavedMftRecord,
                  sizeof(SavedMftRecord));
    RtlCopyMemory(Storage->Image + 1024, SavedBitmapSector,
                  sizeof(SavedBitmapSector));
    RtlCopyMemory(Vcb->BitmapBuffer, SavedBitmapBuffer,
                  sizeof(SavedBitmapBuffer));
    Vcb->FreeClusterCount = SavedFreeClusterCount;
    Vcb->Flags = SavedFlags;

Cleanup:
    if (McbInitialized)
        FsRtlUninitializeLargeMcb(&AttrContext.DataRunsMCB);
    ExFreePoolWithTag(AttrContext.pRecord, TAG_NTFS);
    return Status;
}

/*
 * Exercise the sparse-write allocation transaction with a private VCB:
 * $Bitmap reservation -> zeroing -> mapping-pair/MFT persistence -> user
 * data write. All three physical writes pass through the fake lower device.
 */
static
NTSTATUS
NtfsTestRunSparseAllocation(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PNTFS_TEST_STORAGE Storage)
{
    DEVICE_EXTENSION Vcb;
    NTFS_ATTR_CONTEXT BitmapContext;
    NTFS_ATTR_CONTEXT MftContext;
    NTFS_ATTR_CONTEXT DataContext;
    NTFS_ATTR_RECORD BitmapAttribute;
    NTFS_ATTR_RECORD MftAttribute;
    UCHAR BitmapPairs[] = { 0x11, 1, 2, 0 };
    UCHAR MftPairs[] = { 0x11, 2, 4, 0 };
    UCHAR DataPairs[] = { 0x11, 1, 8, 0x01, 1, 0x11, 1, 2, 0 };
    UCHAR FileRecordBuffer[1024];
    UCHAR OriginalFileRecord[1024];
    UCHAR Payload[512];
    PFILE_RECORD_HEADER FileRecord;
    PNTFS_ATTR_RECORD FileDataAttribute;
    PNTFS_ATTR_RECORD DiskDataAttribute;
    PFILE_RECORD_HEADER DiskFileRecord;
    LARGE_MCB DiskMapping;
    ULONGLONG NextVbn;
    ULONG Written;
    ULONG Index;
    ULONG RequestsBefore;
    LONGLONG Lcn;
    NTSTATUS Status;

    RtlZeroMemory(&Vcb, sizeof(Vcb));
    RtlZeroMemory(&BitmapContext, sizeof(BitmapContext));
    RtlZeroMemory(&MftContext, sizeof(MftContext));
    RtlZeroMemory(&DataContext, sizeof(DataContext));
    RtlZeroMemory(&BitmapAttribute, sizeof(BitmapAttribute));
    RtlZeroMemory(&MftAttribute, sizeof(MftAttribute));
    RtlZeroMemory(FileRecordBuffer, sizeof(FileRecordBuffer));

    Vcb.StorageDevice = DeviceObject;
    Vcb.NtfsInfo.BytesPerSector = 512;
    Vcb.NtfsInfo.BytesPerCluster = 512;
    Vcb.NtfsInfo.SectorsPerCluster = 1;
    Vcb.NtfsInfo.BytesPerFileRecord = sizeof(FileRecordBuffer);
    Vcb.NtfsInfo.ClusterCount = 128;
    Vcb.BitmapSize = 16;
    Vcb.BitmapBuffer = ExAllocatePoolWithTag(NonPagedPool, 512, TAG_NTFS);
    if (Vcb.BitmapBuffer == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Vcb.BitmapBuffer, 512);
    RtlInitializeBitMap(&Vcb.ClusterBitmap, Vcb.BitmapBuffer, 128);
    RtlSetBits(&Vcb.ClusterBitmap, 0, 9);
    RtlSetBits(&Vcb.ClusterBitmap, 10, 1);
    Vcb.FreeClusterCount = 118;

    Status = ExInitializeResourceLite(&Vcb.BitmapResource);
    if (!NT_SUCCESS(Status))
        goto CleanupBitmap;

    BitmapAttribute.Type = AttributeData;
    BitmapAttribute.Length = sizeof(BitmapAttribute);
    BitmapAttribute.IsNonResident = TRUE;
    BitmapAttribute.NonResident.LowestVCN = 0;
    BitmapAttribute.NonResident.HighestVCN = 0;
    BitmapAttribute.NonResident.MappingPairsOffset = 72;
    BitmapAttribute.NonResident.AllocatedSize = 512;
    BitmapAttribute.NonResident.DataSize = 16;
    BitmapContext.pRecord = &BitmapAttribute;

    NextVbn = 0;
    Status = ConvertDataRunsToLargeMCB(BitmapPairs, sizeof(BitmapPairs), 0,
                                       &BitmapContext.DataRunsMCB, &NextVbn);
    if (!NT_SUCCESS(Status) || NextVbn != 1)
    {
        if (NT_SUCCESS(Status))
        {
            FsRtlUninitializeLargeMcb(&BitmapContext.DataRunsMCB);
            Status = STATUS_DATA_ERROR;
        }
        goto CleanupResource;
    }
    Vcb.BitmapContext = &BitmapContext;

    MftAttribute.Type = AttributeData;
    MftAttribute.Length = sizeof(MftAttribute);
    MftAttribute.IsNonResident = TRUE;
    MftAttribute.NonResident.LowestVCN = 0;
    MftAttribute.NonResident.HighestVCN = 1;
    MftAttribute.NonResident.MappingPairsOffset = 72;
    MftAttribute.NonResident.AllocatedSize = sizeof(FileRecordBuffer);
    MftAttribute.NonResident.DataSize = sizeof(FileRecordBuffer);
    MftContext.pRecord = &MftAttribute;

    NextVbn = 0;
    Status = ConvertDataRunsToLargeMCB(MftPairs, sizeof(MftPairs), 0,
                                       &MftContext.DataRunsMCB, &NextVbn);
    if (!NT_SUCCESS(Status) || NextVbn != 2)
    {
        if (NT_SUCCESS(Status))
        {
            FsRtlUninitializeLargeMcb(&MftContext.DataRunsMCB);
            Status = STATUS_DATA_ERROR;
        }
        goto CleanupBitmapMcb;
    }
    Vcb.MFTContext = &MftContext;

    FileRecord = (PFILE_RECORD_HEADER)FileRecordBuffer;
    FileRecord->Ntfs.Type = NRH_FILE_TYPE;
    FileRecord->Ntfs.UsaOffset = 48;
    FileRecord->Ntfs.UsaCount = 3;
    FileRecord->SequenceNumber = 1;
    FileRecord->Flags = FRH_IN_USE;
    FileRecord->BytesAllocated = sizeof(FileRecordBuffer);
    FileRecord->AttributeOffset = 64;
    FileRecord->BytesInUse = 64 + 88 + sizeof(ULONG);
    FileRecord->MFTRecordNumber = 0;
    *(PUSHORT)(FileRecordBuffer + 48) = 0x2345;
    *(PUSHORT)(FileRecordBuffer + 510) = 0x1111;
    *(PUSHORT)(FileRecordBuffer + 1022) = 0x2222;

    FileDataAttribute = (PNTFS_ATTR_RECORD)(FileRecordBuffer + 64);
    FileDataAttribute->Type = AttributeData;
    FileDataAttribute->Length = 88;
    FileDataAttribute->NonResident.LowestVCN = 0;
    FileDataAttribute->NonResident.HighestVCN = 2;
    FileDataAttribute->Flags = NTFS_ATTR_SPARSE;
    FileDataAttribute->IsNonResident = TRUE;
    FileDataAttribute->Instance = 7;
    FileDataAttribute->NonResident.MappingPairsOffset = 72;
    FileDataAttribute->NonResident.AllocatedSize = 1024;
    FileDataAttribute->NonResident.DataSize = 1536;
    FileDataAttribute->NonResident.InitializedSize = 1536;
    FileDataAttribute->NonResident.CompressedSize = 1024;
    RtlCopyMemory((PUCHAR)FileDataAttribute + 72,
                  DataPairs, sizeof(DataPairs));
    *(PULONG)(FileRecordBuffer + 64 + 88) = AttributeEnd;

    DataContext.pRecord = ExAllocatePoolWithTag(NonPagedPool,
                                                FileDataAttribute->Length,
                                                TAG_NTFS);
    if (DataContext.pRecord == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto CleanupMftMcb;
    }
    RtlCopyMemory(DataContext.pRecord, FileDataAttribute,
                  FileDataAttribute->Length);
    DataContext.FileMFTIndex = 0;

    NextVbn = 0;
    Status = ConvertDataRunsToLargeMCB(DataPairs, sizeof(DataPairs), 0,
                                       &DataContext.DataRunsMCB, &NextVbn);
    if (!NT_SUCCESS(Status) || NextVbn != 3)
    {
        if (NT_SUCCESS(Status))
        {
            FsRtlUninitializeLargeMcb(&DataContext.DataRunsMCB);
            Status = STATUS_DATA_ERROR;
        }
        goto CleanupDataRecord;
    }

    /*
     * Paging I/O is not allowed to allocate. It must fail before touching
     * the bitmap or submitting any lower-device request when a hole remains.
     */
    RequestsBefore = Storage->RequestCount;
    Status = NtfsAllocateSparseWriteRange(&Vcb, &DataContext,
                                          512, 512, FileRecord, FALSE);
    if (Status != STATUS_NOT_IMPLEMENTED ||
        Storage->RequestCount != RequestsBefore ||
        Vcb.FreeClusterCount != 118 ||
        RtlAreBitsSet(&Vcb.ClusterBitmap, 9, 1))
    {
        Status = STATUS_DATA_ERROR;
        goto CleanupDataMcb;
    }

    /*
     * Fail the zeroing write after the bitmap reservation succeeds. The
     * reservation must remain allocated (a safe leak), while the sparse MFT
     * mapping must remain unchanged and no MFT write may be issued.
     */
    RtlFillMemory(Storage->Image + 9 * 512, 512, 0xCC);
    RequestsBefore = Storage->RequestCount;
    Storage->FaultRequest = RequestsBefore + 1;
    Storage->FaultMode = NTFS_TEST_FAULT_ERROR;
    Status = NtfsAllocateSparseWriteRange(&Vcb, &DataContext,
                                          512, 512, FileRecord, TRUE);
    Storage->FaultMode = NTFS_TEST_FAULT_NONE;
    Storage->FaultRequest = -1;
    if (Status != STATUS_IO_DEVICE_ERROR ||
        Storage->RequestCount != RequestsBefore + 2 ||
        Storage->RequestOffset[RequestsBefore] != 1024 ||
        Storage->RequestOffset[RequestsBefore + 1] != 9 * 512 ||
        Vcb.FreeClusterCount != 117 ||
        !RtlAreBitsSet(&Vcb.ClusterBitmap, 9, 1) ||
        DataContext.pRecord->NonResident.AllocatedSize != 1024 ||
        DataContext.pRecord->NonResident.CompressedSize != 1024 ||
        RtlCompareMemory((PUCHAR)DataContext.pRecord + 72,
                         DataPairs, sizeof(DataPairs)) != sizeof(DataPairs) ||
        Storage->Image[9 * 512] != 0xCC)
    {
        Status = STATUS_DATA_ERROR;
        goto CleanupDataMcb;
    }

    /*
     * Next inject an MFT commit error. Cluster 11 is reserved and zeroed,
     * but its mapping must be rolled back in memory while the cluster stays
     * allocated because the failed metadata write may be ambiguous.
     */
    RtlFillMemory(Storage->Image + 11 * 512, 512, 0xCC);
    RtlCopyMemory(OriginalFileRecord, FileRecordBuffer,
                  sizeof(OriginalFileRecord));
    RequestsBefore = Storage->RequestCount;
    Storage->FaultRequest = RequestsBefore + 2;
    Storage->FaultMode = NTFS_TEST_FAULT_ERROR;
    Status = NtfsAllocateSparseWriteRange(&Vcb, &DataContext,
                                          512, 512, FileRecord, TRUE);
    Storage->FaultMode = NTFS_TEST_FAULT_NONE;
    Storage->FaultRequest = -1;
    if (Status != STATUS_IO_DEVICE_ERROR ||
        Storage->RequestCount != RequestsBefore + 3 ||
        Storage->RequestOffset[RequestsBefore] != 1024 ||
        Storage->RequestOffset[RequestsBefore + 1] != 11 * 512 ||
        Storage->RequestOffset[RequestsBefore + 2] != 2048 ||
        Vcb.FreeClusterCount != 116 ||
        !RtlAreBitsSet(&Vcb.ClusterBitmap, 11, 1) ||
        DataContext.pRecord->NonResident.AllocatedSize != 1024 ||
        DataContext.pRecord->NonResident.CompressedSize != 1024 ||
        RtlCompareMemory((PUCHAR)DataContext.pRecord + 72,
                         DataPairs, sizeof(DataPairs)) != sizeof(DataPairs) ||
        FileDataAttribute->NonResident.AllocatedSize != 1024 ||
        FileDataAttribute->NonResident.CompressedSize != 1024 ||
        RtlCompareMemory(FileRecordBuffer, OriginalFileRecord,
                         sizeof(OriginalFileRecord)) != sizeof(OriginalFileRecord) ||
        (FsRtlLookupLargeMcbEntry(&DataContext.DataRunsMCB, 1,
                                   &Lcn, NULL, NULL, NULL, NULL) &&
         Lcn != -1))
    {
        Status = STATUS_DATA_ERROR;
        goto CleanupDataMcb;
    }

    for (Index = 0; Index < 512; ++Index)
    {
        if (Storage->Image[11 * 512 + Index] != 0)
        {
            Status = STATUS_DATA_ERROR;
            goto CleanupDataMcb;
        }
    }

    /*
     * Retry again. Clusters 9 and 11 are deliberately leaked, so the next
     * committed extent must use cluster 12.
     */
    RtlFillMemory(Storage->Image + 12 * 512, 512, 0xCC);
    RequestsBefore = Storage->RequestCount;
    Status = NtfsAllocateSparseWriteRange(&Vcb, &DataContext,
                                          512, 512, FileRecord, TRUE);
    if (!NT_SUCCESS(Status))
        goto CleanupDataMcb;

    if (Storage->RequestCount != RequestsBefore + 3 ||
        Storage->RequestOffset[RequestsBefore] != 1024 ||
        Storage->RequestLength[RequestsBefore] != 512 ||
        Storage->RequestOffset[RequestsBefore + 1] != 12 * 512 ||
        Storage->RequestLength[RequestsBefore + 1] != 512 ||
        Storage->RequestOffset[RequestsBefore + 2] != 2048 ||
        Storage->RequestLength[RequestsBefore + 2] != 1024 ||
        Storage->RequestMajor[RequestsBefore] != IRP_MJ_WRITE ||
        Storage->RequestMajor[RequestsBefore + 1] != IRP_MJ_WRITE ||
        Storage->RequestMajor[RequestsBefore + 2] != IRP_MJ_WRITE ||
        DataContext.pRecord->NonResident.AllocatedSize != 1536 ||
        DataContext.pRecord->NonResident.CompressedSize != 1536 ||
        DataContext.pRecord->NonResident.HighestVCN != 2 ||
        Vcb.FreeClusterCount != 115 ||
        !RtlAreBitsSet(&Vcb.ClusterBitmap, 12, 1) ||
        Storage->Image[1024] != 0xFF ||
        Storage->Image[1025] != 0x1F ||
        !FsRtlLookupLargeMcbEntry(&DataContext.DataRunsMCB, 1,
                                  &Lcn, NULL, NULL, NULL, NULL) ||
        Lcn != 12)
    {
        Status = STATUS_DATA_ERROR;
        goto CleanupDataMcb;
    }

    for (Index = 0; Index < 512; ++Index)
    {
        if (Storage->Image[12 * 512 + Index] != 0)
        {
            Status = STATUS_DATA_ERROR;
            goto CleanupDataMcb;
        }
    }

    /* The persisted MFT record must contain the new mapping and sizes. */
    DiskFileRecord = (PFILE_RECORD_HEADER)(Storage->Image + 2048);
    DiskDataAttribute = (PNTFS_ATTR_RECORD)
        ((PUCHAR)DiskFileRecord + DiskFileRecord->AttributeOffset);
    if (DiskDataAttribute->Type != AttributeData ||
        DiskDataAttribute->NonResident.AllocatedSize != 1536 ||
        DiskDataAttribute->NonResident.CompressedSize != 1536 ||
        DiskDataAttribute->NonResident.HighestVCN != 2)
    {
        Status = STATUS_DATA_ERROR;
        goto CleanupDataMcb;
    }

    NextVbn = 0;
    Status = ConvertDataRunsToLargeMCB(
        (PUCHAR)DiskDataAttribute + DiskDataAttribute->NonResident.MappingPairsOffset,
        DiskDataAttribute->Length - DiskDataAttribute->NonResident.MappingPairsOffset,
        0, &DiskMapping, &NextVbn);
    if (!NT_SUCCESS(Status) || NextVbn != 3)
    {
        if (NT_SUCCESS(Status))
        {
            FsRtlUninitializeLargeMcb(&DiskMapping);
            Status = STATUS_DATA_ERROR;
        }
        goto CleanupDataMcb;
    }

    if (!FsRtlLookupLargeMcbEntry(&DiskMapping, 0, &Lcn, NULL, NULL, NULL, NULL) ||
        Lcn != 8 ||
        !FsRtlLookupLargeMcbEntry(&DiskMapping, 1, &Lcn, NULL, NULL, NULL, NULL) ||
        Lcn != 12 ||
        !FsRtlLookupLargeMcbEntry(&DiskMapping, 2, &Lcn, NULL, NULL, NULL, NULL) ||
        Lcn != 10)
    {
        FsRtlUninitializeLargeMcb(&DiskMapping);
        Status = STATUS_DATA_ERROR;
        goto CleanupDataMcb;
    }
    FsRtlUninitializeLargeMcb(&DiskMapping);

    /* Now write user data through the updated production attribute mapper. */
    for (Index = 0; Index < sizeof(Payload); ++Index)
        Payload[Index] = (UCHAR)(Index ^ 0x6D);
    Written = 0;
    Status = WriteAttribute(&Vcb, &DataContext, 512, Payload,
                            sizeof(Payload), &Written, FileRecord);
    if (!NT_SUCCESS(Status) || Written != sizeof(Payload) ||
        Storage->RequestCount != RequestsBefore + 4 ||
        Storage->RequestOffset[RequestsBefore + 3] != 12 * 512 ||
        Storage->RequestLength[RequestsBefore + 3] != sizeof(Payload) ||
        RtlCompareMemory(Storage->Image + 12 * 512, Payload,
                         sizeof(Payload)) != sizeof(Payload))
    {
        Status = NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
        goto CleanupDataMcb;
    }

    Status = NtfsTestRunFreeSparseTail(&Vcb, Storage, FileRecord,
                                            DataPairs);
    if (!NT_SUCCESS(Status))
        goto CleanupDataMcb;

    Status = NtfsTestRunFreeSparseTail(&Vcb, Storage, FileRecord,
                                            DataPairs);
    if (!NT_SUCCESS(Status))
        goto CleanupDataMcb;

    /*
     * FreeClusters must reject an oversized tail release before writing
     * either the MFT record or the volume bitmap.
     */
    RequestsBefore = Storage->RequestCount;
    Status = FreeClusters(&Vcb, &DataContext, 64, FileRecord, 4);
    if (Status != STATUS_INVALID_PARAMETER ||
        Storage->RequestCount != RequestsBefore ||
        Vcb.FreeClusterCount != 115 ||
        !RtlAreBitsSet(&Vcb.ClusterBitmap, 10, 1) ||
        !RtlAreBitsSet(&Vcb.ClusterBitmap, 12, 1) ||
        DataContext.pRecord->NonResident.HighestVCN != 2)
    {
        Status = STATUS_DATA_ERROR;
        goto CleanupDataMcb;
    }

    /*
     * A stale runlist that points at an already-free bitmap bit must be
     * rejected before any MFT/bitmap I/O or free-count adjustment.
     */
    RtlClearBits(&Vcb.ClusterBitmap, 10, 1);
    RequestsBefore = Storage->RequestCount;
    Status = FreeClusters(&Vcb, &DataContext, 64, FileRecord, 1);
    RtlSetBits(&Vcb.ClusterBitmap, 10, 1);
    if (Status != STATUS_FILE_CORRUPT_ERROR ||
        Storage->RequestCount != RequestsBefore ||
        Vcb.FreeClusterCount != 115 ||
        DataContext.pRecord->NonResident.HighestVCN != 2 ||
        !FsRtlLookupLargeMcbEntry(&DataContext.DataRunsMCB, 2,
                                  &Lcn, NULL, NULL, NULL, NULL) ||
        Lcn != 10)
    {
        Status = STATUS_DATA_ERROR;
        goto CleanupDataMcb;
    }

    /*
     * An MFT commit error must restore the complete caller record and leave
     * the live MCB and bitmap untouched.
     */
    RtlCopyMemory(OriginalFileRecord, FileRecordBuffer,
                  sizeof(OriginalFileRecord));
    RequestsBefore = Storage->RequestCount;
    Storage->FaultRequest = RequestsBefore;
    Storage->FaultMode = NTFS_TEST_FAULT_ERROR;
    Status = FreeClusters(&Vcb, &DataContext, 64, FileRecord, 1);
    Storage->FaultMode = NTFS_TEST_FAULT_NONE;
    Storage->FaultRequest = -1;
    if (Status != STATUS_IO_DEVICE_ERROR ||
        Storage->RequestCount != RequestsBefore + 1 ||
        Vcb.FreeClusterCount != 115 ||
        !RtlAreBitsSet(&Vcb.ClusterBitmap, 10, 1) ||
        !RtlAreBitsSet(&Vcb.ClusterBitmap, 12, 1) ||
        DataContext.pRecord->NonResident.HighestVCN != 2 ||
        RtlCompareMemory(FileRecordBuffer, OriginalFileRecord,
                         sizeof(OriginalFileRecord)) != sizeof(OriginalFileRecord) ||
        !FsRtlLookupLargeMcbEntry(&DataContext.DataRunsMCB, 2,
                                  &Lcn, NULL, NULL, NULL, NULL) ||
        Lcn != 10)
    {
        Status = STATUS_DATA_ERROR;
        goto CleanupDataMcb;
    }

    /*
     * A successful short MFT completion is also a failed commit. It must
     * restore the complete caller record and leave bitmap/MCB state intact.
     */
    RtlCopyMemory(OriginalFileRecord, FileRecordBuffer,
                  sizeof(OriginalFileRecord));
    RequestsBefore = Storage->RequestCount;
    Storage->FaultRequest = RequestsBefore;
    Storage->FaultMode = NTFS_TEST_FAULT_SHORT;
    Status = FreeClusters(&Vcb, &DataContext, 64, FileRecord, 1);
    Storage->FaultMode = NTFS_TEST_FAULT_NONE;
    Storage->FaultRequest = -1;
    if (Status != STATUS_UNEXPECTED_IO_ERROR ||
        Storage->RequestCount != RequestsBefore + 1 ||
        Vcb.FreeClusterCount != 115 ||
        !RtlAreBitsSet(&Vcb.ClusterBitmap, 10, 1) ||
        !RtlAreBitsSet(&Vcb.ClusterBitmap, 12, 1) ||
        DataContext.pRecord->NonResident.HighestVCN != 2 ||
        RtlCompareMemory(FileRecordBuffer, OriginalFileRecord,
                         sizeof(OriginalFileRecord)) != sizeof(OriginalFileRecord) ||
        !FsRtlLookupLargeMcbEntry(&DataContext.DataRunsMCB, 2,
                                  &Lcn, NULL, NULL, NULL, NULL) ||
        Lcn != 10)
    {
        Status = STATUS_DATA_ERROR;
        goto CleanupDataMcb;
    }

    /*
     * A successful release commits the shortened MFT record before clearing
     * the bitmap bit. The sparse stream's physical allocation drops by one.
     */
    RequestsBefore = Storage->RequestCount;
    Status = FreeClusters(&Vcb, &DataContext, 64, FileRecord, 1);
    if (!NT_SUCCESS(Status) ||
        Storage->RequestCount != RequestsBefore + 2 ||
        Storage->RequestOffset[RequestsBefore] != 2048 ||
        Storage->RequestLength[RequestsBefore] != 1024 ||
        Storage->RequestOffset[RequestsBefore + 1] != 1024 ||
        Storage->RequestLength[RequestsBefore + 1] != 512 ||
        Storage->RequestMajor[RequestsBefore] != IRP_MJ_WRITE ||
        Storage->RequestMajor[RequestsBefore + 1] != IRP_MJ_WRITE ||
        Vcb.FreeClusterCount != 116 ||
        RtlAreBitsSet(&Vcb.ClusterBitmap, 10, 1) ||
        !RtlAreBitsSet(&Vcb.ClusterBitmap, 12, 1) ||
        DataContext.pRecord->NonResident.HighestVCN != 1 ||
        DataContext.pRecord->NonResident.CompressedSize != 1024 ||
        !FsRtlLookupLargeMcbEntry(&DataContext.DataRunsMCB, 1,
                                  &Lcn, NULL, NULL, NULL, NULL) ||
        Lcn != 12 ||
        FsRtlLookupLargeMcbEntry(&DataContext.DataRunsMCB, 2,
                                 &Lcn, NULL, NULL, NULL, NULL))
    {
        Status = NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
        goto CleanupDataMcb;
    }

    /*
     * Match the caller-side allocation-size update performed after a
     * successful FreeClusters() return.
     */
    FileDataAttribute->NonResident.AllocatedSize = 1024;
    DataContext.pRecord->NonResident.AllocatedSize = 1024;
    Status = UpdateFileRecord(&Vcb, 0, FileRecord);
    if (!NT_SUCCESS(Status) ||
        Storage->RequestCount != RequestsBefore + 3 ||
        Storage->RequestOffset[RequestsBefore + 2] != 2048 ||
        Storage->RequestLength[RequestsBefore + 2] != 1024)
    {
        Status = NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
        goto CleanupDataMcb;
    }

    DiskFileRecord = (PFILE_RECORD_HEADER)(Storage->Image + 2048);
    DiskDataAttribute = (PNTFS_ATTR_RECORD)
        ((PUCHAR)DiskFileRecord + DiskFileRecord->AttributeOffset);
    if (DiskDataAttribute->NonResident.HighestVCN != 1 ||
        DiskDataAttribute->NonResident.AllocatedSize != 1024 ||
        DiskDataAttribute->NonResident.CompressedSize != 1024 ||
        !RtlAreBitsClear(&Vcb.ClusterBitmap, 10, 1) ||
        Storage->Image[1025] != 0x1B)
    {
        Status = STATUS_DATA_ERROR;
        goto CleanupDataMcb;
    }

    /*
     * Fail the bitmap write after the shortened MFT record is durable.
     * The released cluster remains allocated on disk as a safe leak, and
     * the volume must latch its bitmap-write failure state.
     */
    RequestsBefore = Storage->RequestCount;
    Storage->FaultRequest = RequestsBefore + 1;
    Storage->FaultMode = NTFS_TEST_FAULT_ERROR;
    Status = FreeClusters(&Vcb, &DataContext, 64, FileRecord, 1);
    Storage->FaultMode = NTFS_TEST_FAULT_NONE;
    Storage->FaultRequest = -1;
    if (Status != STATUS_IO_DEVICE_ERROR ||
        Storage->RequestCount != RequestsBefore + 2 ||
        Storage->RequestOffset[RequestsBefore] != 2048 ||
        Storage->RequestOffset[RequestsBefore + 1] != 1024 ||
        Vcb.FreeClusterCount != 116 ||
        !(Vcb.Flags & VCB_BITMAP_WRITE_FAILED) ||
        !RtlAreBitsClear(&Vcb.ClusterBitmap, 12, 1) ||
        (Storage->Image[1025] & 0x10) == 0 ||
        DataContext.pRecord->NonResident.HighestVCN != 0 ||
        DataContext.pRecord->NonResident.CompressedSize != 512 ||
        !FsRtlLookupLargeMcbEntry(&DataContext.DataRunsMCB, 0,
                                  &Lcn, NULL, NULL, NULL, NULL) ||
        Lcn != 8 ||
        FsRtlLookupLargeMcbEntry(&DataContext.DataRunsMCB, 1,
                                 &Lcn, NULL, NULL, NULL, NULL))
    {
        Status = STATUS_DATA_ERROR;
        goto CleanupDataMcb;
    }

    DiskFileRecord = (PFILE_RECORD_HEADER)(Storage->Image + 2048);
    DiskDataAttribute = (PNTFS_ATTR_RECORD)
        ((PUCHAR)DiskFileRecord + DiskFileRecord->AttributeOffset);
    if (DiskDataAttribute->NonResident.HighestVCN != 0 ||
        DiskDataAttribute->NonResident.CompressedSize != 512 ||
        (Storage->Image[1025] & 0x10) == 0)
    {
        Status = STATUS_DATA_ERROR;
        goto CleanupDataMcb;
    }

    Status = STATUS_SUCCESS;
    DPRINT1("NTFS_TEST_BUILD: sparse allocation and FreeClusters integration passed\\n");

CleanupDataMcb:
    FsRtlUninitializeLargeMcb(&DataContext.DataRunsMCB);
CleanupDataRecord:
    ExFreePoolWithTag(DataContext.pRecord, TAG_NTFS);
CleanupMftMcb:
    FsRtlUninitializeLargeMcb(&MftContext.DataRunsMCB);
CleanupBitmapMcb:
    FsRtlUninitializeLargeMcb(&BitmapContext.DataRunsMCB);
CleanupResource:
    ExDeleteResourceLite(&Vcb.BitmapResource);
CleanupBitmap:
    ExFreePoolWithTag(Vcb.BitmapBuffer, TAG_NTFS);
    return Status;
}

NTSTATUS
NtfsTestRunStorageIo(
    _In_ PDRIVER_OBJECT DriverObject)
{
    PDEVICE_OBJECT DeviceObject = NULL;
    PNTFS_TEST_STORAGE Storage;
    PDRIVER_DISPATCH OldRead;
    PDRIVER_DISPATCH OldWrite;
    NTFS_IO_RUN_LIST RunList;
    UCHAR Buffer[1024];
    ULONG BytesTransferred = 0;
    ULONG Index;
    NTSTATUS Status;

    if (DriverObject == NULL)
        return STATUS_INVALID_PARAMETER;

    OldRead = DriverObject->MajorFunction[IRP_MJ_READ];
    OldWrite = DriverObject->MajorFunction[IRP_MJ_WRITE];

    Status = IoCreateDevice(DriverObject,
                            sizeof(NTFS_TEST_STORAGE),
                            NULL,
                            FILE_DEVICE_DISK,
                            0,
                            FALSE,
                            &DeviceObject);
    if (!NT_SUCCESS(Status))
        return Status;

    Storage = (PNTFS_TEST_STORAGE)DeviceObject->DeviceExtension;
    RtlZeroMemory(Storage, sizeof(*Storage));
    Storage->ImageLength = NTFS_TEST_STORAGE_SIZE;
    Storage->FaultRequest = -1;
    Storage->Image = ExAllocatePoolWithTag(NonPagedPool,
                                           Storage->ImageLength,
                                           TAG_NTFS);
    if (Storage->Image == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }

    RtlZeroMemory(Storage->Image, Storage->ImageLength);
    DeviceObject->Flags |= DO_DIRECT_IO;
    DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    DriverObject->MajorFunction[IRP_MJ_READ] = NtfsTestStorageDispatch;
    DriverObject->MajorFunction[IRP_MJ_WRITE] = NtfsTestStorageDispatch;

    NtfsInitIoRunList(&RunList);
    Status = NtfsAddIoRun(&RunList, 512, 512);
    if (!NT_SUCCESS(Status))
        goto CleanupRunList;

    /* Keep a physical gap so the production path must issue two run IRPs. */
    Status = NtfsAddIoRun(&RunList, 1536, 512);
    if (!NT_SUCCESS(Status))
        goto CleanupRunList;

    /*
     * The public run-list entry point must reject unsupported operations
     * before submitting any request to the lower device.
     */
    BytesTransferred = 123;
    Status = NtfsPerformIoRuns(DeviceObject,
                               IRP_MJ_FLUSH_BUFFERS,
                               512,
                               Buffer,
                               &RunList,
                               FALSE,
                               &BytesTransferred);
    if (Status != STATUS_INVALID_PARAMETER ||
        BytesTransferred != 0 ||
        Storage->RequestCount != 0)
    {
        Status = STATUS_DATA_ERROR;
        goto CleanupRunList;
    }

    /*
     * Sector sizes must be powers of two because the alignment path uses
     * bit-mask ROUND_UP arithmetic. Reject malformed geometry before I/O.
     */
    BytesTransferred = 123;
    Status = NtfsPerformIoRuns(DeviceObject,
                               IRP_MJ_READ,
                               768,
                               Buffer,
                               &RunList,
                               FALSE,
                               &BytesTransferred);
    if (Status != STATUS_INVALID_PARAMETER ||
        BytesTransferred != 0 ||
        Storage->RequestCount != 0)
    {
        Status = STATUS_DATA_ERROR;
        goto CleanupRunList;
    }

    /*
     * The run list must tile the buffer exactly. A gap, overlap, or
     * mismatched aggregate length is rejected before lower-device I/O.
     */
    {
        ULONG SavedOffset = RunList.Runs[1].BufferOffset;
        ULONG SavedLength = RunList.TotalLength;

        RunList.Runs[1].BufferOffset++;
        Status = NtfsPerformIoRuns(DeviceObject,
                                   IRP_MJ_READ,
                                   512,
                                   Buffer,
                                   &RunList,
                                   FALSE,
                                   &BytesTransferred);
        RunList.Runs[1].BufferOffset = SavedOffset;
        if (Status != STATUS_INVALID_PARAMETER ||
            BytesTransferred != 0 || Storage->RequestCount != 0)
        {
            Status = STATUS_DATA_ERROR;
            goto CleanupRunList;
        }

        RunList.TotalLength--;
        Status = NtfsPerformIoRuns(DeviceObject,
                                   IRP_MJ_READ,
                                   512,
                                   Buffer,
                                   &RunList,
                                   FALSE,
                                   &BytesTransferred);
        RunList.TotalLength = SavedLength;
        if (Status != STATUS_INVALID_PARAMETER ||
            BytesTransferred != 0 || Storage->RequestCount != 0)
        {
            Status = STATUS_DATA_ERROR;
            goto CleanupRunList;
        }
    }

    /* A complete write must reach the backing image at the exact LBO. */
    for (Index = 0; Index < sizeof(Buffer); Index++)
        Buffer[Index] = (UCHAR)(Index ^ 0xA5);

    Status = NtfsPerformIoRuns(DeviceObject,
                               IRP_MJ_WRITE,
                               512,
                               Buffer,
                               &RunList,
                               FALSE,
                               &BytesTransferred);
    if (!NT_SUCCESS(Status) || BytesTransferred != sizeof(Buffer) ||
        RtlCompareMemory(Storage->Image + 512, Buffer, 512) != 512 ||
        RtlCompareMemory(Storage->Image + 1536, Buffer + 512, 512) != 512)
    {
        Status = NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
        goto CleanupRunList;
    }

    /*
     * Exercise the one-run zero-copy path with the caller's original IRP and
     * MDL. The lower device must receive the original buffer pages directly.
     */
    {
        NTFS_IO_RUN_LIST DirectRunList;
        PUCHAR DirectBuffer = NULL;
        PIRP DirectIrp = NULL;
        PMDL DirectMdl = NULL;
        LONG RequestsBeforeDirect;

        NtfsInitIoRunList(&DirectRunList);
        Status = NtfsAddIoRun(&DirectRunList, 2560, sizeof(Buffer));
        if (!NT_SUCCESS(Status))
        {
            NtfsFreeIoRunList(&DirectRunList);
            goto CleanupRunList;
        }

        DirectBuffer = ExAllocatePoolWithTag(NonPagedPool,
                                             sizeof(Buffer),
                                             TAG_NTFS);
        if (DirectBuffer == NULL)
        {
            NtfsFreeIoRunList(&DirectRunList);
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto CleanupRunList;
        }

        for (Index = 0; Index < sizeof(Buffer); Index++)
            DirectBuffer[Index] = (UCHAR)(Index ^ 0x5A);

        DirectIrp = IoAllocateIrp(DeviceObject->StackSize, FALSE);
        if (DirectIrp == NULL)
        {
            ExFreePoolWithTag(DirectBuffer, TAG_NTFS);
            NtfsFreeIoRunList(&DirectRunList);
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto CleanupRunList;
        }

        DirectMdl = IoAllocateMdl(DirectBuffer, sizeof(Buffer), FALSE, FALSE, NULL);
        if (DirectMdl == NULL)
        {
            IoFreeIrp(DirectIrp);
            ExFreePoolWithTag(DirectBuffer, TAG_NTFS);
            NtfsFreeIoRunList(&DirectRunList);
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto CleanupRunList;
        }
        MmBuildMdlForNonPagedPool(DirectMdl);
        DirectIrp->MdlAddress = DirectMdl;

        /* The direct-forwarding shortcut must enforce the same geometry. */
        RequestsBeforeDirect = Storage->RequestCount;
        BytesTransferred = 123;
        Status = NtfsPerformIrpIoRuns(DeviceObject,
                                      IRP_MJ_WRITE,
                                      768,
                                      DirectIrp,
                                      DirectBuffer,
                                      &DirectRunList,
                                      FALSE,
                                      &BytesTransferred);
        if (Status != STATUS_INVALID_PARAMETER || BytesTransferred != 0 ||
            Storage->RequestCount != RequestsBeforeDirect)
        {
            IoFreeMdl(DirectMdl);
            IoFreeIrp(DirectIrp);
            ExFreePoolWithTag(DirectBuffer, TAG_NTFS);
            NtfsFreeIoRunList(&DirectRunList);
            Status = STATUS_DATA_ERROR;
            goto CleanupRunList;
        }

        BytesTransferred = 0;
        Status = NtfsPerformIrpIoRuns(DeviceObject,
                                      IRP_MJ_WRITE,
                                      512,
                                      DirectIrp,
                                      DirectBuffer,
                                      &DirectRunList,
                                      FALSE,
                                      &BytesTransferred);
        IoFreeMdl(DirectMdl);
        IoFreeIrp(DirectIrp);
        DirectIrp = NULL;
        DirectMdl = NULL;

        if (!NT_SUCCESS(Status) || BytesTransferred != sizeof(Buffer) ||
            RtlCompareMemory(Storage->Image + 2560,
                             DirectBuffer,
                             sizeof(Buffer)) != sizeof(Buffer))
        {
            ExFreePoolWithTag(DirectBuffer, TAG_NTFS);
            NtfsFreeIoRunList(&DirectRunList);
            Status = NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
            goto CleanupRunList;
        }

        /* The forwarded-IRP completion path must reject successful short I/O. */
        DirectIrp = IoAllocateIrp(DeviceObject->StackSize, FALSE);
        if (DirectIrp == NULL)
        {
            ExFreePoolWithTag(DirectBuffer, TAG_NTFS);
            NtfsFreeIoRunList(&DirectRunList);
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto CleanupRunList;
        }
        DirectMdl = IoAllocateMdl(DirectBuffer, sizeof(Buffer), FALSE, FALSE, NULL);
        if (DirectMdl == NULL)
        {
            IoFreeIrp(DirectIrp);
            ExFreePoolWithTag(DirectBuffer, TAG_NTFS);
            NtfsFreeIoRunList(&DirectRunList);
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto CleanupRunList;
        }
        MmBuildMdlForNonPagedPool(DirectMdl);
        DirectIrp->MdlAddress = DirectMdl;

        Storage->FaultRequest = Storage->RequestCount;
        Storage->FaultMode = NTFS_TEST_FAULT_SHORT;
        BytesTransferred = 123;
        Status = NtfsPerformIrpIoRuns(DeviceObject,
                                      IRP_MJ_READ,
                                      512,
                                      DirectIrp,
                                      DirectBuffer,
                                      &DirectRunList,
                                      FALSE,
                                      &BytesTransferred);
        Storage->FaultMode = NTFS_TEST_FAULT_NONE;
        Storage->FaultRequest = -1;
        IoFreeMdl(DirectMdl);
        IoFreeIrp(DirectIrp);
        ExFreePoolWithTag(DirectBuffer, TAG_NTFS);
        NtfsFreeIoRunList(&DirectRunList);

        if (Status != STATUS_UNEXPECTED_IO_ERROR || BytesTransferred != 0)
        {
            Status = STATUS_DATA_ERROR;
            goto CleanupRunList;
        }
    }

    /* A complete read must return exactly the bytes persisted above. */
    RtlZeroMemory(Buffer, sizeof(Buffer));
    Status = NtfsPerformIoRuns(DeviceObject,
                               IRP_MJ_READ,
                               512,
                               Buffer,
                               &RunList,
                               FALSE,
                               &BytesTransferred);
    if (!NT_SUCCESS(Status) || BytesTransferred != sizeof(Buffer) ||
        RtlCompareMemory(Storage->Image + 512, Buffer, 512) != 512 ||
        RtlCompareMemory(Storage->Image + 1536, Buffer + 512, 512) != 512)
    {
        Status = NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
        goto CleanupRunList;
    }

    /* Successful short completions must be rejected by production code. */
    Storage->FaultRequest = Storage->RequestCount;
    Storage->FaultMode = NTFS_TEST_FAULT_SHORT;
    BytesTransferred = 0;
    Status = NtfsPerformIoRuns(DeviceObject,
                               IRP_MJ_READ,
                               512,
                               Buffer,
                               &RunList,
                               FALSE,
                               &BytesTransferred);
    if (Status != STATUS_UNEXPECTED_IO_ERROR)
    {
        Status = STATUS_DATA_ERROR;
        goto CleanupRunList;
    }

    /* A lower-device error must be propagated without claiming transferred bytes. */
    Storage->FaultRequest = Storage->RequestCount;
    Storage->FaultMode = NTFS_TEST_FAULT_ERROR;
    BytesTransferred = 0;
    Status = NtfsPerformIoRuns(DeviceObject,
                               IRP_MJ_READ,
                               512,
                               Buffer,
                               &RunList,
                               FALSE,
                               &BytesTransferred);
    if (Status != STATUS_IO_DEVICE_ERROR || BytesTransferred != 0)
    {
        Status = STATUS_DATA_ERROR;
        goto CleanupRunList;
    }

    /*
     * A read spanning physical/sparse/physical runs must zero the hole while
     * still fetching both physical extents from their exact disk offsets.
     */
    {
        NTFS_IO_RUN_LIST SparseRunList;
        UCHAR SparseBuffer[1536];
        LONG RequestsBeforeWrite;

        NtfsInitIoRunList(&SparseRunList);
        Status = NtfsAddIoRun(&SparseRunList, 512, 512);
        if (!NT_SUCCESS(Status))
            goto CleanupRunList;
        Status = NtfsAddIoRun(&SparseRunList, NTFS_SPARSE_LBO, 512);
        if (!NT_SUCCESS(Status))
        {
            NtfsFreeIoRunList(&SparseRunList);
            goto CleanupRunList;
        }
        Status = NtfsAddIoRun(&SparseRunList, 1536, 512);
        if (!NT_SUCCESS(Status))
        {
            NtfsFreeIoRunList(&SparseRunList);
            goto CleanupRunList;
        }

        RtlFillMemory(SparseBuffer, sizeof(SparseBuffer), 0xCC);
        BytesTransferred = 0;
        Status = NtfsPerformIoRuns(DeviceObject,
                                   IRP_MJ_READ,
                                   512,
                                   SparseBuffer,
                                   &SparseRunList,
                                   FALSE,
                                   &BytesTransferred);
        if (!NT_SUCCESS(Status) || BytesTransferred != sizeof(SparseBuffer))
        {
            NtfsFreeIoRunList(&SparseRunList);
            Status = NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
            goto CleanupRunList;
        }

        for (Index = 512; Index < 1024; Index++)
        {
            if (SparseBuffer[Index] != 0)
            {
                NtfsFreeIoRunList(&SparseRunList);
                Status = STATUS_DATA_ERROR;
                goto CleanupRunList;
            }
        }

        /* Sparse writes must fail before issuing any physical part of the batch. */
        RequestsBeforeWrite = Storage->RequestCount;
        Status = NtfsPerformIoRuns(DeviceObject,
                                   IRP_MJ_WRITE,
                                   512,
                                   SparseBuffer,
                                   &SparseRunList,
                                   FALSE,
                                   &BytesTransferred);
        NtfsFreeIoRunList(&SparseRunList);
        if (Status != STATUS_NOT_IMPLEMENTED ||
            Storage->RequestCount != RequestsBeforeWrite ||
            BytesTransferred != 0)
        {
            Status = STATUS_DATA_ERROR;
            goto CleanupRunList;
        }
    }

    Status = NtfsTestRunBitmapAllocation(DeviceObject, Storage);
    if (!NT_SUCCESS(Status))
        goto CleanupRunList;

    Status = NtfsTestRunMftPersistence(DeviceObject, Storage);
    if (!NT_SUCCESS(Status))
        goto CleanupRunList;

    Status = NtfsTestRunSparseAllocation(DeviceObject, Storage);
    if (!NT_SUCCESS(Status))
        goto CleanupRunList;

    Status = STATUS_SUCCESS;
    DPRINT1("NTFS_TEST_BUILD: in-memory storage I/O and fault tests passed\\n");

CleanupRunList:
    NtfsFreeIoRunList(&RunList);
    Storage->FaultMode = NTFS_TEST_FAULT_NONE;
    Storage->FaultRequest = -1;
    DriverObject->MajorFunction[IRP_MJ_READ] = OldRead;
    DriverObject->MajorFunction[IRP_MJ_WRITE] = OldWrite;

Cleanup:
    if (Storage->Image != NULL)
        ExFreePoolWithTag(Storage->Image, TAG_NTFS);
    IoDeleteDevice(DeviceObject);
    return Status;
}
