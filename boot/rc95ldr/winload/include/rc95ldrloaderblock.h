#ifndef _RC95LDR_NATIVE_LOADER_BLOCK_H_
#define _RC95LDR_NATIVE_LOADER_BLOCK_H_
#include "rc95ldrntabi.h"
#include "rc95ldrprofile.h"
#ifdef __cplusplus
extern "C" {
#endif
#define RC95LDR_MODULE_BOOT_DRIVER 0x00000001u
#define RC95LDR_MODULE_KERNEL      0x00000002u
#define RC95LDR_MODULE_HAL         0x00000004u
#define RC95LDR_MEMORY_MAX_RANGES  128u
#define RC95LDR_MODULE_MAX         96u
#define RC95LDR_NATIVE_LOADER_INPUT_VERSION 5u
typedef struct _RC95LDR_LOADED_MODULE {
    uint64_t ImageBase, ImageSize, EntryPoint;
    uint32_t Flags;
    const char *Path;
    const char *RegistryPath;
} RC95LDR_LOADED_MODULE;
typedef struct _RC95LDR_MEMORY_RANGE {
    uint64_t BasePage, PageCount;
    uint32_t Type;
    uint32_t Reserved;
} RC95LDR_MEMORY_RANGE;
typedef struct _RC95LDR_NATIVE_LOADER_INPUT {
    uint32_t StructureSize, Version;
    uint32_t OsMajorVersion, OsMinorVersion, OsBuildNumber;
    RC95LDR_NT_TARGET_VERSION NtVersion;
    RC95LDR_NT_TARGET_ARCH Architecture;
    uint32_t MachineType;
    const RC95LDR_NT_ARCH_LOADER_BLOCK *ArchitectureBlock;
    uint32_t FirmwareIsUefi, FirmwareVersion;
    uint64_t FirmwareSystemTable, VirtualEfiRuntimeServices, FirmwareMapAddress;
    uint32_t FirmwareMapSize, FirmwareMapDescriptorSize;
    uint32_t RegistryLength;
    const void *RegistryBase;
    uint32_t ModuleCount;
    const RC95LDR_LOADED_MODULE *Modules;
    uint32_t MemoryRangeCount;
    const RC95LDR_MEMORY_RANGE *MemoryRanges;
    const char *ArcBootDevice, *ArcHalDevice, *NtBootPath, *NtHalPath, *LoadOptions;
    RC95LDR_NT_PTR ConfigurationRoot, LoaderExtension, NlsData, ArcDiskInformation;
} RC95LDR_NATIVE_LOADER_INPUT;
typedef struct _RC95LDR_RC95_LIST_ENTRY { RC95LDR_NT_PTR Flink, Blink; } RC95LDR_RC95_LIST_ENTRY;
typedef struct _RC95LDR_RC95_UNICODE_STRING {
    uint16_t Length, MaximumLength;
    RC95LDR_NT_PTR Buffer;
} RC95LDR_RC95_UNICODE_STRING;
typedef struct _RC95LDR_RC95_MODULE_ENTRY {
    RC95LDR_RC95_LIST_ENTRY LoadOrderLinks;
    RC95LDR_NT_PTR ImageBase, EntryPoint;
    uint32_t ImageSize, Attributes;
    RC95LDR_RC95_UNICODE_STRING FullImagePath, BaseImageName;
} RC95LDR_RC95_MODULE_ENTRY;
typedef struct _RC95LDR_RC95_MEMORY_DESCRIPTOR {
    RC95LDR_RC95_LIST_ENTRY ListEntry;
    uint32_t MemoryType;
    RC95LDR_NT_PTR BasePage, PageCount;
} RC95LDR_RC95_MEMORY_DESCRIPTOR;
typedef struct _RC95LDR_RC95_BOOT_DRIVER_ENTRY {
    RC95LDR_RC95_LIST_ENTRY Link;
    RC95LDR_RC95_UNICODE_STRING FilePath, RegistryPath;
    RC95LDR_NT_PTR ModuleEntry;
} RC95LDR_RC95_BOOT_DRIVER_ENTRY;
typedef struct _RC95LDR_RC95_NATIVE_LOADER_BLOCK {
    uint32_t Signature, Size, AbiVersion, Flags;
    uint32_t OsMajorVersion, OsMinorVersion, OsBuildNumber, Architecture;
    uint32_t MachineType, FirmwareIsUefi, FirmwareVersion, RegistryLength;
    RC95LDR_NT_PTR FirmwareSystemTable, FirmwareServices, FirmwareMap;
    uint32_t FirmwareMapSize, FirmwareMapDescriptorSize;
    RC95LDR_RC95_LIST_ENTRY LoadOrderListHead;
    RC95LDR_RC95_LIST_ENTRY MemoryDescriptorListHead;
    RC95LDR_RC95_LIST_ENTRY BootDriverListHead;
    RC95LDR_NT_PTR KernelEntry, KernelBase, HalBase, SystemHive, ConfigurationRoot, NlsData, ArcDiskInformation;
    uint32_t ModuleCount, MemoryRangeCount, BootDriverCount, Reserved;
    RC95LDR_NT_PTR BootDevice, HalDevice, SystemRoot, LoadOptions;
} RC95LDR_RC95_NATIVE_LOADER_BLOCK;
RC95LDR_STATUS Rc95LdrBuildNtLegacyLoaderBlock(const RC95LDR_NATIVE_LOADER_INPUT*,void*,size_t,size_t*);
RC95LDR_STATUS Rc95LdrBuildReactOsLoaderBlock(const RC95LDR_NATIVE_LOADER_INPUT*,void*,size_t,size_t*);
RC95LDR_STATUS Rc95LdrBuildWindowsModernLoaderBlock(const RC95LDR_NATIVE_LOADER_INPUT*,void*,size_t,size_t*);
RC95LDR_STATUS Rc95LdrBuildRc95NativeLoaderBlock(const RC95LDR_NATIVE_LOADER_INPUT*,void*,size_t,size_t*);
#ifdef __cplusplus
}
#endif
#endif
