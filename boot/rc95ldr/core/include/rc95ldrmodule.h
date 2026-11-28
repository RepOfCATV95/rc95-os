#ifndef _RC95LDR_MODULE_H_
#define _RC95LDR_MODULE_H_

#include "rc95ldr.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RC95LDR_MODULE_HANDOFF_VERSION 1u
#define RC95LDR_MODULE_SERVICES_VERSION 1u
#define RC95LDR_MODULE_SERVICES_SIGNATURE 0x53353952u /* R95S */
#define RC95LDR_MODULE_MAX_SERVICES_SIZE (64u * 1024u)
#define RC95LDR_MODULE_SIGNATURE 0x4d393552u /* R95M */
#define RC95LDR_MODULE_MAX_IMAGE_SIZE (256ull * 1024ull * 1024ull)
#define RC95LDR_MODULE_FLAG_FIRMWARE_MANAGED_ENTRY 0x00000001u
#define RC95LDR_MODULE_VALID_FLAGS RC95LDR_MODULE_FLAG_FIRMWARE_MANAGED_ENTRY

#if defined(_MSC_VER)
#define RC95LDR_MODULE_ABI __cdecl
#elif defined(__i386__)
#define RC95LDR_MODULE_ABI __attribute__((cdecl))
#else
#define RC95LDR_MODULE_ABI
#endif

typedef enum _RC95LDR_MODULE_KIND {
    Rc95LdrModuleRc95Ldr = 1,
    Rc95LdrModuleVeneer = 2,
    Rc95LdrModuleWinload = 3
} RC95LDR_MODULE_KIND;

typedef enum _RC95LDR_MODULE_FIRMWARE {
    Rc95LdrModuleBios = 1,
    Rc95LdrModuleUefi = 2
} RC95LDR_MODULE_FIRMWARE;

typedef enum _RC95LDR_MODULE_ARCH {
    Rc95LdrModuleI386 = 1,
    Rc95LdrModuleAmd64 = 2
} RC95LDR_MODULE_ARCH;

/*
 * Immutable handoff record. ImageBase is the mapped virtual address, while
 * ImageSize bounds every byte the receiving module may inspect. The loader
 * retains ownership of the image and services until Return() completes.
 */
typedef struct _RC95LDR_MODULE_HANDOFF {
    uint32_t Signature;
    uint32_t StructureSize;
    uint32_t Version;
    uint32_t ModuleKind;
    uint32_t Firmware;
    uint32_t Architecture;
    uint32_t Flags;
    uint32_t Reserved;
    uint64_t ImageBase;
    uint64_t ImageSize;
    uint64_t EntryPoint;
    uint64_t Services;
    uint64_t ServicesSize;
    uint64_t ParentHandoff;
    uint64_t ParentHandoffSize;
} RC95LDR_MODULE_HANDOFF;

/* Stable layout checks: all consumers compile against this exact wire ABI. */
typedef char RC95LDR_MODULE_HANDOFF_SIZE_MUST_BE_88[(sizeof(RC95LDR_MODULE_HANDOFF) == 88u) ? 1 : -1];
typedef char RC95LDR_MODULE_HANDOFF_ENTRY_OFFSET_MUST_BE_48[(offsetof(RC95LDR_MODULE_HANDOFF, EntryPoint) == 48u) ? 1 : -1];


typedef struct _RC95LDR_MODULE_FILE {
    void *FileContext;
    uint64_t FileSize;
} RC95LDR_MODULE_FILE;

/* Provider-owned synchronous services; Context and callback resources remain
 * valid through child return or the final handoff. Quiesce closes firmware
 * handles before ExitBootServices without freeing child-owned page allocations. */
typedef struct _RC95LDR_MODULE_SERVICES {
    uint32_t Signature, StructureSize, Version, Firmware;
    uint32_t Architecture, Flags;
    void *Context;
    RC95LDR_STATUS (RC95LDR_MODULE_ABI *OpenFile)(
        void *, const uint16_t *, RC95LDR_MODULE_FILE *);
    RC95LDR_STATUS (RC95LDR_MODULE_ABI *ReadFileAt)(
        void *, const RC95LDR_MODULE_FILE *, uint64_t, void *, size_t);
    void (RC95LDR_MODULE_ABI *CloseFile)(void *, RC95LDR_MODULE_FILE *);
    RC95LDR_STATUS (RC95LDR_MODULE_ABI *AllocatePages)(
        void *, uint64_t, uint64_t, uint64_t *);
    void (RC95LDR_MODULE_ABI *FreePages)(void *, uint64_t, uint64_t);
    RC95LDR_STATUS (RC95LDR_MODULE_ABI *ValidateAllocationExtent)(
        void *, uint64_t, uint64_t, uint64_t, uint64_t);
    /* Quiesces firmware-owned handles before the final handoff/ExitBootServices. */
    RC95LDR_STATUS (RC95LDR_MODULE_ABI *Quiesce)(void *);
} RC95LDR_MODULE_SERVICES;

typedef char RC95LDR_MODULE_SERVICES_SIZE_CHECK[
    (sizeof(RC95LDR_MODULE_SERVICES) <= RC95LDR_MODULE_MAX_SERVICES_SIZE) ? 1 : -1];
#if UINTPTR_MAX == UINT64_MAX
typedef char RC95LDR_MODULE_SERVICES_AMD64_SIZE_CHECK[
    (sizeof(RC95LDR_MODULE_SERVICES) == 88u &&
     offsetof(RC95LDR_MODULE_SERVICES, Context) == 24u &&
     offsetof(RC95LDR_MODULE_SERVICES, OpenFile) == 32u) ? 1 : -1];
#else
typedef char RC95LDR_MODULE_SERVICES_I386_SIZE_CHECK[
    (sizeof(RC95LDR_MODULE_SERVICES) == 56u &&
     offsetof(RC95LDR_MODULE_SERVICES, Context) == 24u &&
     offsetof(RC95LDR_MODULE_SERVICES, OpenFile) == 28u) ? 1 : -1];
#endif


/*
 * Module-services ABI V2 is additive. The complete V1 table is the first
 * member, preserving every V1 field offset and callback signature. V2 adds
 * synchronous, read-only block-device and partition discovery/read services.
 * V1 producers and consumers continue to use RC95LDR_MODULE_SERVICES only.
 */
#define RC95LDR_MODULE_SERVICES_V2_VERSION 2u
#define RC95LDR_MODULE_BLOCK_DEVICE_INFO_VERSION 1u
#define RC95LDR_MODULE_PARTITION_INFO_VERSION 1u
#define RC95LDR_MODULE_BLOCK_DEVICE_READ_ONLY 0x00000001u
#define RC95LDR_MODULE_BLOCK_DEVICE_VALID_FLAGS RC95LDR_MODULE_BLOCK_DEVICE_READ_ONLY
#define RC95LDR_MODULE_PARTITION_GPT 1u
#define RC95LDR_MODULE_PARTITION_MBR 2u

typedef struct _RC95LDR_MODULE_BLOCK_DEVICE_INFO {
    uint32_t StructureSize;
    uint32_t Version;
    uint64_t BlockCount;
    uint32_t BlockSize;
    uint32_t Flags;
    uint64_t DeviceId;
    uint64_t Reserved;
} RC95LDR_MODULE_BLOCK_DEVICE_INFO;

typedef struct _RC95LDR_MODULE_PARTITION_INFO {
    uint32_t StructureSize;
    uint32_t Version;
    uint32_t Scheme;
    uint32_t Flags;
    uint64_t StartLba;
    uint64_t BlockCount;
    uint8_t TypeGuid[16];
    uint8_t UniqueGuid[16];
    uint8_t MbrType;
    uint8_t Reserved[7];
} RC95LDR_MODULE_PARTITION_INFO;

typedef struct _RC95LDR_MODULE_SERVICES_V2 {
    RC95LDR_MODULE_SERVICES V1;
    RC95LDR_STATUS (RC95LDR_MODULE_ABI *GetBlockDeviceCount)(
        void *, uint32_t *);
    RC95LDR_STATUS (RC95LDR_MODULE_ABI *GetBlockDeviceInfo)(
        void *, uint32_t, RC95LDR_MODULE_BLOCK_DEVICE_INFO *);
    RC95LDR_STATUS (RC95LDR_MODULE_ABI *GetPartitionCount)(
        void *, uint32_t, uint32_t *);
    RC95LDR_STATUS (RC95LDR_MODULE_ABI *GetPartitionInfo)(
        void *, uint32_t, uint32_t, RC95LDR_MODULE_PARTITION_INFO *);
    RC95LDR_STATUS (RC95LDR_MODULE_ABI *ReadBlocks)(
        void *, uint32_t, uint64_t, uint32_t, void *, size_t);
} RC95LDR_MODULE_SERVICES_V2;

#if UINTPTR_MAX == UINT64_MAX
typedef char RC95LDR_MODULE_SERVICES_V2_PREFIX_OFFSET_CHECK[
    (offsetof(RC95LDR_MODULE_SERVICES_V2, V1) == 0u &&
     offsetof(RC95LDR_MODULE_SERVICES_V2, GetBlockDeviceCount) == 88u) ? 1 : -1];
#else
typedef char RC95LDR_MODULE_SERVICES_V2_PREFIX_OFFSET_CHECK[
    (offsetof(RC95LDR_MODULE_SERVICES_V2, V1) == 0u &&
     offsetof(RC95LDR_MODULE_SERVICES_V2, GetBlockDeviceCount) == 56u) ? 1 : -1];
#endif
typedef char RC95LDR_MODULE_BLOCK_DEVICE_INFO_SIZE_CHECK[
    (sizeof(RC95LDR_MODULE_BLOCK_DEVICE_INFO) == 40u) ? 1 : -1];
typedef char RC95LDR_MODULE_PARTITION_INFO_SIZE_CHECK[
    (sizeof(RC95LDR_MODULE_PARTITION_INFO) == 72u) ? 1 : -1];

RC95LDR_STATUS Rc95LdrValidateModuleServicesV2(
    const RC95LDR_MODULE_SERVICES_V2 *Services, size_t AvailableBytes,
    uint32_t ExpectedFirmware, uint32_t ExpectedArchitecture);
RC95LDR_STATUS Rc95LdrValidateModuleBlockDeviceInfo(
    const RC95LDR_MODULE_BLOCK_DEVICE_INFO *Info, size_t AvailableBytes);
RC95LDR_STATUS Rc95LdrValidateModulePartitionInfo(
    const RC95LDR_MODULE_PARTITION_INFO *Info, size_t AvailableBytes,
    const RC95LDR_MODULE_BLOCK_DEVICE_INFO *Device);

RC95LDR_STATUS Rc95LdrValidateModuleServices(
    const RC95LDR_MODULE_SERVICES *Services, size_t AvailableBytes,
    uint32_t ExpectedFirmware, uint32_t ExpectedArchitecture);

typedef RC95LDR_STATUS (RC95LDR_MODULE_ABI *RC95LDR_MODULE_ENTRY)(
    const RC95LDR_MODULE_HANDOFF *Handoff);
typedef RC95LDR_STATUS (RC95LDR_MODULE_ABI *RC95LDR_MODULE_TRANSFER)(
    void *Context, uint64_t EntryPoint,
    const RC95LDR_MODULE_HANDOFF *Handoff);

RC95LDR_STATUS Rc95LdrValidateModuleHandoff(
    const RC95LDR_MODULE_HANDOFF *Handoff,
    size_t AvailableBytes,
    uint32_t ExpectedKind,
    uint32_t ExpectedFirmware,
    uint32_t ExpectedArchitecture);

RC95LDR_STATUS Rc95LdrInvokeModule(
    const RC95LDR_MODULE_HANDOFF *Handoff,
    size_t AvailableBytes,
    uint32_t ExpectedKind,
    uint32_t ExpectedFirmware,
    uint32_t ExpectedArchitecture,
    RC95LDR_MODULE_TRANSFER Transfer,
    void *TransferContext);

#ifdef __cplusplus
}
#endif
#endif
