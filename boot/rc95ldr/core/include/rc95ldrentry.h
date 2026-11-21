#ifndef _RC95LDR_ENTRY_H_
#define _RC95LDR_ENTRY_H_

#include "rc95ldr.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum _RC95LDR_ARCHITECTURE
{
    Rc95LdrArchitectureUnknown = 0,
    Rc95LdrArchitectureI386 = 1,
    Rc95LdrArchitectureAmd64 = 2
} RC95LDR_ARCHITECTURE;

/* Returns the architecture selected by the FreeLoader build configuration. */
RC95LDR_ARCHITECTURE Rc95LdrGetBuildArchitecture(void);

/*
 * Canonical normal-boot entry used by the x86 FreeLoader integration.
 * PlatformContext is reserved for the forthcoming firmware/platform adapter.
 * Until that adapter can complete discovery and winload handoff, this routine
 * initializes the common boot context and returns Rc95LdrUnsupported so the
 * caller can take its explicit compatibility path.
 */
RC95LDR_STATUS Rc95LdrNormalBootEntry(
    RC95LDR_BOOT_CONTEXT *Context,
    void *PlatformContext);

#ifdef __cplusplus
}
#endif
#endif
