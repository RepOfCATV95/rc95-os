#ifndef _RC95LDR_UEFI_IMAGE_BOOT_H_
#define _RC95LDR_UEFI_IMAGE_BOOT_H_

#include "rc95ldruefinativeboot.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RC95LDR_UEFI_IMAGE_BOOT_CONFIG_SIGNATURE 0x42353952u /* R95B */
#define RC95LDR_UEFI_IMAGE_BOOT_CONFIG_VERSION 1u

/*
 * Minimal image-owned input/output contract for the PE/COFF entry shim.
 *
 * The final EFI executable defines exactly one writable instance named
 * Rc95LdrUefiImageBootConfiguration. It supplies static discovery workspace
 * and the WINLOAD startup contract. Startup.Platform and the request/release
 * callbacks carry the image's memory-management, memory-map and
 * architecture-specific kernel-handoff operations.
 *
 * This record deliberately contains no disk, partition, ESP, BCD, NTFS or
 * WINLOAD-dispatch callbacks: Rc95LdrUefiNativeDiscoverAndStartWindows is
 * the sole owner of that complete discovery-to-WINLOAD transaction.
 * Configuration and buffers remain image-owned for the synchronous call.
 */
typedef struct _RC95LDR_UEFI_IMAGE_BOOT_CONFIG {
    uint32_t Signature;
    uint32_t StructureSize;
    uint32_t Version;
    uint32_t Architecture;

    /* Caller-owned native discovery scratch and result storage. */
    RC95LDR_PARTITION_INFO *Partitions;
    size_t PartitionCapacity;
    RC95LDR_BCD_BOOT_CANDIDATE *Candidates;
    size_t CandidateCapacity;
    size_t CandidateCount;
    void *HiveBuffer;
    size_t HiveCapacity;

    /* Platform memory/handoff policy and WINLOAD transaction result. */
    RC95LDR_WINLOAD_STARTUP Startup;
    RC95LDR_WINLOAD_RESULT Result;

    /* Native discovery outputs and optional presentation. */
    RC95LDR_WINDOWS_INSTALLATION SelectedInstallation;
    size_t SelectedEspIndex;
    const RC95LDR_TEXTUI *TextUi;
} RC95LDR_UEFI_IMAGE_BOOT_CONFIG;

/* Defined by the final EFI application, not by the shared static library. */
extern RC95LDR_UEFI_IMAGE_BOOT_CONFIG Rc95LdrUefiImageBootConfiguration;

RC95LDR_STATUS Rc95LdrUefiValidateImageBootConfig(
    const RC95LDR_UEFI_IMAGE_BOOT_CONFIG *Configuration,
    uint32_t NativeArchitecture);

#ifdef __cplusplus
}
#endif
#endif
