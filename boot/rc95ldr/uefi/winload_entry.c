#include "../winload/include/rc95ldrentry.h"
#if defined(_M_AMD64) || defined(_M_X64) || defined(_AMD64_)
#include "include/rc95ldruefihandoff.h"
#include <stddef.h>
#endif

/*
 * The PE/COFF UEFI entry stub must obtain Loaded Image, system/boot services,
 * the originating device and its file providers before constructing Startup.
 * This policy-facing entry never calls ExitBootServices itself; that operation
 * belongs to the UEFI platform's ExitFirmwareAndHandoff callback.
 */
RC95LDR_STATUS
Rc95LdrUefiWinloadEntry(const RC95LDR_WINLOAD_STARTUP *Startup,
                        RC95LDR_WINLOAD_RESULT *Result)
{
    if (!Startup || !Result ||
        Startup->Firmware != Rc95LdrFirmwareUefi)
        return Rc95LdrInvalidParameter;

#if defined(_M_AMD64) || defined(_M_X64) || defined(_AMD64_)
    {
        RC95LDR_WINLOAD_STARTUP BoundStartup;
        RC95LDR_WINLOAD_PLATFORM BoundPlatform;
        RC95LDR_UEFI_AMD64_HANDOFF_CONTEXT *HandoffContext;
        RC95LDR_STATUS Status;

        if (Startup->StructureSize <
                offsetof(RC95LDR_WINLOAD_STARTUP, FirmwareHandoffContext) +
                sizeof(Startup->FirmwareHandoffContext) ||
            !Startup->Platform ||
            Startup->Platform->StructureSize <
                offsetof(RC95LDR_WINLOAD_PLATFORM, FirmwareRuntimeServices) +
                sizeof(Startup->Platform->FirmwareRuntimeServices) ||
            !Startup->FirmwareHandoffContext)
            return Rc95LdrInvalidParameter;

        HandoffContext = (RC95LDR_UEFI_AMD64_HANDOFF_CONTEXT *)
            Startup->FirmwareHandoffContext;
        BoundStartup = *Startup;
        BoundPlatform = *Startup->Platform;

        Status = Rc95LdrUefiBindAmd64Handoff(
            &BoundPlatform, HandoffContext);
        if (Status != Rc95LdrSuccess) {
            Rc95LdrUefiReleaseAmd64HandoffContext(HandoffContext);
            return Status;
        }

        BoundStartup.Platform = &BoundPlatform;
        Status = Rc95LdrWinloadStart(&BoundStartup, Result);

        /* A successful ExitBootServices path never returns. Therefore a
         * returned status means firmware is still live and the preallocated
         * map buffer can be released safely. */
        Rc95LdrUefiReleaseAmd64HandoffContext(HandoffContext);
        return Status;
    }
#else
    return Rc95LdrWinloadStart(Startup, Result);
#endif
}
