#ifndef _RC95LDR_FIRMWARE_MENU_H_
#define _RC95LDR_FIRMWARE_MENU_H_
#include "rc95ldrbgfx.h"
#include "rc95bcdwalk.h"
#include "rc95ldrpolicy.h"
#ifdef __cplusplus
extern "C" {
#endif
#define RC95LDR_FIRMWARE_MENU_VERSION 3u
#define RC95LDR_FIRMWARE_MAX_VIDEO_MODES 64u
typedef enum _RC95LDR_FIRMWARE_KIND { Rc95LdrFirmwareMenuBios=1, Rc95LdrFirmwareMenuUefi=2 } RC95LDR_FIRMWARE_KIND;
/* Firmware adapters normalize VBE/GOP formats to the BGFX pixel-format IDs.
 * FramebufferBase is the physical framebuffer address; MapFramebuffer must
 * return a loader-accessible mapping valid until the menu returns. */
typedef struct _RC95LDR_FIRMWARE_VIDEO_MODE {
    uint32_t ModeId, Width, Height, PixelsPerScanLine, PixelFormat;
    uint64_t FramebufferBase, FramebufferSize;
} RC95LDR_FIRMWARE_VIDEO_MODE;
typedef RC95LDR_STATUS (*RC95LDR_FIRMWARE_RESTART)(void *Context);
typedef struct _RC95LDR_FIRMWARE_MENU_OPS {
    uint32_t StructureSize, Version;
    void *Context;
    RC95LDR_STATUS (*EnumerateModes)(void *, RC95LDR_FIRMWARE_VIDEO_MODE *, size_t, size_t *);
    RC95LDR_STATUS (*SetMode)(void *, uint32_t);
    RC95LDR_STATUS (*MapFramebuffer)(void *, uint64_t, uint64_t, void **);
    RC95LDR_TEXTUI_READ_KEY ReadKey;
    RC95LDR_TEXTUI_ADVANCED_OPTION SelectAdvancedOption;
    const RC95LDR_TEXTUI *TextUi;
    const RC95LDR_TEXTUI_EXT *TextUiExtension;
    /* Optional output receives the exact selected loader's BCD Boolean. */
    uint32_t *GraphicsModeDisabledOutput;
    /* Optional platform reset callback; candidate menus expose Restart only
       when this callback is bound by the active firmware adapter. */
    void *RestartContext;
    RC95LDR_FIRMWARE_RESTART RestartSystem;
} RC95LDR_FIRMWARE_MENU_OPS;
/* Reads the OS-loader BCD Boolean element. Missing means the documented
 * default (graphics enabled); malformed payloads are rejected. */
RC95LDR_STATUS Rc95BcdReadGraphicsModeDisabled(const RC95BCD_HIVE *, uint32_t, uint32_t *);
RC95LDR_STATUS Rc95LdrRunFirmwareBootMenu(RC95LDR_FIRMWARE_KIND,
    const RC95LDR_FIRMWARE_MENU_OPS *, const RC95BCD_HIVE *, uint32_t,
    const char *, const char *const *, size_t, size_t, size_t *);
/* Candidate-native menu: the returned pointer is the exact record from
 * Candidates, not an index into a parallel compatibility list. Candidate
 * storage and its source BCD hive must remain alive until dispatch completes. */
RC95LDR_STATUS Rc95LdrRunFirmwareCandidateMenu(
    RC95LDR_FIRMWARE_KIND,
    const RC95LDR_FIRMWARE_MENU_OPS *,
    const RC95BCD_HIVE *,
    const RC95LDR_BCD_BOOT_CANDIDATE *, size_t,
    const char *, const char *const *, size_t,
    const RC95LDR_BCD_BOOT_CANDIDATE **);
RC95LDR_STATUS Rc95LdrRunBiosCandidateMenu(
    const RC95LDR_FIRMWARE_MENU_OPS *, const RC95BCD_HIVE *,
    const RC95LDR_BCD_BOOT_CANDIDATE *, size_t,
    const char *, const char *const *,
    const RC95LDR_BCD_BOOT_CANDIDATE **);
RC95LDR_STATUS Rc95LdrRunUefiCandidateMenu(
    const RC95LDR_FIRMWARE_MENU_OPS *, const RC95BCD_HIVE *,
    const RC95LDR_BCD_BOOT_CANDIDATE *, size_t,
    const char *, const char *const *, size_t Initial,
    const RC95LDR_BCD_BOOT_CANDIDATE **);
RC95LDR_STATUS Rc95LdrRunBiosBootMenu(const RC95LDR_FIRMWARE_MENU_OPS *,
    const RC95BCD_HIVE *, uint32_t, const char *, const char *const *, size_t, size_t, size_t *);
RC95LDR_STATUS Rc95LdrRunUefiBootMenu(const RC95LDR_FIRMWARE_MENU_OPS *,
    const RC95BCD_HIVE *, uint32_t, const char *, const char *const *, size_t, size_t, size_t *);
#ifdef __cplusplus
}
#endif
#endif
