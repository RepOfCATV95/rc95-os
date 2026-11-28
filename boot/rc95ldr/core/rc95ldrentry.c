#include "rc95ldrentry.h"

RC95LDR_ARCHITECTURE
Rc95LdrGetBuildArchitecture(void)
{
#if defined(RC95LDR_ARCH_I386)
    return Rc95LdrArchitectureI386;
#elif defined(RC95LDR_ARCH_AMD64)
    return Rc95LdrArchitectureAmd64;
#else
    return Rc95LdrArchitectureUnknown;
#endif
}

RC95LDR_STATUS
Rc95LdrNormalBootEntry(
    RC95LDR_BOOT_CONTEXT *Context,
    void *PlatformContext)
{
    RC95LDR_ARCHITECTURE Architecture;

    if (Context == NULL)
        return Rc95LdrInvalidParameter;

    Rc95LdrInitializeContext(Context, PlatformContext);
    Architecture = Rc95LdrGetBuildArchitecture();
    if (Architecture != Rc95LdrArchitectureI386 &&
        Architecture != Rc95LdrArchitectureAmd64)
        return Rc95LdrFail(Context, Rc95LdrUnsupported,
                           (uint32_t)Architecture);

    /*
     * This is the stable, architecture-neutral entry seam. Do not claim that
     * discovery or Windows loading succeeded here: the platform adapters,
     * PE loader, loader-block construction and kernel handoff are still being
     * migrated from FreeLoader/rosload.
     */
    Rc95LdrSetStage(Context, Rc95LdrStagePlatform);
    return Rc95LdrFail(Context, Rc95LdrUnsupported,
                       (uint32_t)Architecture);
}
