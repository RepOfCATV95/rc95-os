#ifndef _RC95LDR_LOADERSTATE_H_
#define _RC95LDR_LOADERSTATE_H_
#include "rc95ldrloaderblock.h"
#include "rc95ldrphysmem.h"
#ifdef __cplusplus
extern "C" {
#endif
#define RC95LDR_CONFIG_COMPONENT_MAX 160u
#define RC95LDR_LOADERSTATE_VERSION 3u
typedef struct _RC95LDR_CONFIG_COMPONENT_INPUT {
    uint32_t Class, Type, Flags;
    uint16_t Version, Revision;
    uint32_t Key, AffinityMask;
    const char *Identifier;
    const void *ConfigurationData;
    uint32_t ConfigurationDataLength;
    int32_t ParentIndex;
} RC95LDR_CONFIG_COMPONENT_INPUT;
typedef struct _RC95LDR_NT_CONFIGURATION_COMPONENT {
    uint32_t Class, Type, Flags;
    uint16_t Version, Revision;
    uint32_t Key, AffinityMask, ConfigurationDataLength, IdentifierLength;
    RC95LDR_NT_PTR Identifier;
} RC95LDR_NT_CONFIGURATION_COMPONENT;
typedef struct _RC95LDR_NT_CONFIGURATION_COMPONENT_DATA {
    RC95LDR_NT_PTR Parent, Child, Sibling;
    RC95LDR_NT_CONFIGURATION_COMPONENT ComponentEntry;
    RC95LDR_NT_PTR ConfigurationData;
} RC95LDR_NT_CONFIGURATION_COMPONENT_DATA;
typedef struct _RC95LDR_LOADERSTATE_INPUT {
    uint32_t StructureSize, Version;
    RC95LDR_NT_TARGET_VERSION NtVersion;
    uint32_t OsMajorVersion, OsMinorVersion;
    const RC95LDR_CONFIG_COMPONENT_INPUT *Components;
    uint32_t ComponentCount;
    const RC95LDR_PHYSMEM_OPS *PhysicalMemory;
} RC95LDR_LOADERSTATE_INPUT;
typedef struct _RC95LDR_LOADERSTATE_RESULT {
    RC95LDR_NT_PTR ConfigurationRoot;
    size_t BytesUsed;
    RC95LDR_PHYSMEM_OPS PhysicalMemory;
    RC95LDR_PHYSMEM_ALLOCATION Allocation;
    uint32_t OwnsAllocation;
} RC95LDR_LOADERSTATE_RESULT;
RC95LDR_STATUS Rc95LdrBuildNtLoaderState(
    const RC95LDR_LOADERSTATE_INPUT *Input,
    RC95LDR_LOADERSTATE_RESULT *Result);
void Rc95LdrReleaseNtLoaderState(RC95LDR_LOADERSTATE_RESULT *Result);
#ifdef __cplusplus
}
#endif
#endif
