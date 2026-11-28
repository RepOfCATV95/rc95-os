#include "rc95ldrentry.h"
#include <string.h>
#include <stddef.h>

#define RC95LDR_ENTRY_BUILD_REQUEST_FAILED 0x200u
#define RC95LDR_ENTRY_REQUEST_INVALID      0x201u

RC95LDR_STATUS
Rc95LdrWinloadStart(const RC95LDR_WINLOAD_STARTUP *Startup,
                    RC95LDR_WINLOAD_RESULT *Result)
{
    RC95LDR_WINLOAD_REQUEST Request;
    RC95LDR_STATUS Status;

    if (!Startup || !Result ||
        Startup->StructureSize < offsetof(RC95LDR_WINLOAD_STARTUP, FirmwareHandoffContext) ||
        Startup->Version != RC95LDR_STARTUP_ABI_VERSION ||
        !Startup->BootContext || !Startup->Platform ||
        !Startup->BuildBootRequest || !Startup->ReleaseBootRequest ||
        (Startup->Firmware != Rc95LdrFirmwareBios &&
         Startup->Firmware != Rc95LdrFirmwareUefi) ||
        Startup->Platform->StructureSize <
            offsetof(RC95LDR_WINLOAD_PLATFORM, TextUi) ||
        Startup->Platform->Version != RC95LDR_WINLOAD_ABI_VERSION)
        return Rc95LdrInvalidParameter;

    /*
     * Result is an output ABI, not an input contract. Initialize it here so
     * firmware entry shims cannot accidentally pass an uninitialized result
     * header into Rc95LdrLoadAndBootWindows (which validates that header).
     */
    memset(Result, 0, sizeof(*Result));
    Result->StructureSize = sizeof(*Result);
    Result->Version = RC95LDR_WINLOAD_ABI_VERSION;

    memset(&Request, 0, sizeof(Request));
    Request.StructureSize = sizeof(Request);
    Request.Version = RC95LDR_WINLOAD_ABI_VERSION;
    Request.Firmware = Startup->Firmware;

    Rc95LdrInitializeContext(Startup->BootContext, Startup->Context);
    Rc95LdrSetStage(Startup->BootContext, Rc95LdrStageStartup);

    Status = Startup->BuildBootRequest(Startup->Context, &Request);
    if (Status == Rc95LdrSuccess && Startup->SelectedInstallation)
        Status = Rc95LdrApplyInstallationGraphicsPolicy(
            Startup->SelectedInstallation,&Request);
    if (Status == Rc95LdrSuccess)
    {
        /* Firmware identity is authoritative; a request builder cannot
         * accidentally route a BIOS request through the UEFI backend. */
        if (Request.Firmware != Startup->Firmware ||
            Request.StructureSize < sizeof(Request) ||
            Request.Version != RC95LDR_WINLOAD_ABI_VERSION)
            Status = Rc95LdrFail(Startup->BootContext,
                                 Rc95LdrInvalidParameter,
                                 RC95LDR_ENTRY_REQUEST_INVALID);
        else
            Status = Rc95LdrLoadAndBootWindows(Startup->BootContext,
                                                Startup->Platform,
                                                &Request, Result);
    }
    else
    {
        Status = Rc95LdrFail(Startup->BootContext, Status,
                             RC95LDR_ENTRY_BUILD_REQUEST_FAILED);
    }

    /*
     * BuildBootRequest may allocate discovery/BCD state before reporting an
     * error. ReleaseBootRequest is therefore paired with every invocation,
     * including partial-build failures; providers must accept a zeroed or
     * partially populated request.
     */
    Startup->ReleaseBootRequest(Startup->Context, &Request);
    return Status;
}
