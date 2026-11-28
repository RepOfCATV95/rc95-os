#ifndef _RC95LDR_BIOS_STAGE_H_
#define _RC95LDR_BIOS_STAGE_H_

#include "rc95ldrbiosstartup.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RC95LDR_BIOS_STAGE_SIGNATURE 0x3942u
#define RC95LDR_BIOS_STAGE_VERSION 1u

/* On-disk B95S header read by the 16-bit Stage-0 EDD loader at LBA 1.
 * CRC-32/IEEE covers exactly ImageLength bytes (not sector padding).
 * LoadAddress + ImageLength must remain in conventional RAM; Stage 0 must
 * additionally compare the end against INT 12h memory and reject overlap
 * with its own boot-sector/stack/scratch reservations.
 */
#define RC95LDR_BIOS_BOOTSTRAP_SIGNATURE 0x53353942u /* B95S */
#define RC95LDR_BIOS_BOOTSTRAP_VERSION 1u
#define RC95LDR_BIOS_BOOTSTRAP_HEADER_SIZE 32u
#define RC95LDR_BIOS_BOOTSTRAP_MIN_ADDRESS 0x8000u
#define RC95LDR_BIOS_BOOTSTRAP_MAX_END 0x10000u

#pragma pack(push, 1)
typedef struct _RC95LDR_BIOS_BOOTSTRAP_HEADER {
    uint32_t Signature;
    uint16_t Version;
    uint16_t HeaderSize;
    uint32_t LoadAddress;
    uint32_t ImageLength;
    uint32_t EntryOffset;
    uint32_t Crc32;
    uint32_t Flags;
    uint32_t Reserved;
} RC95LDR_BIOS_BOOTSTRAP_HEADER;
#pragma pack(pop)

#if defined(__cplusplus)
static_assert(sizeof(RC95LDR_BIOS_BOOTSTRAP_HEADER) == RC95LDR_BIOS_BOOTSTRAP_HEADER_SIZE, "BIOS bootstrap header ABI");
#else
typedef char RC95LDR_BIOS_BOOTSTRAP_HEADER_SIZE_CHECK[
    sizeof(RC95LDR_BIOS_BOOTSTRAP_HEADER) == RC95LDR_BIOS_BOOTSTRAP_HEADER_SIZE ? 1 : -1];
#endif

/*
 * Packed real-mode handoff consumed by arch/i386/stage.S.
 * All addresses are physical/linear addresses below the active identity map.
 */
#pragma pack(push, 1)
typedef struct _RC95LDR_BIOS_STAGE_HANDOFF {
    uint16_t Signature;
    uint16_t Version;
    uint32_t StartupDescriptor;
    uint32_t PlatformProvider;
    uint32_t ProtectedStackTop;
} RC95LDR_BIOS_STAGE_HANDOFF;
#pragma pack(pop)

#if defined(__cplusplus)
static_assert(sizeof(RC95LDR_BIOS_STAGE_HANDOFF) == 16, "BIOS stage handoff ABI");
#else
typedef char RC95LDR_BIOS_STAGE_HANDOFF_SIZE_CHECK[
    sizeof(RC95LDR_BIOS_STAGE_HANDOFF) == 16 ? 1 : -1];
#endif

/*
 * Platform bootstrap is deliberately an RC95LDR-owned provider. The stage
 * invokes it after protected-mode entry to populate every WINLOAD callback
 * and the BIOS service table; it must not import FreeLoader platform code.
 */
typedef RC95LDR_STATUS (*RC95LDR_BIOS_BUILD_PLATFORM)(
    uint8_t BootDrive,
    RC95LDR_BIOS_STARTUP *Startup,
    void *ProviderContext);

typedef void (*RC95LDR_BIOS_RELEASE_PLATFORM)(
    uint8_t BootDrive,
    RC95LDR_BIOS_STARTUP *Startup,
    void *ProviderContext);

typedef struct _RC95LDR_BIOS_PLATFORM_PROVIDER {
    uint32_t StructureSize;
    uint32_t Version;
    void *Context;
    RC95LDR_BIOS_BUILD_PLATFORM BuildPlatform;
    /* Version 2 optional unwind hook. It must tolerate partially built state. */
    RC95LDR_BIOS_RELEASE_PLATFORM ReleasePlatform;
} RC95LDR_BIOS_PLATFORM_PROVIDER;

#define RC95LDR_BIOS_PLATFORM_PROVIDER_VERSION 2u
#define RC95LDR_BIOS_PLATFORM_PROVIDER_VERSION_LEGACY 1u

RC95LDR_STATUS Rc95LdrBiosStageMain(
    uint8_t BootDrive,
    RC95LDR_BIOS_STARTUP *Startup,
    const RC95LDR_BIOS_PLATFORM_PROVIDER *Provider);

#ifdef __cplusplus
}
#endif
#endif
