#ifndef _RC95LDR_TEXTUI_H_
#define _RC95LDR_TEXTUI_H_
#include "rc95ldr.h"
#ifdef __cplusplus
extern "C" {
#endif
#define RC95LDR_TEXTUI_VERSION 1u
#define RC95LDR_TEXTUI_MAX_MENU_ITEMS 12u
#define RC95LDR_TEXTUI_PROGRESS_WIDTH 50u
#define RC95LDR_TEXTUI_MODE_COLUMNS_80 80u
#define RC95LDR_TEXTUI_MODE_ROWS_50 50u
#define RC95LDR_TEXTUI_KEY_UP 0x100u
#define RC95LDR_TEXTUI_KEY_DOWN 0x101u
#define RC95LDR_TEXTUI_KEY_ENTER 0x102u
#define RC95LDR_TEXTUI_KEY_ESCAPE 0x103u
#define RC95LDR_TEXTUI_KEY_F8 0x108u
#define RC95LDR_TEXTUI_KEY_SPACE 0x020u
typedef RC95LDR_STATUS (*RC95LDR_TEXTUI_WRITE)(void *Context,const char *Text);
typedef RC95LDR_STATUS (*RC95LDR_TEXTUI_CLEAR)(void *Context);
typedef RC95LDR_STATUS (*RC95LDR_TEXTUI_SET_MODE)(void *Context,uint32_t Columns,uint32_t Rows);
typedef RC95LDR_STATUS (*RC95LDR_TEXTUI_READ_KEY)(void *Context,uint32_t *Key);
typedef RC95LDR_STATUS (*RC95LDR_TEXTUI_ADVANCED_OPTION)(void *Context,size_t OptionIndex);
#define RC95LDR_TEXTUI_EXT_VERSION 1u
typedef struct _RC95LDR_TEXTUI_EXT {
    uint32_t StructureSize;
    uint32_t Version;
    void *Context;
    RC95LDR_TEXTUI_SET_MODE SetMode;
    RC95LDR_TEXTUI_READ_KEY ReadKey;
    RC95LDR_TEXTUI_ADVANCED_OPTION SelectAdvancedOption;
} RC95LDR_TEXTUI_EXT;
typedef struct _RC95LDR_TEXTUI {
    uint32_t StructureSize;
    uint32_t Version;
    void *Context;
    RC95LDR_TEXTUI_WRITE Write;
    RC95LDR_TEXTUI_CLEAR Clear;
} RC95LDR_TEXTUI;
RC95LDR_STATUS Rc95LdrTextUiValidate(const RC95LDR_TEXTUI *Ui);
RC95LDR_STATUS Rc95LdrTextUiClear(const RC95LDR_TEXTUI *Ui);
RC95LDR_STATUS Rc95LdrTextUiSetVideoMode(const RC95LDR_TEXTUI_EXT *Extension,uint32_t Columns,uint32_t Rows);
RC95LDR_STATUS Rc95LdrTextUiShowWelcome(const RC95LDR_TEXTUI *Ui,const char *Subtitle);
RC95LDR_STATUS Rc95LdrTextUiShowBootMenu(const RC95LDR_TEXTUI *Ui,const char *Title,const char *const *Items,size_t ItemCount,size_t SelectedIndex,const char *Hint);
RC95LDR_STATUS Rc95LdrTextUiRunBootMenu(const RC95LDR_TEXTUI *Ui,const RC95LDR_TEXTUI_EXT *Extension,const char *Title,const char *const *Items,size_t ItemCount,size_t InitialIndex,size_t *SelectedIndex);
RC95LDR_STATUS Rc95LdrTextUiRunHiberOptions(const RC95LDR_TEXTUI *Ui,const RC95LDR_TEXTUI_EXT *Extension,size_t *Action);
#define RC95LDR_HIBER_ACTION_NORMAL_BOOT 0u
#define RC95LDR_HIBER_ACTION_RESUME 1u
RC95LDR_STATUS Rc95LdrTextUiShowAdvancedOptions(const RC95LDR_TEXTUI *Ui,const char *const *Items,size_t ItemCount,size_t SelectedIndex);
RC95LDR_STATUS Rc95LdrTextUiShowProgress(const RC95LDR_TEXTUI *Ui,const char *Operation);
RC95LDR_STATUS Rc95LdrTextUiUpdateProgress(const RC95LDR_TEXTUI *Ui,const char *Operation,uint32_t Percent);
RC95LDR_STATUS Rc95LdrTextUiShowError(const RC95LDR_TEXTUI *Ui,const char *Title,const char *Message,RC95LDR_STATUS Status,uint32_t Detail,const char *Hint);
RC95LDR_STATUS Rc95LdrTextUiShowBcdError(const RC95LDR_TEXTUI *Ui,RC95LDR_STATUS Status,uint32_t Detail);
#ifdef __cplusplus
}
#endif
#endif
