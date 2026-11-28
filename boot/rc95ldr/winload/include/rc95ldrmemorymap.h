#ifndef _RC95LDR_MEMORYMAP_H_
#define _RC95LDR_MEMORYMAP_H_
#include "rc95ldrloaderblock.h"
#include <ntdef.h>
/*
 * arc.h is shared with the NT loader/kernel and intentionally consumes
 * PFN_NUMBER from the memory-manager type headers. RC95LDR's freestanding
 * winload policy target only needs the ARC descriptor layouts, so provide
 * the native pointer-sized PFN type when the DDK memory headers have not
 * already supplied it. Do not change the shared ARC header for this target.
 */
/* Same native-width definition used by the SDK memory-manager headers.
 * Repeating the typedef is harmless when a DDK header already supplied it. */
typedef ULONG_PTR PFN_NUMBER;
#include <reactos/arc/arc.h>
#ifdef __cplusplus
extern "C" {
#endif
#define RC95LDR_MEMORYMAP_ABI_VERSION 1u
#define RC95LDR_MEMORYMAP_PAGE_SIZE 4096u
#define RC95LDR_MEMORYMAP_MAX_RAW 256u
#define RC95LDR_MEMORYMAP_MAX_RESERVATIONS 128u
#define RC95LDR_NATIVE_ALLOCATION_VERSION 1u
#define RC95LDR_ALLOC_REGISTRY       1u
#define RC95LDR_ALLOC_NLS            2u
#define RC95LDR_ALLOC_LOADER_HEAP    3u
#define RC95LDR_ALLOC_LOADER_STACK   4u
#define RC95LDR_ALLOC_PAGE_TABLES    5u
#define RC95LDR_ALLOC_UEFI_MAP       6u
#define RC95LDR_ALLOC_LOADER_BLOCK  7u
#define RC95LDR_ALLOC_METADATA      8u
#define RC95LDR_ALLOC_RAMDISK        9u
#define RC95LDR_ALLOC_BIOS_FIRMWARE_STATE 10u

typedef struct _RC95LDR_NATIVE_ALLOCATION {
    uint32_t Kind;
    TYPE_OF_MEMORY MemoryType;
    uint64_t PhysicalBase, Length;
} RC95LDR_NATIVE_ALLOCATION;
typedef struct _RC95LDR_NATIVE_ALLOCATION_SET {
    size_t Count;
    RC95LDR_NATIVE_ALLOCATION Entries[RC95LDR_MEMORYMAP_MAX_RESERVATIONS];
} RC95LDR_NATIVE_ALLOCATION_SET;
RC95LDR_STATUS Rc95LdrTrackNativeAllocation(
    RC95LDR_NATIVE_ALLOCATION_SET *Set, uint32_t Kind,
    uint64_t PhysicalBase, uint64_t Length);

typedef enum _RC95LDR_FIRMWARE_MEMORY_KIND {
    Rc95LdrFirmwareMemoryAvailable = 1,
    Rc95LdrFirmwareMemoryReserved,
    Rc95LdrFirmwareMemoryUnusable,
    Rc95LdrFirmwareMemoryAcpiReclaim,
    Rc95LdrFirmwareMemoryAcpiNvs,
    Rc95LdrFirmwareMemoryRuntime,
    Rc95LdrFirmwareMemoryLoaderTemporary
} RC95LDR_FIRMWARE_MEMORY_KIND;

typedef struct _RC95LDR_FIRMWARE_MEMORY_RANGE {
    uint64_t Base, Length;
    uint32_t Kind, Attributes;
} RC95LDR_FIRMWARE_MEMORY_RANGE;

typedef struct _RC95LDR_MEMORY_RESERVATION {
    uint64_t Base, Length;
    TYPE_OF_MEMORY MemoryType;
} RC95LDR_MEMORY_RESERVATION;

typedef struct _RC95LDR_MEMORY_RESERVATION_SET {
    size_t Count;
    RC95LDR_MEMORY_RESERVATION Entries[RC95LDR_MEMORYMAP_MAX_RESERVATIONS];
} RC95LDR_MEMORY_RESERVATION_SET;

RC95LDR_STATUS Rc95LdrRegisterNativeAllocations(
    const RC95LDR_LOADED_MODULE *Modules, size_t ModuleCount,
    const RC95LDR_NATIVE_ALLOCATION *Allocations, size_t AllocationCount,
    int FirmwareIsUefi, RC95LDR_MEMORY_RESERVATION_SET *Reservations);
RC95LDR_STATUS Rc95LdrReserveMemoryRange(
    RC95LDR_MEMORY_RESERVATION_SET *Set, uint64_t Base, uint64_t Length,
    TYPE_OF_MEMORY MemoryType);

typedef RC95LDR_STATUS (*RC95LDR_BIOS_E820_NEXT)(
    void *Context, uint32_t *Continuation, RC95LDR_FIRMWARE_MEMORY_RANGE *Range,
    int *Done);
typedef struct _RC95LDR_BIOS_MEMORY_OPS {
    void *Context;
    RC95LDR_BIOS_E820_NEXT E820Next;
} RC95LDR_BIOS_MEMORY_OPS;

typedef RC95LDR_STATUS (*RC95LDR_UEFI_GET_MEMORY_MAP)(
    void *Context, void *Buffer, size_t *BufferSize, size_t *DescriptorSize,
    uint64_t *MapKey, uint32_t *DescriptorVersion);
typedef RC95LDR_STATUS (*RC95LDR_UEFI_EXIT_BOOT_SERVICES)(
    void *Context, uint64_t ImageHandle, uint64_t MapKey);
typedef struct _RC95LDR_UEFI_MEMORY_OPS {
    void *Context;
    uint64_t ImageHandle;
    RC95LDR_UEFI_GET_MEMORY_MAP GetMemoryMap;
    RC95LDR_UEFI_EXIT_BOOT_SERVICES ExitBootServices;
} RC95LDR_UEFI_MEMORY_OPS;

typedef struct _RC95LDR_MEMORYMAP_RESULT {
    uint32_t StructureSize, Version;
    uint32_t RangeCount, Reserved;
    RC95LDR_MEMORY_RANGE Ranges[RC95LDR_MEMORY_MAX_RANGES];
} RC95LDR_MEMORYMAP_RESULT;

RC95LDR_STATUS Rc95LdrCollectBiosE820(
    const RC95LDR_BIOS_MEMORY_OPS *Ops,
    RC95LDR_FIRMWARE_MEMORY_RANGE *Ranges, size_t Capacity, size_t *Count);
RC95LDR_STATUS Rc95LdrCollectUefiMemoryMap(
    const RC95LDR_UEFI_MEMORY_OPS *Ops, void *MapBuffer, size_t MapCapacity,
    RC95LDR_FIRMWARE_MEMORY_RANGE *Ranges, size_t Capacity, size_t *Count,
    size_t *RawMapSize, size_t *RawDescriptorSize, uint64_t *MapKey,
    uint32_t *DescriptorVersion);
RC95LDR_STATUS Rc95LdrBuildFinalMemoryMap(
    const RC95LDR_FIRMWARE_MEMORY_RANGE *FirmwareRanges, size_t FirmwareCount,
    const RC95LDR_MEMORY_RESERVATION *Reservations, size_t ReservationCount,
    RC95LDR_MEMORYMAP_RESULT *Result);
RC95LDR_STATUS Rc95LdrBuildArcMemoryDescriptorList(
    const RC95LDR_MEMORYMAP_RESULT *Map,
    MEMORY_ALLOCATION_DESCRIPTOR *Descriptors, size_t Capacity,
    LIST_ENTRY *ListHead);
/* Final NT handoff variant: builds the final NT descriptor list from the refreshed
 * map using caller-preallocated buffers, then exits boot services without any
 * intervening firmware calls or allocations. */
RC95LDR_STATUS Rc95LdrUefiFinalizeNtMemoryMap(
    const RC95LDR_UEFI_MEMORY_OPS *Ops, void *MapBuffer, size_t MapCapacity,
    RC95LDR_FIRMWARE_MEMORY_RANGE *FirmwareRanges, size_t RangeCapacity,
    const RC95LDR_MEMORY_RESERVATION *Reservations, size_t ReservationCount,
    RC95LDR_MEMORYMAP_RESULT *FinalMap,
    MEMORY_ALLOCATION_DESCRIPTOR *Descriptors, size_t DescriptorCapacity,
    LIST_ENTRY *DescriptorListHead, size_t *RawMapSize,
    size_t *RawDescriptorSize, uint32_t *DescriptorVersion);
/* Performs the final GetMemoryMap immediately followed by ExitBootServices.
 * The caller must not allocate, print, or otherwise call firmware between them. */
RC95LDR_STATUS Rc95LdrUefiFinalizeMemoryMap(
    const RC95LDR_UEFI_MEMORY_OPS *Ops, void *MapBuffer, size_t MapCapacity,
    RC95LDR_FIRMWARE_MEMORY_RANGE *Ranges, size_t RangeCapacity,
    size_t *RangeCount, size_t *RawMapSize, size_t *RawDescriptorSize,
    uint32_t *DescriptorVersion);
#ifdef __cplusplus
}
#endif
#endif
