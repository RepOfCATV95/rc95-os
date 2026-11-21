#ifndef _RC95LDR_TEXTUI_H_
#define _RC95LDR_TEXTUI_H_
#include "rc95ldr.h"
#ifdef __cplusplus
extern "C" {
#endif

#define RC95LDR_TEXTUI_VERSION 1u
#define RC95LDR_TEXTUI_MAX_MENU_ITEMS 12u

typedef RC95LDR_STATUS (*RC95LDR_TEXTUI_WRITE)(void *Context,
                                                const char *Text);
typedef RC95LDR_STATUS (*RC95LDR_TEXTUI_CLEAR)(void *Context);

typedef struct _RC95LDR_TEXTUI {
    uint32_t StructureSize;
    uint32_t Version;
    void *Context;
    RC95LDR_TEXTUI_WRITE Write;
    RC95LDR_TEXTUI_CLEAR Clear;
} RC95LDR_TEXTUI;

RC95LDR_STATUS Rc95LdrTextUiValidate(const RC95LDR_TEXTUI *Ui);
/* Clears the attached console; succeeds without clearing when Clear is absent. */
RC95LDR_STATUS Rc95LdrTextUiClear(const RC95LDR_TEXTUI *Ui);
RC95LDR_STATUS Rc95LdrTextUiShowWelcome(const RC95LDR_TEXTUI *Ui,
                                        const char *Subtitle);
RC95LDR_STATUS Rc95LdrTextUiShowBootMenu(const RC95LDR_TEXTUI *Ui,
    const char *Title, const char *const *Items, size_t ItemCount,
    size_t SelectedIndex, const char *Hint);
RC95LDR_STATUS Rc95LdrTextUiShowProgress(const RC95LDR_TEXTUI *Ui,
                                         const char *Operation);
RC95LDR_STATUS Rc95LdrTextUiShowError(const RC95LDR_TEXTUI *Ui,
    const char *Title, const char *Message, RC95LDR_STATUS Status,
    uint32_t Detail, const char *Hint);

#ifdef __cplusplus
}
#endif
#endif
