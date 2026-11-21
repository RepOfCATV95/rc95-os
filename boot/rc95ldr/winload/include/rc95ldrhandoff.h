#ifndef _RC95LDR_KERNEL_HANDOFF_H_
#define _RC95LDR_KERNEL_HANDOFF_H_
#include "rc95ldrloaderblock.h"
#ifdef __cplusplus
extern "C" {
#endif
#define RC95LDR_HANDOFF_VERSION 4u
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
#define RC95LDR_HANDOFF_REQUIRED (RC95LDR_HANDOFF_KERNEL_MAPPED | RC95LDR_HANDOFF_HAL_MAPPED | RC95LDR_HANDOFF_SYSTEM_HIVE | RC95LDR_HANDOFF_BOOT_DRIVERS | RC95LDR_HANDOFF_MEMORY_MAP | RC95LDR_HANDOFF_LOADER_BLOCK | RC95LDR_HANDOFF_CONFIGURATION_TREE | RC95LDR_HANDOFF_NLS | RC95LDR_HANDOFF_ARC_DISK_INFO | RC95LDR_HANDOFF_LOADER_EXTENSION | RC95LDR_HANDOFF_ARCH_STATE | RC95LDR_HANDOFF_FIRMWARE_STATE | RC95LDR_HANDOFF_STACK | RC95LDR_HANDOFF_PAGE_TABLES | RC95LDR_HANDOFF_CPU_STATE | RC95LDR_HANDOFF_PROCESSOR_CONTROL | RC95LDR_HANDOFF_REGISTER_FRAME)
typedef struct _RC95LDR_I386_PROCESSOR_CONTROL {
    uint64_t PcrAddress, PcrSize, PrcbOffset, PrcbSize;
} RC95LDR_I386_PROCESSOR_CONTROL;
typedef struct _RC95LDR_AMD64_PROCESSOR_CONTROL {
    uint64_t PcrAddress, PcrSize, PrcbAddress, PrcbSize;
} RC95LDR_AMD64_PROCESSOR_CONTROL;
typedef union _RC95LDR_PROCESSOR_CONTROL {
    RC95LDR_I386_PROCESSOR_CONTROL I386;
    RC95LDR_AMD64_PROCESSOR_CONTROL Amd64;
} RC95LDR_PROCESSOR_CONTROL;
typedef struct _RC95LDR_KERNEL_HANDOFF_CONTEXT {
    uint32_t Signature, StructureSize, Version, Flags;
    RC95LDR_OS_FAMILY OsFamily;
    RC95LDR_BOOT_FIRMWARE Firmware;
    RC95LDR_NT_TARGET_ARCH Architecture;
    uint32_t MachineType;
    uint32_t NtMajorVersion, NtMinorVersion;
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
} RC95LDR_KERNEL_HANDOFF_CONTEXT;
/* Performs preflight only. Architecture-specific transfer stubs are a later
 * implementation and must consume this context without changing OS ABIs. */
RC95LDR_STATUS Rc95LdrValidateKernelHandoff(
    const RC95LDR_KERNEL_HANDOFF_CONTEXT *Context);
#ifdef __cplusplus
}
#endif
#endif
