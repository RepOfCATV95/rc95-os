#ifndef _RC95LDR_RC95LOADERSTATE_H_
#define _RC95LDR_RC95LOADERSTATE_H_
#include "rc95ldrphysmem.h"

#ifdef __cplusplus
extern "C" {
#endif
#define RC95LDR_RC95_LOADERSTATE_VERSION 1u
#define RC95LDR_RC95_CONFIG_COMPONENT_MAX 160u
typedef struct _RC95LDR_RC95_CONFIG_NODE {
    uint32_t Class, Type, Flags, Key, AffinityMask;
    uint16_t Version, Revision;
    uint32_t IdentifierLength, DataLength;
    uint64_t Parent, FirstChild, NextSibling, Identifier, ConfigurationData;
} RC95LDR_RC95_CONFIG_NODE;
typedef struct _RC95LDR_RC95_CONFIG_COMPONENT_INPUT {
    uint32_t Class, Type, Flags;
    uint16_t Version, Revision;
    uint32_t Key, AffinityMask;
    const char *Identifier;
    const void *ConfigurationData;
    uint32_t ConfigurationDataLength;
    int32_t ParentIndex;
} RC95LDR_RC95_CONFIG_COMPONENT_INPUT;
typedef struct _RC95LDR_RC95_LOADERSTATE_INPUT {
    uint32_t StructureSize, Version;
    const RC95LDR_RC95_CONFIG_COMPONENT_INPUT *Components;
    uint32_t ComponentCount;
    const RC95LDR_PHYSMEM_OPS *PhysicalMemory;
} RC95LDR_RC95_LOADERSTATE_INPUT;
typedef struct _RC95LDR_RC95_LOADERSTATE_RESULT {
    uint64_t ConfigurationRoot;
    size_t BytesUsed;
    RC95LDR_PHYSMEM_OPS PhysicalMemory;
    RC95LDR_PHYSMEM_ALLOCATION Allocation;
    uint32_t OwnsAllocation;
} RC95LDR_RC95_LOADERSTATE_RESULT;
RC95LDR_STATUS Rc95LdrBuildRc95LoaderState(const RC95LDR_RC95_LOADERSTATE_INPUT*, RC95LDR_RC95_LOADERSTATE_RESULT*);
void Rc95LdrReleaseRc95LoaderState(RC95LDR_RC95_LOADERSTATE_RESULT*);
#ifdef __cplusplus
}
#endif
#endif
