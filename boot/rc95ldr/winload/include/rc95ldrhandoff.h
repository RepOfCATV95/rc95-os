#ifndef _RC95LDR_KERNEL_HANDOFF_H_
#define _RC95LDR_KERNEL_HANDOFF_H_
#include "rc95ldrloaderblock.h"
#ifdef __cplusplus
extern "C" {
#endif
#define RC95LDR_HANDOFF_VERSION 6u
#define RC95LDR_HANDOFF_SIGNATURE 0x484c3935u /* "59LH" */
#define RC95LDR_HANDOFF_KERNEL_MAPPED       0x00000001u
#define RC95LDR_HANDOFF_HAL_MAPPED          0x00000002u
#define RC95LDR_HANDOFF_SYSTEM_HIVE         0x00000004u
#define RC95LDR_HANDOFF_BOOT_DRIVERS        0x00000008u
#define RC95LDR_HANDOFF_MEMORY_MAP          0x00000010u
#define RC95LDR_HANDOFF_LOADER_BLOCK        0x00000020u
#define RC95LDR_HANDOFF_CONFIGURATION_TREE  0x00000040u
#define RC95LDR_HANDOFF_NLS                 0x00000080u
#define RC95LDR_HANDOFF_ARC_DISK_INFO       0x00000100u
#define RC95LDR_HANDOFF_LOADER_EXTENSION    0x00000200u
#define RC95LDR_HANDOFF_ARCH_STATE          0x00000400u
#define RC95LDR_HANDOFF_FIRMWARE_STATE      0x00000800u
#define RC95LDR_HANDOFF_STACK               0x00001000u
#define RC95LDR_HANDOFF_PAGE_TABLES         0x00002000u
#define RC95LDR_HANDOFF_CPU_STATE           0x00004000u
#define RC95LDR_HANDOFF_PROCESSOR_CONTROL  0x00008000u
#define RC95LDR_HANDOFF_REGISTER_FRAME      0x00020000u
#define RC95LDR_HANDOFF_RAMDISK             0x00040000u
#define RC95LDR_HANDOFF_REQUIRED (RC95LDR_HANDOFF_KERNEL_MAPPED | RC95LDR_HANDOFF_HAL_MAPPED | RC95LDR_HANDOFF_SYSTEM_HIVE | RC95LDR_HANDOFF_BOOT_DRIVERS | RC95LDR_HANDOFF_MEMORY_MAP | RC95LDR_HANDOFF_LOADER_BLOCK | RC95LDR_HANDOFF_CONFIGURATION_TREE | RC95LDR_HANDOFF_NLS | RC95LDR_HANDOFF_ARC_DISK_INFO | RC95LDR_HANDOFF_LOADER_EXTENSION | RC95LDR_HANDOFF_ARCH_STATE | RC95LDR_HANDOFF_FIRMWARE_STATE | RC95LDR_HANDOFF_STACK | RC95LDR_HANDOFF_PAGE_TABLES | RC95LDR_HANDOFF_CPU_STATE | RC95LDR_HANDOFF_PROCESSOR_CONTROL | RC95LDR_HANDOFF_REGISTER_FRAME)
/* RC95-OS owns a distinct loader-block ABI and has no NT loader extension. */
#define RC95LDR_HANDOFF_RC95_REQUIRED (RC95LDR_HANDOFF_REQUIRED & ~RC95LDR_HANDOFF_LOADER_EXTENSION)
/* NT-owned processor bootstrap ABI. RC95-OS must use the RC95 state records below. */
typedef struct _RC95LDR_I386_PROCESSOR_CONTROL {
    uint64_t PcrAddress, PcrSize, PrcbOffset, PrcbSize;
} RC95LDR_I386_PROCESSOR_CONTROL;
typedef struct _RC95LDR_AMD64_PROCESSOR_CONTROL {
    uint64_t PcrAddress, PcrSize, PrcbAddress, PrcbSize;
} RC95LDR_AMD64_PROCESSOR_CONTROL;
/* RC95-owned CPU bootstrap records. These are deliberately not NT PCR/PRCB
 * layouts and carry only RC95 kernel-owned per-CPU and scheduler-state ranges. */
typedef struct _RC95LDR_RC95_I386_PROCESSOR_STATE {
    uint64_t PerCpuDataAddress, PerCpuDataSize;
    uint64_t SchedulerStateAddress, SchedulerStateSize;
} RC95LDR_RC95_I386_PROCESSOR_STATE;
typedef struct _RC95LDR_RC95_AMD64_PROCESSOR_STATE {
    uint64_t PerCpuDataAddress, PerCpuDataSize;
    uint64_t SchedulerStateAddress, SchedulerStateSize;
} RC95LDR_RC95_AMD64_PROCESSOR_STATE;
typedef union _RC95LDR_PROCESSOR_CONTROL {
    RC95LDR_I386_PROCESSOR_CONTROL I386;
    RC95LDR_AMD64_PROCESSOR_CONTROL Amd64; /* Windows NT only */
    RC95LDR_RC95_I386_PROCESSOR_STATE Rc95I386;
    RC95LDR_RC95_AMD64_PROCESSOR_STATE Rc95Amd64;
} RC95LDR_PROCESSOR_CONTROL;
typedef struct _RC95LDR_KERNEL_HANDOFF_CONTEXT {
    uint32_t Signature, StructureSize, Version, Flags;
    RC95LDR_OS_FAMILY OsFamily;
    RC95LDR_BOOT_FIRMWARE Firmware;
    RC95LDR_NT_TARGET_ARCH Architecture;
    uint32_t MachineType;
    uint32_t OsMajorVersion, OsMinorVersion;
    uint64_t KernelEntry, KernelBase, KernelSize, HalBase;
    uint64_t LoaderBlockAddress;
    uint32_t LoaderBlockSize, MemoryRangeCount;
    uint64_t StackBase, StackSize, PageTableRoot;
    uint64_t ConfigurationRoot, NlsData, ArcDiskInformation, LoaderExtension;
    uint64_t ArchitectureState, CpuState;
    RC95LDR_PROCESSOR_CONTROL ProcessorControl;
    uint64_t RegisterFrameAddress;
    uint32_t RegisterFrameSize, ReservedHandoff;
    uint64_t FirmwareSystemTable, FirmwareRuntimeServices, FirmwareMemoryMap;
    uint32_t FirmwareMemoryMapSize, FirmwareMemoryMapDescriptorSize;
    uint32_t CompletedRequirements, RequiredRequirements;
    uint64_t RamdiskPhysicalBase, RamdiskAllocationSize;
    uint64_t RamdiskImageOffset, RamdiskImageLength;
    /* BIOS-specific state is a persistent RC95-owned snapshot, never an EFI
       system table or runtime-services pointer. */
    uint64_t BiosFirmwareStateAddress;
    uint32_t BiosFirmwareStateSize, BiosFirmwareStateVersion;
} RC95LDR_KERNEL_HANDOFF_CONTEXT;
/* Performs the mandatory profile-aware preflight. The native AMD64 cold-boot
 * transfer consumes this context only after this validator succeeds; other
 * architecture/firmware adapters retain their own transfer contracts. */
RC95LDR_STATUS Rc95LdrValidateKernelHandoff(
    const RC95LDR_KERNEL_HANDOFF_CONTEXT *Context);
#ifdef __cplusplus
}
#endif
#endif
