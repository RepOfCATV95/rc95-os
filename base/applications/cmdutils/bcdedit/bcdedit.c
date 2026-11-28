/*
 * PROJECT:     Republic of CATV 95 Boot Configuration Data Editor
 * LICENSE:     GPL-3.0-or-later
 * PURPOSE:     Inspect and validate an offline BCD registry-hive store.
 * COPYRIGHT:   Copyright 1997-2028 Republic of CATV 95 <catv95republic@proton.me>
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <stdint.h>

#include "rc95bcd.h"
#include "rc95bcdwalk.h"

#define BCD_MAX_STORE_SIZE (64u * 1024u * 1024u)

static void
Usage(void)
{
    wprintf(L"BCDEDIT - Boot Configuration Data store editor\n\n"
            L"Usage: bcdedit /store <BCD-file> /enum [all|{object-guid}]\n"
            L"       bcdedit /? \n\n"
            L"  /store <file>       Inspect the specified offline BCD hive\n"
            L"  /enum [all|{guid}]  Enumerate all objects or one object\n"
            L"  /?                  Display this help\n\n"
            L"This version performs validated, read-only offline inspection.\n"
            L"It never mounts or modifies the live system BCD store.\n");
}

static BOOL
ReadStore(PCWSTR Path, BYTE **Image, SIZE_T *ImageSize)
{
    HANDLE File;
    LARGE_INTEGER Size;
    BYTE *Buffer;
    DWORD Read = 0;

    *Image = NULL;
    *ImageSize = 0;
    File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ,
                       NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return FALSE;

    if (!GetFileSizeEx(File, &Size) || Size.QuadPart < RC95BCD_HIVE_HEADER_SIZE ||
        (ULONGLONG)Size.QuadPart > BCD_MAX_STORE_SIZE)
    {
        SetLastError(ERROR_INVALID_DATA);
        CloseHandle(File);
        return FALSE;
    }

    Buffer = (BYTE *)malloc((SIZE_T)Size.QuadPart);
    if (Buffer == NULL)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        CloseHandle(File);
        return FALSE;
    }

    if (!ReadFile(File, Buffer, (DWORD)Size.QuadPart, &Read, NULL) ||
        Read != (DWORD)Size.QuadPart)
    {
        DWORD Error = GetLastError();
        free(Buffer);
        CloseHandle(File);
        SetLastError(Error ? Error : ERROR_READ_FAULT);
        return FALSE;
    }

    CloseHandle(File);
    *Image = Buffer;
    *ImageSize = (SIZE_T)Size.QuadPart;
    return TRUE;
}

static BOOL
GuidToText(const RC95BCD_GUID *Guid, WCHAR Text[39])
{
    static const WCHAR Hex[] = L"0123456789abcdef";
    BYTE Bytes[16];
    unsigned i, p = 0;

    if (Guid == NULL || Text == NULL)
        return FALSE;

    Bytes[0] = (BYTE)(Guid->Data1 >> 24);
    Bytes[1] = (BYTE)(Guid->Data1 >> 16);
    Bytes[2] = (BYTE)(Guid->Data1 >> 8);
    Bytes[3] = (BYTE)Guid->Data1;
    Bytes[4] = (BYTE)(Guid->Data2 >> 8);
    Bytes[5] = (BYTE)Guid->Data2;
    Bytes[6] = (BYTE)(Guid->Data3 >> 8);
    Bytes[7] = (BYTE)Guid->Data3;
    CopyMemory(Bytes + 8, Guid->Data4, 8);

    Text[p++] = L'{';
    for (i = 0; i < 16; ++i)
    {
        if (i == 4 || i == 6 || i == 8 || i == 10)
            Text[p++] = L'-';
        Text[p++] = Hex[Bytes[i] >> 4];
        Text[p++] = Hex[Bytes[i] & 0xf];
    }
    Text[p++] = L'}';
    Text[p] = UNICODE_NULL;
    return TRUE;
}

static BOOL
NormalizeObjectName(PCWSTR Input, char Output[39])
{
    size_t i, Length;
    if (Input == NULL)
        return FALSE;
    Length = wcslen(Input);
    if (Length != 38 || Input[0] != L'{' || Input[37] != L'}')
        return FALSE;
    for (i = 0; i < Length; ++i)
    {
        WCHAR Ch = Input[i];
        if (Ch > 0x7f)
            return FALSE;
        if (Ch >= L'A' && Ch <= L'Z')
            Ch += L'a' - L'A';
        Output[i] = (char)Ch;
    }
    Output[38] = '\0';
    return TRUE;
}

static void
PrintElementValue(uint32_t Id, const RC95BCD_VALUE *Value)
{
    RC95BCD_ELEMENT_VIEW View;
    RC95LDR_STATUS Status;
    uint32_t i;

    wprintf(L"    %08lx  REG_%s  %lu bytes",
            (unsigned long)Id,
            Value->Type == 1 ? L"NONE" :
            Value->Type == 2 ? L"SZ" :
            Value->Type == 3 ? L"BINARY" :
            Value->Type == 4 ? L"DWORD" :
            Value->Type == 7 ? L"MULTI_SZ" : L"UNKNOWN",
            (unsigned long)Value->Length);

    Status = Rc95BcdDecodeElement(Id, Value, &View);
    if (Status != Rc95LdrSuccess)
    {
        wprintf(L" (opaque or unsupported payload)\n");
        return;
    }

    switch (View.Kind)
    {
        case Rc95BcdElementString:
            wprintf(L" = \"");
            for (i = 0; i < View.StringCharacters; ++i)
            {
                uint16_t Ch = Rc95LdrReadU16(View.StringData + i * 2);
                if (Ch >= 0x20 && Ch < 0x7f)
                    wprintf(L"%c", (WCHAR)Ch);
                else
                    wprintf(L"\\u%04x", (unsigned)Ch);
            }
            wprintf(L"\"");
            break;

        case Rc95BcdElementInteger:
            wprintf(L" = 0x%llx", (unsigned long long)View.IntegerValue);
            break;

        case Rc95BcdElementBoolean:
            wprintf(L" = %s", View.BooleanValue ? L"Yes" : L"No");
            break;

        case Rc95BcdElementObject:
        {
            WCHAR GuidText[39];
            GuidToText(&View.ObjectGuid, GuidText);
            wprintf(L" = %s", GuidText);
            break;
        }

        case Rc95BcdElementObjectList:
            wprintf(L" (%lu object references)",
                    (unsigned long)View.ItemCount);
            break;

        case Rc95BcdElementIntegerList:
            wprintf(L" (%lu integer entries)",
                    (unsigned long)View.ItemCount);
            break;

        case Rc95BcdElementDevice:
            wprintf(L" (device type %lu, %lu opaque bytes)",
                    (unsigned long)View.DeviceType,
                    (unsigned long)View.DeviceDataLength);
            break;

        case Rc95BcdElementBinary:
            wprintf(L" (binary)");
            break;

        default:
            break;
    }
    wprintf(L"\n");
}

static RC95LDR_STATUS
PrintElement(uint32_t Id, const RC95BCD_VALUE *Value, void *Context)
{
    (void)Context;
    if (Value == NULL)
        return Rc95LdrInvalidParameter;
    PrintElementValue(Id, Value);
    return Rc95LdrSuccess;
}

static BOOL
EnumerateObject(const RC95BCD_HIVE *Hive,
                const char *ObjectName,
                PCWSTR DisplayName)
{
    uint32_t ObjectCell;
    RC95LDR_STATUS Status;
    Status = Rc95BcdFindObject(Hive, ObjectName, &ObjectCell);
    if (Status != Rc95LdrSuccess)
    {
        fwprintf(stderr, L"BCDEDIT: object %s was not found (status %u).\n",
                 DisplayName, (unsigned)Status);
        return FALSE;
    }

    wprintf(L"\nObject: %s\n", DisplayName);
    Status = Rc95BcdEnumerateElements(Hive, ObjectCell, PrintElement, NULL);
    if (Status == Rc95LdrNotFound)
        return TRUE;
    if (Status != Rc95LdrSuccess)
    {
        fwprintf(stderr, L"BCDEDIT: element enumeration failed (status %u).\n",
                 (unsigned)Status);
        return FALSE;
    }
    return TRUE;
}

#define BCD_NK_COMP_NAME 0x0020u

typedef struct _ALL_OBJECTS_CONTEXT
{
    const RC95BCD_HIVE *Hive;
    BOOL Success;
} ALL_OBJECTS_CONTEXT;

static RC95LDR_STATUS
EnumerateObjectKey(const RC95BCD_KEY_INFO *Key, void *Opaque)
{
    ALL_OBJECTS_CONTEXT *Context = (ALL_OBJECTS_CONTEXT *)Opaque;
    char Name[39];
    WCHAR DisplayName[39];
    uint32_t i;

    if (Key == NULL || Context == NULL || Key->Name == NULL)
        return Rc95LdrInvalidParameter;

    if ((Key->Flags & BCD_NK_COMP_NAME) != 0)
    {
        if (Key->NameLength != 38u)
            return Rc95LdrCorruptData;
        for (i = 0; i < 38u; ++i)
        {
            Name[i] = (char)Key->Name[i];
            DisplayName[i] = (WCHAR)Key->Name[i];
        }
    }
    else
    {
        if (Key->NameLength != 76u)
            return Rc95LdrCorruptData;
        for (i = 0; i < 38u; ++i)
        {
            uint16_t Ch = Rc95LdrReadU16(Key->Name + i * 2u);
            if (Ch > 0x7fu)
                return Rc95LdrCorruptData;
            Name[i] = (char)Ch;
            DisplayName[i] = (WCHAR)Ch;
        }
    }
    Name[38] = '\0';
    DisplayName[38] = UNICODE_NULL;

    if (!NormalizeObjectName(DisplayName, Name))
        return Rc95LdrCorruptData;

    if (!EnumerateObject(Context->Hive, Name, DisplayName))
    {
        Context->Success = FALSE;
        return Rc95LdrCorruptData;
    }
    return Rc95LdrSuccess;
}

static BOOL
EnumerateAllObjects(const RC95BCD_HIVE *Hive)
{
    uint32_t ObjectsCell;
    RC95BCD_KEY_INFO Root;
    RC95LDR_STATUS Status;
    ALL_OBJECTS_CONTEXT Context;

    Status = Rc95BcdGetRootKey(Hive, &Root);
    if (Status == Rc95LdrSuccess)
        Status = Rc95BcdFindSubKey(Hive, Root.Cell, "Objects", &ObjectsCell);
    if (Status != Rc95LdrSuccess)
    {
        fwprintf(stderr, L"BCDEDIT: cannot locate BCD Objects key (status %u).\n",
                 (unsigned)Status);
        return FALSE;
    }

    Context.Hive = Hive;
    Context.Success = TRUE;
    Status = Rc95BcdEnumerateSubKeys(Hive, ObjectsCell,
                                     EnumerateObjectKey, &Context);
    if (Status != Rc95LdrSuccess || !Context.Success)
    {
        fwprintf(stderr, L"BCDEDIT: object enumeration failed (status %u).\n",
                 (unsigned)Status);
        return FALSE;
    }
    return TRUE;
}

int
wmain(int argc, WCHAR **argv)
{
    PCWSTR StorePath = NULL;
    PCWSTR ObjectArg = NULL;
    char ObjectName[39];
    BYTE *Image = NULL;
    SIZE_T ImageSize = 0;
    RC95BCD_HIVE Hive;
    RC95LDR_STATUS Status;
    BOOL Success;

    if (argc == 1 || (argc == 2 &&
        (wcscmp(argv[1], L"/?") == 0 || wcscmp(argv[1], L"-?") == 0)))
    {
        Usage();
        return argc == 1 ? 1 : 0;
    }

    {
        int i;
        BOOL DoEnum = FALSE;
        for (i = 1; i < argc; ++i)
        {
            if (_wcsicmp(argv[i], L"/store") == 0 && i + 1 < argc)
                StorePath = argv[++i];
            else if (_wcsicmp(argv[i], L"/enum") == 0)
            {
                DoEnum = TRUE;
                if (i + 1 < argc && argv[i + 1][0] != L'/')
                {
                    if (_wcsicmp(argv[i + 1], L"all") != 0)
                        ObjectArg = argv[i + 1];
                    ++i;
                }
            }
            else
            {
                fwprintf(stderr, L"BCDEDIT: unsupported or incomplete option: %s\n",
                         argv[i]);
                Usage();
                return 1;
            }
        }

        if (StorePath == NULL || !DoEnum)
        {
            fwprintf(stderr, L"BCDEDIT: /store and /enum are required.\n");
            Usage();
            return 1;
        }
    }

    if (ObjectArg != NULL && !NormalizeObjectName(ObjectArg, ObjectName))
    {
        fwprintf(stderr, L"BCDEDIT: invalid object GUID: %s\n", ObjectArg);
        return 1;
    }

    if (!ReadStore(StorePath, &Image, &ImageSize))
    {
        fwprintf(stderr, L"BCDEDIT: cannot read %s (error %lu).\n",
                 StorePath, GetLastError());
        return 1;
    }

    Status = Rc95BcdOpenHive(Image, ImageSize, &Hive);
    if (Status == Rc95LdrSuccess)
        Status = Rc95BcdValidateHiveBins(Image, ImageSize);
    if (Status != Rc95LdrSuccess)
    {
        fwprintf(stderr, L"BCDEDIT: invalid or unsupported BCD hive (status %u).\n",
                 (unsigned)Status);
        free(Image);
        return 1;
    }

    wprintf(L"BCD store: %s\n", StorePath);
    if (ObjectArg != NULL)
        Success = EnumerateObject(&Hive, ObjectName, ObjectArg);
    else
        Success = EnumerateAllObjects(&Hive);

    free(Image);
    return Success ? 0 : 1;
}
