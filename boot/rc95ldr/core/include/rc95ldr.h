#ifndef _RC95LDR_H_
#define _RC95LDR_H_
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define RC95LDR_ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))
typedef enum _RC95LDR_STATUS { Rc95LdrSuccess=0, Rc95LdrInvalidParameter, Rc95LdrInvalidImage, Rc95LdrUnsupported, Rc95LdrNotFound, Rc95LdrBufferTooSmall, Rc95LdrIntegerOverflow, Rc95LdrCorruptData, Rc95LdrOutOfBounds, Rc95LdrIoError, Rc95LdrOutOfMemory } RC95LDR_STATUS;
typedef enum _RC95LDR_STAGE { Rc95LdrStageNone=0, Rc95LdrStageStartup, Rc95LdrStagePlatform, Rc95LdrStageBlockIo, Rc95LdrStagePartition, Rc95LdrStageBcd, Rc95LdrStageFilesystem, Rc95LdrStageNtfs, Rc95LdrStageImageLoad, Rc95LdrStageOsPrepare, Rc95LdrStageHandoff } RC95LDR_STAGE;
/* Stable detail codes shared by BIOS, UEFI, VENEER and WINLOAD diagnostics. */
#define RC95LDR_DETAIL_BTRFS_PROVIDER_UNAVAILABLE 0x42544653u /* BTFS */
#define RC95LDR_DETAIL_INVALID_FILESYSTEM_PROVIDER 0x46535953u /* FSYS */
typedef struct _RC95LDR_ERROR { RC95LDR_STAGE Stage; RC95LDR_STATUS Status; uint32_t Detail; } RC95LDR_ERROR;
typedef struct _RC95LDR_BOOT_CONTEXT { uint32_t StructureSize; uint32_t Version; RC95LDR_STAGE CurrentStage; RC95LDR_ERROR LastError; void *PlatformContext; } RC95LDR_BOOT_CONTEXT;
typedef struct _RC95LDR_MEMORY_VIEW { const uint8_t *Base; size_t Length; } RC95LDR_MEMORY_VIEW;
typedef RC95LDR_STATUS (*RC95LDR_BLOCK_READ)(void*,uint64_t,uint32_t,void*,size_t);
typedef RC95LDR_STATUS (*RC95LDR_BLOCK_WRITE)(void*,uint64_t,uint32_t,const void*,size_t);
/* Write is optional. A missing callback means the device is strictly read-only. */
typedef struct _RC95LDR_BLOCK_DEVICE { void *DeviceContext; uint64_t BlockCount; uint32_t BlockSize; RC95LDR_BLOCK_READ Read; RC95LDR_BLOCK_WRITE Write; } RC95LDR_BLOCK_DEVICE;
typedef RC95LDR_STATUS (*RC95LDR_FS_OPEN_READ)(void*,const char*,void**,uint64_t*);
typedef RC95LDR_STATUS (*RC95LDR_FS_READ_AT)(void*,void*,uint64_t,void*,size_t);
typedef RC95LDR_STATUS (*RC95LDR_FS_DELETE_FILE)(void*,const char*);
typedef void (*RC95LDR_FS_CLOSE)(void*,void*);
typedef struct _RC95LDR_FILESYSTEM { void *FileSystemContext; RC95LDR_FS_OPEN_READ OpenRead; RC95LDR_FS_READ_AT ReadAt; RC95LDR_FS_CLOSE Close; RC95LDR_FS_DELETE_FILE DeleteFile; } RC95LDR_FILESYSTEM;
/* Filesystem adapter owns partition/volume translation and reads through the associated device. */
typedef struct _RC95LDR_VOLUME { const RC95LDR_BLOCK_DEVICE *BlockDevice; const RC95LDR_FILESYSTEM *FileSystem; } RC95LDR_VOLUME;

/* Loader-scoped, read-only file access. The provider owns volume translation;
 * callers never receive or construct an ARC/default-volume path. */
typedef struct _RC95LDR_FILE_INFORMATION
{
    uint64_t Size;
    uint32_t Attributes;
} RC95LDR_FILE_INFORMATION;
typedef RC95LDR_STATUS (*RC95LDR_FILE_OPEN)(void*, const char*, void**);
typedef RC95LDR_STATUS (*RC95LDR_FILE_GET_INFORMATION)(void*, void*, RC95LDR_FILE_INFORMATION*);
typedef RC95LDR_STATUS (*RC95LDR_FILE_READ)(void*, void*, void*, size_t, size_t*);
typedef RC95LDR_STATUS (*RC95LDR_FILE_SEEK)(void*, void*, uint64_t);
typedef void (*RC95LDR_FILE_CLOSE)(void*, void*);
typedef struct _RC95LDR_FILE_PROVIDER
{
    uint32_t StructureSize;
    void *VolumeContext;
    RC95LDR_FILE_OPEN Open;
    RC95LDR_FILE_GET_INFORMATION GetFileInformation;
    RC95LDR_FILE_READ Read;
    RC95LDR_FILE_SEEK Seek;
    RC95LDR_FILE_CLOSE Close;
} RC95LDR_FILE_PROVIDER;
typedef struct _RC95LDR_FILE_PROVIDER_SCOPE
{
    const RC95LDR_FILE_PROVIDER *Previous;
    const RC95LDR_FILE_PROVIDER *Installed;
    uint32_t Active;
} RC95LDR_FILE_PROVIDER_SCOPE;
typedef RC95LDR_STATUS (*RC95LDR_SCOPED_OPERATION)(void*);
RC95LDR_STATUS Rc95LdrBeginFileProviderScope(const RC95LDR_FILE_PROVIDER*, RC95LDR_FILE_PROVIDER_SCOPE*);
RC95LDR_STATUS Rc95LdrEndFileProviderScope(RC95LDR_FILE_PROVIDER_SCOPE*);
const RC95LDR_FILE_PROVIDER *Rc95LdrGetFileProvider(void);
/* Filesystem driver identity is volume metadata, not a file-provider operation. */
RC95LDR_STATUS Rc95LdrSetFileSystemDriverName(const RC95LDR_FILE_PROVIDER*, const uint16_t*);
const uint16_t *Rc95LdrGetFileSystemDriverName(void);
RC95LDR_STATUS Rc95LdrRunWithFileProvider(const RC95LDR_FILE_PROVIDER*, RC95LDR_SCOPED_OPERATION, void*);
void Rc95LdrInitializeContext(RC95LDR_BOOT_CONTEXT*,void*);
void Rc95LdrSetStage(RC95LDR_BOOT_CONTEXT*,RC95LDR_STAGE);
RC95LDR_STATUS Rc95LdrFail(RC95LDR_BOOT_CONTEXT*,RC95LDR_STATUS,uint32_t);
int Rc95LdrRangeValid(size_t,size_t,size_t);
int Rc95LdrAddSize(size_t,size_t,size_t*);
int Rc95LdrMultiplySize(size_t,size_t,size_t*);
uint16_t Rc95LdrReadU16(const void*);
uint32_t Rc95LdrReadU32(const void*);
uint64_t Rc95LdrReadU64(const void*);
RC95LDR_STATUS Rc95LdrReadBlocks(const RC95LDR_BLOCK_DEVICE*,uint64_t,uint32_t,void*,size_t);
RC95LDR_STATUS Rc95LdrWriteBlocks(const RC95LDR_BLOCK_DEVICE*,uint64_t,uint32_t,const void*,size_t);
#ifdef __cplusplus
}
#endif
#endif
