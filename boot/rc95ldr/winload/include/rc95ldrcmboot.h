#ifndef _RC95LDR_CMBoot_ADAPTER_H_
#define _RC95LDR_CMBoot_ADAPTER_H_

#include "rc95ldrsystemhive.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Uses the bootloader build of ntoskrnl/config/cmboot.c and cmlib to select,
 * order, and enumerate SYSTEM-hive boot-start services. */
RC95LDR_STATUS Rc95LdrCmEnumerateBootDrivers(
    const void *SystemHiveBytes,
    size_t SystemHiveSize,
    RC95LDR_HIVE_BOOT_DRIVER_CALLBACK Callback,
    void *Context);

#ifdef __cplusplus
}
#endif
#endif
