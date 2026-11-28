#include "../winload/include/rc95ldrentry.h"

/*
 * BIOS startup code must establish a protected-mode C environment, stack,
 * memory map, disk services and a firmware-owned context before calling here.
 * This function is deliberately not an ARC/FreeLoader compatibility shim.
 */
RC95LDR_STATUS
Rc95LdrBiosWinloadEntry(const RC95LDR_WINLOAD_STARTUP *Startup,
                        RC95LDR_WINLOAD_RESULT *Result)
{
    if (!Startup || Startup->Firmware != Rc95LdrFirmwareBios)
        return Rc95LdrInvalidParameter;
    return Rc95LdrWinloadStart(Startup, Result);
}
