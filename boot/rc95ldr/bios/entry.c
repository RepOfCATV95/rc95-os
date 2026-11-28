#include "include/rc95ldrbiosstartup.h"

/*
 * Assembly boot-protocol bridge. Keep the result storage private to this
 * non-reentrant early-startup path; the public status is returned in EAX.
 */
RC95LDR_STATUS
Rc95LdrBiosEntryMain(RC95LDR_BIOS_STARTUP *Startup)
{
    static RC95LDR_WINLOAD_RESULT Result;

    if (!Startup) return Rc95LdrInvalidParameter;
    return Rc95LdrPrepareNativePlatform(Startup, &Result);
}
