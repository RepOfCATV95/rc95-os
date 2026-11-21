/*
 * RC95LDR <-> FreeLoader ARC filesystem bridge.
 * This file is compiled into both BIOS FreeLoader and UEFI FreeLoader.
 */
#include <freeldr.h>
#include <string.h>
#include <debug.h>
#include <fs.h>
#include <disk.h>
#include <rc95bcd.h>
#include <rc95ldrfs.h>
#include <fs/fat.h>

#define RC95LDR_ARC_BLOCK_SIZE 512u

typedef RC95LDR_FREELDR_VOLUME RC95LDR_FREELDR_FS_CONTEXT;

static RC95LDR_STATUS
Rc95LdrArcStatus(ARC_STATUS Status)
{
    if (Status == ESUCCESS)
        return Rc95LdrSuccess;
    if (Status == ENOMEM)
        return Rc95LdrOutOfMemory;
    if (Status == ENOENT || Status == ENODEV)
        return Rc95LdrNotFound;
    return Rc95LdrIoError;
}

static RC95LDR_STATUS
Rc95LdrArcBlockRead(
    void *DeviceContext,
    uint64_t Lba,
    uint32_t BlockCount,
    void *Buffer,
    size_t BufferLength)
{
    RC95LDR_FREELDR_FS_CONTEXT *Context = DeviceContext;
    ULONGLONG Offset;
    ULONGLONG Bytes;
    LARGE_INTEGER Position;
    ULONGLONG Remaining;
    ULONG Count;
    PUCHAR Destination = Buffer;

    if (!Context || !Buffer || BlockCount == 0)
        return Rc95LdrInvalidParameter;
    if (Lba > ((ULONGLONG)-1) / RC95LDR_ARC_BLOCK_SIZE)
        return Rc95LdrIntegerOverflow;

    Offset = Lba * RC95LDR_ARC_BLOCK_SIZE;
    Bytes = (ULONGLONG)BlockCount * RC95LDR_ARC_BLOCK_SIZE;
    if (Bytes > BufferLength)
        return Rc95LdrBufferTooSmall;
    if (Offset > Context->RawDeviceBytes ||
        Bytes > Context->RawDeviceBytes - Offset)
        return Rc95LdrOutOfBounds;

    if (Offset > 0x7fffffffffffffffULL)
        return Rc95LdrOutOfBounds;
    Position.QuadPart = (LONGLONG)Offset;
    if (ArcSeek(Context->RawDeviceId, &Position, SeekAbsolute) != ESUCCESS)
        return Rc95LdrIoError;

    Remaining = Bytes;
    while (Remaining)
    {
        ULONG Chunk = (Remaining > 0x40000u) ? 0x40000u : (ULONG)Remaining;
        Count = 0;
        if (ArcRead(Context->RawDeviceId, Destination, Chunk, &Count) != ESUCCESS ||
            Count != Chunk)
            return Rc95LdrIoError;
        Destination += Chunk;
        Remaining -= Chunk;
    }
    return Rc95LdrSuccess;
}

static RC95LDR_STATUS
Rc95LdrArcOpenRead(
    void *FileSystemContext,
    const char *Path,
    void **FileHandle,
    uint64_t *FileSize)
{
    RC95LDR_FREELDR_FS_CONTEXT *Context = FileSystemContext;
    ULONG FileId;
    ARC_STATUS Status;
    FILEINFORMATION Information;
    PCWSTR ServiceName;

    if (!Context || !Path || !FileHandle || !FileSize)
        return Rc95LdrInvalidParameter;

    *FileHandle = NULL;
    *FileSize = 0;
    Status = FsOpenFile((PSTR)Path, Context->DefaultPath, OpenReadOnly, &FileId);
    if (Status != ESUCCESS)
        return Rc95LdrArcStatus(Status);

    ServiceName = FsGetServiceName(FileId);
    if (Context->RequiredFileSystem == Rc95LdrFsNtfs)
    {
        if (ServiceName == NULL ||
            !RtlEqualMemory(ServiceName, L"ntfs", sizeof(L"ntfs")))
        {
            ArcClose(FileId);
            return Rc95LdrUnsupported;
        }
    }
    else
    {
#ifdef UEFIBOOT
        if (ServiceName == NULL ||
            (!RtlEqualMemory(ServiceName, L"ntfs", sizeof(L"ntfs")) &&
             (!RtlEqualMemory(ServiceName, L"fastfat", sizeof(L"fastfat")) ||
              !FatIsFat32File(FileId))))
#else
        if (ServiceName == NULL ||
            !RtlEqualMemory(ServiceName, L"ntfs", sizeof(L"ntfs")))
#endif
        {
            ArcClose(FileId);
            return Rc95LdrUnsupported;
        }
    }

    Status = ArcGetFileInformation(FileId, &Information);
    if (Status != ESUCCESS || Information.EndingAddress.QuadPart < 0)
    {
        ArcClose(FileId);
        return Rc95LdrIoError;
    }

    *FileSize = (uint64_t)Information.EndingAddress.QuadPart;
    *FileHandle = (void *)(ULONG_PTR)(FileId + 1);
    return Rc95LdrSuccess;
}

static RC95LDR_STATUS
Rc95LdrArcReadAt(
    void *FileSystemContext,
    void *FileHandle,
    uint64_t Offset,
    void *Buffer,
    size_t Length)
{
    ULONG FileId;
    LARGE_INTEGER Position;
    size_t Remaining = Length;
    PUCHAR Destination = Buffer;
    (void)FileSystemContext;

    if (!FileHandle || (!Buffer && Length))
        return Rc95LdrInvalidParameter;
    FileId = (ULONG)((ULONG_PTR)FileHandle - 1);
    if (Offset > 0x7fffffffffffffffULL ||
        Length > (size_t)(0x7fffffffffffffffULL - Offset))
        return Rc95LdrOutOfBounds;

    Position.QuadPart = (LONGLONG)Offset;
    if (ArcSeek(FileId, &Position, SeekAbsolute) != ESUCCESS)
        return Rc95LdrIoError;

    while (Remaining)
    {
        ULONG Chunk = (Remaining > 0x40000u) ? 0x40000u : (ULONG)Remaining;
        ULONG Count = 0;
        if (ArcRead(FileId, Destination, Chunk, &Count) != ESUCCESS ||
            Count != Chunk)
            return Rc95LdrIoError;
        Destination += Chunk;
        Remaining -= Chunk;
    }
    return Rc95LdrSuccess;
}

static void
Rc95LdrArcClose(
    void *FileSystemContext,
    void *FileHandle)
{
    (void)FileSystemContext;
    if (FileHandle)
        ArcClose((ULONG)((ULONG_PTR)FileHandle - 1));
}

/*
 * The boot partition is already identified by FreeLoader. Opening its ARC
 * path without a filename gives the raw partition device; the mounted
 * filesystem is selected by FsOpenFile's normal NTFS/FAT probing.
 */
RC95LDR_STATUS
Rc95LdrFreeldrLoadBcd(
    void *Buffer,
    size_t BufferCapacity,
    size_t *ImageSize,
    RC95BCD_HIVE *Hive)
{
    static const CHAR *BcdPaths[] =
    {
#ifdef UEFIBOOT
        "\\EFI\\Microsoft\\Boot\\BCD",
#endif
        "\\Boot\\BCD"
    };
    RC95LDR_FREELDR_FS_CONTEXT Context;
    RC95LDR_BLOCK_DEVICE BlockDevice;
    RC95LDR_FILESYSTEM FileSystem;
    RC95LDR_VOLUME Volume;
    FILEINFORMATION DeviceInformation;
    ARC_STATUS ArcStatus;
    RC95LDR_STATUS Status;
    ULONG RawDeviceId, Index;

    if (!Buffer || !ImageSize || !Hive || !FrLdrBootPath[0])
        return Rc95LdrInvalidParameter;

    RtlZeroMemory(&Context, sizeof(Context));
    Context.DefaultPath = FrLdrBootPath;

    ArcStatus = FsOpenFile(FrLdrBootPath, NULL, OpenReadOnly, &RawDeviceId);
    if (ArcStatus != ESUCCESS)
        return Rc95LdrArcStatus(ArcStatus);

    ArcStatus = ArcGetFileInformation(RawDeviceId, &DeviceInformation);
    if (ArcStatus != ESUCCESS ||
        DeviceInformation.EndingAddress.QuadPart < DeviceInformation.StartingAddress.QuadPart)
    {
        ArcClose(RawDeviceId);
        return Rc95LdrIoError;
    }

    Context.RawDeviceId = RawDeviceId;
    Context.RawDeviceBytes = (ULONGLONG)(DeviceInformation.EndingAddress.QuadPart -
                                          DeviceInformation.StartingAddress.QuadPart);

    RtlZeroMemory(&BlockDevice, sizeof(BlockDevice));
    BlockDevice.DeviceContext = &Context;
    BlockDevice.BlockSize = RC95LDR_ARC_BLOCK_SIZE;
    BlockDevice.BlockCount = Context.RawDeviceBytes / RC95LDR_ARC_BLOCK_SIZE;
    if (BlockDevice.BlockCount == 0)
    {
        ArcClose(RawDeviceId);
        return Rc95LdrInvalidImage;
    }
    BlockDevice.Read = Rc95LdrArcBlockRead;

    FileSystem.FileSystemContext = &Context;
    FileSystem.OpenRead = Rc95LdrArcOpenRead;
    FileSystem.ReadAt = Rc95LdrArcReadAt;
    FileSystem.Close = Rc95LdrArcClose;

    Volume.BlockDevice = &BlockDevice;
    Volume.FileSystem = &FileSystem;

    Status = Rc95LdrNotFound;
    for (Index = 0; Index < RTL_NUMBER_OF(BcdPaths); ++Index)
    {
        Status = Rc95BcdLoadFromVolume(&Volume, BcdPaths[Index],
                                       Buffer, BufferCapacity, ImageSize, Hive);
        if (Status == Rc95LdrSuccess)
        {
            TRACE("RC95LDR: loaded BCD from %s via %s\n",
                  BcdPaths[Index], FrLdrBootPath);
            break;
        }
        if (Status != Rc95LdrNotFound && Status != Rc95LdrUnsupported)
            break;
    }

    ArcClose(RawDeviceId);
    return Status;
}

/* Bind a partition ARC device to FreeLoader's existing filesystem dispatch.
 * FsOpenFile performs the actual NTFS/FAT32 mount and file access; this
 * adapter deliberately does not duplicate either filesystem implementation. */
RC95LDR_STATUS
Rc95LdrFreeldrOpenVolume(
    const char *ArcDevicePath,
    RC95LDR_FREELDR_VOLUME *Adapter,
    RC95LDR_VOLUME **Volume)
{
    FILEINFORMATION Information;
    ARC_STATUS ArcStatus;
    ULONG DeviceId;
    SIZE_T PathLength;
    if (!ArcDevicePath || !Adapter || !Volume)
        return Rc95LdrInvalidParameter;
    PathLength = strlen(ArcDevicePath);
    if (PathLength == 0 || PathLength >= sizeof(Adapter->DevicePath))
        return Rc95LdrBufferTooSmall;
    RtlZeroMemory(Adapter, sizeof(*Adapter));
    RtlCopyMemory(Adapter->DevicePath, ArcDevicePath, PathLength + 1);
    *Volume = NULL;
    ArcStatus = FsOpenFile(Adapter->DevicePath, NULL, OpenReadOnly, &DeviceId);
    if (ArcStatus != ESUCCESS)
        return Rc95LdrArcStatus(ArcStatus);
    ArcStatus = ArcGetFileInformation(DeviceId, &Information);
    if (ArcStatus != ESUCCESS ||
        Information.EndingAddress.QuadPart < Information.StartingAddress.QuadPart)
    {
        ArcClose(DeviceId);
        return Rc95LdrIoError;
    }
    Adapter->RawDeviceBytes = (uint64_t)(Information.EndingAddress.QuadPart -
                                         Information.StartingAddress.QuadPart);
    if (Adapter->RawDeviceBytes < RC95LDR_ARC_BLOCK_SIZE)
    {
        ArcClose(DeviceId);
        RtlZeroMemory(Adapter, sizeof(*Adapter));
        return Rc95LdrInvalidImage;
    }
    Adapter->DefaultPath = Adapter->DevicePath;
    Adapter->RawDeviceId = DeviceId;
    Adapter->RequiredFileSystem = Rc95LdrFsNtfs;
    Adapter->BlockDevice.DeviceContext = Adapter;
    Adapter->BlockDevice.BlockSize = RC95LDR_ARC_BLOCK_SIZE;
    Adapter->BlockDevice.BlockCount =
        Adapter->RawDeviceBytes / RC95LDR_ARC_BLOCK_SIZE;
    Adapter->BlockDevice.Read = Rc95LdrArcBlockRead;
    Adapter->FileSystem.FileSystemContext = Adapter;
    Adapter->FileSystem.OpenRead = Rc95LdrArcOpenRead;
    Adapter->FileSystem.ReadAt = Rc95LdrArcReadAt;
    Adapter->FileSystem.Close = Rc95LdrArcClose;
    Adapter->Volume.BlockDevice = &Adapter->BlockDevice;
    Adapter->Volume.FileSystem = &Adapter->FileSystem;
    Adapter->IsOpen = 1;
    *Volume = &Adapter->Volume;
    return Rc95LdrSuccess;
}

void
Rc95LdrFreeldrCloseVolume(RC95LDR_FREELDR_VOLUME *Adapter)
{
    if (!Adapter || !Adapter->IsOpen)
        return;
    ArcClose(Adapter->RawDeviceId);
    RtlZeroMemory(Adapter, sizeof(*Adapter));
}
