#ifndef _RC95LDR_BIOS_STARTUP_H_
#define _RC95LDR_BIOS_STARTUP_H_

#include "rc95ldrbios.h"
#include "rc95ldrbioscd.h"
#include "rc95ldrentry.h"
#include "rc95ldrfirmwaremenu.h"
#include "rc95ldrbiosntfsboot.h"
#include "rc95ldrbiosbtrfsboot.h"
#include "rc95ldrrc95nativeprovider.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RC95LDR_BIOS_STARTUP_VERSION 6u
#define RC95LDR_BIOS_ENTRY_MAGIC 0x52353942u /* 'B95R' */

typedef struct _RC95LDR_BIOS_STARTUP RC95LDR_BIOS_STARTUP;

/*
 * Native i386 boot protocol: enter Rc95LdrBiosI386Entry with EAX set to
 * RC95LDR_BIOS_ENTRY_MAGIC and EBX set to a flat pointer to a fully populated
 * RC95LDR_BIOS_STARTUP. See README.md for the required CPU/GDT state.
 * The assembly entry returns RC95LDR_STATUS in EAX and restores the caller's
 * stack. This is a protected-mode loader protocol, not a BIOS boot-sector ABI.
 */
#if defined(_M_IX86) || defined(__i386__)
/* Raw register-level entry plus its ordinary C-callable stage bridge. */
void Rc95LdrBiosI386Entry(void);
RC95LDR_STATUS Rc95LdrBiosInvokeNativeEntry(
    RC95LDR_BIOS_STARTUP *Startup);
#elif defined(_M_AMD64) || defined(_M_X64) || defined(_AMD64_) || defined(__x86_64__) || defined(__amd64__)
/*
 * The raw AMD64 entry label is a register-level boot protocol, not a normal
 * C-callable function: RCX=Startup and EDX=RC95LDR_BIOS_ENTRY_MAGIC. It must
 * only be entered by the assembly bridge below, with long mode and paging
 * active. Calling it as an ordinary C function leaves EDX unspecified.
 *
 * This bridge is the C ABI entry used by the stage coordinator. It supplies
 * the protocol magic and preserves the caller's nonvolatile registers.
 */
RC95LDR_STATUS Rc95LdrBiosInvokeNativeEntry(
    RC95LDR_BIOS_STARTUP *Startup);
#endif

/*
 * Caller-owned storage and platform policy for native BIOS paths.
 * Entry is called after the BIOS boot protocol has transferred control to
 * RC95LDR's flat 32-bit protected-mode environment (paging off, identity
 * mapping, A20 on, CS=08h and data/stack selectors=10h). The private gateway
 * performs subsequent real-mode BIOS calls; this is not a FreeLoader shim.
 *
 * Context is shared by BuildBootRequest, ReleaseBootRequest and Platform.
 * The request builder can obtain the initialized BlockDevice, BIOS memory
 * ranges, and other startup objects through the caller's context.
 */
struct _RC95LDR_BIOS_STARTUP {
    uint32_t StructureSize;
    uint32_t Version;
    void *Context;
    RC95LDR_BIOS_SERVICES *BiosServices;
    RC95LDR_MEMORY_RESERVATION_SET *MemoryReservations;
    RC95LDR_FIRMWARE_MEMORY_RANGE *FirmwareMemoryRanges;
    size_t FirmwareMemoryCapacity;
    size_t FirmwareMemoryCount;
    RC95LDR_BIOS_DISK Disk;
    RC95LDR_BIOS_BLOCK_CONTEXT BlockContext;
    RC95LDR_BLOCK_DEVICE BlockDevice;
    RC95LDR_ELTORITO_BOOT_INFO ElTorito;
    RC95LDR_ISO9660 IsoVolume;
    RC95LDR_FILE_PROVIDER_SCOPE FileProviderScope;
    uint8_t IsoMounted;
    uint8_t ReservedIso[7];
    RC95LDR_BIOS_TEXT TextContext;
    RC95LDR_BIOS_VIDEO VideoContext;
    RC95LDR_BGFX_PLATFORM VideoPlatform;
    RC95LDR_BGFX_FRAMEBUFFER Framebuffer;
    /* Set RC95LDR_FIRMWARE_MENU_OPS.GraphicsModeDisabledOutput to this field
       so the exact selected loader's BCD Boolean reaches BIOS startup. */
    uint32_t GraphicsModeDisabled;
    RC95LDR_TEXTUI TextUi;
    RC95LDR_TEXTUI_EXT TextUiExtension;
    RC95LDR_BOOT_CONTEXT BootContext;
    RC95LDR_WINLOAD_PLATFORM Platform;
    RC95LDR_BUILD_BOOT_REQUEST BuildBootRequest;
    RC95LDR_RELEASE_BOOT_REQUEST ReleaseBootRequest;
    /* Required for non-ISO hard-disk boots: caller-owned BCD/NTFS buffers. */
    RC95LDR_BIOS_NTFS_BOOT_CONFIG *NtfsBoot;
    /* Alternative hard-disk boot transaction for a BCD-selected Btrfs OS volume. */
    RC95LDR_BIOS_BTRFS_BOOT_CONFIG *BtrfsBoot;
    /* Optional RC95-OS provider; BIOS binds its callbacks directly into WINLOAD. */
    RC95LDR_RC95_NATIVE_PLATFORM_CONTEXT *NativeProvider;
    /* Optional interactive BCD boot menu. Items must correspond to the
       filtered candidate order supplied by BIOS discovery. */
    RC95LDR_FIRMWARE_MENU_OPS *FirmwareMenuOps;
    const char *FirmwareMenuTitle;
    const char *const *FirmwareMenuItems;
};

/* Bind the firmware-menu BCD output directly to Startup->GraphicsModeDisabled.
 * Call before Rc95LdrRunBiosCandidateMenu/Rc95LdrRunBiosBootMenu. */
RC95LDR_STATUS Rc95LdrBiosBindFirmwareMenuOps(
    RC95LDR_BIOS_STARTUP *Startup,
    RC95LDR_FIRMWARE_MENU_OPS *MenuOps);
/* Preferred entry point: binds the BCD output before running the menu. */
RC95LDR_STATUS Rc95LdrBiosRunCandidateMenu(
    RC95LDR_BIOS_STARTUP *Startup,
    RC95LDR_FIRMWARE_MENU_OPS *MenuOps,
    const RC95BCD_HIVE *Hive,
    const RC95LDR_BCD_BOOT_CANDIDATE *Candidates,
    size_t CandidateCount,
    const char *Title,
    const char *const *Items,
    const RC95LDR_BCD_BOOT_CANDIDATE **Selected);

/*
 * Initializes the RC95LDR-owned real-mode gateway, reserves its low-memory
 * window, obtains E820 and EDD information, installs the BIOS text console,
 * then enters the common WINLOAD coordinator. Returns only on failure or
 * when the shared loader path reports a recoverable error.
 */
RC95LDR_STATUS Rc95LdrPrepareNativePlatform(
    RC95LDR_BIOS_STARTUP *Startup,
    RC95LDR_WINLOAD_RESULT *Result);

#ifdef __cplusplus
}
#endif
#endif
