#ifndef _RC95LDR_BIOS_FIRMWARE_STATE_H_
#define _RC95LDR_BIOS_FIRMWARE_STATE_H_

#include "rc95ldrhandoff.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RC95LDR_BIOS_FIRMWARE_STATE_VERSION 1u
#define RC95LDR_BIOS_FIRMWARE_STATE_SIGNATURE 0x53463935u /* "59FS" */
#define RC95LDR_BIOS_E820_MAX 128u
#define RC95LDR_BIOS_STATE_IVT_VALID 0x00000001u
#define RC95LDR_BIOS_STATE_BDA_VALID 0x00000002u
#define RC95LDR_BIOS_STATE_E820_VALID 0x00000004u

typedef const void *(*RC95LDR_BIOS_MAP_PHYSICAL)(
    void *Context, uint64_t PhysicalAddress, size_t Length);

typedef struct _RC95LDR_BIOS_E820_ENTRY {
    uint64_t Base;
    uint64_t Length;
    uint32_t Type;
    uint32_t Attributes;
} RC95LDR_BIOS_E820_ENTRY;

typedef struct _RC95LDR_BIOS_FIRMWARE_STATE {
    uint32_t Signature;
    uint32_t StructureSize;
    uint32_t Version;
    uint32_t Flags;
    uint8_t BootDrive;
    uint8_t VideoMode;
    uint8_t VideoColumns;
    uint8_t ActiveVideoPage;
    uint16_t ConventionalMemoryKilobytes;
    uint16_t EquipmentWord;
    uint16_t EbdaSegment;
    uint16_t Reserved;
    uint32_t BiosTickCount;
    uint8_t InterruptVectorTable[1024];
    uint8_t BiosDataArea[256];
    uint8_t HardDiskCount;
    uint8_t FloppyDriveCount;
    uint32_t E820Count;
    RC95LDR_BIOS_E820_ENTRY E820[RC95LDR_BIOS_E820_MAX];
} RC95LDR_BIOS_FIRMWARE_STATE;

typedef struct _RC95LDR_BIOS_FIRMWARE_CAPTURE_INPUT {
    uint32_t StructureSize;
    uint32_t Version;
    RC95LDR_BIOS_MAP_PHYSICAL MapPhysical;
    void *Context;
    uint8_t BootDrive;
    uint8_t Reserved[7];
    const RC95LDR_BIOS_E820_ENTRY *E820;
    uint32_t E820Count;
} RC95LDR_BIOS_FIRMWARE_CAPTURE_INPUT;

/* Capture the BIOS IVT/BDA and caller-collected E820 map into a preallocated,
 * persistent buffer. The buffer must remain reserved and mapped at handoff. */
RC95LDR_STATUS Rc95LdrBiosCaptureFirmwareState(
    const RC95LDR_BIOS_FIRMWARE_CAPTURE_INPUT *Input,
    RC95LDR_BIOS_FIRMWARE_STATE *State,
    size_t StateCapacity);

/* Attach the snapshot to the common handoff without populating EFI fields. */
RC95LDR_STATUS Rc95LdrBiosAttachFirmwareState(
    const RC95LDR_BIOS_FIRMWARE_CAPTURE_INPUT *Input,
    RC95LDR_BIOS_FIRMWARE_STATE *State,
    size_t StateCapacity,
    RC95LDR_KERNEL_HANDOFF_CONTEXT *Handoff);

#ifdef __cplusplus
}
#endif
#endif
