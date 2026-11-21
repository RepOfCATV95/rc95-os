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
        "| RC95-OS  /  RC95LDR                                                        |\r\n"
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
Rc95LdrTextUiShowWelcome(const RC95LDR_TEXTUI *Ui, const char *Subtitle)
{
    RC95LDR_STATUS Status = BeginScreen(Ui);
    if (Status != Rc95LdrSuccess) return Status;
    Status = WriteFrame(Ui);
    if (Status != Rc95LdrSuccess) return Status;
    Status = Write(Ui,
        "|                                                                            |\r\n"
        "|                         RC95LDR BOOT MANAGER                              |\r\n"
        "|                                                                            |\r\n");
    if (Status != Rc95LdrSuccess) return Status;
    Status = Write(Ui, "|  ");
    if (Status == Rc95LdrSuccess) Status = Write(Ui, Subtitle ? Subtitle :
        "Preparing the boot environment.");
    if (Status == Rc95LdrSuccess) Status = Write(Ui,
        "\r\n|                                                                            |\r\n"
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
    RC95LDR_STATUS Status = BeginScreen(Ui);
    if (Status != Rc95LdrSuccess) return Status;
    Status = WriteFrame(Ui);
    if (Status == Rc95LdrSuccess) Status = Write(Ui,
        "\r\n  Starting Windows...\r\n\r\n  ");
    if (Status == Rc95LdrSuccess) Status = Write(Ui, Operation ? Operation :
        "Preparing boot files.");
    if (Status == Rc95LdrSuccess) Status = Write(Ui,
        "\r\n\r\n  Please wait.\r\n");
    return Status;
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
    if (Status == Rc95LdrSuccess) Status = Write(Ui, "\r\n\r\n  Status: ");
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
