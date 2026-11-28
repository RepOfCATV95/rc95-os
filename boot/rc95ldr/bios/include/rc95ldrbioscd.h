#ifndef _RC95LDR_BIOS_CD_H_
#define _RC95LDR_BIOS_CD_H_
#include "rc95ldrbiosdisk.h"
#include "rc95ldr_iso9660.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Identifies the firmware's El Torito boot drive, probes its EDD geometry,
 * and mounts its ISO9660 filesystem. Caller-owned Disk, BlockContext,
 * BlockDevice and Volume must remain alive while the mount is in use.
 * Only no-emulation El Torito media with 2048-byte EDD blocks are accepted. */
RC95LDR_STATUS Rc95LdrBiosMountElToritoIso(
    const RC95LDR_BIOS_SERVICES *Services,
    uint8_t Drive,
    RC95LDR_ELTORITO_BOOT_INFO *BootInfo,
    RC95LDR_BIOS_DISK *Disk,
    RC95LDR_BIOS_BLOCK_CONTEXT *BlockContext,
    RC95LDR_BLOCK_DEVICE *BlockDevice,
    RC95LDR_ISO9660 *Volume);
#ifdef __cplusplus
}
#endif
#endif
