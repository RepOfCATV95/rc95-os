#include "include/rc95ldruefinativeboot.h"
#include "include/rc95ldruefiservices.h"
#include <string.h>

typedef struct _RC95LDR_UEFI_MENU_CONTEXT {
    const RC95LDR_FIRMWARE_MENU_OPS *MenuOps;
} RC95LDR_UEFI_MENU_CONTEXT;

static size_t UefiAppendNumber(char *Buffer, size_t Capacity,
                               size_t Position, size_t Value)
{
    char Digits[20];
    size_t Count = 0;
    do {
        Digits[Count++] = (char)('0' + (Value % 10u));
        Value /= 10u;
    } while (Value && Count < sizeof(Digits));
    while (Count && Position + 1u < Capacity)
        Buffer[Position++] = Digits[--Count];
    Buffer[Position] = 0;
    return Position;
}

#define RC95BCD_LIBRARY_STRING_DESCRIPTION 0x12000004u

static size_t UefiAppendBcdDescription(
    char *Buffer, size_t Capacity, size_t Position,
    const RC95LDR_BCD_BOOT_CANDIDATE *Candidate)
{
    uint32_t ElementIndex;
    if (Candidate->ElementCount > RC95LDR_MAX_BCD_ELEMENTS ||
        Candidate->ElementStringCharacters > RC95LDR_MAX_BCD_STRING_CHARS)
        return Position;
    for (ElementIndex = 0; ElementIndex < Candidate->ElementCount;
         ++ElementIndex)
    {
        const RC95LDR_BCD_CANDIDATE_ELEMENT *Element =
            &Candidate->Elements[ElementIndex];
        uint32_t CharacterIndex;
        if (Element->ElementId != RC95BCD_LIBRARY_STRING_DESCRIPTION)
            continue;
        if (Element->Kind != Rc95BcdElementString ||
            Element->StringOffset > Candidate->ElementStringCharacters ||
            Element->StringCharacters >
                Candidate->ElementStringCharacters - Element->StringOffset)
            return Position;
        if (!Element->StringCharacters) return Position;
        if (Position + 2u < Capacity)
        {
            Buffer[Position++] = ':';
            Buffer[Position++] = ' ';
        }
        for (CharacterIndex = 0;
             CharacterIndex < Element->StringCharacters &&
             Position + 1u < Capacity; ++CharacterIndex)
        {
            uint16_t Ch = Candidate->ElementStringStorage[
                Element->StringOffset + CharacterIndex];
            Buffer[Position++] = (Ch >= 0x20u && Ch <= 0x7eu) ?
                (char)Ch : ' ';
        }
        Buffer[Position] = 0;
        return Position;
    }
    return Position;
}

static RC95LDR_STATUS UefiSelectBootCandidate(
    void *Opaque, const RC95BCD_HIVE *Hive,
    const RC95LDR_BCD_BOOT_CANDIDATE *Candidates, size_t CandidateCount,
    const RC95LDR_BCD_BOOT_CANDIDATE **Selected)
{
    RC95LDR_UEFI_MENU_CONTEXT *Context =
        (RC95LDR_UEFI_MENU_CONTEXT *)Opaque;
    char Labels[RC95LDR_TEXTUI_MAX_MENU_ITEMS][80];
    const char *Items[RC95LDR_TEXTUI_MAX_MENU_ITEMS];
    size_t Index, Initial = 0;
    if (!Context || !Context->MenuOps || !Candidates || !CandidateCount ||
        !Selected)
        return Rc95LdrInvalidParameter;
    /* Reserve the final row only when firmware restart is available. */
    if (CandidateCount > RC95LDR_TEXTUI_MAX_MENU_ITEMS ||
        (Context->MenuOps->RestartSystem &&
         CandidateCount >= RC95LDR_TEXTUI_MAX_MENU_ITEMS))
        return Rc95LdrBufferTooSmall;
    for (Index = 0; Index < CandidateCount; ++Index)
    {
        size_t Position = 0;
        const char Prefix[] = "Windows Boot Entry ";
        size_t P;
        for (P = 0; Prefix[P] && Position + 1u < sizeof(Labels[Index]); ++P)
            Labels[Index][Position++] = Prefix[P];
        Position = UefiAppendNumber(Labels[Index], sizeof(Labels[Index]),
                                    Position, Index + 1u);
        Position = UefiAppendBcdDescription(Labels[Index],
            sizeof(Labels[Index]), Position, &Candidates[Index]);
        if (Candidates[Index].IsDefault)
        {
            const char Suffix[] = " (default)";
            for (P = 0; Suffix[P] && Position + 1u < sizeof(Labels[Index]); ++P)
                Labels[Index][Position++] = Suffix[P];
            Labels[Index][Position] = 0;
            Initial = Index;
        }
        Items[Index] = Labels[Index];
    }
    return Rc95LdrRunUefiCandidateMenu(Context->MenuOps, Hive,
        Candidates, CandidateCount, "Choose an operating system",
        Items, Initial, Selected);
}

RC95LDR_STATUS
Rc95LdrUefiNativeDiscoverAndStartWindows(
    RC95LDR_UEFI_NATIVE_DISCOVERY_CONTEXT *Context,
    const RC95LDR_UEFI_IMAGE_CONTEXT *Image,
    uint32_t Architecture,
    RC95LDR_PARTITION_INFO *Partitions,
    size_t PartitionCapacity,
    RC95LDR_BCD_BOOT_CANDIDATE *Candidates,
    size_t CandidateCapacity,
    size_t *CandidateCount,
    void *HiveBuffer,
    size_t HiveCapacity,
    const RC95LDR_WINLOAD_STARTUP *Startup,
    RC95LDR_WINLOAD_RESULT *Result,
    RC95LDR_WINDOWS_INSTALLATION *SelectedInstallation,
    size_t *SelectedEspIndex,
    const RC95LDR_TEXTUI *TextUi)
{
    RC95LDR_UEFI_DISCOVERY_OPS Discovery;
    RC95LDR_UEFI_VIDEO Video;
    RC95LDR_FIRMWARE_MENU_OPS MenuOps;
    RC95LDR_TEXTUI_EXT TextUiExtension;
    RC95LDR_UEFI_MENU_CONTEXT MenuContext;
    RC95LDR_STATUS Status;

    if (!Context || !Image || !Partitions || !PartitionCapacity ||
        !Candidates || !CandidateCapacity || !CandidateCount ||
        !HiveBuffer || !HiveCapacity || !Startup || !Result ||
        !SelectedInstallation || !SelectedEspIndex)
        return Rc95LdrInvalidParameter;

    memset(Context, 0, sizeof(*Context));
    memset(&Discovery, 0, sizeof(Discovery));
    memset(&Video, 0, sizeof(Video));
    memset(&MenuOps, 0, sizeof(MenuOps));
    memset(&TextUiExtension, 0, sizeof(TextUiExtension));
    memset(&MenuContext, 0, sizeof(MenuContext));

    Status = Rc95LdrUefiInitializeNativeDiscovery(
        Context, Image, Architecture);
    if (Status != Rc95LdrSuccess)
        return Status;

    Status = Rc95LdrUefiGetNativeDiscoveryOps(Context, &Discovery);
    if (Status == Rc95LdrSuccess)
    {
        Discovery.TextUi = TextUi;
        if (TextUi && Rc95LdrUefiInitializeFirmwareMenuOps(
                Image, TextUi, &Video, &MenuOps, &TextUiExtension) ==
                Rc95LdrSuccess)
        {
            MenuContext.MenuOps = &MenuOps;
            Discovery.SelectBootCandidate = UefiSelectBootCandidate;
            /* Keep the native disk backend context intact. */
            Discovery.SelectionContext = &MenuContext;
        }
        Status = Rc95LdrUefiDiscoverAndStartWindows(
            &Discovery, Partitions, PartitionCapacity,
            Candidates, CandidateCapacity, CandidateCount,
            HiveBuffer, HiveCapacity, Startup, Result,
            SelectedInstallation, SelectedEspIndex);
    }

    /* Restore the native backend context before teardown: the selector uses
       its own context only during discovery, not for volume operations. */
    Rc95LdrUefiDestroyNativeDiscovery(Context);
    return Status;
}
