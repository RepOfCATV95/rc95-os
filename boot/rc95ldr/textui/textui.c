#include "rc95ldrtextui.h"

static RC95LDR_STATUS
Write(const RC95LDR_TEXTUI *Ui, const char *Text)
{
    if (!Text || !*Text) return Rc95LdrSuccess;
    return Ui->Write(Ui->Context, Text);
}

RC95LDR_STATUS
Rc95LdrTextUiClear(const RC95LDR_TEXTUI *Ui)
{
    RC95LDR_STATUS Status = Rc95LdrTextUiValidate(Ui);
    if (Status != Rc95LdrSuccess) return Status;
    if (!Ui->Clear) return Rc95LdrSuccess;
    return Ui->Clear(Ui->Context);
}

static RC95LDR_STATUS
BeginScreen(const RC95LDR_TEXTUI *Ui)
{
    return Rc95LdrTextUiClear(Ui);
}

static RC95LDR_STATUS
WriteFrame(const RC95LDR_TEXTUI *Ui)
{
    return Write(Ui,
        "+----------------------------------------------------------------------------+\r\n"
        "| Republic of CATV 95 OS Loader                 |\r\n"
        "+----------------------------------------------------------------------------+\r\n");
}

RC95LDR_STATUS
Rc95LdrTextUiValidate(const RC95LDR_TEXTUI *Ui)
{
    if (!Ui || Ui->StructureSize < sizeof(*Ui) ||
        Ui->Version != RC95LDR_TEXTUI_VERSION || !Ui->Write)
        return Rc95LdrInvalidParameter;
    return Rc95LdrSuccess;
}


RC95LDR_STATUS
Rc95LdrTextUiSetVideoMode(const RC95LDR_TEXTUI_EXT *Extension,uint32_t Columns,uint32_t Rows)
{
    if (!Extension || Extension->StructureSize < sizeof(*Extension) ||
        Extension->Version != RC95LDR_TEXTUI_EXT_VERSION || !Extension->SetMode ||
        Columns == 0 || Rows == 0) return Rc95LdrInvalidParameter;
    return Extension->SetMode(Extension->Context,Columns,Rows);
}
RC95LDR_STATUS
Rc95LdrTextUiShowAdvancedOptions(const RC95LDR_TEXTUI *Ui,const char *const *Items,size_t ItemCount,size_t SelectedIndex)
{
    size_t i; RC95LDR_STATUS s;
    if (!Items || !ItemCount || ItemCount > RC95LDR_TEXTUI_MAX_MENU_ITEMS || SelectedIndex >= ItemCount)
        return Rc95LdrInvalidParameter;
    s=BeginScreen(Ui); if(s!=Rc95LdrSuccess)return s;
    s=WriteFrame(Ui);
    if(s==Rc95LdrSuccess)s=Write(Ui,"\r\n  Advanced Boot Options\r\n  Choose a diagnostic or recovery startup mode.\r\n\r\n");
    for(i=0;s==Rc95LdrSuccess && i<ItemCount;i++){
        s=Write(Ui,i==SelectedIndex?"  > ":"    ");
        if(s==Rc95LdrSuccess)s=Write(Ui,Items[i]?Items[i]:"(unnamed option)");
        if(s==Rc95LdrSuccess)s=Write(Ui,"\r\n");
    }
    if(s==Rc95LdrSuccess)s=Write(Ui,"\r\n  ENTER Select    ESC Return to boot menu\r\n");
    return s;
}
RC95LDR_STATUS
Rc95LdrTextUiRunHiberOptions(const RC95LDR_TEXTUI *Ui,
    const RC95LDR_TEXTUI_EXT *Ext,size_t *Action)
{
    static const char *Options[]={
        "Start with normal boot (delete hibernation image)",
        "Proceed with hibernation resume"
    };
    uint32_t Key;
    size_t Selected=RC95LDR_HIBER_ACTION_RESUME;
    RC95LDR_STATUS Status;
    if(!Action || Rc95LdrTextUiValidate(Ui)!=Rc95LdrSuccess ||
       !Ext || Ext->StructureSize<sizeof(*Ext) ||
       Ext->Version!=RC95LDR_TEXTUI_EXT_VERSION || !Ext->ReadKey)
        return Rc95LdrInvalidParameter;
    Status=BeginScreen(Ui);
    if(Status==Rc95LdrSuccess) Status=WriteFrame(Ui);
    if(Status==Rc95LdrSuccess) Status=Write(Ui,
        "\r\n  Resuming from hibernation\r\n"
        "  Press SPACEBAR or F8 to open advanced options.\r\n"
        "  Press ENTER to proceed with hibernation resume.\r\n");
    if(Status!=Rc95LdrSuccess)return Status;
    Status=Ext->ReadKey(Ext->Context,&Key);
    if(Status!=Rc95LdrSuccess)return Status;
    if(Key!=RC95LDR_TEXTUI_KEY_SPACE && Key!=RC95LDR_TEXTUI_KEY_F8) {
        *Action=RC95LDR_HIBER_ACTION_RESUME;
        return Rc95LdrSuccess;
    }
    for(;;) {
        size_t Index;
        Status=BeginScreen(Ui);
        if(Status==Rc95LdrSuccess)Status=WriteFrame(Ui);
        if(Status==Rc95LdrSuccess)Status=Write(Ui,
            "\r\n  Advanced Hibernation Options\r\n"
            "  Choose how RC95-OS should start.\r\n\r\n");
        for(Index=0;Status==Rc95LdrSuccess &&
            Index<RC95LDR_ARRAY_COUNT(Options);++Index) {
            Status=Write(Ui,Index==Selected?"  > ":"    ");
            if(Status==Rc95LdrSuccess)Status=Write(Ui,Options[Index]);
            if(Status==Rc95LdrSuccess)Status=Write(Ui,"\r\n");
        }
        if(Status==Rc95LdrSuccess)
            Status=Write(Ui,"\r\n  UP/DOWN Select    ENTER Confirm    ESC Resume\r\n");
        if(Status!=Rc95LdrSuccess)return Status;
        Status=Ext->ReadKey(Ext->Context,&Key);
        if(Status!=Rc95LdrSuccess)return Status;
        if(Key==RC95LDR_TEXTUI_KEY_UP || Key==RC95LDR_TEXTUI_KEY_DOWN)
            Selected=Selected==RC95LDR_HIBER_ACTION_RESUME ?
                RC95LDR_HIBER_ACTION_NORMAL_BOOT : RC95LDR_HIBER_ACTION_RESUME;
        else if(Key==RC95LDR_TEXTUI_KEY_ENTER) {
            *Action=Selected;
            return Rc95LdrSuccess;
        } else if(Key==RC95LDR_TEXTUI_KEY_ESCAPE) {
            *Action=RC95LDR_HIBER_ACTION_RESUME;
            return Rc95LdrSuccess;
        }
    }
}

RC95LDR_STATUS
Rc95LdrTextUiRunBootMenu(const RC95LDR_TEXTUI *Ui,const RC95LDR_TEXTUI_EXT *Ext,const char *Title,const char *const *Items,size_t Count,size_t Initial,size_t *SelectedIndex)
{
    size_t selected=Initial; uint32_t key; RC95LDR_STATUS s;
    if(!Ui || !Ext || !Ext->ReadKey || !SelectedIndex || !Items || !Count ||
       Count>RC95LDR_TEXTUI_MAX_MENU_ITEMS || Initial>=Count ||
       Ext->StructureSize<sizeof(*Ext) || Ext->Version!=RC95LDR_TEXTUI_EXT_VERSION)
        return Rc95LdrInvalidParameter;
    for(;;){
        s=Rc95LdrTextUiShowBootMenu(Ui,Title,Items,Count,selected,"UP/DOWN Select    ENTER Boot    F8 Advanced Options");
        if(s!=Rc95LdrSuccess)return s;
        s=Ext->ReadKey(Ext->Context,&key); if(s!=Rc95LdrSuccess)return s;
        if(key==RC95LDR_TEXTUI_KEY_UP)selected=selected?selected-1:Count-1;
        else if(key==RC95LDR_TEXTUI_KEY_DOWN)selected=(selected+1)%Count;
        else if(key==RC95LDR_TEXTUI_KEY_ENTER){*SelectedIndex=selected;return Rc95LdrSuccess;}
        else if(key==RC95LDR_TEXTUI_KEY_ESCAPE)return Rc95LdrNotFound;
        else if(key==RC95LDR_TEXTUI_KEY_F8){
            static const char *advanced[]={"Safe Mode","Safe Mode with Networking","Disable automatic restart","Enable boot logging","Enable debugging","Return to boot menu"};
            size_t a=0;
            for(;;){
                s=Rc95LdrTextUiShowAdvancedOptions(Ui,advanced,RC95LDR_ARRAY_COUNT(advanced),a);
                if(s!=Rc95LdrSuccess)return s;
                s=Ext->ReadKey(Ext->Context,&key);if(s!=Rc95LdrSuccess)return s;
                if(key==RC95LDR_TEXTUI_KEY_ESCAPE || (key==RC95LDR_TEXTUI_KEY_ENTER && a==RC95LDR_ARRAY_COUNT(advanced)-1))break;
                if(key==RC95LDR_TEXTUI_KEY_UP)a=a?a-1:RC95LDR_ARRAY_COUNT(advanced)-1;
                else if(key==RC95LDR_TEXTUI_KEY_DOWN)a=(a+1)%RC95LDR_ARRAY_COUNT(advanced);
                else if(key==RC95LDR_TEXTUI_KEY_ENTER){
                    if(!Ext->SelectAdvancedOption)return Rc95LdrUnsupported;
                    s=Ext->SelectAdvancedOption(Ext->Context,a);if(s!=Rc95LdrSuccess)return s;
                    break;
                }
            }
        }
    }
}

RC95LDR_STATUS
Rc95LdrTextUiShowWelcome(const RC95LDR_TEXTUI *Ui, const char *Subtitle)
{
    RC95LDR_STATUS Status = BeginScreen(Ui);
    if (Status != Rc95LdrSuccess) return Status;
    Status = WriteFrame(Ui);
    if (Status != Rc95LdrSuccess) return Status;
    Status = Write(Ui,
        "|                               |\r\n"
        "|          Republic of CATV 95 OS Loader               |\r\n"
        "|                               |\r\n");
    if (Status != Rc95LdrSuccess) return Status;
    Status = Write(Ui, "|  ");
    if (Status == Rc95LdrSuccess) Status = Write(Ui, Subtitle ? Subtitle :
        "Preparing the boot environment.");
    if (Status == Rc95LdrSuccess) Status = Write(Ui,
        "\r\n|                               |\r\n"
        "+----------------------------------------------------------------------------+\r\n");
    return Status;
}

RC95LDR_STATUS
Rc95LdrTextUiShowBootMenu(const RC95LDR_TEXTUI *Ui, const char *Title,
    const char *const *Items, size_t ItemCount, size_t SelectedIndex,
    const char *Hint)
{
    size_t Index;
    RC95LDR_STATUS Status;
    if ((!Items && ItemCount) || ItemCount > RC95LDR_TEXTUI_MAX_MENU_ITEMS ||
        (ItemCount && SelectedIndex >= ItemCount))
        return Rc95LdrInvalidParameter;
    Status = BeginScreen(Ui);
    if (Status != Rc95LdrSuccess) return Status;
    Status = WriteFrame(Ui);
    if (Status == Rc95LdrSuccess) Status = Write(Ui, "\r\n  ");
    if (Status == Rc95LdrSuccess) Status = Write(Ui, Title ? Title : "Boot options");
    if (Status == Rc95LdrSuccess) Status = Write(Ui, "\r\n\r\n");
    for (Index = 0; Status == Rc95LdrSuccess && Index < ItemCount; ++Index) {
        Status = Write(Ui, Index == SelectedIndex ? "  > " : "    ");
        if (Status == Rc95LdrSuccess) Status = Write(Ui, Items[Index] ? Items[Index] : "(unnamed entry)");
        if (Status == Rc95LdrSuccess) Status = Write(Ui, "\r\n");
    }
    if (Status == Rc95LdrSuccess) Status = Write(Ui, "\r\n  ");
    if (Status == Rc95LdrSuccess) Status = Write(Ui, Hint ? Hint : "Use the arrow keys to select an entry.");
    if (Status == Rc95LdrSuccess) Status = Write(Ui, "\r\n");
    return Status;
}

RC95LDR_STATUS
Rc95LdrTextUiShowProgress(const RC95LDR_TEXTUI *Ui, const char *Operation)
{
    return Rc95LdrTextUiUpdateProgress(Ui, Operation, 0);
}

RC95LDR_STATUS
Rc95LdrTextUiUpdateProgress(const RC95LDR_TEXTUI *Ui,
    const char *Operation, uint32_t Percent)
{
    /* The firmware console ABI is stream-only (Write/Clear), so repaint the
       screen rather than relying on unsupported cursor-positioning escapes. */
    char Bar[RC95LDR_TEXTUI_PROGRESS_WIDTH + 3];
    char Percentage[5];
    uint32_t Filled, Index;
    RC95LDR_STATUS Status;
    static const char PanelTop[] =
        "          +----------------------------------------------------------+\r\n";
    static const char PanelBlank[] =
        "          |                            |\r\n";
    static const char PanelBottom[] =
        "          +----------------------------------------------------------+\r\n";

    if (Percent > 100u) Percent = 100u;
    Filled = (Percent * RC95LDR_TEXTUI_PROGRESS_WIDTH) / 100u;
    Bar[0] = '[';
    for (Index = 0; Index < RC95LDR_TEXTUI_PROGRESS_WIDTH; ++Index)
        Bar[Index + 1] = (Index < Filled) ? '#' : '-';
    Bar[RC95LDR_TEXTUI_PROGRESS_WIDTH + 1] = ']';
    Bar[RC95LDR_TEXTUI_PROGRESS_WIDTH + 2] = '\0';

    if (Percent == 100u) {
        Percentage[0]='1'; Percentage[1]='0'; Percentage[2]='0';
        Percentage[3]='%'; Percentage[4]='\0';
    } else if (Percent >= 10u) {
        Percentage[0]=(char)('0'+Percent/10u);
        Percentage[1]=(char)('0'+Percent%10u);
        Percentage[2]='%'; Percentage[3]='\0';
    } else {
        Percentage[0]=(char)('0'+Percent);
        Percentage[1]='%'; Percentage[2]='\0';
    }

    Status = BeginScreen(Ui);
    if (Status != Rc95LdrSuccess) return Status;
    Status = WriteFrame(Ui);
    if (Status == Rc95LdrSuccess) Status = Write(Ui, "\r\n");
    if (Status == Rc95LdrSuccess) Status = Write(Ui, PanelTop);
    if (Status == Rc95LdrSuccess) Status = Write(Ui, PanelBlank);
    if (Status == Rc95LdrSuccess) Status = Write(Ui,
        "          |          Republic of CATV 95 OS Loader              |\r\n");
    if (Status == Rc95LdrSuccess) Status = Write(Ui, PanelBlank);
    if (Status == Rc95LdrSuccess) Status = Write(Ui,
        "          |  ");
    if (Status == Rc95LdrSuccess) Status = Write(Ui, Operation ? Operation :
        "Restoring the hibernation image.");
    if (Status == Rc95LdrSuccess) Status = Write(Ui, "\r\n");
    if (Status == Rc95LdrSuccess) Status = Write(Ui,
        "          |                            |\r\n"
        "          |  ");
    if (Status == Rc95LdrSuccess) Status = Write(Ui, Bar);
    if (Status == Rc95LdrSuccess) Status = Write(Ui, "  ");
    if (Status == Rc95LdrSuccess) Status = Write(Ui, Percentage);
    if (Status == Rc95LdrSuccess) Status = Write(Ui, "\r\n");
    if (Status == Rc95LdrSuccess) Status = Write(Ui, PanelBlank);
    if (Status == Rc95LdrSuccess) Status = Write(Ui,
        "          |                   Please wait...                      |\r\n");
    if (Status == Rc95LdrSuccess) Status = Write(Ui, PanelBlank);
    if (Status == Rc95LdrSuccess) Status = Write(Ui, PanelBottom);
    return Status;
}

static const char *
StatusName(RC95LDR_STATUS Status)
{
    switch (Status) {
        case Rc95LdrSuccess: return "Success";
        case Rc95LdrInvalidParameter: return "Invalid parameter or incompatible loader request";
        case Rc95LdrInvalidImage: return "Invalid executable or image format";
        case Rc95LdrUnsupported: return "Required feature or platform operation is unsupported";
        case Rc95LdrNotFound: return "Required file, device, or configuration entry was not found";
        case Rc95LdrBufferTooSmall: return "Provided buffer is too small";
        case Rc95LdrIntegerOverflow: return "Integer or address calculation overflow";
        case Rc95LdrCorruptData: return "Corrupt or inconsistent on-disk data";
        case Rc95LdrOutOfBounds: return "Address or data range is outside permitted bounds";
        case Rc95LdrIoError: return "Firmware or storage I/O operation failed";
        case Rc95LdrOutOfMemory: return "Insufficient memory for the requested operation";
        default: return "Unknown loader error";
    }
}

RC95LDR_STATUS
Rc95LdrTextUiShowError(const RC95LDR_TEXTUI *Ui, const char *Title,
    const char *Message, RC95LDR_STATUS ErrorStatus, uint32_t Detail,
    const char *Hint)
{
    static const char Hex[] = "0123456789ABCDEF";
    char Code[11] = "0x00000000";
    uint32_t Value = (uint32_t)ErrorStatus;
    unsigned Index;
    RC95LDR_STATUS Status = BeginScreen(Ui);
    if (Status != Rc95LdrSuccess) return Status;
    for (Index = 0; Index < 8; ++Index)
        Code[2 + Index] = Hex[(Value >> ((7 - Index) * 4)) & 0xf];
    Status = WriteFrame(Ui);
    if (Status == Rc95LdrSuccess) Status = Write(Ui, "\r\n  ERROR: ");
    if (Status == Rc95LdrSuccess) Status = Write(Ui, Title ? Title : "Boot failed");
    if (Status == Rc95LdrSuccess) Status = Write(Ui, "\r\n\r\n  ");
    if (Status == Rc95LdrSuccess) Status = Write(Ui, Message ? Message :
        "The selected operating system could not be started.");
    if (Status == Rc95LdrSuccess) Status = Write(Ui, "\r\n\r\n  Reason: ");
    if (Status == Rc95LdrSuccess) Status = Write(Ui, StatusName(ErrorStatus));
    if (Status == Rc95LdrSuccess) Status = Write(Ui, "\r\n  Status code: ");
    if (Status == Rc95LdrSuccess) Status = Write(Ui, Code);
    if (Status == Rc95LdrSuccess) Status = Write(Ui, "  Detail: 0x");
    for (Index = 0; Status == Rc95LdrSuccess && Index < 8; ++Index) {
        char Digit[2];
        Digit[0] = Hex[(Detail >> ((7 - Index) * 4)) & 0xf];
        Digit[1] = 0;
        Status = Write(Ui, Digit);
    }
    if (Status == Rc95LdrSuccess) Status = Write(Ui, "\r\n\r\n  ");
    if (Status == Rc95LdrSuccess) Status = Write(Ui, Hint ? Hint :
        "Press any key to return to the boot menu.");
    if (Status == Rc95LdrSuccess) Status = Write(Ui, "\r\n");
    return Status;
}

RC95LDR_STATUS
Rc95LdrTextUiShowBcdError(const RC95LDR_TEXTUI *Ui,
    RC95LDR_STATUS ErrorStatus, uint32_t Detail)
{
    if (ErrorStatus == Rc95LdrNotFound)
        return Rc95LdrTextUiShowError(Ui,
            "Boot Configuration Data is missing",
            "RC95LDR could not find a BCD store at the expected boot-volume path. The boot configuration cannot be read, so no Windows installation can be selected.",
            ErrorStatus, Detail,
            "Verify that the boot partition contains \\Boot\\BCD (BIOS) or \\EFI\\Microsoft\\Boot\\BCD (UEFI), and rebuild the store if necessary.");
    if (ErrorStatus == Rc95LdrInvalidImage ||
        ErrorStatus == Rc95LdrCorruptData)
        return Rc95LdrTextUiShowError(Ui,
            "Boot Configuration Data is corrupt",
            "RC95LDR found a BCD file, but its registry hive is invalid, truncated, or internally inconsistent. The store was rejected to avoid booting from an unsafe configuration.",
            Rc95LdrCorruptData, Detail,
            "Restore a known-good BCD backup or rebuild the boot configuration with the Windows recovery tools.");
    return Rc95LdrTextUiShowError(Ui,
        "Unable to read Boot Configuration Data",
        "RC95LDR encountered an error while opening, reading, or validating the BCD store.",
        ErrorStatus, Detail,
        "Check the boot volume and its filesystem, then retry.");
}
