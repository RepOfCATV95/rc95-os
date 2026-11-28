#include "rc95bcd.h"

#include <string.h>

static RC95LDR_STATUS
Rc95BcdValidateUtf16(const uint8_t *Data, uint32_t Length, uint32_t *Characters)
{
    uint32_t Count, Index;

    if ((Length & 1u) != 0)
        return Rc95LdrCorruptData;

    Count = Length / 2;
    if (Count && Rc95LdrReadU16(Data + (Count - 1) * 2) == 0)
        --Count;

    for (Index = 0; Index < Count; ++Index)
    {
        uint16_t Ch = Rc95LdrReadU16(Data + Index * 2);
        if (Ch >= 0xd800 && Ch <= 0xdbff)
        {
            uint16_t Low;
            if (Index + 1 >= Count)
                return Rc95LdrCorruptData;
            Low = Rc95LdrReadU16(Data + (Index + 1) * 2);
            if (Low < 0xdc00 || Low > 0xdfff)
                return Rc95LdrCorruptData;
            ++Index;
        }
        else if (Ch >= 0xdc00 && Ch <= 0xdfff)
            return Rc95LdrCorruptData;
    }

    *Characters = Count;
    return Rc95LdrSuccess;
}

RC95LDR_STATUS
Rc95BcdReadGuid(const uint8_t *Data, size_t Length, RC95BCD_GUID *Guid)
{
    uint32_t Index;

    if (Data == NULL || Guid == NULL)
        return Rc95LdrInvalidParameter;
    if (Length < 16)
        return Rc95LdrBufferTooSmall;

    Guid->Data1 = Rc95LdrReadU32(Data);
    Guid->Data2 = Rc95LdrReadU16(Data + 4);
    Guid->Data3 = Rc95LdrReadU16(Data + 6);
    for (Index = 0; Index < 8; ++Index)
        Guid->Data4[Index] = Data[8 + Index];
    return Rc95LdrSuccess;
}

RC95LDR_STATUS
Rc95BcdDecodeElement(
    uint32_t ElementId,
    const RC95BCD_VALUE *Value,
    RC95BCD_ELEMENT_VIEW *Element)
{
    uint32_t Class, Count, Required;
    RC95LDR_STATUS Status;

    if (Value == NULL || Element == NULL ||
        (Value->Data == NULL && Value->Length != 0))
        return Rc95LdrInvalidParameter;

    memset(Element, 0, sizeof(*Element));
    /* BCD element IDs encode application type in bits 28..31 and payload
     * format in bits 24..27 (device/string/object/object-list/etc.). */
    Class = (ElementId >> 24) & 0x0fu;
    Element->ElementId = ElementId;
    Element->Data = Value->Data;
    Element->Length = Value->Length;

    switch (Class)
    {
        case 1: /* BCD device descriptor */
            if (Value->Length < 8)
                return Rc95LdrCorruptData;
            Element->Kind = Rc95BcdElementDevice;
            Element->DeviceType = Rc95LdrReadU32(Value->Data);
            Element->AdditionalOptions = Rc95LdrReadU32(Value->Data + 4);
            Element->DeviceData = Value->Data + 8;
            Element->DeviceDataLength = Value->Length - 8;
            break;

        case 2: /* UTF-16LE string */
            Element->Kind = Rc95BcdElementString;
            Status = Rc95BcdValidateUtf16(Value->Data, Value->Length,
                                            &Element->StringCharacters);
            if (Status != Rc95LdrSuccess)
                return Status;
            Element->StringData = Value->Data;
            break;

        case 3: /* Object GUID */
            if (Value->Length != 16)
                return Rc95LdrCorruptData;
            Element->Kind = Rc95BcdElementObject;
            Status = Rc95BcdReadGuid(Value->Data, Value->Length, &Element->ObjectGuid);
            if (Status != Rc95LdrSuccess)
                return Status;
            break;

        case 4: /* DWORD count followed by packed 16-byte GUIDs */
            if (Value->Length < 4)
                return Rc95LdrCorruptData;
            Count = Rc95LdrReadU32(Value->Data);
            if (Count > (Value->Length - 4) / 16)
                return Rc95LdrCorruptData;
            Required = 4 + Count * 16;
            if (Required != Value->Length)
                return Rc95LdrCorruptData;
            Element->Kind = Rc95BcdElementObjectList;
            Element->ItemCount = Count;
            Element->Items = Value->Data + 4;
            break;

        case 5: /* 64-bit unsigned integer */
            if (Value->Length != 8)
                return Rc95LdrCorruptData;
            Element->Kind = Rc95BcdElementInteger;
            Element->IntegerValue = Rc95LdrReadU64(Value->Data);
            break;

        case 6: /* 32-bit boolean */
            if (Value->Length != 4)
                return Rc95LdrCorruptData;
            Element->Kind = Rc95BcdElementBoolean;
            Element->BooleanValue = Rc95LdrReadU32(Value->Data);
            if (Element->BooleanValue > 1)
                return Rc95LdrCorruptData;
            break;

        case 7: /* DWORD count followed by 64-bit integers */
            if (Value->Length < 4)
                return Rc95LdrCorruptData;
            Count = Rc95LdrReadU32(Value->Data);
            if (Count > (Value->Length - 4) / 8)
                return Rc95LdrCorruptData;
            Required = 4 + Count * 8;
            if (Required != Value->Length)
                return Rc95LdrCorruptData;
            Element->Kind = Rc95BcdElementIntegerList;
            Element->ItemCount = Count;
            Element->Items = Value->Data + 4;
            break;

        case 8: /* Opaque binary payload */
            Element->Kind = Rc95BcdElementBinary;
            break;

        default:
            return Rc95LdrUnsupported;
    }

    return Rc95LdrSuccess;
}
