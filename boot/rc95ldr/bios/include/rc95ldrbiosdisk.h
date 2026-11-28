#ifndef _RC95LDR_BIOS_DISK_H_
#define _RC95LDR_BIOS_DISK_H_
#include "rc95ldrbios.h"
#include "rc95ldrpolicy.h"
#include "rc95ldr_fat32.h"
#include "rc95ldr_ntfs_volume.h"
#include "rc95ldr_btrfs.h"
#include "rc95ldr_volumeprovider.h"
#include "rc95ldrtextui.h"
#include "rc95bcdwalk.h"
#ifdef __cplusplus
extern "C" {
#endif

#define RC95LDR_BIOS_PARTITION_MAX 128u

typedef struct _RC95LDR_ELTORITO_BOOT_INFO {
    uint8_t DriveNumber;
    uint8_t MediaType; /* 0 = no emulation; 1..4 = floppy/hard-disk emulation */
    uint16_t Reserved;
    uint32_t BootCatalogLba;
    uint32_t LoadRba;
    uint16_t LoadSegment;
    uint16_t SectorCount;
} RC95LDR_ELTORITO_BOOT_INFO;

/* Query INT 13h AH=4Bh, AL=01h to identify a BIOS-provided El Torito boot.
 * This does not parse ISO9660 or load files from the optical medium. */
RC95LDR_STATUS Rc95LdrBiosQueryElToritoBoot(
    const RC95LDR_BIOS_SERVICES *Services, uint8_t Drive,
    RC95LDR_ELTORITO_BOOT_INFO *BootInfo);
#define RC95LDR_BIOS_IMAGE_SIGNATURE 0x49353942u /* B95I */
#define RC95LDR_BIOS_IMAGE_VERSION 1u
#define RC95LDR_BIOS_IMAGE_MAX_SEGMENTS 16u

#pragma pack(push, 1)
typedef struct _RC95LDR_BIOS_IMAGE_SEGMENT {
    uint64_t Lba;
    uint64_t ByteLength;
    uint64_t LoadAddress;
    uint32_t Crc32;
    uint32_t Flags;
} RC95LDR_BIOS_IMAGE_SEGMENT;
typedef struct _RC95LDR_BIOS_IMAGE_MANIFEST {
    uint32_t Signature;
    uint16_t Version;
    uint16_t HeaderSize;
    uint32_t SegmentCount;
    uint32_t Flags;
    uint64_t ImageEntry;
    RC95LDR_BIOS_IMAGE_SEGMENT Segments[RC95LDR_BIOS_IMAGE_MAX_SEGMENTS];
} RC95LDR_BIOS_IMAGE_MANIFEST;
#pragma pack(pop)

typedef struct _RC95LDR_BIOS_DISK_DISCOVERY {
    const RC95LDR_BIOS_BLOCK_CONTEXT *BlockContext;
    const RC95LDR_BIOS_DISK *Disk;
    RC95LDR_BLOCK_DEVICE Device;
    uint8_t *Scratch;
    size_t ScratchLength;
} RC95LDR_BIOS_DISK_DISCOVERY;

typedef struct _RC95LDR_BIOS_FAT32_VOLUME {
    RC95LDR_FAT32 Fat32;
    RC95LDR_VOLUME Volume;
} RC95LDR_BIOS_FAT32_VOLUME;

typedef struct _RC95LDR_BIOS_BTRFS_VOLUME {
    RC95LDR_BTRFS_VOLUME Btrfs;
    RC95LDR_BTRFS_FILESYSTEM_CONTEXT FileSystemContext;
    RC95LDR_FILESYSTEM FileSystem;
    RC95LDR_VOLUME Volume;
    RC95LDR_VOLUME_PROVIDER_CONTEXT ProviderContext;
    RC95LDR_FILE_PROVIDER Provider;
    RC95LDR_FILE_PROVIDER_SCOPE ProviderScope;
    uint32_t ProviderScopeActive;
} RC95LDR_BIOS_BTRFS_VOLUME;

RC95LDR_STATUS Rc95LdrBiosValidateManifest(
    const RC95LDR_BIOS_DISK *Disk,
    uint64_t ManifestLba,
    const RC95LDR_MEMORY_RESERVATION_SET *Reservations,
    const RC95LDR_BIOS_IMAGE_MANIFEST *Manifest);
RC95LDR_STATUS Rc95LdrBiosLoadManifestImage(
    const RC95LDR_BIOS_SERVICES *Services,
    const RC95LDR_BIOS_DISK *Disk,
    uint64_t ManifestLba,
    const RC95LDR_MEMORY_RESERVATION_SET *Reservations,
    RC95LDR_BIOS_IMAGE_MANIFEST *Manifest);
RC95LDR_STATUS Rc95LdrBiosEnumeratePartitions(
    void *Context, RC95LDR_PARTITION_INFO *Partitions,
    size_t Capacity, size_t *Count);
RC95LDR_STATUS Rc95LdrBiosMountFat32(
    RC95LDR_BIOS_DISK_DISCOVERY *Discovery,
    const RC95LDR_PARTITION_INFO *Partition,
    RC95LDR_BIOS_FAT32_VOLUME *Mounted);
void Rc95LdrBiosUnmountFat32(RC95LDR_BIOS_FAT32_VOLUME *Mounted);

/* Mount the exact discovered Btrfs partition read-only, load its chunk and
 * default filesystem trees, and bind the native file provider. All work
 * buffers are caller-owned and must outlive the mount. */
RC95LDR_STATUS Rc95LdrBiosMountBtrfs(
    const RC95LDR_BIOS_DISK_DISCOVERY *Discovery,
    const RC95LDR_PARTITION_INFO *Partition,
    void *ItemBuffer, size_t ItemBufferLength,
    void *InlineBuffer, size_t InlineBufferLength,
    void *StagingBuffer, size_t StagingBufferLength,
    RC95LDR_BIOS_BTRFS_VOLUME *Mounted);
void Rc95LdrBiosUnmountBtrfs(RC95LDR_BIOS_BTRFS_VOLUME *Mounted);
/* Activate the mounted Btrfs volume as WINLOAD's scoped read-only provider.
 * The mount, provider context and scope must remain alive through WINLOAD. */
RC95LDR_STATUS Rc95LdrBiosBeginBtrfsFileProvider(
    RC95LDR_BIOS_BTRFS_VOLUME *Mounted);
RC95LDR_STATUS Rc95LdrBiosEndBtrfsFileProvider(
    RC95LDR_BIOS_BTRFS_VOLUME *Mounted);

typedef RC95LDR_STATUS (*RC95LDR_BIOS_VALIDATE_WINDOWS_VOLUME)(
    void *Context, const RC95LDR_NTFS_VOLUME *Volume,
    const RC95LDR_WINDOWS_INSTALLATION *Installation);

typedef struct _RC95LDR_BIOS_NTFS_VALIDATION_CONTEXT {
    void *Scratch;
    size_t ScratchLength;
    void *ProbeBuffer;
    size_t ProbeCapacity;
} RC95LDR_BIOS_NTFS_VALIDATION_CONTEXT;

/* Built-in BIOS-side validator. It verifies the selected loader and
 * ntoskrnl.exe as PE images and checks the SYSTEM hive base-block signature.
 * Context buffers are caller-owned and must outlive the callback. */
RC95LDR_STATUS Rc95LdrBiosValidateWindowsVolume(
    void *Context, const RC95LDR_NTFS_VOLUME *Volume,
    const RC95LDR_WINDOWS_INSTALLATION *Installation);

typedef RC95LDR_STATUS (*RC95LDR_BIOS_VALIDATE_BTRFS_WINDOWS_VOLUME)(
    void *Context, const RC95LDR_VOLUME *Volume,
    const RC95LDR_WINDOWS_INSTALLATION *Installation);

typedef struct _RC95LDR_BIOS_BTRFS_VALIDATION_CONTEXT {
    void *ProbeBuffer;
    size_t ProbeCapacity;
} RC95LDR_BIOS_BTRFS_VALIDATION_CONTEXT;

/* Built-in read-only Btrfs validator. Checks the selected loader and
 * ntoskrnl.exe as PE images and verifies the SYSTEM hive base-block signature.
 * The probe buffer is caller-owned and must remain valid for the call. */
RC95LDR_STATUS Rc95LdrBiosValidateBtrfsWindowsVolume(
    void *Context, const RC95LDR_VOLUME *Volume,
    const RC95LDR_WINDOWS_INSTALLATION *Installation);

typedef RC95LDR_STATUS (*RC95LDR_BIOS_SELECT_BOOT_CANDIDATE)(
    void *Context, const RC95BCD_HIVE *Hive,
    const RC95LDR_BCD_BOOT_CANDIDATE *Candidates, size_t CandidateCount,
    const RC95LDR_BCD_BOOT_CANDIDATE **SelectedCandidate);

typedef struct _RC95LDR_BIOS_DISCOVERY_OPS {
    void *Context;
    /* Optional override. NULL selects Rc95LdrBiosValidateWindowsVolume and
       interprets Context as RC95LDR_BIOS_NTFS_VALIDATION_CONTEXT. */
    RC95LDR_BIOS_VALIDATE_WINDOWS_VOLUME ValidateWindowsVolume;
    const RC95LDR_TEXTUI *TextUi;
    /* Optional Btrfs output. When supplied with the validation callback and
       all three workspaces, BCD discovery may return a mounted Btrfs volume
       with its WINLOAD provider scope active. Caller must End then Unmount. */
    RC95LDR_BIOS_BTRFS_VOLUME *BtrfsWindowsVolume;
    void *BtrfsItemBuffer;
    size_t BtrfsItemBufferLength;
    void *BtrfsInlineBuffer;
    size_t BtrfsInlineBufferLength;
    void *BtrfsStagingBuffer;
    size_t BtrfsStagingBufferLength;
    RC95LDR_BIOS_VALIDATE_BTRFS_WINDOWS_VOLUME ValidateBtrfsWindowsVolume;
    /* Optional interactive selection. When supplied, only the selected
       candidate is validated/mounted; discovery never silently substitutes
       another installation if that explicit choice fails. */
    void *SelectionContext;
    RC95LDR_BIOS_SELECT_BOOT_CANDIDATE SelectBootCandidate;
} RC95LDR_BIOS_DISCOVERY_OPS;

/* On success either WindowsVolume (NTFS) remains mounted, or the optional
 * Ops->BtrfsWindowsVolume remains mounted with its WINLOAD provider scope
 * active. The Btrfs result is available only when all Btrfs workspaces and
 * ValidateBtrfsWindowsVolume are supplied. Caller must end the Btrfs provider
 * scope before unmounting. The BCD hive is read from FAT32 boot/system volumes
 * and shared BCD policy resolves exact partition identities. */
RC95LDR_STATUS Rc95LdrDiscoverBiosWindows(
    RC95LDR_BIOS_DISK_DISCOVERY *Discovery,
    const RC95LDR_BIOS_DISCOVERY_OPS *Ops,
    RC95LDR_PARTITION_INFO *Partitions, size_t PartitionCapacity,
    RC95LDR_BCD_BOOT_CANDIDATE *Candidates, size_t CandidateCapacity,
    size_t *CandidateCountOut, void *HiveBuffer, size_t HiveCapacity,
    RC95LDR_WINDOWS_INSTALLATION *Installation,
    RC95LDR_NTFS_VOLUME *WindowsVolume, size_t *BcdPartitionIndex);

void Rc95LdrBiosUnmountNtfs(RC95LDR_NTFS_VOLUME *Mounted);
RC95LDR_STATUS Rc95LdrBiosMountNtfs(
    const RC95LDR_BIOS_DISK_DISCOVERY *Discovery,
    const RC95LDR_PARTITION_INFO *Partition,
    RC95LDR_NTFS_VOLUME *Mounted);

#ifdef __cplusplus
}
#endif
#endif
