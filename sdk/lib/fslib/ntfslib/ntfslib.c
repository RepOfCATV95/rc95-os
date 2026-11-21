/*
 * COPYRIGHT:   See COPYING in the top level directory
 * PROJECT:     ReactOS NTFS FS library
 * FILE:        lib/fslib/ntfslib/ntfslib.c
 * PURPOSE:     NTFS lib
 * PROGRAMMERS: Pierre Schweitzer, Klachkov Valery
 */

/* INCLUDES ******************************************************************/

#include "ntfslib.h"
#include "mft.h"

#define NDEBUG
#include <debug.h>


/* GLOBALS *******************************************************************/

// Legacy format state is serialized until it is moved into a per-operation context.
NTFS_FORMAT_DATA NtfsFormatData;
static volatile LONG NtfsFormatInProgress;


/* FUNCTIONS *****************************************************************/

VOID
NtfsGetSystemTimeAsFileTime(OUT PFILETIME lpFileTime)
{
    LARGE_INTEGER SystemTime;

    do
    {
        SystemTime.HighPart = SharedUserData->SystemTime.High1Time;
        SystemTime.LowPart = SharedUserData->SystemTime.LowPart;
    }
    while (SystemTime.HighPart != SharedUserData->SystemTime.High2Time);

    lpFileTime->dwLowDateTime = SystemTime.LowPart;
    lpFileTime->dwHighDateTime = SystemTime.HighPart;
}

BYTE
GetSectorsPerCluster(VOID)
{
    // Default NTFS cluster size selection based on the volume size,
    // mirroring the classic Windows defaults (for 512-byte sectors):
    //   <= 512 MB : 512 B, <= 1 GB : 1 KB, <= 2 GB : 2 KB, else 4 KB.
    ULONGLONG Size = DISK_LEN->Length.QuadPart;
    ULONG     Bps  = BYTES_PER_SECTOR;
    ULONG     ClusterBytes;

    if (Size <= (512ULL * 1024 * 1024))
        ClusterBytes = 512;
    else if (Size <= (1024ULL * 1024 * 1024))
        ClusterBytes = 1024;
    else if (Size <= (2048ULL * 1024 * 1024))
        ClusterBytes = 2048;
    else
        ClusterBytes = 4096;

    if (ClusterBytes < Bps)
        ClusterBytes = Bps;

    return (BYTE)(ClusterBytes / Bps);
}

//
// Encodes a record size (MFT record / index record) the way the NTFS boot
// sector expects it: a positive value is a count of clusters per record; a
// negative value -n means the record size is 2^n bytes (used when the record
// is smaller than a cluster).
//
static
CHAR
EncodeRecordSize(IN ULONG RecordBytes,
                 IN ULONG ClusterBytes)
{
    if (RecordBytes >= ClusterBytes)
    {
        return (CHAR)(RecordBytes / ClusterBytes);
    }
    else
    {
        CHAR  Log = 0;
        ULONG Value = RecordBytes;

        while (Value > 1)
        {
            Value >>= 1;
            Log++;
        }

        return (CHAR)(-Log);
    }
}

#define CEIL_DIV(a, b) (((a) + (b) - 1) / (b))

NTSTATUS
ComputeLayout(IN ULONG ClusterSize)
{
    ULONG     Bps = BYTES_PER_SECTOR;
    ULONGLONG VolumeBytes;
    ULONGLONG VolumeSectors;
    ULONG     Spc;
    ULONG     C;
    ULONGLONG Lcn;

    if (Bps == 0)
        return STATUS_INVALID_PARAMETER;

    VolumeBytes   = DISK_LEN->Length.QuadPart;
    VolumeSectors = VolumeBytes / Bps;

    // Need at least the backup boot sector plus something to format.
    if (VolumeSectors < 2)
        return STATUS_INVALID_PARAMETER;

    // Sectors per cluster
    if (ClusterSize != 0)
    {
        Spc = ClusterSize / Bps;
        if (Spc == 0)
            Spc = 1;
    }
    else
    {
        Spc = GetSectorsPerCluster();
    }

    LAYOUT.SectorsPerCluster = Spc;
    LAYOUT.BytesPerCluster   = Spc * Bps;
    C = LAYOUT.BytesPerCluster;

    // The last sector holds the backup boot sector and is not addressable.
    LAYOUT.TotalSectors = VolumeSectors - 1;
    LAYOUT.ClusterCount = LAYOUT.TotalSectors / Spc;
    if (LAYOUT.ClusterCount == 0)
        return STATUS_INVALID_PARAMETER;

    LAYOUT.ClustersPerMftRecord   = EncodeRecordSize(MFT_RECORD_SIZE, C);
    LAYOUT.ClustersPerIndexRecord = EncodeRecordSize(INDEX_RECORD_SIZE, C);

    //
    // Metadata sizes (in clusters)
    //

    // $Boot is exactly the first 8192 bytes of the volume.
    LAYOUT.BootClusters = (ULONG)CEIL_DIV(8192ULL, (ULONGLONG)C);

    // $MFT: reserve a large MFT.
    ULONGLONG MftBytes = VolumeBytes / 128;

    if (MftBytes < (4ULL * 1024 * 1024))
        MftBytes = 4ULL * 1024 * 1024;     // 4 MB min (~4096 records)
    if (MftBytes > (32ULL * 1024 * 1024))
        MftBytes = 32ULL * 1024 * 1024;    // 32 MB cap (~32768 records)

    LAYOUT.MftAllocRecords = (ULONG)(MftBytes / MFT_RECORD_SIZE);
    LAYOUT.MftClusters = (ULONG)CEIL_DIV((ULONGLONG)LAYOUT.MftAllocRecords * MFT_RECORD_SIZE, (ULONGLONG)C);

    // $MFTMirr: the first four system records.
    LAYOUT.MftMirrClusters = (ULONG)CEIL_DIV((ULONGLONG)MFT_MIRR_COUNT * MFT_RECORD_SIZE, (ULONGLONG)C);

    // $LogFile: ~0.78% of the volume, clamped to [256 KB, 64 MB].
    {
        ULONGLONG LogBytes = VolumeBytes / 128;

        if (LogBytes < (256ULL * 1024))
            LogBytes = 256ULL * 1024;
        if (LogBytes > (64ULL * 1024 * 1024))
            LogBytes = 64ULL * 1024 * 1024;

        LAYOUT.LogFileClusters = (ULONG)CEIL_DIV(LogBytes, (ULONGLONG)C);
    }

    // $Bitmap: one bit per cluster. The stream length must be a multiple of
    // 8 bytes (NTFS tracks the bitmap in 64-bit groups). If it isn't, the OS
    // rounds it up on first write and zero-fills the gap, leaving phantom
    // trailing clusters marked free. Round up here; WriteMetafiles matches.
    {
        ULONGLONG BitmapBytes = CEIL_DIV(LAYOUT.ClusterCount, 8ULL);
        BitmapBytes = (BitmapBytes + 7) & ~7ULL;
        LAYOUT.BitmapClusters = (ULONG)CEIL_DIV(BitmapBytes, (ULONGLONG)C);
    }

    // $UpCase: 128 KB (65536 UTF-16 entries).
    LAYOUT.UpCaseClusters = (ULONG)CEIL_DIV(128ULL * 1024, (ULONGLONG)C);

    // $AttrDef: reserve 4 KB (the attribute-definition table is ~2.5 KB).
    LAYOUT.AttrDefClusters = (ULONG)CEIL_DIV(4096ULL, (ULONGLONG)C);

    // $MFT's own allocation bitmap: one bit per reserved MFT record.
    {
        ULONGLONG MftBmpBytes = CEIL_DIV((ULONGLONG)LAYOUT.MftAllocRecords, 8ULL);
        LAYOUT.MftBitmapClusters = (ULONG)CEIL_DIV(MftBmpBytes, (ULONGLONG)C);
    }

    // Root directory $I30 index: one INDX block.
    LAYOUT.RootIdxClusters = (ULONG)CEIL_DIV((ULONGLONG)INDEX_RECORD_SIZE, (ULONGLONG)C);

    // $Secure:$SDS: the 8 default descriptors plus their mirror 256 KiB later.
    LAYOUT.SdsClusters = (ULONG)CEIL_DIV((ULONGLONG)NTFS_SDS_MIRROR + C, (ULONGLONG)C);

    // $Secure:$SDH: one INDX block (large view index).
    LAYOUT.SdhIdxClusters = (ULONG)CEIL_DIV((ULONGLONG)INDEX_RECORD_SIZE, (ULONGLONG)C);

    // $Extend / TxF payload streams (contents zero-initialized on a fresh volume;
    // chkdsk validates the metadata, not the CLFS log data).
    LAYOUT.TopsTClusters   = (ULONG)CEIL_DIV(1024ULL * 1024,     (ULONGLONG)C);  // 1 MiB
    LAYOUT.BlfClusters     = (ULONG)CEIL_DIV(64ULL * 1024,       (ULONGLONG)C);  // 64 KiB
    LAYOUT.Cont1Clusters   = (ULONG)CEIL_DIV(2ULL * 1024 * 1024, (ULONGLONG)C);  // 2 MiB
    LAYOUT.Cont2Clusters   = (ULONG)CEIL_DIV(2ULL * 1024 * 1024, (ULONGLONG)C);  // 2 MiB
    LAYOUT.DeletedIdxClusters = (ULONG)CEIL_DIV(64ULL * 1024,    (ULONGLONG)C);  // 64 KiB

    //
    // Placement: everything contiguous from the start of the volume.
    //
    Lcn = 0;
    LAYOUT.BootLcn      = Lcn; Lcn += LAYOUT.BootClusters;
    LAYOUT.MftLcn       = Lcn; Lcn += LAYOUT.MftClusters;
    LAYOUT.MftMirrLcn   = Lcn; Lcn += LAYOUT.MftMirrClusters;
    LAYOUT.LogFileLcn   = Lcn; Lcn += LAYOUT.LogFileClusters;
    LAYOUT.BitmapLcn    = Lcn; Lcn += LAYOUT.BitmapClusters;
    LAYOUT.UpCaseLcn    = Lcn; Lcn += LAYOUT.UpCaseClusters;
    LAYOUT.AttrDefLcn   = Lcn; Lcn += LAYOUT.AttrDefClusters;
    LAYOUT.MftBitmapLcn = Lcn; Lcn += LAYOUT.MftBitmapClusters;
    LAYOUT.RootIdxLcn   = Lcn; Lcn += LAYOUT.RootIdxClusters;
    LAYOUT.SdsLcn       = Lcn; Lcn += LAYOUT.SdsClusters;
    LAYOUT.SdhIdxLcn    = Lcn; Lcn += LAYOUT.SdhIdxClusters;
    LAYOUT.TopsTLcn     = Lcn; Lcn += LAYOUT.TopsTClusters;
    LAYOUT.BlfLcn       = Lcn; Lcn += LAYOUT.BlfClusters;
    LAYOUT.Cont1Lcn     = Lcn; Lcn += LAYOUT.Cont1Clusters;
    LAYOUT.Cont2Lcn     = Lcn; Lcn += LAYOUT.Cont2Clusters;
    LAYOUT.DeletedIdxLcn = Lcn; Lcn += LAYOUT.DeletedIdxClusters;
    LAYOUT.FirstFreeLcn = Lcn;

    // The volume must be large enough to hold the metadata (plus slack).
    if (LAYOUT.FirstFreeLcn + 16 >= LAYOUT.ClusterCount)
    {
        DPRINT1("Volume too small for NTFS: need %I64u clusters, have %I64u\n",
                LAYOUT.FirstFreeLcn + 16, LAYOUT.ClusterCount);
        return STATUS_DISK_FULL;
    }

    // Volume serial number
    {
        ULONG Seed = NtGetTickCount();
        ULONG Hi   = RtlRandom(&Seed);
        ULONG Lo   = RtlRandom(&Seed);
        LAYOUT.SerialNumber = ((ULONGLONG)Hi << 32) | (ULONGLONG)Lo;
    }

    return STATUS_SUCCESS;
}

//
// Zeroes the primary and backup boot sectors (the latter one sector past the
// addressable area). Called first, right after lock/dismount: from that moment
// the volume reads as RAW, so an interrupted format cannot leave a partially
// recognizable mix of stale and new metadata. The new boot sector is written
// last, once all metadata is complete.
//
static
NTSTATUS
InvalidateStaleBootSectors(VOID)
{
    NTSTATUS        Status;
    IO_STATUS_BLOCK IoStatusBlock;
    LARGE_INTEGER   Offset;
    PBYTE           Sector;
    ULONG           Bps = BYTES_PER_SECTOR;

    Sector = RtlAllocateHeap(RtlGetProcessHeap(), 0, Bps);
    if (!Sector)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Sector, Bps);

    // Primary boot sector
    Offset.QuadPart = 0LL;
    Status = NtWriteFile(DISK_HANDLE,
                         NULL,
                         NULL,
                         NULL,
                         &IoStatusBlock,
                         Sector,
                         Bps,
                         &Offset,
                         NULL);

    // Backup boot sector
    if (NT_SUCCESS(Status))
    {
        Offset.QuadPart = (LONGLONG)TOTAL_SECTORS * Bps;
        Status = NtWriteFile(DISK_HANDLE,
                             NULL,
                             NULL,
                             NULL,
                             &IoStatusBlock,
                             Sector,
                             Bps,
                             &Offset,
                             NULL);
    }

    FREE(Sector);

    return Status;
}

BOOLEAN
NTAPI
NtfsFormat(
    IN PUNICODE_STRING DriveRoot,
    IN PFMIFSCALLBACK Callback,
    IN BOOLEAN QuickFormat,
    IN BOOLEAN BackwardCompatible,
    IN MEDIA_TYPE MediaType,
    IN PUNICODE_STRING Label,
    IN ULONG ClusterSize)
{
    HANDLE                 DiskHandle;
    OBJECT_ATTRIBUTES      Attributes;
    IO_STATUS_BLOCK        Iosb;
    GET_LENGTH_INFORMATION LengthInformation;
    DISK_GEOMETRY          DiskGeometry;
    NTSTATUS               Status;
    BOOLEAN                Result = FALSE;
    BOOLEAN                VolumeLocked = FALSE;
    BOOLEAN                VolumeDismounted = FALSE;

    DPRINT1("NtfsFormat(DriveRoot '%wZ')\n", DriveRoot);

    if (DriveRoot == NULL || DriveRoot->Buffer == NULL || DriveRoot->Length == 0 || DriveRoot->Length > DriveRoot->MaximumLength)
        return FALSE;

    if (InterlockedCompareExchange(&NtfsFormatInProgress, 1, 0) != 0)
        return FALSE;

    // We always perform a quick format (metadata only); the volume body is
    // not zeroed.
    UNREFERENCED_PARAMETER(QuickFormat);
    UNREFERENCED_PARAMETER(BackwardCompatible);
    UNREFERENCED_PARAMETER(MediaType);

    InitializeObjectAttributes(&Attributes, DriveRoot, 0, NULL, NULL);

    // Open volume
    Status = NtOpenFile(&DiskHandle,
                        FILE_GENERIC_READ | FILE_GENERIC_WRITE | SYNCHRONIZE,
                        &Attributes,
                        &Iosb,
                        FILE_SHARE_READ,
                        FILE_SYNCHRONOUS_IO_ALERT);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("NtOpenFile() failed with status 0x%.08x\n", Status);
        InterlockedExchange(&NtfsFormatInProgress, 0);
        return FALSE;
    }

    // Get length info
    Status = NtDeviceIoControlFile(DiskHandle, 
                                   NULL,
                                   NULL,
                                   NULL, 
                                   &Iosb, 
                                   IOCTL_DISK_GET_LENGTH_INFO,
                                   NULL, 
                                   0, 
                                   &LengthInformation, 
                                   sizeof(GET_LENGTH_INFORMATION));
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("IOCTL_DISK_GET_LENGTH_INFO failed with status 0x%.08x\n", Status);
        NtClose(DiskHandle);
        InterlockedExchange(&NtfsFormatInProgress, 0);
        return FALSE;
    }

    // Get disk geometry
    Status = NtDeviceIoControlFile(DiskHandle, 
                                   NULL,
                                   NULL, 
                                   NULL, 
                                   &Iosb,
                                   IOCTL_DISK_GET_DRIVE_GEOMETRY,
                                   NULL, 
                                   0,
                                   &DiskGeometry,
                                   sizeof(DiskGeometry));
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("IOCTL_DISK_GET_DRIVE_GEOMETRY failed with status 0x%.08x\n", Status);
        NtClose(DiskHandle);
        InterlockedExchange(&NtfsFormatInProgress, 0);
        return FALSE;
    }

    // Reject impossible geometry before it reaches the layout arithmetic.
    if (LengthInformation.Length.QuadPart <= 0 ||
        DiskGeometry.BytesPerSector < 512 ||
        DiskGeometry.BytesPerSector > 4096 ||
        (DiskGeometry.BytesPerSector & (DiskGeometry.BytesPerSector - 1)) != 0 ||
        (LengthInformation.Length.QuadPart % DiskGeometry.BytesPerSector) != 0)
    {
        DPRINT1("Unsupported disk geometry: length=%I64d, bytes/sector=%lu\n",
                LengthInformation.Length.QuadPart, DiskGeometry.BytesPerSector);
        NtClose(DiskHandle);
        InterlockedExchange(&NtfsFormatInProgress, 0);
        return FALSE;
    }

    // Initialize progress bar
    if (Callback)
    {
        ULONG pc = 0;
        Callback(PROGRESS, 0, (PVOID)&pc);
    }

    // Setup global data
    DISK_HANDLE = DiskHandle;
    DISK_GEO    = &DiskGeometry;
    DISK_LEN    = &LengthInformation;
    LABEL       = Label;


    NtfsFormatData.HiddenSectors = 0;
    PARTITION_INFORMATION_EX PartitionInfo;
    NTSTATUS PartStatus =
        NtDeviceIoControlFile(DiskHandle, NULL, NULL, NULL, &Iosb,
                              IOCTL_DISK_GET_PARTITION_INFO_EX,
                              NULL, 0,
                              &PartitionInfo, sizeof(PartitionInfo));
    if (NT_SUCCESS(PartStatus) && NT_SUCCESS(Iosb.Status) &&
        DiskGeometry.BytesPerSector != 0 &&
        PartitionInfo.StartingOffset.QuadPart >= 0 &&
        (PartitionInfo.StartingOffset.QuadPart % DiskGeometry.BytesPerSector) == 0 &&
        (ULONGLONG)(PartitionInfo.StartingOffset.QuadPart / DiskGeometry.BytesPerSector) <= MAXULONG)
    {
        NtfsFormatData.HiddenSectors =
                (ULONG)(PartitionInfo.StartingOffset.QuadPart / DiskGeometry.BytesPerSector);
    }
    else
    {
        DPRINT1("IOCTL_DISK_GET_PARTITION_INFO_EX failed (0x%.08x); HiddenSectors=0\n", PartStatus);
    }

    // Capture a single timestamp used for every record, so a file's timestamps
    // match the copies stored in its parent directory's index entries.
    {
        LARGE_INTEGER SystemTime;
        KeQuerySystemTime(&SystemTime);
        NtfsFormatData.FormatTime = (ULONGLONG)SystemTime.QuadPart;
    }

    // Compute the on-disk layout (cluster size, metadata placement, serial)
    Status = ComputeLayout(ClusterSize);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ComputeLayout() failed with status 0x%.08x\n", Status);
        goto end;
    }

    Status = NtFsControlFile(DiskHandle, NULL, NULL, NULL, &Iosb, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0);
    if (!NT_SUCCESS(Status) || !NT_SUCCESS(Iosb.Status))
    {
        if (NT_SUCCESS(Status)) Status = Iosb.Status;
        DPRINT1("FSCTL_LOCK_VOLUME failed with status 0x%.08x\n", Status);
        goto end;
    }
    VolumeLocked = TRUE;

    Status = NtFsControlFile(DiskHandle, NULL, NULL, NULL, &Iosb, FSCTL_DISMOUNT_VOLUME, NULL, 0, NULL, 0);
    if (!NT_SUCCESS(Status) || !NT_SUCCESS(Iosb.Status))
    {
        if (NT_SUCCESS(Status)) Status = Iosb.Status;
        DPRINT1("FSCTL_DISMOUNT_VOLUME failed with status 0x%.08x\n", Status);
        goto end;
    }
    VolumeDismounted = TRUE;

    // Kill any stale boot sectors FIRST: the volume reads as RAW from here on,
    // so no partially-formatted state is ever recognizable as a filesystem.
    Status = InvalidateStaleBootSectors();
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("InvalidateStaleBootSectors() failed with status 0x%.08x\n", Status);
        goto end;
    }

    // Create metafiles
    Status = WriteMetafiles();
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("WriteMetafiles() failed with status 0x%.08x\n", Status);
        goto end;
    }

    // Write the boot sector LAST - only now, with all metadata complete behind
    // it, does the volume become recognizable as NTFS.
    Status = WriteBootSector();
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("WriteBootSector() failed with status 0x%.08x\n", Status);
        goto end;
    }

    Result = TRUE;
    DPRINT1("NtfsFormat() completed successfully\n");

end:

    if (VolumeLocked)
    {
        if (VolumeDismounted)
            NtFsControlFile(DiskHandle, NULL, NULL, NULL, &Iosb, FSCTL_DISMOUNT_VOLUME, NULL, 0, NULL, 0);
        NtFsControlFile(DiskHandle, NULL, NULL, NULL, &Iosb, FSCTL_UNLOCK_VOLUME, NULL, 0, NULL, 0);
    }

    // Close the volume handle
    NtClose(DiskHandle);

    DISK_HANDLE = NULL;
    DISK_GEO    = NULL;
    DISK_LEN    = NULL;
    LABEL       = NULL;
    InterlockedExchange(&NtfsFormatInProgress, 0);

    // Update progress bar
    if (Callback)
    {
        BOOL success = Result;
        Callback(DONE, 0, (PVOID)&success);
    }

    return Result;
}

BOOLEAN
NTAPI
NtfsChkdsk(
    IN PUNICODE_STRING DriveRoot,
    IN PFMIFSCALLBACK Callback,
    IN BOOLEAN FixErrors,
    IN BOOLEAN Verbose,
    IN BOOLEAN CheckOnlyIfDirty,
    IN BOOLEAN ScanDrive,
    IN PVOID pUnknown1,
    IN PVOID pUnknown2,
    IN PVOID pUnknown3,
    IN PVOID pUnknown4,
    IN PULONG ExitStatus)
{
    HANDLE DiskHandle = NULL;
    OBJECT_ATTRIBUTES Attributes;
    IO_STATUS_BLOCK Iosb;
    GET_LENGTH_INFORMATION LengthInformation;
    BOOT_SECTOR BootSector;
    UCHAR *RecordBuffer = NULL, *Scratch = NULL, *MirrorBuffer = NULL, *PrefixBuffer = NULL;
    PNTFS_MFT_RUN Runs = NULL;
    NTFS_MFT_RECORD_VIEW RecordView;
    NTFS_MFT_ATTRIBUTE_VIEW Attribute;
    NTSTATUS Status = STATUS_UNSUCCESSFUL;
    ULONG BytesPerSector, SectorsPerCluster, ClusterBytes;
    ULONG RecordSize, RunCapacity, RunCount, Cursor = 0;
    ULONGLONG MftOffset;
    BOOLEAN FoundMftData = FALSE, Success = FALSE;

    UNREFERENCED_PARAMETER(Verbose);
    UNREFERENCED_PARAMETER(CheckOnlyIfDirty);
    UNREFERENCED_PARAMETER(pUnknown1);
    UNREFERENCED_PARAMETER(pUnknown2);
    UNREFERENCED_PARAMETER(pUnknown3);
    UNREFERENCED_PARAMETER(pUnknown4);

    if (ExitStatus)
        *ExitStatus = (ULONG)STATUS_UNSUCCESSFUL;

    if (!DriveRoot || !DriveRoot->Buffer || DriveRoot->Length == 0 ||
        DriveRoot->Length > DriveRoot->MaximumLength)
    {
        Status = STATUS_INVALID_PARAMETER;
        goto cleanup;
    }

    /* This first checker is deliberately read-only. Repair and surface scans
       must not be advertised until their transaction/recovery paths exist. */
    if (FixErrors || ScanDrive)
    {
        Status = STATUS_NOT_SUPPORTED;
        goto cleanup;
    }

    InitializeObjectAttributes(&Attributes, DriveRoot, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenFile(&DiskHandle,
                        FILE_READ_DATA | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                        &Attributes,
                        &Iosb,
                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                        FILE_SYNCHRONOUS_IO_ALERT);
    if (!NT_SUCCESS(Status) || !NT_SUCCESS(Iosb.Status))
    {
        if (NT_SUCCESS(Status)) Status = Iosb.Status;
        goto cleanup;
    }

    Status = NtDeviceIoControlFile(DiskHandle, NULL, NULL, NULL, &Iosb,
                                   IOCTL_DISK_GET_LENGTH_INFO, NULL, 0,
                                   &LengthInformation, sizeof(LengthInformation));
    if (!NT_SUCCESS(Status) || !NT_SUCCESS(Iosb.Status) ||
        Iosb.Information < sizeof(LengthInformation))
    {
        if (NT_SUCCESS(Status))
            Status = NT_SUCCESS(Iosb.Status) ? STATUS_DEVICE_DATA_ERROR : Iosb.Status;
        goto cleanup;
    }

    {
        LARGE_INTEGER Offset;
        Offset.QuadPart = 0;
        Status = NtReadFile(DiskHandle, NULL, NULL, NULL, &Iosb,
                            &BootSector, sizeof(BootSector), &Offset, NULL);
        if (!NT_SUCCESS(Status) || !NT_SUCCESS(Iosb.Status) ||
            Iosb.Information != sizeof(BootSector))
        {
            if (NT_SUCCESS(Status))
                Status = NT_SUCCESS(Iosb.Status) ? STATUS_DEVICE_DATA_ERROR : Iosb.Status;
            goto cleanup;
        }
    }

    BytesPerSector = BootSector.BPB.BytesPerSector;
    SectorsPerCluster = BootSector.BPB.SectorsPerCluster;
    if (BootSector.OEMID.QuadPart != OEM_ID ||
        BootSector.EndSector != BOOT_SECTOR_END ||
        BytesPerSector < 512 || BytesPerSector > 4096 ||
        (BytesPerSector & (BytesPerSector - 1)) != 0 ||
        SectorsPerCluster == 0 || SectorsPerCluster > 128 ||
        (SectorsPerCluster & (SectorsPerCluster - 1)) != 0 ||
        BootSector.EBPB.SectorCount == 0 ||
        LengthInformation.Length.QuadPart <= 0 ||
        BootSector.EBPB.SectorCount >
            (ULONGLONG)LengthInformation.Length.QuadPart / BytesPerSector)
    {
        Status = STATUS_DISK_CORRUPT_ERROR;
        goto cleanup;
    }

    ClusterBytes = BytesPerSector * SectorsPerCluster;
    if (BootSector.EBPB.ClustersPerMftRecord > 0)
    {
        if ((ULONG)BootSector.EBPB.ClustersPerMftRecord > 64 ||
            (ULONG)BootSector.EBPB.ClustersPerMftRecord > MAXULONG / ClusterBytes)
        {
            Status = STATUS_DISK_CORRUPT_ERROR;
            goto cleanup;
        }
        RecordSize = (ULONG)BootSector.EBPB.ClustersPerMftRecord * ClusterBytes;
    }
    else if (BootSector.EBPB.ClustersPerMftRecord < 0)
    {
        ULONG Exponent = (ULONG)(-(LONG)BootSector.EBPB.ClustersPerMftRecord);
        if (Exponent < 9 || Exponent > 16)
        {
            Status = STATUS_DISK_CORRUPT_ERROR;
            goto cleanup;
        }
        RecordSize = 1UL << Exponent;
    }
    else
    {
        Status = STATUS_DISK_CORRUPT_ERROR;
        goto cleanup;
    }

    if (RecordSize < BytesPerSector || RecordSize > 0x10000 ||
        RecordSize % BytesPerSector != 0 ||
        (ULONGLONG)LengthInformation.Length.QuadPart < RecordSize ||
        ClusterBytes == 0 ||
        BootSector.EBPB.MftLocation >
            ((ULONGLONG)LengthInformation.Length.QuadPart - RecordSize) / ClusterBytes)
    {
        Status = STATUS_DISK_CORRUPT_ERROR;
        goto cleanup;
    }

    MftOffset = BootSector.EBPB.MftLocation * (ULONGLONG)ClusterBytes;
    RecordBuffer = RtlAllocateHeap(RtlGetProcessHeap(), 0, RecordSize);
    Scratch = RtlAllocateHeap(RtlGetProcessHeap(), 0, RecordSize);
    if (!RecordBuffer || !Scratch)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto cleanup;
    }

    {
        LARGE_INTEGER Offset;
        Offset.QuadPart = (LONGLONG)MftOffset;
        Status = NtReadFile(DiskHandle, NULL, NULL, NULL, &Iosb,
                            RecordBuffer, RecordSize, &Offset, NULL);
        if (!NT_SUCCESS(Status) || !NT_SUCCESS(Iosb.Status) ||
            Iosb.Information != RecordSize)
        {
            if (NT_SUCCESS(Status))
                Status = NT_SUCCESS(Iosb.Status) ? STATUS_DEVICE_DATA_ERROR : Iosb.Status;
            goto cleanup;
        }
    }

    Status = NtfsMftOpenRecord(RecordBuffer, RecordSize, BytesPerSector,
                               Scratch, &RecordView);
    if (!NT_SUCCESS(Status))
        goto cleanup;

    if (RecordView.RecordNumber != 0 ||
        (RecordView.Flags & MFT_RECORD_IN_USE) == 0)
    {
        Status = STATUS_DISK_CORRUPT_ERROR;
        goto cleanup;
    }

    while (TRUE)
    {
        Status = NtfsMftGetAttribute(&RecordView, &Cursor, &Attribute);
        if (Status == STATUS_NO_MORE_ENTRIES)
        {
            Status = STATUS_SUCCESS;
            break;
        }
        if (!NT_SUCCESS(Status))
            goto cleanup;

        if (!FoundMftData && Attribute.Type == AttributeData &&
            Attribute.NameLength == 0 && Attribute.NonResident)
        {
            ULONG RunlistLength = Attribute.Length - Attribute.DataRunsOffset;
            const UCHAR *Runlist = Attribute.Buffer + Attribute.DataRunsOffset;
            RunCapacity = (RunlistLength / 2) + 1;
            if (RunCapacity > MAXULONG / sizeof(NTFS_MFT_RUN))
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                goto cleanup;
            }

            Runs = RtlAllocateHeap(RtlGetProcessHeap(), 0,
                                   RunCapacity * sizeof(NTFS_MFT_RUN));
            if (!Runs)
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                goto cleanup;
            }

            Status = NtfsMftDecodeRunlist(Runlist, RunlistLength,
                                          Attribute.LowestVcn, Runs,
                                          RunCapacity, &RunCount);
            if (!NT_SUCCESS(Status))
                goto cleanup;

            if (RunCount == 0 ||
                Runs[0].StartingVcn != Attribute.LowestVcn ||
                Runs[0].StartingLcn != (LONGLONG)BootSector.EBPB.MftLocation ||
                Runs[RunCount - 1].StartingVcn +
                    Runs[RunCount - 1].ClusterCount - 1 != Attribute.HighestVcn)
            {
                Status = STATUS_DISK_CORRUPT_ERROR;
                goto cleanup;
            }

            {
                ULONGLONG VolumeClusters =
                    (ULONGLONG)LengthInformation.Length.QuadPart / ClusterBytes;
                ULONG RunIndex;
                for (RunIndex = 0; RunIndex < RunCount; ++RunIndex)
                {
                    if (!Runs[RunIndex].Sparse &&
                        ((ULONGLONG)Runs[RunIndex].StartingLcn > VolumeClusters ||
                         Runs[RunIndex].ClusterCount >
                            VolumeClusters - (ULONGLONG)Runs[RunIndex].StartingLcn))
                    {
                        Status = STATUS_DISK_CORRUPT_ERROR;
                        goto cleanup;
                    }
                }
            }

            FoundMftData = TRUE;
        }
    }

    if (!FoundMftData)
    {
        Status = STATUS_DISK_CORRUPT_ERROR;
        goto cleanup;
    }

    /*
     * Verify the four-record $MFTMirr prefix without attempting repair.
     * NTFS stores the first four MFT records in this stream. The boot-sector
     * location is authoritative for finding the mirror; disagreement is
     * reported as corruption and never resolved by blindly copying either side.
     */
    {
        ULONGLONG MirrorOffset, VolumeBytes;
        ULONG MirrorBytes = RecordSize * 4;
        LARGE_INTEGER IoOffset;

        VolumeBytes = (ULONGLONG)LengthInformation.Length.QuadPart;
        if (VolumeBytes < MirrorBytes || ClusterBytes == 0 ||
            BootSector.EBPB.MftMirrLocation >
            (VolumeBytes - MirrorBytes) / ClusterBytes)
        {
            Status = STATUS_DISK_CORRUPT_ERROR;
            goto cleanup;
        }

        /* The first MFT extent must contain the mirrored system-record prefix. */
        if (Runs[0].Sparse ||
            Runs[0].StartingVcn != 0 ||
            Runs[0].ClusterCount < ((ULONGLONG)MirrorBytes + ClusterBytes - 1) / ClusterBytes)
        {
            Status = STATUS_NOT_SUPPORTED;
            goto cleanup;
        }

        MirrorOffset = BootSector.EBPB.MftMirrLocation * (ULONGLONG)ClusterBytes;
        MirrorBuffer = RtlAllocateHeap(RtlGetProcessHeap(), 0, MirrorBytes);
        PrefixBuffer = RtlAllocateHeap(RtlGetProcessHeap(), 0, MirrorBytes);
        if (!MirrorBuffer || !PrefixBuffer)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto cleanup;
        }

        IoOffset.QuadPart = (LONGLONG)MftOffset;
        Status = NtReadFile(DiskHandle, NULL, NULL, NULL, &Iosb,
                            PrefixBuffer, MirrorBytes, &IoOffset, NULL);
        if (!NT_SUCCESS(Status) || !NT_SUCCESS(Iosb.Status) ||
            Iosb.Information != MirrorBytes)
        {
            if (NT_SUCCESS(Status))
                Status = NT_SUCCESS(Iosb.Status) ? STATUS_DEVICE_DATA_ERROR : Iosb.Status;
            goto cleanup;
        }

        IoOffset.QuadPart = (LONGLONG)MirrorOffset;
        Status = NtReadFile(DiskHandle, NULL, NULL, NULL, &Iosb,
                            MirrorBuffer, MirrorBytes, &IoOffset, NULL);
        if (!NT_SUCCESS(Status) || !NT_SUCCESS(Iosb.Status) ||
            Iosb.Information != MirrorBytes)
        {
            if (NT_SUCCESS(Status))
                Status = NT_SUCCESS(Iosb.Status) ? STATUS_DEVICE_DATA_ERROR : Iosb.Status;
            goto cleanup;
        }

        if (RtlCompareMemory(PrefixBuffer, MirrorBuffer, MirrorBytes) != MirrorBytes)
        {
            Status = STATUS_DISK_CORRUPT_ERROR;
            goto cleanup;
        }
    }

    Status = STATUS_SUCCESS;
    Success = TRUE;

cleanup:
    if (PrefixBuffer) RtlFreeHeap(RtlGetProcessHeap(), 0, PrefixBuffer);
    if (MirrorBuffer) RtlFreeHeap(RtlGetProcessHeap(), 0, MirrorBuffer);
    if (Runs) RtlFreeHeap(RtlGetProcessHeap(), 0, Runs);
    if (Scratch) RtlFreeHeap(RtlGetProcessHeap(), 0, Scratch);
    if (RecordBuffer) RtlFreeHeap(RtlGetProcessHeap(), 0, RecordBuffer);
    if (DiskHandle) NtClose(DiskHandle);

    if (ExitStatus)
        *ExitStatus = (ULONG)Status;

    if (Callback)
    {
        TEXTOUTPUT TextOut;
        TextOut.Lines = 1;
        if (Success)
            TextOut.Output = "NTFS basic read-only check passed (boot sector and $MFT record 0).";
        else if (Status == STATUS_NOT_SUPPORTED)
            TextOut.Output = "NTFS repair and surface scanning are not implemented; no changes were made.";
        else
            TextOut.Output = "NTFS basic read-only check failed; see the returned NTSTATUS.";
        Callback(OUTPUT, 0, &TextOut);
        Callback(DONE, 0, &Success);
    }

    return Success;
}
