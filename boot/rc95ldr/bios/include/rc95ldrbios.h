#ifndef _RC95LDR_BIOS_H_
#define _RC95LDR_BIOS_H_
#include "rc95ldr.h"
#include "rc95ldrmemorymap.h"
#include "rc95ldrtextui.h"
#include "rc95ldrbgfx.h"
#include "rc95ldrfirmwaremenu.h"
#ifdef __cplusplus
extern "C" {
#endif

#define RC95LDR_BIOS_ABI_VERSION 1u
#define RC95LDR_BIOS_E820_SIGNATURE 0x534D4150u
#define RC95LDR_BIOS_EDD_SIGNATURE 0xAA55u
#define RC95LDR_BIOS_EDD_MAX_BLOCKS 127u

/* Register image exchanged with the RC95LDR-owned real-mode gateway.
 * Carry is returned in Eflags bit 0. Segment registers are 16-bit values. */
typedef struct _RC95LDR_BIOS_REGISTERS {
    uint32_t Eax, Ebx, Ecx, Edx, Esi, Edi, Ebp, Eflags;
    uint16_t Ds, Es, Fs, Gs;
} RC95LDR_BIOS_REGISTERS;

typedef RC95LDR_STATUS (*RC95LDR_BIOS_INVOKE_INTERRUPT)(
    void *Context, uint8_t Interrupt, RC95LDR_BIOS_REGISTERS *Registers);
typedef RC95LDR_STATUS (*RC95LDR_BIOS_MAP_LOW_MEMORY)(
    void *Context, uint32_t PhysicalAddress, size_t Length, void **Mapping);
typedef void (*RC95LDR_BIOS_UNMAP_LOW_MEMORY)(
    void *Context, void *Mapping, size_t Length);

typedef struct _RC95LDR_BIOS_SERVICES {
    uint32_t StructureSize, Version;
    void *Context;
    RC95LDR_BIOS_INVOKE_INTERRUPT InvokeInterrupt;
    RC95LDR_BIOS_MAP_LOW_MEMORY MapLowMemory;
    RC95LDR_BIOS_UNMAP_LOW_MEMORY UnmapLowMemory;
    uint32_t ScratchPhysical;
    uint32_t ScratchLength;
    uint8_t BootDrive;
    uint8_t Reserved[3];
} RC95LDR_BIOS_SERVICES;

typedef struct _RC95LDR_BIOS_DISK {
    uint8_t Drive;
    uint8_t EddSupported;
    uint16_t BytesPerSector;
    uint64_t BlockCount;
} RC95LDR_BIOS_DISK;

typedef struct _RC95LDR_BIOS_BLOCK_CONTEXT {
    const RC95LDR_BIOS_SERVICES *Services;
    const RC95LDR_BIOS_DISK *Disk;
} RC95LDR_BIOS_BLOCK_CONTEXT;

typedef struct _RC95LDR_BIOS_TEXT {
    const RC95LDR_BIOS_SERVICES *Services;
    uint8_t VideoPage;
    uint8_t BackgroundAttribute;
    uint8_t Reserved[2];
} RC95LDR_BIOS_TEXT;

typedef struct _RC95LDR_BIOS_VIDEO {
    RC95LDR_BIOS_TEXT Text;
    RC95LDR_BGFX_FRAMEBUFFER Framebuffer;
} RC95LDR_BIOS_VIDEO;


RC95LDR_STATUS Rc95LdrBiosValidateServices(const RC95LDR_BIOS_SERVICES *);
#if defined(_M_IX86) || defined(__i386__)
/* Native i386 BIOS frontend: reserves the low-memory window, installs the
 * private thunk, and replaces InvokeInterrupt with the RC95LDR gateway. */
RC95LDR_STATUS Rc95LdrBiosEnableNativeGateway(RC95LDR_BIOS_SERVICES *);
RC95LDR_STATUS Rc95LdrBiosPrepareNativePlatform(
    RC95LDR_BIOS_SERVICES *, RC95LDR_MEMORY_RESERVATION_SET *);
/* Initialize the native flat/identity-mapped low-memory BIOS service backend.
 * InvokeInterrupt is a temporary rejecting stub and is replaced when the
 * private RC95LDR gateway is installed. */
RC95LDR_STATUS Rc95LdrBiosInitializeNativeServices(
    RC95LDR_BIOS_SERVICES *, uint8_t BootDrive);
#endif
RC95LDR_STATUS Rc95LdrBiosE820Next(void *, uint32_t *,
    RC95LDR_FIRMWARE_MEMORY_RANGE *, int *);
RC95LDR_STATUS Rc95LdrBiosAddMemoryReservations(
    const RC95LDR_BIOS_SERVICES *, RC95LDR_MEMORY_RESERVATION_SET *);
RC95LDR_STATUS Rc95LdrBiosCollectMemoryMap(
    const RC95LDR_BIOS_SERVICES *, RC95LDR_FIRMWARE_MEMORY_RANGE *,
    size_t, size_t *);
RC95LDR_STATUS Rc95LdrBiosProbeDisk(const RC95LDR_BIOS_SERVICES *,
    uint8_t, RC95LDR_BIOS_DISK *);
RC95LDR_STATUS Rc95LdrBiosInitializeBlockDevice(
    const RC95LDR_BIOS_SERVICES *, const RC95LDR_BIOS_DISK *,
    RC95LDR_BIOS_BLOCK_CONTEXT *, RC95LDR_BLOCK_DEVICE *);
RC95LDR_STATUS Rc95LdrBiosReadBlocks(void *, uint64_t, uint32_t,
    void *, size_t);
RC95LDR_STATUS Rc95LdrBiosWriteBlocks(void *, uint64_t, uint32_t,
    const void *, size_t);
RC95LDR_STATUS Rc95LdrBiosWriteDisk(const RC95LDR_BIOS_SERVICES *,
    const RC95LDR_BIOS_DISK *, uint64_t, uint32_t, const void *, size_t);
RC95LDR_STATUS Rc95LdrBiosReadDisk(const RC95LDR_BIOS_SERVICES *,
    const RC95LDR_BIOS_DISK *, uint64_t, uint32_t, void *, size_t);
RC95LDR_STATUS Rc95LdrBiosWriteText(void *, const char *);
RC95LDR_STATUS Rc95LdrBiosSetGraphicsMode(void *, uint32_t, uint32_t);
RC95LDR_STATUS Rc95LdrBiosAcquireFramebuffer(void *, RC95LDR_BGFX_FRAMEBUFFER *);
RC95LDR_STATUS Rc95LdrBiosEnumerateFirmwareModes(void *, RC95LDR_FIRMWARE_VIDEO_MODE *, size_t, size_t *);
RC95LDR_STATUS Rc95LdrBiosSetFirmwareVideoMode(void *, uint32_t);
RC95LDR_STATUS Rc95LdrBiosMapFirmwareFramebuffer(void *, uint64_t, uint64_t, void **);
RC95LDR_STATUS Rc95LdrBiosInitializeVideo(const RC95LDR_BIOS_SERVICES *, RC95LDR_BIOS_VIDEO *, RC95LDR_BGFX_PLATFORM *);
RC95LDR_STATUS Rc95LdrBiosInitializeTextUi(
    const RC95LDR_BIOS_SERVICES *, RC95LDR_BIOS_TEXT *,
    RC95LDR_TEXTUI *, RC95LDR_TEXTUI_EXT *);
RC95LDR_STATUS Rc95LdrBiosClearText(void *);
RC95LDR_STATUS Rc95LdrBiosSetTextBackground(void *, uint8_t);
RC95LDR_STATUS Rc95LdrBiosShowErrorScreen(RC95LDR_BIOS_TEXT *,
    const RC95LDR_TEXTUI *, const char *, const char *, RC95LDR_STATUS,
    uint32_t, const char *);
RC95LDR_STATUS Rc95LdrBiosSetTextMode(void *, uint32_t, uint32_t);
RC95LDR_STATUS Rc95LdrBiosReadKey(void *, uint32_t *);

#ifdef __cplusplus
}
#endif
#endif
