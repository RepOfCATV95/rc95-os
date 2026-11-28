#include "rc95ldruefinativemounts.h"
#include <string.h>

#define RC95LDR_UEFI_NTFS_MOUNT_SCRATCH_BYTES (128u * 1024u)
#define RC95LDR_UEFI_BTRFS_WORKSPACE_BYTES (128u * 1024u)

static RC95LDR_STATUS
ReadNtfsPartition(void *Opaque, uint64_t Lba, uint32_t Blocks,
                  void *Buffer, size_t Capacity)
{
    RC95LDR_UEFI_NTFS_MOUNT_STATE *State =
        (RC95LDR_UEFI_NTFS_MOUNT_STATE *)Opaque;
    size_t Bytes;
    if (!State || !State->Mounted || !Buffer || !Blocks ||
        !State->BlockIo.ReadBlocks || !State->BlockIo.Media.BlockSize)
        return Rc95LdrInvalidParameter;
    if (Lba > State->Device.BlockCount ||
        (uint64_t)Blocks > State->Device.BlockCount - Lba)
        return Rc95LdrOutOfBounds;
    if (!Rc95LdrMultiplySize((size_t)Blocks,
            State->Device.BlockSize, &Bytes))
        return Rc95LdrIntegerOverflow;
    if (Bytes > Capacity) return Rc95LdrBufferTooSmall;
    return State->BlockIo.ReadBlocks(State->BlockIo.Protocol,
        State->BlockIo.Media.MediaId, Lba, Bytes, Buffer);
}

static RC95LDR_STATUS
ReadBtrfsPartition(void *Opaque, uint64_t Lba, uint32_t Blocks,
                  void *Buffer, size_t Capacity)
{
    RC95LDR_UEFI_BTRFS_MOUNT_STATE *State =
        (RC95LDR_UEFI_BTRFS_MOUNT_STATE *)Opaque;
    size_t Bytes;
    if (!State || !State->Mounted || !Buffer || !Blocks ||
        !State->BlockIo.ReadBlocks || !State->Device.BlockSize)
        return Rc95LdrInvalidParameter;
    if (Lba > State->Device.BlockCount ||
        (uint64_t)Blocks > State->Device.BlockCount - Lba)
        return Rc95LdrOutOfBounds;
    if (!Rc95LdrMultiplySize((size_t)Blocks, State->Device.BlockSize, &Bytes))
        return Rc95LdrIntegerOverflow;
    if (Bytes > Capacity) return Rc95LdrBufferTooSmall;
    return State->BlockIo.ReadBlocks(State->BlockIo.Protocol,
        State->BlockIo.Media.MediaId, Lba, Bytes, Buffer);
}

static void
FreeBtrfsMount(RC95LDR_UEFI_NATIVE_MOUNT_CONTEXT *Context)
{
    RC95LDR_UEFI_BTRFS_MOUNT_STATE *State = &Context->Btrfs;
    Rc95LdrBtrfsUnmount(&State->Btrfs);
    if (State->StagingBuffer) Context->FreePool(Context->AllocatorContext, State->StagingBuffer);
    if (State->InlineBuffer) Context->FreePool(Context->AllocatorContext, State->InlineBuffer);
    if (State->ItemBuffer) Context->FreePool(Context->AllocatorContext, State->ItemBuffer);
    if (State->Scratch) Context->FreePool(Context->AllocatorContext, State->Scratch);
    memset(State, 0, sizeof(*State));
}

static RC95LDR_STATUS
AllocateBtrfsWorkspace(RC95LDR_UEFI_NATIVE_MOUNT_CONTEXT *Context, void **Buffer)
{
    RC95LDR_STATUS Status = Context->AllocatePool(Context->AllocatorContext,
        RC95LDR_UEFI_BTRFS_WORKSPACE_BYTES, Buffer);
    if (Status != Rc95LdrSuccess) return Status;
    return *Buffer ? Rc95LdrSuccess : Rc95LdrOutOfMemory;
}

static RC95LDR_STATUS
MountBtrfs(RC95LDR_UEFI_NATIVE_MOUNT_CONTEXT *Context,
           const RC95LDR_PARTITION_INFO *Partition,
           RC95LDR_VOLUME *MountedVolume)
{
    RC95LDR_UEFI_BTRFS_MOUNT_STATE *State = &Context->Btrfs;
    void *Handle = NULL;
    const uint8_t *DevicePath = NULL;
    size_t DevicePathLength = 0;
    RC95LDR_STATUS Status;
    if (State->Mounted) return Rc95LdrUnsupported;
    memset(State, 0, sizeof(*State));
    if (Partition->FirmwareHandleIndex == UINT32_MAX || !Partition->BlockCount || !Partition->BlockSize)
        return Rc95LdrInvalidParameter;
    Status = Context->Firmware.GetHandleAt(Context->Firmware.Context, Partition->FirmwareHandleIndex, &Handle);
    if (Status != Rc95LdrSuccess) goto Failure;
    if (!Handle) { Status = Rc95LdrNotFound; goto Failure; }
    Status = Context->Firmware.OpenBlockIo(Context->Firmware.Context, Handle, &State->BlockIo);
    if (Status != Rc95LdrSuccess) goto Failure;
    if (!State->BlockIo.Media.MediaPresent || !State->BlockIo.Media.BlockSize ||
        State->BlockIo.Media.BlockSize != Partition->BlockSize || State->BlockIo.Media.IoAlign > 1u ||
        State->BlockIo.Media.LastBlock == UINT64_MAX ||
        State->BlockIo.Media.LastBlock + 1u != Partition->BlockCount || !State->BlockIo.ReadBlocks)
    { Status = Rc95LdrCorruptData; goto Failure; }
    Status = Context->Firmware.GetDevicePath(Context->Firmware.Context, Handle, &DevicePath, &DevicePathLength);
    if (Status != Rc95LdrSuccess) goto Failure;
    Status = Rc95LdrUefiValidatePartitionHandle(Partition, &State->BlockIo, DevicePath, DevicePathLength);
    if (Status != Rc95LdrSuccess) goto Failure;
    State->Device.DeviceContext = State;
    State->Device.BlockCount = Partition->BlockCount;
    State->Device.BlockSize = Partition->BlockSize;
    State->Device.Read = ReadBtrfsPartition;
    State->Device.Write = NULL;
    State->Mounted = 1;
    Status = AllocateBtrfsWorkspace(Context, &State->Scratch);
    if (Status != Rc95LdrSuccess) goto Failure;
    State->ScratchLength = RC95LDR_UEFI_BTRFS_WORKSPACE_BYTES;
    Status = Rc95LdrBtrfsMountPartition(&State->Btrfs, &State->Device, 0,
        Partition->BlockCount, (uint8_t *)State->Scratch, State->ScratchLength);
    if (Status != Rc95LdrSuccess) goto Failure;
    Status = Rc95LdrBtrfsLoadChunkTree(&State->Btrfs, (uint8_t *)State->Scratch, State->ScratchLength);
    if (Status != Rc95LdrSuccess) goto Failure;
    Status = Rc95LdrBtrfsLoadRootTree(&State->Btrfs, (uint8_t *)State->Scratch, State->ScratchLength);
    if (Status != Rc95LdrSuccess) goto Failure;
    Status = AllocateBtrfsWorkspace(Context, &State->ItemBuffer);
    if (Status != Rc95LdrSuccess) goto Failure;
    Status = AllocateBtrfsWorkspace(Context, &State->InlineBuffer);
    if (Status != Rc95LdrSuccess) goto Failure;
    Status = AllocateBtrfsWorkspace(Context, &State->StagingBuffer);
    if (Status != Rc95LdrSuccess) goto Failure;
    State->WorkspaceLength = RC95LDR_UEFI_BTRFS_WORKSPACE_BYTES;
    Status = Rc95LdrBtrfsInitializeFileSystem(&State->FileSystemContext, &State->Btrfs,
        (uint8_t *)State->Scratch, State->ScratchLength, State->ItemBuffer, State->WorkspaceLength,
        State->InlineBuffer, State->WorkspaceLength, State->StagingBuffer, State->WorkspaceLength,
        &State->FileSystem);
    if (Status != Rc95LdrSuccess) goto Failure;
    Status = Rc95LdrBtrfsBindVolumeWithFileSystem(&State->Btrfs, MountedVolume, &State->FileSystem);
    if (Status != Rc95LdrSuccess) goto Failure;
    return Rc95LdrSuccess;
Failure:
    FreeBtrfsMount(Context);
    memset(MountedVolume, 0, sizeof(*MountedVolume));
    return Status;
}

static void
FreeNtfsMount(RC95LDR_UEFI_NATIVE_MOUNT_CONTEXT *Context)
{
    RC95LDR_UEFI_NTFS_MOUNT_STATE *State = &Context->Ntfs;
    if (State->Provider.Initialized)
        Rc95LdrNtfsDestroyFileSystem(&State->Provider);
    if (State->Ntfs.Mounted)
        Rc95LdrNtfsUnmount(&State->Ntfs);
    if (State->ProviderWorkspace)
        Context->FreePool(Context->AllocatorContext,
                          State->ProviderWorkspace);
    if (State->MountScratch)
        Context->FreePool(Context->AllocatorContext, State->MountScratch);
    memset(State, 0, sizeof(*State));
}

static RC95LDR_STATUS
MountNtfs(RC95LDR_UEFI_NATIVE_MOUNT_CONTEXT *Context,
          const RC95LDR_PARTITION_INFO *Partition,
          RC95LDR_VOLUME *MountedVolume)
{
    RC95LDR_UEFI_NTFS_MOUNT_STATE *State = &Context->Ntfs;
    void *Handle = NULL;
    const uint8_t *DevicePath = NULL;
    size_t DevicePathLength = 0, WorkspaceBytes = 0;
    RC95LDR_STATUS Status;

    if (State->Mounted) return Rc95LdrUnsupported;
    memset(State, 0, sizeof(*State));

    if (Partition->FirmwareHandleIndex == UINT32_MAX ||
        !Partition->BlockCount || !Partition->BlockSize)
        return Rc95LdrInvalidParameter;

    Status = Context->Firmware.GetHandleAt(Context->Firmware.Context,
        Partition->FirmwareHandleIndex, &Handle);
    if (Status != Rc95LdrSuccess) goto Failure;
    if (!Handle) { Status = Rc95LdrNotFound; goto Failure; }

    Status = Context->Firmware.OpenBlockIo(Context->Firmware.Context,
        Handle, &State->BlockIo);
    if (Status != Rc95LdrSuccess) goto Failure;

    if (!State->BlockIo.Media.MediaPresent ||
        !State->BlockIo.Media.BlockSize ||
        State->BlockIo.Media.BlockSize != Partition->BlockSize ||
        State->BlockIo.Media.IoAlign > 1u ||
        State->BlockIo.Media.LastBlock == UINT64_MAX ||
        State->BlockIo.Media.LastBlock + 1u != Partition->BlockCount ||
        !State->BlockIo.ReadBlocks)
    {
        Status = Rc95LdrCorruptData;
        goto Failure;
    }

    Status = Context->Firmware.GetDevicePath(Context->Firmware.Context,
        Handle, &DevicePath, &DevicePathLength);
    if (Status != Rc95LdrSuccess) goto Failure;
    Status = Rc95LdrUefiValidatePartitionHandle(Partition,
        &State->BlockIo, DevicePath, DevicePathLength);
    if (Status != Rc95LdrSuccess) goto Failure;

    State->Device.DeviceContext = State;
    State->Device.BlockCount = Partition->BlockCount;
    State->Device.BlockSize = Partition->BlockSize;
    State->Device.Read = ReadNtfsPartition;
    State->Device.Write = NULL;

    Status = Context->AllocatePool(Context->AllocatorContext,
        RC95LDR_UEFI_NTFS_MOUNT_SCRATCH_BYTES, &State->MountScratch);
    if (Status != Rc95LdrSuccess) goto Failure;
    if (!State->MountScratch) { Status = Rc95LdrOutOfMemory; goto Failure; }
    State->MountScratchLength = RC95LDR_UEFI_NTFS_MOUNT_SCRATCH_BYTES;

    /* Mark the block adapter live while the NTFS bootstrap reads the BPB/MFT. */
    State->Mounted = 1;
    Status = Rc95LdrNtfsMountPartition(&State->Ntfs, &State->Device,
        0, Partition->BlockCount, State->MountScratch,
        State->MountScratchLength);
    if (Status != Rc95LdrSuccess) goto Failure;

    Status = Rc95LdrNtfsFileSystemWorkspaceSize(&State->Ntfs,
        &WorkspaceBytes);
    if (Status != Rc95LdrSuccess) goto Failure;
    Status = Context->AllocatePool(Context->AllocatorContext,
        WorkspaceBytes, &State->ProviderWorkspace);
    if (Status != Rc95LdrSuccess) goto Failure;
    if (!State->ProviderWorkspace) { Status = Rc95LdrOutOfMemory; goto Failure; }
    State->ProviderWorkspaceLength = WorkspaceBytes;

    Status = Rc95LdrNtfsBindVolume(&State->Ntfs, &State->Provider,
        State->ProviderWorkspace, State->ProviderWorkspaceLength,
        MountedVolume);
    if (Status != Rc95LdrSuccess) goto Failure;
    return Rc95LdrSuccess;

Failure:
    FreeNtfsMount(Context);
    memset(MountedVolume, 0, sizeof(*MountedVolume));
    return Status;
}

RC95LDR_STATUS
Rc95LdrUefiInitializeNativeMounts(
    RC95LDR_UEFI_NATIVE_MOUNT_CONTEXT *Context,
    const RC95LDR_UEFI_FIRMWARE_OPS *Firmware,
    RC95LDR_UEFI_ALLOCATE_POOL AllocatePool,
    RC95LDR_UEFI_FREE_POOL FreePool,
    void *AllocatorContext)
{
    if (!Context || !Firmware || !Firmware->GetHandleAt ||
        !Firmware->OpenBlockIo || !Firmware->GetDevicePath ||
        !AllocatePool || !FreePool)
        return Rc95LdrInvalidParameter;
    memset(Context, 0, sizeof(*Context));
    Context->Firmware = *Firmware;
    Context->AllocatePool = AllocatePool;
    Context->FreePool = FreePool;
    Context->AllocatorContext = AllocatorContext;
    Context->Initialized = 1;
    return Rc95LdrSuccess;
}

RC95LDR_STATUS
Rc95LdrUefiMountNativeVolume(
    void *Opaque, const RC95LDR_PARTITION_INFO *Partition,
    RC95LDR_FILESYSTEM_KIND FileSystem, RC95LDR_VOLUME *MountedVolume)
{
    RC95LDR_UEFI_NATIVE_MOUNT_CONTEXT *Context =
        (RC95LDR_UEFI_NATIVE_MOUNT_CONTEXT *)Opaque;
    if (!Context || !Context->Initialized || !Partition || !MountedVolume)
        return Rc95LdrInvalidParameter;
    memset(MountedVolume, 0, sizeof(*MountedVolume));

    if (FileSystem == Rc95LdrFsFat32)
    {
        if (Context->Esp.Mounted) return Rc95LdrUnsupported;
        return Rc95LdrUefiMountFat32Volume(&Context->Firmware, Partition,
            &Context->Esp, MountedVolume);
    }
    if (FileSystem == Rc95LdrFsNtfs)
        return MountNtfs(Context, Partition, MountedVolume);
    if (FileSystem == Rc95LdrFsBtrfs)
    {
        if (Partition->FileSystem != Rc95LdrFsBtrfs) return Rc95LdrInvalidParameter;
        return MountBtrfs(Context, Partition, MountedVolume);
    }
    return Rc95LdrUnsupported;
}

void
Rc95LdrUefiUnmountNativeVolume(void *Opaque, RC95LDR_VOLUME *Volume)
{
    RC95LDR_UEFI_NATIVE_MOUNT_CONTEXT *Context =
        (RC95LDR_UEFI_NATIVE_MOUNT_CONTEXT *)Opaque;
    if (!Context || !Context->Initialized || !Volume) return;

    if (Volume->BlockDevice == &Context->Esp.Device)
        Rc95LdrUefiUnmountFat32Volume(&Context->Esp);
    else if (Volume->BlockDevice == &Context->Ntfs.Ntfs.DeviceStorage)
        FreeNtfsMount(Context);
    else if (Volume->BlockDevice == &Context->Btrfs.Btrfs.PartitionDevice)
        FreeBtrfsMount(Context);
    memset(Volume, 0, sizeof(*Volume));
}

void
Rc95LdrUefiDestroyNativeMounts(
    RC95LDR_UEFI_NATIVE_MOUNT_CONTEXT *Context)
{
    if (!Context || !Context->Initialized) return;
    if (Context->Esp.Mounted)
        Rc95LdrUefiUnmountFat32Volume(&Context->Esp);
    if (Context->Ntfs.Mounted || Context->Ntfs.MountScratch ||
        Context->Ntfs.ProviderWorkspace)
        FreeNtfsMount(Context);
    if (Context->Btrfs.Mounted || Context->Btrfs.Scratch || Context->Btrfs.ItemBuffer ||
        Context->Btrfs.InlineBuffer || Context->Btrfs.StagingBuffer)
        FreeBtrfsMount(Context);
    memset(Context, 0, sizeof(*Context));
}

#include "rc95ldruefinativemounts.h"
#include "rc95ldrpe.h"
#include "rc95ldrsystemhive.h"
#include <string.h>

#define RC95LDR_UEFI_MAX_BOOT_IMAGE (64u * 1024u * 1024u)
#define RC95LDR_UEFI_MAX_SYSTEM_HIVE (256u * 1024u * 1024u)
#define RC95LDR_UEFI_PATH_CAPACITY 1024u

typedef struct _UEFI_BOOT_FILE {
    void *Handle;
    uint64_t Size;
} UEFI_BOOT_FILE;

static int
AsciiEqualInsensitive(char A, char B)
{
    if (A >= 'A' && A <= 'Z') A = (char)(A + ('a' - 'A'));
    if (B >= 'A' && B <= 'Z') B = (char)(B + ('a' - 'A'));
    return A == B;
}

static int
PathHasParentComponent(const char *Path)
{
    const char *P = Path;
    while (*P)
    {
        const char *Start;
        size_t Length;
        while (*P == '\\' || *P == '/') ++P;
        Start = P;
        while (*P && *P != '\\' && *P != '/') ++P;
        Length = (size_t)(P - Start);
        if (Length == 2u && Start[0] == '.' && Start[1] == '.')
            return 1;
        if (Length == 1u && Start[0] == '.') return 1;
        if (*P) ++P;
    }
    return 0;
}

static RC95LDR_STATUS
ConvertInstallationPath(const uint16_t *Source, uint32_t Characters,
                        char *Destination, size_t Capacity)
{
    size_t I, Out = 0;
    if (!Source || !Characters || !Destination || Capacity < 2u ||
        Characters >= Capacity)
        return Rc95LdrInvalidParameter;
    for (I = 0; I < Characters; ++I)
    {
        uint16_t Ch = Source[I];
        if (!Ch || Ch > 0x7fu || Ch == ':' || Ch < 0x20u)
            return Rc95LdrUnsupported;
        Destination[Out++] = (char)Ch;
    }
    Destination[Out] = 0;
    if (Destination[0] != '\\' && Destination[0] != '/')
        return Rc95LdrCorruptData;
    if (PathHasParentComponent(Destination))
        return Rc95LdrCorruptData;
    /* The filesystem adapter is volume-root-relative, not ARC/drive based. */
    while (Destination[0] == '\\' || Destination[0] == '/')
    {
        memmove(Destination, Destination + 1, strlen(Destination));
        --Out;
    }
    while (Out && (Destination[Out - 1u] == '\\' ||
                   Destination[Out - 1u] == '/'))
        Destination[--Out] = 0;
    if (!Destination[0]) return Rc95LdrCorruptData;
    return Rc95LdrSuccess;
}

static RC95LDR_STATUS
JoinSystemPath(const char *SystemRoot, const char *Suffix,
               char *Path, size_t Capacity)
{
    size_t A = strlen(SystemRoot), B = strlen(Suffix);
    if (A > Capacity || B > Capacity ||
        A + B + 2u > Capacity)
        return Rc95LdrBufferTooSmall;
    memcpy(Path, SystemRoot, A);
    if (A && Path[A - 1u] != '\\') Path[A++] = '\\';
    while (*Suffix == '\\' || *Suffix == '/') ++Suffix;
    B = strlen(Suffix);
    if (A + B + 1u > Capacity) return Rc95LdrBufferTooSmall;
    memcpy(Path + A, Suffix, B + 1u);
    return PathHasParentComponent(Path) ? Rc95LdrCorruptData : Rc95LdrSuccess;
}

static RC95LDR_STATUS
OpenBootFile(const RC95LDR_VOLUME *Volume, const char *Path,
             UEFI_BOOT_FILE *File)
{
    RC95LDR_STATUS Status;
    if (!Volume || !Volume->FileSystem || !Volume->FileSystem->OpenRead ||
        !Volume->FileSystem->ReadAt || !Volume->FileSystem->Close ||
        !Path || !File)
        return Rc95LdrInvalidParameter;
    memset(File, 0, sizeof(*File));
    Status = Volume->FileSystem->OpenRead(
        Volume->FileSystem->FileSystemContext, Path,
        &File->Handle, &File->Size);
    if (Status != Rc95LdrSuccess) return Status;
    if (!File->Handle || !File->Size)
    {
        if (File->Handle)
            Volume->FileSystem->Close(
                Volume->FileSystem->FileSystemContext, File->Handle);
        memset(File, 0, sizeof(*File));
        return Rc95LdrCorruptData;
    }
    return Rc95LdrSuccess;
}

static void
CloseBootFile(const RC95LDR_VOLUME *Volume, UEFI_BOOT_FILE *File)
{
    if (Volume && Volume->FileSystem && Volume->FileSystem->Close &&
        File && File->Handle)
        Volume->FileSystem->Close(
            Volume->FileSystem->FileSystemContext, File->Handle);
    if (File) memset(File, 0, sizeof(*File));
}

static RC95LDR_STATUS
ReadWholeBootFile(RC95LDR_UEFI_NATIVE_MOUNT_CONTEXT *Context,
                  const RC95LDR_VOLUME *Volume, const char *Path,
                  uint64_t Maximum, void **Bytes, size_t *Length)
{
    UEFI_BOOT_FILE File;
    RC95LDR_STATUS Status;
    void *Buffer = NULL;
    uint64_t FileSize = 0;
    if (Bytes) *Bytes = NULL;
    if (Length) *Length = 0;
    if (!Context || !Bytes || !Length) return Rc95LdrInvalidParameter;
    Status = OpenBootFile(Volume, Path, &File);
    if (Status != Rc95LdrSuccess) return Status;
    FileSize = File.Size;
    if (FileSize > Maximum || FileSize > (uint64_t)SIZE_MAX)
    {
        CloseBootFile(Volume, &File);
        return Rc95LdrUnsupported;
    }
    Status = Context->AllocatePool(Context->AllocatorContext,
        (size_t)FileSize, &Buffer);
    if (Status == Rc95LdrSuccess && !Buffer)
        Status = Rc95LdrOutOfMemory;
    if (Status == Rc95LdrSuccess)
        Status = Volume->FileSystem->ReadAt(
            Volume->FileSystem->FileSystemContext, File.Handle, 0,
            Buffer, (size_t)FileSize);
    CloseBootFile(Volume, &File);
    if (Status != Rc95LdrSuccess)
    {
        if (Buffer) Context->FreePool(Context->AllocatorContext, Buffer);
        return Status;
    }
    *Bytes = Buffer;
    *Length = (size_t)FileSize;
    return Rc95LdrSuccess;
}

static RC95LDR_STATUS
ValidatePePath(RC95LDR_UEFI_NATIVE_MOUNT_CONTEXT *Context,
               const RC95LDR_VOLUME *Volume, const char *Path,
               uint16_t Machine)
{
    void *Bytes = NULL;
    size_t Length = 0;
    RC95LDR_PE_INFO Info;
    RC95LDR_STATUS Status = ReadWholeBootFile(Context, Volume, Path,
        RC95LDR_UEFI_MAX_BOOT_IMAGE, &Bytes, &Length);
    if (Status == Rc95LdrSuccess)
    {
        Status = Rc95LdrPeValidate(Bytes, Length, Machine, &Info);
        if (Status == Rc95LdrSuccess && !(Info.Characteristics & 0x0002u))
            Status = Rc95LdrInvalidImage;
    }
    if (Bytes) Context->FreePool(Context->AllocatorContext, Bytes);
    return Status;
}

static RC95LDR_STATUS
ValidateSystemHivePath(RC95LDR_UEFI_NATIVE_MOUNT_CONTEXT *Context,
                       const RC95LDR_VOLUME *Volume, const char *Path)
{
    void *Bytes = NULL;
    size_t Length = 0;
    RC95LDR_SYSTEM_HIVE Hive;
    RC95LDR_HIVE_KEY ControlSet;
    RC95LDR_STATUS Status = ReadWholeBootFile(Context, Volume, Path,
        RC95LDR_UEFI_MAX_SYSTEM_HIVE, &Bytes, &Length);
    if (Status == Rc95LdrSuccess)
        Status = Rc95LdrOpenSystemHive(Bytes, Length, &Hive);
    if (Status == Rc95LdrSuccess)
        Status = Rc95LdrHiveResolveCurrentControlSet(&Hive, &ControlSet);
    if (Bytes) Context->FreePool(Context->AllocatorContext, Bytes);
    return Status;
}

static uint16_t
NativePeMachine(void)
{
#if defined(_M_IX86) || defined(__i386__)
    return RC95LDR_PE_MACHINE_I386;
#elif defined(_M_AMD64) || defined(_M_X64) || defined(_AMD64_) || \
      defined(__x86_64__) || defined(__amd64__)
    return RC95LDR_PE_MACHINE_AMD64;
#else
    return 0;
#endif
}

RC95LDR_STATUS
Rc95LdrUefiValidateNativeWindowsVolume(
    void *Opaque, const RC95LDR_VOLUME *Volume,
    const RC95LDR_WINDOWS_INSTALLATION *Installation)
{
    RC95LDR_UEFI_NATIVE_MOUNT_CONTEXT *Context =
        (RC95LDR_UEFI_NATIVE_MOUNT_CONTEXT *)Opaque;
    char LoaderPath[RC95LDR_UEFI_PATH_CAPACITY];
    char SystemRoot[RC95LDR_UEFI_PATH_CAPACITY];
    char Path[RC95LDR_UEFI_PATH_CAPACITY];
    uint16_t Machine = NativePeMachine();
    RC95LDR_STATUS Status;

    if (!Context || !Context->Initialized || !Volume ||
        !Volume->FileSystem || !Installation || !Machine ||
        !Installation->LoaderPathCharacters ||
        !Installation->SystemRootCharacters)
        return Rc95LdrInvalidParameter;

    Status = ConvertInstallationPath(Installation->LoaderPathUtf16,
        Installation->LoaderPathCharacters, LoaderPath, sizeof(LoaderPath));
    if (Status != Rc95LdrSuccess) return Status;
    Status = ConvertInstallationPath(Installation->SystemRootUtf16,
        Installation->SystemRootCharacters, SystemRoot, sizeof(SystemRoot));
    if (Status != Rc95LdrSuccess) return Status;

    /* The BCD loader must be inside the selected installation's SystemRoot. */
    {
        size_t I, RootLength = strlen(SystemRoot);
        if (strlen(LoaderPath) <= RootLength ||
            LoaderPath[RootLength] != '\\')
            return Rc95LdrCorruptData;
        for (I = 0; I < RootLength; ++I)
            if (!AsciiEqualInsensitive(LoaderPath[I], SystemRoot[I]))
                return Rc95LdrCorruptData;
    }

    {
        size_t LoaderLength = strlen(LoaderPath);
        static const char ExpectedLoader[] = "winload.efi";
        size_t I;
        if (LoaderLength <= sizeof(ExpectedLoader) - 1u ||
            LoaderPath[LoaderLength - (sizeof(ExpectedLoader) - 1u) - 1u] != '\\')
            return Rc95LdrCorruptData;
        for (I = 0; I < sizeof(ExpectedLoader) - 1u; ++I)
            if (!AsciiEqualInsensitive(
                    LoaderPath[LoaderLength - (sizeof(ExpectedLoader) - 1u) + I],
                    ExpectedLoader[I]))
                return Rc95LdrCorruptData;
    }
    Status = ValidatePePath(Context, Volume, LoaderPath, Machine);
    if (Status != Rc95LdrSuccess) return Status;

    Status = JoinSystemPath(SystemRoot, "System32\\ntoskrnl.exe",
        Path, sizeof(Path));
    if (Status != Rc95LdrSuccess) return Status;
    Status = ValidatePePath(Context, Volume, Path, Machine);
    if (Status != Rc95LdrSuccess) return Status;

    Status = JoinSystemPath(SystemRoot, "System32\\hal.dll",
        Path, sizeof(Path));
    if (Status != Rc95LdrSuccess) return Status;
    Status = ValidatePePath(Context, Volume, Path, Machine);
    if (Status != Rc95LdrSuccess) return Status;

    Status = JoinSystemPath(SystemRoot, "System32\\Config\\SYSTEM",
        Path, sizeof(Path));
    if (Status != Rc95LdrSuccess) return Status;
    return ValidateSystemHivePath(Context, Volume, Path);
}


RC95LDR_STATUS
Rc95LdrUefiAllocateLoaderPool(void *Opaque, size_t Bytes, void **Allocation)
{
    RC95LDR_UEFI_POOL_CONTEXT *Context =
        (RC95LDR_UEFI_POOL_CONTEXT *)Opaque;
    EFI_STATUS Status;
    if (Allocation) *Allocation = NULL;
    if (!Context || !Context->BootServices ||
        !Context->BootServices->AllocatePool || !Allocation || !Bytes)
        return Rc95LdrInvalidParameter;
    Status = Context->BootServices->AllocatePool(
        EfiLoaderData, (UINTN)Bytes, Allocation);
    if (EFI_ERROR(Status))
        return Status == EFI_OUT_OF_RESOURCES
            ? Rc95LdrOutOfMemory : Rc95LdrIoError;
    return *Allocation ? Rc95LdrSuccess : Rc95LdrOutOfMemory;
}

void
Rc95LdrUefiFreeLoaderPool(void *Opaque, void *Allocation)
{
    RC95LDR_UEFI_POOL_CONTEXT *Context =
        (RC95LDR_UEFI_POOL_CONTEXT *)Opaque;
    if (Context && Context->BootServices &&
        Context->BootServices->FreePool && Allocation)
        (void)Context->BootServices->FreePool(Allocation);
}

RC95LDR_STATUS
Rc95LdrUefiInitializeNativeMountsFromBootServices(
    RC95LDR_UEFI_NATIVE_MOUNT_CONTEXT *Context,
    const RC95LDR_UEFI_FIRMWARE_OPS *Firmware,
    EFI_BOOT_SERVICES *BootServices)
{
    RC95LDR_STATUS Status;
    if (!Context || !BootServices ||
        BootServices->Hdr.Signature != EFI_BOOT_SERVICES_SIGNATURE ||
        BootServices->Hdr.HeaderSize <
            offsetof(EFI_BOOT_SERVICES, AllocatePool) +
            sizeof(BootServices->AllocatePool) ||
        BootServices->Hdr.HeaderSize <
            offsetof(EFI_BOOT_SERVICES, FreePool) +
            sizeof(BootServices->FreePool) ||
        !BootServices->AllocatePool || !BootServices->FreePool)
        return Rc95LdrInvalidParameter;
    Status = Rc95LdrUefiInitializeNativeMounts(Context, Firmware,
        Rc95LdrUefiAllocateLoaderPool, Rc95LdrUefiFreeLoaderPool,
        &Context->PoolContext);
    if (Status != Rc95LdrSuccess) return Status;
    Context->PoolContext.BootServices = BootServices;
    Context->AllocatorContext = &Context->PoolContext;
    return Rc95LdrSuccess;
}
