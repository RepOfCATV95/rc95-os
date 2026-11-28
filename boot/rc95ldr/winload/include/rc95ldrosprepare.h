#ifndef _RC95LDR_OSPREPARE_H_
#define _RC95LDR_OSPREPARE_H_
#include "rc95ldrnativeprepare.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Profile-specific preparation is implemented by native hive/loader-block
 * builders. ReactOS is delegated to rosload.exe; it is not routed through an
 * interchangeable callback table. */
RC95LDR_STATUS Rc95LdrPrepareOperatingSystem(
    const RC95LDR_NATIVE_PREPARE_REQUEST *Request,
    RC95LDR_NATIVE_PREPARE_RESULT *Result);
/* Explicit spelling retained for callers that want to emphasize this path. */
RC95LDR_STATUS Rc95LdrPrepareOperatingSystemNative(
    const RC95LDR_NATIVE_PREPARE_REQUEST *Request,
    RC95LDR_NATIVE_PREPARE_RESULT *Result);
#ifdef __cplusplus
}
#endif
#endif
