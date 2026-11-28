#include "rc95ldrosprepare.h"
#include "rc95ldrnativeprepare.h"

RC95LDR_STATUS
Rc95LdrPrepareOperatingSystem(
    const RC95LDR_NATIVE_PREPARE_REQUEST *Request,
    RC95LDR_NATIVE_PREPARE_RESULT *Result)
{
    RC95LDR_STATUS Status;
    if (!Request || !Result) return Rc95LdrInvalidParameter;
    Status = Rc95LdrPrepareNativeLoaderBlock(Request,Result);
    if (Status != Rc95LdrSuccess) return Status;
    /* This is a preflight gate only. No architecture stub is invoked here;
       incomplete loader extension/configuration/CPU state keeps this false. */
    Result->HandoffReady =
        Rc95LdrValidateKernelHandoff(&Result->Handoff) == Rc95LdrSuccess;
    return Rc95LdrSuccess;
}

RC95LDR_STATUS
Rc95LdrPrepareOperatingSystemNative(
    const RC95LDR_NATIVE_PREPARE_REQUEST *Request,
    RC95LDR_NATIVE_PREPARE_RESULT *Result)
{
    return Rc95LdrPrepareOperatingSystem(Request,Result);
}
