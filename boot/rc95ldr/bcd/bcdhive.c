#include "rc95bcd.h"

#include <limits.h>
#include <string.h>

#define REGF_SIGNATURE 0x66676572u /* "regf" */
#define HBIN_SIGNATURE 0x6e696268u /* "hbin" */
#define NK_SIGNATURE   0x6b6eu
#define VK_SIGNATURE   0x6b76u
#define LI_SIGNATURE   0x696cu
#define LF_SIGNATURE   0x666cu
#define LH_SIGNATURE   0x686cu
#define RI_SIGNATURE   0x6972u
#define REG_HIVE_BIN_OFFSET 4096u
#define REG_HBIN_HEADER_SIZE 32u
#define REG_CELL_HEADER_SIZE 4u
#define REG_NK_FIXED_SIZE 76u
#define REG_VK_FIXED_SIZE 20u
#define REG_NK_COMP_NAME 0x0020u
#define REG_VK_COMP_NAME 0x0001u
#define REG_VALUE_INLINE 0x80000000u
#define REG_MAX_ENUM_ITEMS 0x100000u

typedef struct _RC95BCD_CELL
{
    const uint8_t *Data;
    uint32_t Size;
    uint32_t AbsoluteOffset;
} RC95BCD_CELL;

static uint32_t
Rc95BcdChecksum(const uint8_t *Header)
{
    uint32_t Sum = 0;
    uint32_t Index;

    for (Index = 0; Index < 0x1fcu; Index += 4)
        Sum ^= Rc95LdrReadU32(Header + Index);

    if (Sum == 0xffffffffu)
        Sum = 0xfffffffeu;
    else if (Sum == 0)
        Sum = 1;

    return Sum;
}

static RC95LDR_STATUS
Rc95BcdGetCell(
    const RC95BCD_HIVE *Hive,
    uint32_t CellOffset,
    RC95BCD_CELL *Cell)
{
    uint32_t BinOffset = 0;

    if (Hive == NULL || Cell == NULL ||
        (CellOffset & 7u) != 0 ||
        CellOffset >= Hive->HiveBinsSize)
        return Rc95LdrOutOfBounds;

    while (BinOffset < Hive->HiveBinsSize)
    {
        const uint8_t *Bin;
        uint32_t BinSize;
        uint32_t WithinBin;
        uint32_t Cursor;
        uint32_t BinEnd;

        if (!Rc95LdrRangeValid(Hive->ImageSize,
                               REG_HIVE_BIN_OFFSET + BinOffset,
                               REG_HBIN_HEADER_SIZE))
            return Rc95LdrCorruptData;

        Bin = Hive->Image + REG_HIVE_BIN_OFFSET + BinOffset;
        if (Rc95LdrReadU32(Bin) != HBIN_SIGNATURE ||
            Rc95LdrReadU32(Bin + 4) != BinOffset)
            return Rc95LdrCorruptData;

        BinSize = Rc95LdrReadU32(Bin + 8);
        if (BinSize < REG_HBIN_HEADER_SIZE ||
            (BinSize & 0xfffu) != 0 ||
            BinSize > Hive->HiveBinsSize - BinOffset)
            return Rc95LdrCorruptData;

        BinEnd = BinOffset + BinSize;
        if (CellOffset >= BinOffset + REG_HBIN_HEADER_SIZE &&
            CellOffset < BinEnd)
        {
            WithinBin = CellOffset - BinOffset;
            Cursor = REG_HBIN_HEADER_SIZE;

            while (Cursor < BinSize)
            {
                const uint8_t *RawCell;
                int32_t SignedSize;
                uint32_t CellSize;

                if (BinSize - Cursor < REG_CELL_HEADER_SIZE)
                    return Rc95LdrCorruptData;

                RawCell = Bin + Cursor;
                SignedSize = (int32_t)Rc95LdrReadU32(RawCell);
                if (SignedSize == 0 || SignedSize == INT32_MIN)
                    return Rc95LdrCorruptData;

                CellSize = (uint32_t)(SignedSize < 0 ? -SignedSize : SignedSize);
                if (CellSize < 8 || (CellSize & 7u) != 0 ||
                    CellSize > BinSize - Cursor)
                    return Rc95LdrCorruptData;

                if (Cursor == WithinBin)
                {
                    if (SignedSize > 0)
                        return Rc95LdrNotFound; /* Free cell. */

                    Cell->Data = RawCell + REG_CELL_HEADER_SIZE;
                    Cell->Size = CellSize - REG_CELL_HEADER_SIZE;
                    Cell->AbsoluteOffset = REG_HIVE_BIN_OFFSET +
                                           BinOffset + Cursor;
                    return Rc95LdrSuccess;
                }

                if (Cursor > WithinBin)
                    return Rc95LdrCorruptData;

                Cursor += CellSize;
            }

            return Rc95LdrCorruptData;
        }

        BinOffset = BinEnd;
    }

    return Rc95LdrOutOfBounds;
}

static int
Rc95BcdNameEquals(
    const uint8_t *Stored,
    uint16_t StoredLength,
    int Compressed,
    const char *Name)
{
    size_t InputLength = strlen(Name);
    size_t Index;

    if (Compressed)
    {
        if (InputLength != StoredLength)
            return 0;

        for (Index = 0; Index < InputLength; ++Index)
        {
            unsigned char Left = Stored[Index];
            unsigned char Right = (unsigned char)Name[Index];
            if (Left >= 'A' && Left <= 'Z') Left = (unsigned char)(Left + 32);
            if (Right >= 'A' && Right <= 'Z') Right = (unsigned char)(Right + 32);
            if (Left != Right)
                return 0;
        }
        return 1;
    }

    if (InputLength > UINT16_MAX / 2 || InputLength * 2 != StoredLength)
        return 0;

    for (Index = 0; Index < InputLength; ++Index)
    {
        uint16_t Left = Rc95LdrReadU16(Stored + Index * 2);
        unsigned char Right = (unsigned char)Name[Index];
        if (Left >= 'A' && Left <= 'Z') Left = (uint16_t)(Left + 32);
        if (Right >= 'A' && Right <= 'Z') Right = (unsigned char)(Right + 32);
        if (Left != Right)
            return 0;
    }

    return 1;
}

static RC95LDR_STATUS
Rc95BcdGetKey(
    const RC95BCD_HIVE *Hive,
    uint32_t KeyOffset,
    RC95BCD_CELL *Cell,
    uint16_t *Flags,
    uint32_t *SubKeyCount,
    uint32_t *SubKeyList,
    uint32_t *ValueCount,
    uint32_t *ValueList)
{
    RC95LDR_STATUS Status = Rc95BcdGetCell(Hive, KeyOffset, Cell);

    if (Status != Rc95LdrSuccess)
        return Status;

    if (Cell->Size < REG_NK_FIXED_SIZE ||
        Rc95LdrReadU16(Cell->Data) != NK_SIGNATURE)
        return Rc95LdrCorruptData;

    *Flags = Rc95LdrReadU16(Cell->Data + 2);
    *SubKeyCount = Rc95LdrReadU32(Cell->Data + 20);
    *SubKeyList = Rc95LdrReadU32(Cell->Data + 28);
    *ValueCount = Rc95LdrReadU32(Cell->Data + 36);
    *ValueList = Rc95LdrReadU32(Cell->Data + 40);

    if (*SubKeyCount > REG_MAX_ENUM_ITEMS || *ValueCount > REG_MAX_ENUM_ITEMS)
        return Rc95LdrCorruptData;

    return Rc95LdrSuccess;
}

static RC95LDR_STATUS
Rc95BcdFindInIndex(
    const RC95BCD_HIVE *Hive,
    uint32_t ListOffset,
    uint32_t ExpectedCount,
    const char *Name,
    uint32_t Depth,
    uint32_t *FoundCell)
{
    RC95BCD_CELL List;
    uint16_t Signature;
    uint16_t Count;
    uint32_t Index;
    uint32_t EntrySize;
    RC95LDR_STATUS Status;

    if (Depth > RC95BCD_MAX_INDEX_DEPTH)
        return Rc95LdrCorruptData;

    Status = Rc95BcdGetCell(Hive, ListOffset, &List);
    if (Status != Rc95LdrSuccess)
        return Status;

    if (List.Size < 4)
        return Rc95LdrCorruptData;

    Signature = Rc95LdrReadU16(List.Data);
    Count = Rc95LdrReadU16(List.Data + 2);

    if (Count > ExpectedCount)
        return Rc95LdrCorruptData;

    if (Signature == RI_SIGNATURE)
    {
        if (List.Size < 4u + (uint32_t)Count * 4u)
            return Rc95LdrCorruptData;

        for (Index = 0; Index < Count; ++Index)
        {
            Status = Rc95BcdFindInIndex(Hive,
                Rc95LdrReadU32(List.Data + 4 + Index * 4),
                ExpectedCount, Name, Depth + 1, FoundCell);
            if (Status == Rc95LdrSuccess)
                return Status;
            if (Status != Rc95LdrNotFound)
                return Status;
        }
        return Rc95LdrNotFound;
    }

    if (Signature == LI_SIGNATURE)
        EntrySize = 4;
    else if (Signature == LF_SIGNATURE || Signature == LH_SIGNATURE)
        EntrySize = 8;
    else
        return Rc95LdrCorruptData;

    if (List.Size < 4u + (uint32_t)Count * EntrySize)
        return Rc95LdrCorruptData;

    for (Index = 0; Index < Count; ++Index)
    {
        uint32_t ChildOffset = Rc95LdrReadU32(List.Data + 4 + Index * EntrySize);
        RC95BCD_CELL Child;
        uint16_t Flags;
        uint16_t NameLength;
        uint32_t NameOffset;

        Status = Rc95BcdGetCell(Hive, ChildOffset, &Child);
        if (Status != Rc95LdrSuccess)
            return Status;

        if (Child.Size < REG_NK_FIXED_SIZE ||
            Rc95LdrReadU16(Child.Data) != NK_SIGNATURE)
            return Rc95LdrCorruptData;

        Flags = Rc95LdrReadU16(Child.Data + 2);
        NameLength = Rc95LdrReadU16(Child.Data + 72);
        NameOffset = 76;

        if (NameLength > Child.Size - NameOffset)
            return Rc95LdrCorruptData;

        if (Rc95BcdNameEquals(Child.Data + NameOffset, NameLength,
                              (Flags & REG_NK_COMP_NAME) != 0, Name))
        {
            *FoundCell = ChildOffset;
            return Rc95LdrSuccess;
        }
    }

    return Rc95LdrNotFound;
}

RC95LDR_STATUS
Rc95BcdOpenHive(
    const void *Image,
    size_t ImageSize,
    RC95BCD_HIVE *Hive)
{
    const uint8_t *Bytes = (const uint8_t *)Image;
    uint32_t HiveBinsSize;
    uint32_t Sequence1;
    uint32_t Sequence2;
    uint32_t BinOffset = 0;
    uint32_t RootCell;

    if (Image == NULL || Hive == NULL ||
        ImageSize < RC95BCD_HIVE_HEADER_SIZE)
        return Rc95LdrInvalidParameter;

    memset(Hive, 0, sizeof(*Hive));

    if (Rc95LdrReadU32(Bytes) != REGF_SIGNATURE)
        return Rc95LdrInvalidImage;

    Sequence1 = Rc95LdrReadU32(Bytes + 4);
    Sequence2 = Rc95LdrReadU32(Bytes + 8);
    if (Sequence1 != Sequence2)
        return Rc95LdrCorruptData;

    if (Rc95LdrReadU32(Bytes + 0x14) != 1 ||
        Rc95LdrReadU32(Bytes + 0x18) < 3 ||
        Rc95LdrReadU32(Bytes + 0x1c) != 0 ||
        Rc95LdrReadU32(Bytes + 0x20) != 1)
        return Rc95LdrUnsupported;

    if (Rc95LdrReadU32(Bytes + 0x1fc) != Rc95BcdChecksum(Bytes))
        return Rc95LdrCorruptData;

    RootCell = Rc95LdrReadU32(Bytes + 0x24);
    HiveBinsSize = Rc95LdrReadU32(Bytes + 0x28);

    if (HiveBinsSize == 0 || (HiveBinsSize & 0xfffu) != 0 ||
        HiveBinsSize > UINT32_MAX - RC95BCD_HIVE_HEADER_SIZE ||
        !Rc95LdrRangeValid(ImageSize, RC95BCD_HIVE_HEADER_SIZE, HiveBinsSize))
        return Rc95LdrCorruptData;

    Hive->Image = Bytes;
    Hive->ImageSize = ImageSize;
    Hive->HiveBinsSize = HiveBinsSize;
    Hive->RootCell = RootCell;
    Hive->Sequence1 = Sequence1;
    Hive->Sequence2 = Sequence2;

    /* Validate the complete HBIN chain and every cell boundary once at open. */
    while (BinOffset < HiveBinsSize)
    {
        const uint8_t *Bin = Bytes + RC95BCD_HIVE_HEADER_SIZE + BinOffset;
        uint32_t BinSize;
        uint32_t Cursor;

        if (!Rc95LdrRangeValid(ImageSize,
            RC95BCD_HIVE_HEADER_SIZE + BinOffset, REG_HBIN_HEADER_SIZE) ||
            Rc95LdrReadU32(Bin) != HBIN_SIGNATURE ||
            Rc95LdrReadU32(Bin + 4) != BinOffset)
            return Rc95LdrCorruptData;

        BinSize = Rc95LdrReadU32(Bin + 8);
        if (BinSize < REG_HBIN_HEADER_SIZE || (BinSize & 0xfffu) != 0 ||
            BinSize > HiveBinsSize - BinOffset)
            return Rc95LdrCorruptData;

        Cursor = REG_HBIN_HEADER_SIZE;
        while (Cursor < BinSize)
        {
            int32_t SignedSize;
            uint32_t CellSize;

            if (BinSize - Cursor < REG_CELL_HEADER_SIZE)
                return Rc95LdrCorruptData;

            SignedSize = (int32_t)Rc95LdrReadU32(Bin + Cursor);
            if (SignedSize == 0 || SignedSize == INT32_MIN)
                return Rc95LdrCorruptData;

            CellSize = (uint32_t)(SignedSize < 0 ? -SignedSize : SignedSize);
            if (CellSize < 8 || (CellSize & 7u) != 0 ||
                CellSize > BinSize - Cursor)
                return Rc95LdrCorruptData;

            Cursor += CellSize;
        }

        if (Cursor != BinSize)
            return Rc95LdrCorruptData;

        BinOffset += BinSize;
    }

    if (BinOffset != HiveBinsSize)
        return Rc95LdrCorruptData;

    {
        RC95BCD_CELL Root;
        RC95LDR_STATUS Status = Rc95BcdGetCell(Hive, RootCell, &Root);
        if (Status != Rc95LdrSuccess)
            return Rc95LdrCorruptData;
        if (Root.Size < REG_NK_FIXED_SIZE ||
            Rc95LdrReadU16(Root.Data) != NK_SIGNATURE ||
            Rc95LdrReadU16(Root.Data + 72) >
                Root.Size - REG_NK_FIXED_SIZE)
            return Rc95LdrCorruptData;
    }

    return Rc95LdrSuccess;
}

RC95LDR_STATUS
Rc95BcdGetRootKey(
    const RC95BCD_HIVE *Hive,
    RC95BCD_KEY_INFO *Key)
{
    RC95BCD_CELL Cell;
    uint16_t Flags;
    uint32_t SubKeyCount, SubKeyList, ValueCount, ValueList;
    RC95LDR_STATUS Status;

    if (Hive == NULL || Key == NULL)
        return Rc95LdrInvalidParameter;

    Status = Rc95BcdGetKey(Hive, Hive->RootCell, &Cell, &Flags,
                           &SubKeyCount, &SubKeyList, &ValueCount, &ValueList);
    if (Status != Rc95LdrSuccess)
        return Status;

    Key->NameLength = Rc95LdrReadU16(Cell.Data + 72);
    if (Key->NameLength > Cell.Size - REG_NK_FIXED_SIZE)
        return Rc95LdrCorruptData;

    Key->Cell = Hive->RootCell;
    Key->SubKeyCount = SubKeyCount;
    Key->ValueCount = ValueCount;
    Key->Name = Cell.Data + REG_NK_FIXED_SIZE;
    Key->Flags = Flags;
    return Rc95LdrSuccess;
}

RC95LDR_STATUS
Rc95BcdFindSubKey(
    const RC95BCD_HIVE *Hive,
    uint32_t ParentCell,
    const char *Name,
    uint32_t *ChildCell)
{
    RC95BCD_CELL Parent;
    uint16_t Flags;
    uint32_t SubKeyCount, SubKeyList, ValueCount, ValueList;
    RC95LDR_STATUS Status;

    if (Hive == NULL || Name == NULL || ChildCell == NULL)
        return Rc95LdrInvalidParameter;

    Status = Rc95BcdGetKey(Hive, ParentCell, &Parent, &Flags,
                           &SubKeyCount, &SubKeyList, &ValueCount, &ValueList);
    if (Status != Rc95LdrSuccess)
        return Status;

    if (SubKeyCount == 0)
        return Rc95LdrNotFound;

    if (SubKeyList == 0xffffffffu)
        return Rc95LdrCorruptData;

    return Rc95BcdFindInIndex(Hive, SubKeyList, SubKeyCount,
                              Name, 0, ChildCell);
}

RC95LDR_STATUS
Rc95BcdFindValue(
    const RC95BCD_HIVE *Hive,
    uint32_t KeyCell,
    const char *Name,
    RC95BCD_VALUE *Value)
{
    RC95BCD_CELL Key, Values;
    uint16_t Flags;
    uint32_t SubKeyCount, SubKeyList, ValueCount, ValueList;
    uint32_t Index;
    RC95LDR_STATUS Status;

    if (Hive == NULL || Name == NULL || Value == NULL)
        return Rc95LdrInvalidParameter;

    memset(Value, 0, sizeof(*Value));

    Status = Rc95BcdGetKey(Hive, KeyCell, &Key, &Flags,
                           &SubKeyCount, &SubKeyList, &ValueCount, &ValueList);
    if (Status != Rc95LdrSuccess)
        return Status;

    if (ValueCount == 0)
        return Rc95LdrNotFound;

    Status = Rc95BcdGetCell(Hive, ValueList, &Values);
    if (Status != Rc95LdrSuccess)
        return Status;

    if (ValueCount > Values.Size / 4)
        return Rc95LdrCorruptData;

    for (Index = 0; Index < ValueCount; ++Index)
    {
        uint32_t ValueOffset = Rc95LdrReadU32(Values.Data + Index * 4);
        RC95BCD_CELL ValueCell;
        uint16_t NameLength, ValueFlags;
        uint32_t DataLength, DataOffset, Type;
        const uint8_t *StoredName;
        const uint8_t *Data;

        Status = Rc95BcdGetCell(Hive, ValueOffset, &ValueCell);
        if (Status != Rc95LdrSuccess)
            return Status;

        if (ValueCell.Size < REG_VK_FIXED_SIZE ||
            Rc95LdrReadU16(ValueCell.Data) != VK_SIGNATURE)
            return Rc95LdrCorruptData;

        NameLength = Rc95LdrReadU16(ValueCell.Data + 2);
        DataLength = Rc95LdrReadU32(ValueCell.Data + 4);
        DataOffset = Rc95LdrReadU32(ValueCell.Data + 8);
        Type = Rc95LdrReadU32(ValueCell.Data + 12);
        ValueFlags = Rc95LdrReadU16(ValueCell.Data + 16);

        if (NameLength > ValueCell.Size - REG_VK_FIXED_SIZE)
            return Rc95LdrCorruptData;

        StoredName = ValueCell.Data + REG_VK_FIXED_SIZE;
        if (!Rc95BcdNameEquals(StoredName, NameLength,
                (ValueFlags & REG_VK_COMP_NAME) != 0, Name))
            continue;

        DataLength &= ~REG_VALUE_INLINE;
        if ((Rc95LdrReadU32(ValueCell.Data + 4) & REG_VALUE_INLINE) != 0)
        {
            if (DataLength > 4)
                return Rc95LdrCorruptData;
            Data = ValueCell.Data + 8;
        }
        else
        {
            RC95BCD_CELL DataCell;
            Status = Rc95BcdGetCell(Hive, DataOffset, &DataCell);
            if (Status != Rc95LdrSuccess)
                return Status;
            if (DataLength > DataCell.Size)
                return Rc95LdrCorruptData;
            Data = DataCell.Data;
        }

        Value->Data = Data;
        Value->Length = DataLength;
        Value->Type = Type;
        Value->KeyCell = KeyCell;
        Value->ValueCell = ValueOffset;
        return Rc95LdrSuccess;
    }

    return Rc95LdrNotFound;
}

RC95LDR_STATUS
Rc95BcdFindObject(
    const RC95BCD_HIVE *Hive,
    const char *ObjectGuid,
    uint32_t *ObjectCell)
{
    uint32_t ObjectsCell;

    if (Hive == NULL || ObjectGuid == NULL || ObjectCell == NULL)
        return Rc95LdrInvalidParameter;

    {
        RC95LDR_STATUS Status = Rc95BcdFindSubKey(
            Hive, Hive->RootCell, "Objects", &ObjectsCell);
        if (Status != Rc95LdrSuccess)
            return Status;
    }

    return Rc95BcdFindSubKey(Hive, ObjectsCell, ObjectGuid, ObjectCell);
}

RC95LDR_STATUS
Rc95BcdFindElement(
    const RC95BCD_HIVE *Hive,
    uint32_t ObjectCell,
    uint32_t ElementId,
    RC95BCD_VALUE *Element)
{
    char ElementName[9];
    uint32_t ElementsCell, ElementCell;
    RC95LDR_STATUS Status;

    if (Hive == NULL || Element == NULL)
        return Rc95LdrInvalidParameter;

    {
        static const char HexDigits[] = "0123456789abcdef";
        uint32_t Digit;
        for (Digit = 0; Digit < 8; ++Digit)
            ElementName[Digit] = HexDigits[(ElementId >> ((7 - Digit) * 4)) & 0x0fu];
        ElementName[8] = '\\0';
    }

    Status = Rc95BcdFindSubKey(Hive, ObjectCell, "Elements", &ElementsCell);
    if (Status != Rc95LdrSuccess)
        return Status;

    Status = Rc95BcdFindSubKey(Hive, ElementsCell, ElementName, &ElementCell);
    if (Status != Rc95LdrSuccess)
        return Status;

    return Rc95BcdFindValue(Hive, ElementCell, "Element", Element);
}
