#include "include/rc95ldrbiosamd64handoff.h"
#include "rc95ldramd64transfer.h"

RC95LDR_STATUS
Rc95LdrBiosAmd64TransferHandoff(
    void *Context, RC95LDR_KERNEL_HANDOFF_CONTEXT *Handoff)
{
    (void)Context;
    if (!Handoff || Handoff->Firmware != Rc95LdrFirmwareBios ||
        Handoff->Architecture != Rc95LdrNtArchAmd64)
        return Rc95LdrInvalidParameter;
    return Rc95LdrAmd64TransferKernel(Handoff);
}
