#ifndef _RC95LDR_NT_ABI_H_
#define _RC95LDR_NT_ABI_H_
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Layout transcription of ReactOS sdk/include/reactos/arc/arc.h and
 * sdk/include/ndk/ldrtypes.h. All pointers are target-native pointers:
 * compile this code for the same pointer width as the kernel being loaded. */
typedef uintptr_t RC95LDR_NT_PTR;
typedef struct _RC95LDR_NT_LIST_ENTRY {
    RC95LDR_NT_PTR Flink, Blink;
} RC95LDR_NT_LIST_ENTRY;
typedef struct _RC95LDR_NT_UNICODE_STRING {
    uint16_t Length, MaximumLength;
    RC95LDR_NT_PTR Buffer;
} RC95LDR_NT_UNICODE_STRING;
typedef struct _RC95LDR_NT_MEMORY_ALLOCATION_DESCRIPTOR {
    RC95LDR_NT_LIST_ENTRY ListEntry;
    uint32_t MemoryType;
    RC95LDR_NT_PTR BasePage, PageCount;
} RC95LDR_NT_MEMORY_ALLOCATION_DESCRIPTOR;
typedef struct _RC95LDR_NT_LDR_DATA_TABLE_ENTRY {
    RC95LDR_NT_LIST_ENTRY InLoadOrderLinks;
    RC95LDR_NT_LIST_ENTRY InMemoryOrderLinks;
    RC95LDR_NT_LIST_ENTRY InInitializationOrderLinks;
    RC95LDR_NT_PTR DllBase, EntryPoint;
    uint32_t SizeOfImage;
    RC95LDR_NT_UNICODE_STRING FullDllName, BaseDllName;
    uint32_t Flags;
    uint16_t LoadCount, TlsIndex;
    union {
        RC95LDR_NT_LIST_ENTRY HashLinks;
        struct { RC95LDR_NT_PTR SectionPointer; uint32_t CheckSum; } Section;
    } u1;
    union { uint32_t TimeDateStamp; RC95LDR_NT_PTR LoadedImports; } u2;
    RC95LDR_NT_PTR EntryPointActivationContext, PatchInformation;
} RC95LDR_NT_LDR_DATA_TABLE_ENTRY;
typedef struct _RC95LDR_NT_BOOT_DRIVER_LIST_ENTRY {
    RC95LDR_NT_LIST_ENTRY Link;
    RC95LDR_NT_UNICODE_STRING FilePath, RegistryPath;
    RC95LDR_NT_PTR LdrEntry;
} RC95LDR_NT_BOOT_DRIVER_LIST_ENTRY;
typedef struct _RC95LDR_NT_I386_LOADER_BLOCK {
    RC95LDR_NT_PTR CommonDataArea;
    uint32_t MachineType, VirtualBias;
} RC95LDR_NT_I386_LOADER_BLOCK;
typedef struct _RC95LDR_NT_PPC_LOADER_BLOCK {
    RC95LDR_NT_PTR BootInfo;
    uint32_t MachineType;
} RC95LDR_NT_PPC_LOADER_BLOCK;
typedef struct _RC95LDR_NT_ARM_LOADER_BLOCK {
#if defined(_ARM_)
    uint32_t InterruptStack, FirstLevelDcacheSize, FirstLevelDcacheFillSize;
    uint32_t FirstLevelIcacheSize, FirstLevelIcacheFillSize, GpBase;
    uint32_t PanicStack, PcrPage, PdrPage, SecondLevelDcacheSize;
    uint32_t SecondLevelDcacheFillSize, SecondLevelIcacheSize;
    uint32_t SecondLevelIcacheFillSize, PcrPage2;
#else
    uint32_t PlaceHolder;
#endif
} RC95LDR_NT_ARM_LOADER_BLOCK;
typedef union _RC95LDR_NT_ARCH_LOADER_BLOCK {
    RC95LDR_NT_I386_LOADER_BLOCK I386;
    RC95LDR_NT_PPC_LOADER_BLOCK PowerPC;
    RC95LDR_NT_ARM_LOADER_BLOCK Arm;
    uint32_t AlphaPlaceHolder, Ia64PlaceHolder;
} RC95LDR_NT_ARCH_LOADER_BLOCK;
/* Firmware-information records mirror EFI_FIRMWARE_INFORMATION and
 * FIRMWARE_INFORMATION_LOADER_BLOCK in ReactOS arc.h. */
typedef struct _RC95LDR_NT_EFI_FIRMWARE_INFORMATION {
    uint32_t FirmwareVersion;
    RC95LDR_NT_PTR VirtualEfiRuntimeServices;
    uint32_t SetVirtualAddressMapStatus, MissedMappingsCount;
} RC95LDR_NT_EFI_FIRMWARE_INFORMATION;
typedef struct _RC95LDR_NT_EFI_FIRMWARE_INFORMATION_V62 {
    uint32_t FirmwareVersion;
    RC95LDR_NT_PTR VirtualEfiRuntimeServices;
    uint32_t SetVirtualAddressMapStatus, MissedMappingsCount;
    RC95LDR_NT_LIST_ENTRY FirmwareResourceList;
} RC95LDR_NT_EFI_FIRMWARE_INFORMATION_V62;
typedef struct _RC95LDR_NT_EFI_FIRMWARE_INFORMATION_V63 {
    uint32_t FirmwareVersion;
    RC95LDR_NT_PTR VirtualEfiRuntimeServices;
    uint32_t SetVirtualAddressMapStatus, MissedMappingsCount;
    RC95LDR_NT_LIST_ENTRY FirmwareResourceList;
    RC95LDR_NT_PTR EfiMemoryMap;
    uint32_t EfiMemoryMapSize, EfiMemoryMapDescriptorSize;
} RC95LDR_NT_EFI_FIRMWARE_INFORMATION_V63;
typedef struct _RC95LDR_NT_FIRMWARE_INFORMATION {
    uint32_t FirmwareFlags;
    union {
        RC95LDR_NT_EFI_FIRMWARE_INFORMATION EfiInformation;
        uint32_t PcatPlaceholder;
    } u;
} RC95LDR_NT_FIRMWARE_INFORMATION;
typedef struct _RC95LDR_NT_FIRMWARE_INFORMATION_V62 {
    uint32_t FirmwareFlags;
    union {
        RC95LDR_NT_EFI_FIRMWARE_INFORMATION_V62 EfiInformation;
        uint32_t PcatPlaceholder;
    } u;
} RC95LDR_NT_FIRMWARE_INFORMATION_V62;
typedef struct _RC95LDR_NT_FIRMWARE_INFORMATION_V63 {
    uint32_t FirmwareFlags;
    union {
        RC95LDR_NT_EFI_FIRMWARE_INFORMATION_V63 EfiInformation;
        uint32_t PcatPlaceholder;
    } u;
} RC95LDR_NT_FIRMWARE_INFORMATION_V63;

/* NT 4.0: no Extension pointer; the slot is Spare1. */
typedef struct _RC95LDR_NT_LOADER_BLOCK_V4 {
    RC95LDR_NT_LIST_ENTRY LoadOrderListHead, MemoryDescriptorListHead, BootDriverListHead;
    RC95LDR_NT_PTR KernelStack, Prcb, Process, Thread;
    uint32_t RegistryLength;
    RC95LDR_NT_PTR RegistryBase, ConfigurationRoot;
    RC95LDR_NT_PTR ArcBootDeviceName, ArcHalDeviceName, NtBootPathName, NtHalPathName, LoadOptions;
    RC95LDR_NT_PTR NlsData, ArcDiskInformation, OemFontFile, SetupLdrBlock;
    uint32_t Spare1;
    RC95LDR_NT_ARCH_LOADER_BLOCK u;
} RC95LDR_NT_LOADER_BLOCK_V4;

/* Windows 2000 through Server 2003: Extension replaces Spare1. */
typedef struct _RC95LDR_NT_LOADER_BLOCK_V5 {
    RC95LDR_NT_LIST_ENTRY LoadOrderListHead, MemoryDescriptorListHead, BootDriverListHead;
    RC95LDR_NT_PTR KernelStack, Prcb, Process, Thread;
    uint32_t RegistryLength;
    RC95LDR_NT_PTR RegistryBase, ConfigurationRoot;
    RC95LDR_NT_PTR ArcBootDeviceName, ArcHalDeviceName, NtBootPathName, NtHalPathName, LoadOptions;
    RC95LDR_NT_PTR NlsData, ArcDiskInformation, OemFontFile, SetupLdrBlock;
    RC95LDR_NT_PTR Extension;
    RC95LDR_NT_ARCH_LOADER_BLOCK u;
} RC95LDR_NT_LOADER_BLOCK_V5;

/* Vista retains the pre-Win7 prefix and appends firmware information. */
typedef struct _RC95LDR_NT_LOADER_BLOCK_V6 {
    RC95LDR_NT_LIST_ENTRY LoadOrderListHead, MemoryDescriptorListHead, BootDriverListHead;
    RC95LDR_NT_PTR KernelStack, Prcb, Process, Thread;
    uint32_t RegistryLength;
    RC95LDR_NT_PTR RegistryBase, ConfigurationRoot;
    RC95LDR_NT_PTR ArcBootDeviceName, ArcHalDeviceName, NtBootPathName, NtHalPathName, LoadOptions;
    RC95LDR_NT_PTR NlsData, ArcDiskInformation, OemFontFile, SetupLdrBlock;
    RC95LDR_NT_PTR Extension;
    RC95LDR_NT_ARCH_LOADER_BLOCK u;
    RC95LDR_NT_FIRMWARE_INFORMATION FirmwareInformation;
} RC95LDR_NT_LOADER_BLOCK_V6;

/* Windows 7 adds the version prefix and removes SetupLdrBlock. */
typedef struct _RC95LDR_NT_LOADER_BLOCK_V61 {
    uint32_t OsMajorVersion, OsMinorVersion, Size, Reserved;
    RC95LDR_NT_LIST_ENTRY LoadOrderListHead, MemoryDescriptorListHead, BootDriverListHead;
    RC95LDR_NT_PTR KernelStack, Prcb, Process, Thread;
    uint32_t RegistryLength;
    RC95LDR_NT_PTR RegistryBase, ConfigurationRoot;
    RC95LDR_NT_PTR ArcBootDeviceName, ArcHalDeviceName, NtBootPathName, NtHalPathName, LoadOptions;
    RC95LDR_NT_PTR NlsData, ArcDiskInformation, OemFontFile, Extension;
    RC95LDR_NT_ARCH_LOADER_BLOCK u;
    RC95LDR_NT_FIRMWARE_INFORMATION FirmwareInformation;
} RC95LDR_NT_LOADER_BLOCK_V61;

/* Windows 8 adds two list heads and KernelStackSize. */
typedef struct _RC95LDR_NT_LOADER_BLOCK_V62 {
    uint32_t OsMajorVersion, OsMinorVersion, Size, Reserved;
    RC95LDR_NT_LIST_ENTRY LoadOrderListHead, MemoryDescriptorListHead, BootDriverListHead;
    RC95LDR_NT_LIST_ENTRY EarlyLaunchListHead, CoreDriverListHead;
    RC95LDR_NT_PTR KernelStack, Prcb, Process, Thread;
    uint32_t KernelStackSize, RegistryLength;
    RC95LDR_NT_PTR RegistryBase, ConfigurationRoot;
    RC95LDR_NT_PTR ArcBootDeviceName, ArcHalDeviceName, NtBootPathName, NtHalPathName, LoadOptions;
    RC95LDR_NT_PTR NlsData, ArcDiskInformation, Extension;
    RC95LDR_NT_ARCH_LOADER_BLOCK u;
    RC95LDR_NT_FIRMWARE_INFORMATION_V62 FirmwareInformation;
} RC95LDR_NT_LOADER_BLOCK_V62;

/* Windows 8.1 adds the EFI memory-map fields to the firmware union. */
typedef struct _RC95LDR_NT_LOADER_BLOCK_V63 {
    uint32_t OsMajorVersion, OsMinorVersion, Size, Reserved;
    RC95LDR_NT_LIST_ENTRY LoadOrderListHead, MemoryDescriptorListHead, BootDriverListHead;
    RC95LDR_NT_LIST_ENTRY EarlyLaunchListHead, CoreDriverListHead;
    RC95LDR_NT_PTR KernelStack, Prcb, Process, Thread;
    uint32_t KernelStackSize, RegistryLength;
    RC95LDR_NT_PTR RegistryBase, ConfigurationRoot;
    RC95LDR_NT_PTR ArcBootDeviceName, ArcHalDeviceName, NtBootPathName, NtHalPathName, LoadOptions;
    RC95LDR_NT_PTR NlsData, ArcDiskInformation, Extension;
    RC95LDR_NT_ARCH_LOADER_BLOCK u;
    RC95LDR_NT_FIRMWARE_INFORMATION_V63 FirmwareInformation;
} RC95LDR_NT_LOADER_BLOCK_V63;

/* Windows 10 adds CoreExtensions and TPM core-driver list heads. */
typedef struct _RC95LDR_NT_LOADER_BLOCK_V10 {
    uint32_t OsMajorVersion, OsMinorVersion, Size, Reserved;
    RC95LDR_NT_LIST_ENTRY LoadOrderListHead, MemoryDescriptorListHead, BootDriverListHead;
    RC95LDR_NT_LIST_ENTRY EarlyLaunchListHead, CoreDriverListHead;
    RC95LDR_NT_LIST_ENTRY CoreExtensionsDriverListHead, TpmCoreDriverListHead;
    RC95LDR_NT_PTR KernelStack, Prcb, Process, Thread;
    uint32_t KernelStackSize, RegistryLength;
    RC95LDR_NT_PTR RegistryBase, ConfigurationRoot;
    RC95LDR_NT_PTR ArcBootDeviceName, ArcHalDeviceName, NtBootPathName, NtHalPathName, LoadOptions;
    RC95LDR_NT_PTR NlsData, ArcDiskInformation, Extension;
    RC95LDR_NT_ARCH_LOADER_BLOCK u;
    RC95LDR_NT_FIRMWARE_INFORMATION_V63 FirmwareInformation;
} RC95LDR_NT_LOADER_BLOCK_V10;

typedef enum _RC95LDR_NT_TARGET_ARCH {
    Rc95LdrNtArchI386=1, Rc95LdrNtArchAmd64=2,
    Rc95LdrNtArchPowerPc=3, Rc95LdrNtArchArm=4
} RC95LDR_NT_TARGET_ARCH;
typedef enum _RC95LDR_NT_TARGET_VERSION {
    Rc95LdrNtVersion4=1, Rc95LdrNtVersion5=2, Rc95LdrNtVersion6=3,
    Rc95LdrNtVersion61=4, Rc95LdrNtVersion62=5, Rc95LdrNtVersion63=6, Rc95LdrNtVersion10=7
} RC95LDR_NT_TARGET_VERSION;
#ifdef __cplusplus
}
#endif
#endif
