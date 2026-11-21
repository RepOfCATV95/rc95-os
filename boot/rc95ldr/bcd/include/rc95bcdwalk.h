#ifndef _RC95BCD_WALK_H_
#define _RC95BCD_WALK_H_
#include "rc95ldrpolicy.h"
#ifdef __cplusplus
extern "C" {
#endif

#define RC95BCD_BOOTMGR_GUID "{9dea862c-5cdd-4e70-acc1-f32b344d4795}"
#define RC95BCD_BOOTMGR_DEFAULT_ELEMENT 0x23000003u
#define RC95BCD_BOOTMGR_DISPLAY_ORDER_ELEMENT 0x24000001u
#define RC95BCD_LIBRARY_APPLICATION_DEVICE 0x11000001u
#define RC95BCD_LIBRARY_APPLICATION_PATH 0x12000002u
#define RC95BCD_OSLOADER_OS_DEVICE 0x21000001u
#define RC95BCD_OSLOADER_SYSTEM_ROOT 0x22000002u

/* Walks Boot Manager Default/DisplayOrder and projects Windows-loader objects
 * into bounded policy candidates. Element views point into Hive->Image. */
RC95LDR_STATUS Rc95BcdCollectWindowsBootCandidates(
    const RC95BCD_HIVE *Hive,
    RC95LDR_BCD_BOOT_CANDIDATE *Candidates,
    size_t CandidateCapacity,
    size_t *CandidateCount);

#ifdef __cplusplus
}
#endif
#endif
