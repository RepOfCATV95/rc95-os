#ifndef _RC95LDR_DEPENDENCY_H_
#define _RC95LDR_DEPENDENCY_H_
#include "rc95ldrloaderblock.h"
#ifdef __cplusplus
extern "C" {
#endif

#define RC95LDR_DEPENDENCY_VERSION 1u
#define RC95LDR_DEP_ARCH_I386  0x01u
#define RC95LDR_DEP_ARCH_AMD64 0x02u
#define RC95LDR_DEP_ARCH_ALL   (RC95LDR_DEP_ARCH_I386 | RC95LDR_DEP_ARCH_AMD64)
#define RC95LDR_DEP_FLAG_NT4_ONLY 0x0001u
#define RC95LDR_DEP_FLAG_OPTIONAL 0x0002u
#define RC95LDR_DEP_FLAG_HARDWARE_MATCH 0x0004u
#define RC95LDR_DEP_FLAG_USER_MODE 0x0008u

typedef enum _RC95LDR_DEPENDENCY_ROLE {
    Rc95LdrDependencyKernel=1,
    Rc95LdrDependencyHal,
    Rc95LdrDependencyBootVideo,
    Rc95LdrDependencyNativeRuntime,
    Rc95LdrDependencyWin32Runtime,
    Rc95LdrDependencyWin32k,
    Rc95LdrDependencyFilesystem,
    Rc95LdrDependencySecurity,
    Rc95LdrDependencyAudio,
    Rc95LdrDependencyPciIde,
    Rc95LdrDependencyUsb
} RC95LDR_DEPENDENCY_ROLE;

typedef enum _RC95LDR_DEPENDENCY_PHASE {
    Rc95LdrDependencyBootCritical=1,
    Rc95LdrDependencyKernelSupport,
    Rc95LdrDependencyUserSubsystem
} RC95LDR_DEPENDENCY_PHASE;

typedef struct _RC95LDR_DEPENDENCY_CANDIDATE {
    const char *FileName;
    RC95LDR_DEPENDENCY_ROLE Role;
    RC95LDR_DEPENDENCY_PHASE Phase;
    uint8_t ArchitectureMask;
    uint8_t MinimumMajor;
    uint8_t MaximumMajor;
    uint16_t Flags;
} RC95LDR_DEPENDENCY_CANDIDATE;

/* Enumerates possible image names only. It does not assert that a file is
 * installed, boot-start, hardware-compatible, or safe to load. SYSTEM hive,
 * PE machine type, platform inventory and dependency policy must decide that. */
size_t Rc95LdrCountDependencyCandidates(RC95LDR_DEPENDENCY_ROLE Role,
    uint32_t OsMajorVersion, RC95LDR_NT_TARGET_ARCH Architecture);
const RC95LDR_DEPENDENCY_CANDIDATE *Rc95LdrGetDependencyCandidate(
    RC95LDR_DEPENDENCY_ROLE Role, uint32_t OsMajorVersion,
    RC95LDR_NT_TARGET_ARCH Architecture, size_t Index);
#ifdef __cplusplus
}
#endif
#endif
