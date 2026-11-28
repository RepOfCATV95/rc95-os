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

    if (Hive == NULL || Cell == NULL)
        return Rc95LdrInvalidParameter;

    /* An unaligned cell reference is malformed hive metadata, not a
     * legitimate out-of-range reference. Keep the status distinction so
     * callers can report corrupt indexes separately from absent cells. */
    if (CellOffset & 7u)
        return Rc95LdrCorruptData;
    if (CellOffset >= Hive->HiveBinsSize)
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


static int
Rc95BcdParseElementName(const RC95BCD_CELL *Key, uint32_t *ElementId)
{
    uint16_t Flags, NameLength;
    const uint8_t *Name;
    uint32_t i, Value=0;
    if(Key->Size<REG_NK_FIXED_SIZE || Rc95LdrReadU16(Key->Data)!=NK_SIGNATURE) return 0;
    Flags=Rc95LdrReadU16(Key->Data+2); NameLength=Rc95LdrReadU16(Key->Data+72);
    if(NameLength!=((Flags&REG_NK_COMP_NAME)?8u:16u) ||
       NameLength>Key->Size-REG_NK_FIXED_SIZE) return 0;
    Name=Key->Data+REG_NK_FIXED_SIZE;
    for(i=0;i<8;i++){ uint32_t ch=(Flags&REG_NK_COMP_NAME)?Name[i]:Rc95LdrReadU16(Name+i*2); uint32_t n;
        if(ch>='0'&&ch<='9') n=ch-'0'; else if(ch>='a'&&ch<='f') n=ch-'a'+10;
        else if(ch>='A'&&ch<='F') n=ch-'A'+10; else return 0;
        Value=(Value<<4)|n;
    }
    *ElementId=Value; return 1;
}
static RC95LDR_STATUS
Rc95BcdEnumerateIndex(const RC95BCD_HIVE *Hive,uint32_t ListOffset,uint32_t Expected,
                      uint32_t *Seen,uint32_t Depth,
                      RC95BCD_ELEMENT_ENUM_CALLBACK Callback,void *Context)
{
    RC95BCD_CELL List; uint16_t Sig,Count; uint32_t i,EntrySize; RC95LDR_STATUS s;
    if(Depth>RC95BCD_MAX_INDEX_DEPTH) return Rc95LdrCorruptData;
    s=Rc95BcdGetCell(Hive,ListOffset,&List); if(s!=Rc95LdrSuccess) return s;
    if(List.Size<4) return Rc95LdrCorruptData;
    Sig=Rc95LdrReadU16(List.Data); Count=Rc95LdrReadU16(List.Data+2);
    if(Count>REG_MAX_ENUM_ITEMS || *Seen>Expected) return Rc95LdrCorruptData;
    if(Sig==RI_SIGNATURE){
        if(List.Size<4u+(uint32_t)Count*4u) return Rc95LdrCorruptData;
        for(i=0;i<Count;i++){
            s=Rc95BcdEnumerateIndex(Hive,Rc95LdrReadU32(List.Data+4+i*4),
                                    Expected,Seen,Depth+1,Callback,Context);
            if(s!=Rc95LdrSuccess)return s;
        }
        return Rc95LdrSuccess;
    }
    if(Sig==LI_SIGNATURE) EntrySize=4; else if(Sig==LF_SIGNATURE||Sig==LH_SIGNATURE) EntrySize=8; else return Rc95LdrCorruptData;
    if(List.Size<4u+(uint32_t)Count*EntrySize) return Rc95LdrCorruptData;
    for(i=0;i<Count;i++){
        uint32_t childOff=Rc95LdrReadU32(List.Data+4+i*EntrySize),id; RC95BCD_CELL child; RC95BCD_VALUE val;
        if(*Seen>=Expected) return Rc95LdrCorruptData;
        s=Rc95BcdGetCell(Hive,childOff,&child); if(s!=Rc95LdrSuccess)return s;
        if(!Rc95BcdParseElementName(&child,&id)) return Rc95LdrCorruptData;
        s=Rc95BcdFindValue(Hive,childOff,"Element",&val); if(s!=Rc95LdrSuccess)return s;
        s=Callback(id,&val,Context); if(s!=Rc95LdrSuccess)return s;
        ++*Seen;
    }
    return Rc95LdrSuccess;
}
RC95LDR_STATUS
Rc95BcdEnumerateElements(const RC95BCD_HIVE *Hive,uint32_t ObjectCell,
                         RC95BCD_ELEMENT_ENUM_CALLBACK Callback,void *Context)
{
    uint32_t elements; RC95BCD_CELL key; uint16_t flags; uint32_t subCount,subList,valCount,valList,seen=0; RC95LDR_STATUS s;
    if(!Hive||!Callback)return Rc95LdrInvalidParameter;
    s=Rc95BcdFindSubKey(Hive,ObjectCell,"Elements",&elements); if(s!=Rc95LdrSuccess)return s;
    s=Rc95BcdGetKey(Hive,elements,&key,&flags,&subCount,&subList,&valCount,&valList); if(s!=Rc95LdrSuccess)return s;
    if(subCount==0)return Rc95LdrSuccess;
    if(subCount>REG_MAX_ENUM_ITEMS)return Rc95LdrCorruptData;
    if(subList==0xffffffffu)return Rc95LdrCorruptData;
    s=Rc95BcdEnumerateIndex(Hive,subList,subCount,&seen,0,Callback,Context);
    if(s!=Rc95LdrSuccess)return s;
    return seen==subCount?Rc95LdrSuccess:Rc95LdrCorruptData;
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
        ElementName[8] = '\0';
    }

    Status = Rc95BcdFindSubKey(Hive, ObjectCell, "Elements", &ElementsCell);
    if (Status != Rc95LdrSuccess)
        return Status;

    Status = Rc95BcdFindSubKey(Hive, ElementsCell, ElementName, &ElementCell);
    if (Status != Rc95LdrSuccess)
        return Status;

    return Rc95BcdFindValue(Hive, ElementCell, "Element", Element);
}

/* In-place update only: no registry cells or indexes are allocated. */
RC95LDR_STATUS
Rc95BcdReplaceBinaryValue(void *Image, size_t ImageSize, uint32_t KeyCell,
                          const char *ValueName, const void *Data, uint32_t Length)
{
    RC95BCD_HIVE Hive;
    RC95BCD_VALUE Value;
    uint8_t *MutableImage = (uint8_t *)Image;
    size_t Offset;
    uint32_t Sequence, Checksum;
    RC95LDR_STATUS Status;

    if (Image == NULL || ValueName == NULL || (Data == NULL && Length != 0) ||
        ImageSize < RC95BCD_HIVE_HEADER_SIZE)
        return Rc95LdrInvalidParameter;
    Status = Rc95BcdOpenHive(Image, ImageSize, &Hive);
    if (Status != Rc95LdrSuccess) return Status;
    Status = Rc95BcdFindValue(&Hive, KeyCell, ValueName, &Value);
    if (Status != Rc95LdrSuccess) return Status;
    if (Value.Type != 3u || Value.Length != Length) return Rc95LdrUnsupported;
    Offset = (size_t)(Value.Data - MutableImage);
    if (Offset > ImageSize || Length > ImageSize - Offset)
        return Rc95LdrCorruptData;

    if (Length) memmove(MutableImage + Offset, Data, Length);
    Sequence = Rc95LdrReadU32(MutableImage + 4) + 1u;
    MutableImage[4]=(uint8_t)Sequence; MutableImage[5]=(uint8_t)(Sequence>>8);
    MutableImage[6]=(uint8_t)(Sequence>>16); MutableImage[7]=(uint8_t)(Sequence>>24);
    MutableImage[8]=(uint8_t)Sequence; MutableImage[9]=(uint8_t)(Sequence>>8);
    MutableImage[10]=(uint8_t)(Sequence>>16); MutableImage[11]=(uint8_t)(Sequence>>24);
    Checksum = Rc95BcdChecksum(MutableImage);
    MutableImage[0x1fc]=(uint8_t)Checksum; MutableImage[0x1fd]=(uint8_t)(Checksum>>8);
    MutableImage[0x1fe]=(uint8_t)(Checksum>>16); MutableImage[0x1ff]=(uint8_t)(Checksum>>24);
    return Rc95LdrSuccess;
}

/*
 * Allocate a registry cell from an existing free cell. CellOffset is relative
 * to the start of the hive bins (the same coordinate system used by NK/VK
 * cell references), not an absolute pointer into Image.
 */
RC95LDR_STATUS
Rc95BcdAllocateCell(void *Image, size_t ImageSize, uint32_t RequestedDataSize,
                    uint32_t *CellOffset, uint32_t *CellDataCapacity)
{
    uint8_t *Bytes = (uint8_t *)Image;
    RC95BCD_HIVE Hive;
    uint32_t BinOffset = 0;
    uint32_t RequiredTotal;
    uint32_t Sequence;
    uint32_t Checksum;
    size_t Rounded;
    RC95LDR_STATUS Status;

    if (Image == NULL || CellOffset == NULL || CellDataCapacity == NULL)
        return Rc95LdrInvalidParameter;

    *CellOffset = 0xffffffffu;
    *CellDataCapacity = 0;

    if ((size_t)RequestedDataSize > SIZE_MAX - REG_CELL_HEADER_SIZE - 7u)
        return Rc95LdrIntegerOverflow;
    Rounded = ((size_t)RequestedDataSize + REG_CELL_HEADER_SIZE + 7u) &
              ~(size_t)7u;
    if (Rounded < 8u || Rounded > INT32_MAX)
        return Rc95LdrUnsupported;
    RequiredTotal = (uint32_t)Rounded;

    Status = Rc95BcdOpenHive(Image, ImageSize, &Hive);
    if (Status != Rc95LdrSuccess)
        return Status;

    /* OpenHive has validated every HBIN and cell boundary. */
    while (BinOffset < Hive.HiveBinsSize)
    {
        uint8_t *Bin = Bytes + REG_HIVE_BIN_OFFSET + BinOffset;
        uint32_t BinSize = Rc95LdrReadU32(Bin + 8);
        uint32_t Cursor = REG_HBIN_HEADER_SIZE;

        while (Cursor < BinSize)
        {
            uint8_t *RawCell = Bin + Cursor;
            int32_t SignedSize = (int32_t)Rc95LdrReadU32(RawCell);
            uint32_t CellSize = (uint32_t)(SignedSize < 0 ?
                                           -SignedSize : SignedSize);

            if (SignedSize > 0 && CellSize >= RequiredTotal)
            {
                uint32_t Remainder = CellSize - RequiredTotal;
                uint32_t ActualTotal = RequiredTotal;
                uint32_t Capacity;
                uint32_t NegativeSize;

                if (Remainder != 0 && Remainder < 8u)
                {
                    ActualTotal = CellSize;
                    Remainder = 0;
                }
                Capacity = ActualTotal - REG_CELL_HEADER_SIZE;

                /* Zero payload before publishing the cell as allocated. */
                memset(RawCell + REG_CELL_HEADER_SIZE, 0, Capacity);
                NegativeSize = 0u - ActualTotal;
                RawCell[0] = (uint8_t)NegativeSize;
                RawCell[1] = (uint8_t)(NegativeSize >> 8);
                RawCell[2] = (uint8_t)(NegativeSize >> 16);
                RawCell[3] = (uint8_t)(NegativeSize >> 24);

                if (Remainder != 0)
                {
                    uint8_t *FreeCell = RawCell + ActualTotal;
                    FreeCell[0] = (uint8_t)Remainder;
                    FreeCell[1] = (uint8_t)(Remainder >> 8);
                    FreeCell[2] = (uint8_t)(Remainder >> 16);
                    FreeCell[3] = (uint8_t)(Remainder >> 24);
                }

                *CellOffset = BinOffset + Cursor;
                *CellDataCapacity = Capacity;

                Sequence = Rc95LdrReadU32(Bytes + 4) + 1u;
                Bytes[4] = (uint8_t)Sequence;
                Bytes[5] = (uint8_t)(Sequence >> 8);
                Bytes[6] = (uint8_t)(Sequence >> 16);
                Bytes[7] = (uint8_t)(Sequence >> 24);
                Bytes[8] = (uint8_t)Sequence;
                Bytes[9] = (uint8_t)(Sequence >> 8);
                Bytes[10] = (uint8_t)(Sequence >> 16);
                Bytes[11] = (uint8_t)(Sequence >> 24);
                Checksum = Rc95BcdChecksum(Bytes);
                Bytes[0x1fc] = (uint8_t)Checksum;
                Bytes[0x1fd] = (uint8_t)(Checksum >> 8);
                Bytes[0x1fe] = (uint8_t)(Checksum >> 16);
                Bytes[0x1ff] = (uint8_t)(Checksum >> 24);
                return Rc95LdrSuccess;
            }
            Cursor += CellSize;
        }
        BinOffset += BinSize;
    }
    return Rc95LdrOutOfMemory;
}



static int
Rc95BcdRangesOverlap(const void *First, size_t FirstSize,
                     const void *Second, size_t SecondSize)
{
    uintptr_t FirstStart = (uintptr_t)First;
    uintptr_t SecondStart = (uintptr_t)Second;
    uintptr_t FirstEnd, SecondEnd;

    if (FirstSize > UINTPTR_MAX - FirstStart ||
        SecondSize > UINTPTR_MAX - SecondStart)
        return 1;
    FirstEnd = FirstStart + FirstSize;
    SecondEnd = SecondStart + SecondSize;
    return FirstStart < SecondEnd && SecondStart < FirstEnd;
}

/* Write a little-endian 16-bit field without relying on host alignment. */
static void
Rc95BcdWriteU16(uint8_t *Destination, uint16_t Value)
{
    Destination[0] = (uint8_t)Value;
    Destination[1] = (uint8_t)(Value >> 8);
}

/* Write a little-endian 32-bit field without relying on host alignment. */
static void
Rc95BcdWriteU32(uint8_t *Destination, uint32_t Value)
{
    Destination[0] = (uint8_t)Value;
    Destination[1] = (uint8_t)(Value >> 8);
    Destination[2] = (uint8_t)(Value >> 16);
    Destination[3] = (uint8_t)(Value >> 24);
}

/*
 * Convert a caller-owned bin-relative cell offset into a checked payload
 * pointer in a mutable image. The image has already passed OpenHive.
 */
static RC95LDR_STATUS
Rc95BcdGetMutableCell(void *Image, size_t ImageSize, uint32_t CellOffset,
                     uint8_t **CellData, uint32_t *CellCapacity)
{
    uint8_t *Bytes = (uint8_t *)Image;
    uint32_t Absolute;
    int32_t SignedSize;
    uint32_t Total;

    if (Image == NULL || CellData == NULL || CellCapacity == NULL)
        return Rc95LdrInvalidParameter;
    if (CellOffset > UINT32_MAX - REG_HIVE_BIN_OFFSET)
        return Rc95LdrOutOfBounds;
    Absolute = REG_HIVE_BIN_OFFSET + CellOffset;
    if (!Rc95LdrRangeValid(ImageSize, Absolute, REG_CELL_HEADER_SIZE))
        return Rc95LdrOutOfBounds;
    SignedSize = (int32_t)Rc95LdrReadU32(Bytes + Absolute);
    if (SignedSize >= 0 || SignedSize == INT32_MIN)
        return Rc95LdrCorruptData;
    Total = (uint32_t)(-SignedSize);
    if (Total < 8u || (Total & 7u) != 0 ||
        !Rc95LdrRangeValid(ImageSize, Absolute, Total))
        return Rc95LdrCorruptData;
    *CellData = Bytes + Absolute + REG_CELL_HEADER_SIZE;
    *CellCapacity = Total - REG_CELL_HEADER_SIZE;
    return Rc95LdrSuccess;
}

/*
 * Flatten an existing RI/LF/LH/LI subkey index into a caller-provided
 * sequence of NK cell references. The caller bounds the destination by the
 * parent's SubKeyCount and verifies the final number of references.
 */
static RC95LDR_STATUS
Rc95BcdCopySubKeyIndex(const RC95BCD_HIVE *Hive, uint32_t ListOffset,
                       uint32_t ParentCell, uint32_t Expected,
                       uint8_t *Destination,
                       uint32_t *Written, uint32_t Depth)
{
    RC95BCD_CELL List;
    uint16_t Signature, Count;
    uint32_t Index, EntrySize;
    RC95LDR_STATUS Status;

    if (Depth > RC95BCD_MAX_INDEX_DEPTH || Written == NULL ||
        Destination == NULL || *Written > Expected)
        return Rc95LdrCorruptData;
    Status = Rc95BcdGetCell(Hive, ListOffset, &List);
    if (Status != Rc95LdrSuccess) return Status;
    if (List.Size < 4u) return Rc95LdrCorruptData;
    Signature = Rc95LdrReadU16(List.Data);
    Count = Rc95LdrReadU16(List.Data + 2u);
    if (Signature == RI_SIGNATURE)
    {
        if (List.Size < 4u + (uint32_t)Count * 4u)
            return Rc95LdrCorruptData;
        for (Index = 0; Index < Count; ++Index)
        {
            Status = Rc95BcdCopySubKeyIndex(Hive,
                Rc95LdrReadU32(List.Data + 4u + Index * 4u),
                ParentCell, Expected, Destination, Written, Depth + 1u);
            if (Status != Rc95LdrSuccess) return Status;
        }
        return Rc95LdrSuccess;
    }
    if (Signature == LI_SIGNATURE) EntrySize = 4u;
    else if (Signature == LF_SIGNATURE || Signature == LH_SIGNATURE)
        EntrySize = 8u;
    else return Rc95LdrCorruptData;
    if (List.Size < 4u + (uint32_t)Count * EntrySize ||
        Count > Expected - *Written)
        return Rc95LdrCorruptData;
    for (Index = 0; Index < Count; ++Index)
    {
        uint32_t ChildOffset = Rc95LdrReadU32(
            List.Data + 4u + Index * EntrySize);
        RC95BCD_CELL Child;
        if (*Written >= Expected) return Rc95LdrCorruptData;
        Status = Rc95BcdGetCell(Hive, ChildOffset, &Child);
        if (Status != Rc95LdrSuccess) return Status;
        if (Child.Size < REG_NK_FIXED_SIZE ||
            Rc95LdrReadU16(Child.Data) != NK_SIGNATURE ||
            Rc95LdrReadU32(Child.Data + 16u) != ParentCell)
            return Rc95LdrCorruptData;
        Rc95BcdWriteU32(Destination + (*Written)++ * 4u, ChildOffset);
    }
    return Rc95LdrSuccess;
}

/*
 * Create an NK cell and automatically rebuild its parent's subkey index.
 * Scratch is a distinct image-sized transaction buffer. The original hive is
 * replaced only after the new child, LI index and parent metadata validate.
 */
RC95LDR_STATUS
Rc95BcdCreateKeyCell(void *Image, size_t ImageSize, void *Scratch,
                     size_t ScratchSize, uint32_t ParentCell,
                     const char *Name, uint32_t *NewKeyCell)
{
    uint8_t *CellData, *ParentData, *IndexData;
    uint32_t Capacity, Offset, ParentCapacity;
    uint32_t OldSubKeyCount = 0, OldSubKeyList = 0xffffffffu;
    uint32_t ValueCount = 0, ValueList = 0xffffffffu;
    uint32_t NewIndexOffset = 0xffffffffu, Written = 0;
    size_t Index, Length;
    RC95BCD_HIVE Hive;
    RC95LDR_STATUS Status;

    if (Image == NULL || Scratch == NULL || Name == NULL ||
        NewKeyCell == NULL || ScratchSize < ImageSize ||
        Rc95BcdRangesOverlap(Image, ImageSize, Scratch, ScratchSize))
        return Rc95LdrInvalidParameter;
    *NewKeyCell = 0xffffffffu;
    Length = strlen(Name);
    if (Length == 0 || Length > UINT16_MAX ||
        Length > UINT32_MAX - REG_NK_FIXED_SIZE ||
        Rc95BcdRangesOverlap(Name, Length + 1u, Scratch, ScratchSize))
        return Rc95LdrInvalidParameter;
    for (Index = 0; Index < Length; ++Index)
    {
        unsigned char Character = (unsigned char)Name[Index];
        if (Character < 0x20u || Character > 0x7eu)
            return Rc95LdrUnsupported;
    }

    Status = Rc95BcdOpenHive(Image, ImageSize, &Hive);
    if (Status != Rc95LdrSuccess) return Status;
    if (ParentCell != 0xffffffffu)
    {
        RC95BCD_CELL Parent;
        uint16_t Flags;
        uint32_t Existing;
        Status = Rc95BcdGetKey(&Hive, ParentCell, &Parent, &Flags,
                               &OldSubKeyCount, &OldSubKeyList,
                               &ValueCount, &ValueList);
        if (Status != Rc95LdrSuccess) return Status;
        if (OldSubKeyCount >= UINT16_MAX) return Rc95LdrUnsupported;
        Status = Rc95BcdFindSubKey(&Hive, ParentCell, Name, &Existing);
        if (Status == Rc95LdrSuccess) return Rc95LdrInvalidParameter;
        if (Status != Rc95LdrNotFound) return Status;
        if ((OldSubKeyCount == 0 && OldSubKeyList != 0xffffffffu) ||
            (OldSubKeyCount != 0 && OldSubKeyList == 0xffffffffu))
            return Rc95LdrCorruptData;
    }

    memmove(Scratch, Image, ImageSize);
    Status = Rc95BcdAllocateCell(Scratch, ImageSize,
                                 REG_NK_FIXED_SIZE + (uint32_t)Length,
                                 &Offset, &Capacity);
    if (Status != Rc95LdrSuccess) return Status;
    if (Capacity < REG_NK_FIXED_SIZE + Length) return Rc95LdrCorruptData;
    Status = Rc95BcdGetMutableCell(Scratch, ImageSize, Offset,
                                   &CellData, &Capacity);
    if (Status != Rc95LdrSuccess) return Status;
    if (Capacity < REG_NK_FIXED_SIZE + Length) return Rc95LdrCorruptData;

    memset(CellData, 0, Capacity);
    Rc95BcdWriteU16(CellData, NK_SIGNATURE);
    Rc95BcdWriteU16(CellData + 2, REG_NK_COMP_NAME);
    Rc95BcdWriteU32(CellData + 16, ParentCell);
    Rc95BcdWriteU32(CellData + 20, 0);
    Rc95BcdWriteU32(CellData + 24, 0);
    Rc95BcdWriteU32(CellData + 28, 0xffffffffu);
    Rc95BcdWriteU32(CellData + 32, 0xffffffffu);
    Rc95BcdWriteU32(CellData + 36, 0);
    Rc95BcdWriteU32(CellData + 40, 0xffffffffu);
    Rc95BcdWriteU32(CellData + 44, 0xffffffffu);
    Rc95BcdWriteU32(CellData + 48, 0xffffffffu);
    Rc95BcdWriteU16(CellData + 72, (uint16_t)Length);
    memcpy(CellData + REG_NK_FIXED_SIZE, Name, Length);

    if (ParentCell != 0xffffffffu)
    {
        uint32_t NewCount = OldSubKeyCount + 1u;
        uint32_t ListSize = 4u + NewCount * 4u;
        Status = Rc95BcdOpenHive(Scratch, ImageSize, &Hive);
        if (Status != Rc95LdrSuccess) return Status;
        Status = Rc95BcdAllocateCell(Scratch, ImageSize, ListSize,
                                     &NewIndexOffset, &Capacity);
        if (Status != Rc95LdrSuccess) return Status;
        if (Capacity < ListSize) return Rc95LdrCorruptData;
        Status = Rc95BcdGetMutableCell(Scratch, ImageSize, NewIndexOffset,
                                       &IndexData, &Capacity);
        if (Status != Rc95LdrSuccess) return Status;
        if (Capacity < ListSize) return Rc95LdrCorruptData;
        memset(IndexData, 0, Capacity);
        Rc95BcdWriteU16(IndexData, LI_SIGNATURE);
        Rc95BcdWriteU16(IndexData + 2u, (uint16_t)NewCount);

        if (OldSubKeyCount != 0)
        {
            /* The old index remains intact while its entries are flattened. */
            Status = Rc95BcdCopySubKeyIndex(&Hive, OldSubKeyList,
                ParentCell, OldSubKeyCount, IndexData + 4u,
                &Written, 0);
            if (Status != Rc95LdrSuccess) return Status;
            if (Written != OldSubKeyCount) return Rc95LdrCorruptData;
        }
        Rc95BcdWriteU32(IndexData + 4u + OldSubKeyCount * 4u, Offset);

        Status = Rc95BcdGetMutableCell(Scratch, ImageSize, ParentCell,
                                       &ParentData, &ParentCapacity);
        if (Status != Rc95LdrSuccess) return Status;
        if (ParentCapacity < REG_NK_FIXED_SIZE ||
            Rc95LdrReadU16(ParentData) != NK_SIGNATURE)
            return Rc95LdrCorruptData;
        Rc95BcdWriteU32(ParentData + 20u, NewCount);
        Rc95BcdWriteU32(ParentData + 24u, 0);
        Rc95BcdWriteU32(ParentData + 28u, NewIndexOffset);
        /* Existing value-list fields are deliberately untouched. */
    }

    Status = Rc95BcdOpenHive(Scratch, ImageSize, &Hive);
    if (Status != Rc95LdrSuccess) return Status;
    if (ParentCell != 0xffffffffu)
    {
        RC95BCD_CELL Parent;
        uint16_t Flags;
        uint32_t CheckCount, CheckList, CheckValues, CheckValueList;
        Status = Rc95BcdGetKey(&Hive, ParentCell, &Parent, &Flags,
                               &CheckCount, &CheckList,
                               &CheckValues, &CheckValueList);
        if (Status != Rc95LdrSuccess || CheckCount != OldSubKeyCount + 1u ||
            CheckList != NewIndexOffset || CheckValues != ValueCount ||
            CheckValueList != ValueList)
            return Status == Rc95LdrSuccess ? Rc95LdrCorruptData : Status;
    }
    memmove(Image, Scratch, ImageSize);
    *NewKeyCell = Offset;
    return Rc95LdrSuccess;
}
/*
 * Create an unlinked VK cell and, for data longer than four bytes, a separate
 * external data cell. The caller supplies a distinct scratch image so failed
 * multi-cell construction cannot leak half-created cells into the source.
 */
/*
 * Create a VK cell and automatically append it to the specified parent's
 * value list. The data cell, VK cell, replacement value list and parent NK
 * metadata are built in Scratch and committed as one transaction.
 */
RC95LDR_STATUS
Rc95BcdCreateValueCell(void *Image, size_t ImageSize, void *Scratch,
                       size_t ScratchSize, uint32_t ParentCell,
                       const char *Name, uint32_t Type,
                       const void *Data, uint32_t DataLength,
                       uint32_t *NewValueCell, uint32_t *NewDataCell)
{
    uint8_t *CellData, *ParentData, *ListData;
    uint32_t Capacity, ValueOffset, DataOffset = 0xffffffffu;
    uint32_t OldValueCount = 0, OldValueList = 0xffffffffu;
    uint32_t NewValueList = 0xffffffffu, ParentCapacity, Index;
    size_t NameLength;
    RC95BCD_HIVE Hive;
    RC95LDR_STATUS Status;

    if (Image == NULL || Scratch == NULL || Name == NULL ||
        NewValueCell == NULL || ParentCell == 0xffffffffu ||
        (Data == NULL && DataLength != 0) || ScratchSize < ImageSize ||
        Rc95BcdRangesOverlap(Image, ImageSize, Scratch, ScratchSize) ||
        (Data != NULL && DataLength != 0 &&
         Rc95BcdRangesOverlap(Data, DataLength, Scratch, ScratchSize)))
        return Rc95LdrInvalidParameter;
    *NewValueCell = 0xffffffffu;
    if (NewDataCell != NULL) *NewDataCell = 0xffffffffu;
    NameLength = strlen(Name);
    if (NameLength > UINT16_MAX ||
        NameLength > UINT32_MAX - REG_VK_FIXED_SIZE ||
        Rc95BcdRangesOverlap(Name, NameLength + 1u, Scratch, ScratchSize))
        return Rc95LdrInvalidParameter;
    for (Index = 0; Index < NameLength; ++Index)
    {
        unsigned char Character = (unsigned char)Name[Index];
        if (Character < 0x20u || Character > 0x7eu)
            return Rc95LdrUnsupported;
    }

    Status = Rc95BcdOpenHive(Image, ImageSize, &Hive);
    if (Status != Rc95LdrSuccess) return Status;
    {
        RC95BCD_CELL Parent;
        uint16_t Flags;
        uint32_t SubKeyCount, SubKeyList;
        Status = Rc95BcdGetKey(&Hive, ParentCell, &Parent, &Flags,
                               &SubKeyCount, &SubKeyList,
                               &OldValueCount, &OldValueList);
        if (Status != Rc95LdrSuccess) return Status;
    }
    if (OldValueCount > UINT32_MAX / 4u - 1u)
        return Rc95LdrIntegerOverflow;
    {
        RC95BCD_VALUE Existing;
        Status = Rc95BcdFindValue(&Hive, ParentCell, Name, &Existing);
        if (Status == Rc95LdrSuccess) return Rc95LdrInvalidParameter;
        if (Status != Rc95LdrNotFound) return Status;
    }
    if ((OldValueCount == 0 && OldValueList != 0xffffffffu) ||
        (OldValueCount != 0 && OldValueList == 0xffffffffu))
        return Rc95LdrCorruptData;

    memmove(Scratch, Image, ImageSize);
    if (DataLength > 4u)
    {
        Status = Rc95BcdAllocateCell(Scratch, ImageSize, DataLength,
                                     &DataOffset, &Capacity);
        if (Status != Rc95LdrSuccess) return Status;
        if (Capacity < DataLength) return Rc95LdrCorruptData;
        Status = Rc95BcdGetMutableCell(Scratch, ImageSize, DataOffset,
                                       &CellData, &Capacity);
        if (Status != Rc95LdrSuccess) return Status;
        if (Capacity < DataLength) return Rc95LdrCorruptData;
        memcpy(CellData, Data, DataLength);
    }

    Status = Rc95BcdAllocateCell(Scratch, ImageSize,
                                 REG_VK_FIXED_SIZE + (uint32_t)NameLength,
                                 &ValueOffset, &Capacity);
    if (Status != Rc95LdrSuccess) return Status;
    if (Capacity < REG_VK_FIXED_SIZE + NameLength)
        return Rc95LdrCorruptData;
    Status = Rc95BcdGetMutableCell(Scratch, ImageSize, ValueOffset,
                                   &CellData, &Capacity);
    if (Status != Rc95LdrSuccess) return Status;
    if (Capacity < REG_VK_FIXED_SIZE + NameLength)
        return Rc95LdrCorruptData;

    memset(CellData, 0, Capacity);
    Rc95BcdWriteU16(CellData, VK_SIGNATURE);
    Rc95BcdWriteU16(CellData + 2, (uint16_t)NameLength);
    Rc95BcdWriteU32(CellData + 4,
                    DataLength <= 4u ? (DataLength | REG_VALUE_INLINE) :
                                       DataLength);
    if (DataLength <= 4u)
    {
        if (DataLength != 0) memcpy(CellData + 8, Data, DataLength);
    }
    else Rc95BcdWriteU32(CellData + 8, DataOffset);
    Rc95BcdWriteU32(CellData + 12, Type);
    Rc95BcdWriteU16(CellData + 16, REG_VK_COMP_NAME);
    if (NameLength != 0)
        memcpy(CellData + REG_VK_FIXED_SIZE, Name, NameLength);

    /* Value lists are flat arrays of VK cell offsets, without a signature. */
    {
        uint32_t NewCount = OldValueCount + 1u;
        uint32_t ListSize = NewCount * 4u;
        RC95BCD_CELL OldList;
        Status = Rc95BcdAllocateCell(Scratch, ImageSize, ListSize,
                                     &NewValueList, &Capacity);
        if (Status != Rc95LdrSuccess) return Status;
        if (Capacity < ListSize) return Rc95LdrCorruptData;
        Status = Rc95BcdGetMutableCell(Scratch, ImageSize, NewValueList,
                                       &ListData, &Capacity);
        if (Status != Rc95LdrSuccess) return Status;
        if (Capacity < ListSize) return Rc95LdrCorruptData;
        memset(ListData, 0, Capacity);

        if (OldValueCount != 0)
        {
            Status = Rc95BcdOpenHive(Scratch, ImageSize, &Hive);
            if (Status != Rc95LdrSuccess) return Status;
            Status = Rc95BcdGetCell(&Hive, OldValueList, &OldList);
            if (Status != Rc95LdrSuccess) return Status;
            if (OldList.Size < OldValueCount * 4u)
                return Rc95LdrCorruptData;
            for (Index = 0; Index < OldValueCount; ++Index)
            {
                RC95BCD_CELL OldValue;
                uint32_t OldOffset = Rc95LdrReadU32(
                    OldList.Data + Index * 4u);
                Status = Rc95BcdGetCell(&Hive, OldOffset, &OldValue);
                if (Status != Rc95LdrSuccess) return Status;
                if (OldValue.Size < REG_VK_FIXED_SIZE ||
                    Rc95LdrReadU16(OldValue.Data) != VK_SIGNATURE)
                    return Rc95LdrCorruptData;
                Rc95BcdWriteU32(ListData + Index * 4u, OldOffset);
            }
        }
        Rc95BcdWriteU32(ListData + OldValueCount * 4u, ValueOffset);

        Status = Rc95BcdGetMutableCell(Scratch, ImageSize, ParentCell,
                                       &ParentData, &ParentCapacity);
        if (Status != Rc95LdrSuccess) return Status;
        if (ParentCapacity < REG_NK_FIXED_SIZE ||
            Rc95LdrReadU16(ParentData) != NK_SIGNATURE)
            return Rc95LdrCorruptData;
        Rc95BcdWriteU32(ParentData + 36u, NewCount);
        Rc95BcdWriteU32(ParentData + 40u, NewValueList);
    }

    Status = Rc95BcdOpenHive(Scratch, ImageSize, &Hive);
    if (Status != Rc95LdrSuccess) return Status;
    {
        RC95BCD_CELL Parent;
        uint16_t Flags;
        uint32_t CheckSubCount, CheckSubList, CheckValueCount, CheckValueList;
        RC95BCD_VALUE CheckValue;
        Status = Rc95BcdGetKey(&Hive, ParentCell, &Parent, &Flags,
                               &CheckSubCount, &CheckSubList,
                               &CheckValueCount, &CheckValueList);
        if (Status != Rc95LdrSuccess || CheckValueCount != OldValueCount + 1u ||
            CheckValueList != NewValueList)
            return Status == Rc95LdrSuccess ? Rc95LdrCorruptData : Status;
        Status = Rc95BcdFindValue(&Hive, ParentCell, Name, &CheckValue);
        if (Status != Rc95LdrSuccess || CheckValue.ValueCell != ValueOffset)
            return Status == Rc95LdrSuccess ? Rc95LdrCorruptData : Status;
    }

    memmove(Image, Scratch, ImageSize);
    *NewValueCell = ValueOffset;
    if (NewDataCell != NULL) *NewDataCell = DataOffset;
    return Rc95LdrSuccess;
}

static RC95LDR_STATUS
Rc95BcdReclaimDetachedCellInPlace(void *Image, size_t ImageSize,
                                  uint32_t CellOffset);
static RC95LDR_STATUS
Rc95BcdReclaimIndexTree(void *Image, size_t ImageSize, void *Scratch,
                        size_t ScratchSize, uint32_t IndexCell,
                        uint32_t Depth);

/*
 * Rebuild a key's stable subkey index and value-list cells from authoritative
 * cell-offset arrays. Offsets are hive-bin-relative, as stored in NK/VK links.
 * The caller creates the child NK/VK cells first; this routine then publishes
 * both lists and their counts together. Once the new links are published in
 * the scratch image, obsolete RI/LF/LH/LI index trees and the old VK-offset
 * list are reclaimed before the transaction is committed. The supplied
 * subkey order is preserved; the replacement index is an LI leaf.
 */
RC95LDR_STATUS
Rc95BcdRebuildKeyStructure(void *Image, size_t ImageSize, void *Scratch,
                           size_t ScratchSize, uint32_t ParentCell,
                           const uint32_t *SubKeyCells, uint32_t SubKeyCount,
                           const uint32_t *ValueCells, uint32_t ValueCount)
{
    uint8_t *ParentData;
    uint8_t *ListData;
    uint32_t ParentCapacity, SubKeyList = 0xffffffffu;
    uint32_t ValueList = 0xffffffffu;
    uint32_t Capacity, Index, Sequence;
    uint32_t OldSubKeyCount = 0, OldSubKeyList = 0xffffffffu;
    uint32_t OldValueCount = 0, OldValueList = 0xffffffffu;
    RC95BCD_HIVE Hive;
    RC95LDR_STATUS Status;

    if (Image == NULL || Scratch == NULL || ParentCell == 0xffffffffu ||
        ScratchSize < ImageSize ||
        (SubKeyCells == NULL && SubKeyCount != 0) ||
        (ValueCells == NULL && ValueCount != 0) ||
        SubKeyCount > UINT16_MAX || ValueCount > UINT16_MAX ||
        Rc95BcdRangesOverlap(Image, ImageSize, Scratch, ScratchSize) ||
        (SubKeyCells != NULL &&
         Rc95BcdRangesOverlap(SubKeyCells,
             (size_t)SubKeyCount * sizeof(*SubKeyCells), Scratch, ScratchSize)) ||
        (ValueCells != NULL &&
         Rc95BcdRangesOverlap(ValueCells,
             (size_t)ValueCount * sizeof(*ValueCells), Scratch, ScratchSize)))
        return Rc95LdrInvalidParameter;

    Status = Rc95BcdOpenHive(Image, ImageSize, &Hive);
    if (Status != Rc95LdrSuccess)
        return Status;

    {
        RC95BCD_CELL Parent;
        uint16_t Flags;
        Status = Rc95BcdGetKey(&Hive, ParentCell, &Parent, &Flags,
                               &OldSubKeyCount, &OldSubKeyList,
                               &OldValueCount, &OldValueList);
        if (Status != Rc95LdrSuccess)
            return Status;
    }

    if ((OldSubKeyCount == 0 && OldSubKeyList != 0xffffffffu) ||
        (OldSubKeyCount != 0 && OldSubKeyList == 0xffffffffu) ||
        (OldValueCount == 0 && OldValueList != 0xffffffffu) ||
        (OldValueCount != 0 && OldValueList == 0xffffffffu))
        return Rc95LdrCorruptData;

    /* Validate every referenced child before changing the caller's image. */
    for (Index = 0; Index < SubKeyCount; ++Index)
    {
        RC95BCD_CELL Child;
        uint32_t Other;
        Status = Rc95BcdGetCell(&Hive, SubKeyCells[Index], &Child);
        if (Status != Rc95LdrSuccess)
            return Status;
        if (Child.Size < REG_NK_FIXED_SIZE ||
            Rc95LdrReadU16(Child.Data) != NK_SIGNATURE ||
            Rc95LdrReadU32(Child.Data + 16) != ParentCell)
            return Rc95LdrCorruptData;
        for (Other = 0; Other < Index; ++Other)
            if (SubKeyCells[Other] == SubKeyCells[Index])
                return Rc95LdrCorruptData;
    }

    for (Index = 0; Index < ValueCount; ++Index)
    {
        RC95BCD_CELL Value;
        uint32_t Other;
        Status = Rc95BcdGetCell(&Hive, ValueCells[Index], &Value);
        if (Status != Rc95LdrSuccess)
            return Status;
        if (Value.Size < REG_VK_FIXED_SIZE ||
            Rc95LdrReadU16(Value.Data) != VK_SIGNATURE)
            return Rc95LdrCorruptData;
        for (Other = 0; Other < Index; ++Other)
            if (ValueCells[Other] == ValueCells[Index])
                return Rc95LdrCorruptData;
    }

    memmove(Scratch, Image, ImageSize);

    if (SubKeyCount != 0)
    {
        uint32_t ListSize;
        if (SubKeyCount > (UINT32_MAX - 4u) / 4u)
            return Rc95LdrIntegerOverflow;
        ListSize = 4u + SubKeyCount * 4u;
        Status = Rc95BcdAllocateCell(Scratch, ImageSize, ListSize,
                                     &SubKeyList, &Capacity);
        if (Status != Rc95LdrSuccess)
            return Status;
        if (Capacity < ListSize)
            return Rc95LdrCorruptData;
        Status = Rc95BcdGetMutableCell(Scratch, ImageSize, SubKeyList,
                                       &ListData, &Capacity);
        if (Status != Rc95LdrSuccess)
            return Status;
        if (Capacity < ListSize)
            return Rc95LdrCorruptData;
        memset(ListData, 0, Capacity);
        Rc95BcdWriteU16(ListData, LI_SIGNATURE);
        Rc95BcdWriteU16(ListData + 2, (uint16_t)SubKeyCount);
        for (Index = 0; Index < SubKeyCount; ++Index)
            Rc95BcdWriteU32(ListData + 4u + Index * 4u, SubKeyCells[Index]);
    }

    if (ValueCount != 0)
    {
        uint32_t ListSize;
        if (ValueCount > (UINT32_MAX / 4u))
            return Rc95LdrIntegerOverflow;
        ListSize = ValueCount * 4u;
        Status = Rc95BcdAllocateCell(Scratch, ImageSize, ListSize,
                                     &ValueList, &Capacity);
        if (Status != Rc95LdrSuccess)
            return Status;
        if (Capacity < ListSize)
            return Rc95LdrCorruptData;
        Status = Rc95BcdGetMutableCell(Scratch, ImageSize, ValueList,
                                       &ListData, &Capacity);
        if (Status != Rc95LdrSuccess)
            return Status;
        if (Capacity < ListSize)
            return Rc95LdrCorruptData;
        memset(ListData, 0, Capacity);
        for (Index = 0; Index < ValueCount; ++Index)
            Rc95BcdWriteU32(ListData + Index * 4u, ValueCells[Index]);
    }

    Status = Rc95BcdGetMutableCell(Scratch, ImageSize, ParentCell,
                                   &ParentData, &ParentCapacity);
    if (Status != Rc95LdrSuccess)
        return Status;
    if (ParentCapacity < REG_NK_FIXED_SIZE ||
        Rc95LdrReadU16(ParentData) != NK_SIGNATURE)
        return Rc95LdrCorruptData;

    /* Publish counts and links only after both replacement cells exist. */
    Rc95BcdWriteU32(ParentData + 20, SubKeyCount);
    Rc95BcdWriteU32(ParentData + 24, 0);
    Rc95BcdWriteU32(ParentData + 28, SubKeyList);
    Rc95BcdWriteU32(ParentData + 36, ValueCount);
    Rc95BcdWriteU32(ParentData + 40, ValueList);

    /* Publish a self-consistent REGF header before helpers reopen the hive. */
    Sequence = Rc95LdrReadU32((const uint8_t *)Scratch + 4u) + 1u;
    Rc95BcdWriteU32((uint8_t *)Scratch + 4u, Sequence);
    Rc95BcdWriteU32((uint8_t *)Scratch + 8u, Sequence);
    Rc95BcdWriteU32((uint8_t *)Scratch + 0x1fcu,
                    Rc95BcdChecksum((const uint8_t *)Scratch));

    /*
     * The parent now points exclusively at the replacement structures. Reclaim
     * the detached old tree root-first (RI parents before their child indexes),
     * then reclaim the old flat VK-offset list. The scratch image remains the
     * transaction boundary: any failed reclamation discards every change.
     */
    if (OldSubKeyCount != 0 && OldSubKeyList != 0xffffffffu)
    {
        Status = Rc95BcdReclaimIndexTree(Scratch, ImageSize, Scratch,
                                         ScratchSize, OldSubKeyList, 0);
        if (Status != Rc95LdrSuccess)
            return Status;
    }
    if (OldValueCount != 0 && OldValueList != 0xffffffffu)
    {
        Status = Rc95BcdReclaimDetachedCellInPlace(Scratch, ImageSize,
                                                   OldValueList);
        if (Status != Rc95LdrSuccess)
            return Status;
    }

    Status = Rc95BcdOpenHive(Scratch, ImageSize, &Hive);
    if (Status != Rc95LdrSuccess)
        return Status;

    /* Re-read the published parent to catch truncated/overwritten metadata. */
    {
        RC95BCD_CELL Check;
        uint16_t Flags;
        uint32_t CheckSubCount, CheckSubList, CheckValueCount, CheckValueList;
        Status = Rc95BcdGetKey(&Hive, ParentCell, &Check, &Flags,
                               &CheckSubCount, &CheckSubList,
                               &CheckValueCount, &CheckValueList);
        if (Status != Rc95LdrSuccess ||
            CheckSubCount != SubKeyCount || CheckSubList != SubKeyList ||
            CheckValueCount != ValueCount || CheckValueList != ValueList)
            return Status == Rc95LdrSuccess ? Rc95LdrCorruptData : Status;
    }

    memmove(Image, Scratch, ImageSize);
    return Rc95LdrSuccess;
}


/*
 * Reclaim a detached registry cell.  Before releasing it, scan every other
 * allocated cell for the exact little-endian hive-bin-relative offset.
 * This deliberately conservative reference test can reject an offset that
 * happens to occur in opaque payload data, but can never free a cell still
 * named by an NK, VK, index, list, or opaque structure.
 *
 * Adjacent free cells in the containing HBIN are coalesced after the target
 * has been marked free.  All work is performed in a scratch image and is
 * committed only after the resulting HBIN chain passes OpenHive validation.
 */
static RC95LDR_STATUS
Rc95BcdReclaimDetachedCellInPlace(void *Image, size_t ImageSize,
                                  uint32_t CellOffset)
{
    uint8_t *Bytes, *Bin;
    uint32_t BinOffset = 0, TargetBin = 0, TargetWithin = 0;
    uint32_t TargetTotal = 0, BinSize, Cursor, Sequence, Checksum;
    RC95BCD_HIVE Hive;
    RC95BCD_CELL TargetCell;
    RC95LDR_STATUS Status;

    Status = Rc95BcdOpenHive(Image, ImageSize, &Hive);
    if (Status != Rc95LdrSuccess) return Status;
    Status = Rc95BcdGetCell(&Hive, CellOffset, &TargetCell);
    if (Status != Rc95LdrSuccess) return Status;
    if (CellOffset == Hive.RootCell) return Rc95LdrInvalidParameter;

    /* Reject reclamation while any allocated cell still contains this link. */
    {
        uint32_t ScanBin = 0;
        while (ScanBin < Hive.HiveBinsSize)
        {
            const uint8_t *Scan = Hive.Image + REG_HIVE_BIN_OFFSET + ScanBin;
            uint32_t ScanSize = Rc95LdrReadU32(Scan + 8);
            uint32_t ScanCursor = REG_HBIN_HEADER_SIZE;
            while (ScanCursor < ScanSize)
            {
                int32_t Signed = (int32_t)Rc95LdrReadU32(Scan + ScanCursor);
                uint32_t Total = (uint32_t)(Signed < 0 ? -Signed : Signed);
                uint32_t ThisOffset = ScanBin + ScanCursor;
                if (Signed < 0 && ThisOffset != CellOffset)
                {
                    const uint8_t *Payload = Scan + ScanCursor + 4u;
                    uint32_t PayloadSize = Total - 4u, i;
                    for (i = 0; i + 4u <= PayloadSize; ++i)
                        if (Rc95LdrReadU32(Payload + i) == CellOffset)
                            return Rc95LdrInvalidParameter;
                }
                ScanCursor += Total;
            }
            ScanBin += ScanSize;
        }
    }

    while (BinOffset < Hive.HiveBinsSize)
    {
        const uint8_t *Current = Hive.Image + REG_HIVE_BIN_OFFSET + BinOffset;
        BinSize = Rc95LdrReadU32(Current + 8);
        if (CellOffset >= BinOffset + REG_HBIN_HEADER_SIZE &&
            CellOffset < BinOffset + BinSize)
        {
            TargetBin = BinOffset;
            TargetWithin = CellOffset - BinOffset;
            Cursor = REG_HBIN_HEADER_SIZE;
            while (Cursor < BinSize)
            {
                int32_t Signed = (int32_t)Rc95LdrReadU32(Current + Cursor);
                uint32_t Total = (uint32_t)(Signed < 0 ? -Signed : Signed);
                if (Cursor == TargetWithin)
                {
                    if (Signed > 0) return Rc95LdrNotFound;
                    TargetTotal = Total;
                    break;
                }
                Cursor += Total;
            }
            break;
        }
        BinOffset += BinSize;
    }
    if (TargetTotal == 0) return Rc95LdrCorruptData;

    Bytes = (uint8_t *)Image;
    Bin = Bytes + REG_HIVE_BIN_OFFSET + TargetBin;
    Rc95BcdWriteU32(Bin + TargetWithin, TargetTotal);

    BinSize = Rc95LdrReadU32(Bin + 8);
    Cursor = REG_HBIN_HEADER_SIZE;
    while (Cursor < BinSize)
    {
        int32_t Signed = (int32_t)Rc95LdrReadU32(Bin + Cursor);
        uint32_t Total = (uint32_t)Signed;
        if (Signed > 0)
        {
            uint32_t Next = Cursor + Total;
            while (Next < BinSize)
            {
                int32_t NextSigned = (int32_t)Rc95LdrReadU32(Bin + Next);
                uint32_t NextTotal = (uint32_t)NextSigned;
                if (NextSigned <= 0) break;
                if (Total > UINT32_MAX - NextTotal)
                    return Rc95LdrCorruptData;
                Total += NextTotal;
                Next += NextTotal;
            }
            Rc95BcdWriteU32(Bin + Cursor, Total);
            Cursor += Total;
        }
        else
        {
            Cursor += (uint32_t)(-Signed);
        }
    }
    if (Cursor != BinSize) return Rc95LdrCorruptData;

    Sequence = Rc95LdrReadU32(Bytes + 4) + 1u;
    Rc95BcdWriteU32(Bytes + 4, Sequence);
    Rc95BcdWriteU32(Bytes + 8, Sequence);
    Checksum = Rc95BcdChecksum(Bytes);
    Rc95BcdWriteU32(Bytes + 0x1fcu, Checksum);

    return Rc95BcdOpenHive(Image, ImageSize, &Hive);
}

RC95LDR_STATUS
Rc95BcdReclaimDetachedCell(void *Image, size_t ImageSize, void *Scratch,
                           size_t ScratchSize, uint32_t CellOffset)
{
    RC95LDR_STATUS Status;
    if (Image == NULL || Scratch == NULL || ScratchSize < ImageSize ||
        Rc95BcdRangesOverlap(Image, ImageSize, Scratch, ScratchSize) ||
        CellOffset == 0xffffffffu ||
        CellOffset > UINT32_MAX - REG_HIVE_BIN_OFFSET)
        return Rc95LdrInvalidParameter;
    memmove(Scratch, Image, ImageSize);
    Status = Rc95BcdReclaimDetachedCellInPlace(Scratch, ImageSize, CellOffset);
    if (Status != Rc95LdrSuccess) return Status;
    memmove(Image, Scratch, ImageSize);
    return Rc95LdrSuccess;
}

/*
 * Reclaim an obsolete subkey index tree in parent-before-child order. Once an
 * RI node is released, its stored child offsets remain in the free-cell bytes
 * (reclamation changes cell headers, not payload bytes), and no allocated
 * cell retains references to those children. This lets the ordinary guarded
 * reclamation routine verify and release each descendant without a third
 * transaction buffer.
 */
static RC95LDR_STATUS
Rc95BcdReclaimIndexTree(void *Image, size_t ImageSize, void *Scratch,
                        size_t ScratchSize, uint32_t IndexCell,
                        uint32_t Depth)
{
    RC95BCD_HIVE Hive;
    RC95BCD_CELL Index;
    uint16_t Signature, Count;
    uint32_t EntrySize, IndexNumber;
    RC95LDR_STATUS Status;

    if (Depth > RC95BCD_MAX_INDEX_DEPTH)
        return Rc95LdrCorruptData;
    Status = Rc95BcdOpenHive(Image, ImageSize, &Hive);
    if (Status != Rc95LdrSuccess)
        return Status;
    Status = Rc95BcdGetCell(&Hive, IndexCell, &Index);
    if (Status != Rc95LdrSuccess)
        return Status;
    if (Index.Size < 4u)
        return Rc95LdrCorruptData;

    Signature = Rc95LdrReadU16(Index.Data);
    Count = Rc95LdrReadU16(Index.Data + 2u);
    if (Signature == RI_SIGNATURE)
        EntrySize = 4u;
    else if (Signature == LI_SIGNATURE)
        EntrySize = 4u;
    else if (Signature == LF_SIGNATURE || Signature == LH_SIGNATURE)
        EntrySize = 8u;
    else
        return Rc95LdrCorruptData;
    if (Count > REG_MAX_ENUM_ITEMS ||
        (uint32_t)Count > (Index.Size - 4u) / EntrySize)
        return Rc95LdrCorruptData;

    /* Release this node first so its RI edges no longer pin its children. */
    Status = Rc95BcdReclaimDetachedCellInPlace(Image, ImageSize,
                                               IndexCell);
    if (Status != Rc95LdrSuccess)
        return Status;

    if (Signature != RI_SIGNATURE)
        return Rc95LdrSuccess;

    /*
     * The released cell's payload is intentionally preserved. Its RI entries
     * are now detached and can be read while recursively releasing children.
     */
    for (IndexNumber = 0; IndexNumber < Count; ++IndexNumber)
    {
        uint32_t ChildOffset = Rc95LdrReadU32(
            (const uint8_t *)Image + REG_HIVE_BIN_OFFSET +
            IndexCell + REG_CELL_HEADER_SIZE + 4u + IndexNumber * 4u);
        Status = Rc95BcdReclaimIndexTree(Image, ImageSize, Scratch,
                                         ScratchSize, ChildOffset, Depth + 1u);
        if (Status != Rc95LdrSuccess)
            return Status;
    }
    return Rc95LdrSuccess;
}


/* Validate the complete REGF/HBIN chain and every allocated/free cell
 * boundary. Rc95BcdOpenHive performs the same structural walk before exposing
 * any cell references, so this entry point is also suitable after mutations. */
RC95LDR_STATUS
Rc95BcdValidateHiveBins(const void *Image, size_t ImageSize)
{
    RC95BCD_HIVE Hive;
    return Rc95BcdOpenHive(Image, ImageSize, &Hive);
}


/*
 * Replace a VK value with an arbitrary-length payload.  A new VK, optional
 * external data cell, and replacement value-offset list are allocated before
 * the parent NK is switched.  The old list, VK and external data cell are
 * reclaimed only after the new references are published in the private
 * scratch hive.  Image is changed only after the rebuilt hive is reopened
 * and the replacement value is read back successfully.
 */
RC95LDR_STATUS
Rc95BcdReplaceValueCell(void *Image, size_t ImageSize, void *Scratch,
                        size_t ScratchSize, uint32_t KeyCell,
                        const char *ValueName, uint32_t Type,
                        const void *Data, uint32_t DataLength,
                        uint32_t *NewValueCell, uint32_t *NewDataCell)
{
    RC95BCD_HIVE Hive;
    RC95BCD_CELL OldVk, OldList, OldDataCell, KeyCellView;
    RC95BCD_VALUE OldValue, CheckValue;
    uint8_t *NewVkData, *NewListData, *KeyData;
    const uint8_t *OldVkData;
    const uint8_t *NameData;
    uint32_t OldSubKeyCount, OldSubKeyList, OldValueCount, OldValueList;
    uint32_t OldVkOffset, OldDataOffset = 0xffffffffu;
    uint32_t NewVkOffset = 0xffffffffu, NewDataOffset = 0xffffffffu;
    uint32_t NewListOffset = 0xffffffffu, Capacity, KeyCapacity;
    uint32_t OldStoredLength, OldNameLength, Index, MatchCount = 0;
    uint16_t OldFlags;
    uint32_t Sequence;
    uint16_t KeyFlags;
    RC95LDR_STATUS Status;

    if (Image == NULL || Scratch == NULL || ValueName == NULL ||
        (Data == NULL && DataLength != 0) || NewValueCell == NULL ||
        ScratchSize < ImageSize ||
        Rc95BcdRangesOverlap(Image, ImageSize, Scratch, ScratchSize) ||
        Rc95BcdRangesOverlap(ValueName, strlen(ValueName) + 1u,
                             Scratch, ScratchSize) ||
        (Data != NULL && DataLength != 0 &&
         Rc95BcdRangesOverlap(Data, DataLength, Scratch, ScratchSize)) ||
        DataLength > 0x7fffffffu)
        return Rc95LdrInvalidParameter;

    *NewValueCell = 0xffffffffu;
    if (NewDataCell != NULL) *NewDataCell = 0xffffffffu;

    Status = Rc95BcdOpenHive(Image, ImageSize, &Hive);
    if (Status != Rc95LdrSuccess) return Status;
    Status = Rc95BcdGetKey(&Hive, KeyCell, &KeyCellView,
                           &KeyFlags, &OldSubKeyCount, &OldSubKeyList,
                           &OldValueCount, &OldValueList);
    if (Status != Rc95LdrSuccess) return Status;
    if (OldValueCount == 0 || OldValueCount > UINT32_MAX / 4u ||
        OldValueList == 0xffffffffu)
        return Rc95LdrNotFound;

    Status = Rc95BcdFindValue(&Hive, KeyCell, ValueName, &OldValue);
    if (Status != Rc95LdrSuccess) return Status;
    OldVkOffset = OldValue.ValueCell;
    Status = Rc95BcdGetCell(&Hive, OldVkOffset, &OldVk);
    if (Status != Rc95LdrSuccess) return Status;
    if (OldVk.Size < REG_VK_FIXED_SIZE ||
        Rc95LdrReadU16(OldVk.Data) != VK_SIGNATURE)
        return Rc95LdrCorruptData;

    OldVkData = OldVk.Data;
    OldNameLength = Rc95LdrReadU16(OldVkData + 2u);
    OldStoredLength = Rc95LdrReadU32(OldVkData + 4u);
    OldFlags = Rc95LdrReadU16(OldVkData + 16u);
    if (OldNameLength > OldVk.Size - REG_VK_FIXED_SIZE ||
        OldValue.Length != (OldStoredLength & 0x7fffffffu) ||
        ((OldStoredLength & REG_VALUE_INLINE) != 0 &&
         OldValue.Length > 4u))
        return Rc95LdrCorruptData;
    NameData = OldVkData + REG_VK_FIXED_SIZE;

    Status = Rc95BcdGetCell(&Hive, OldValueList, &OldList);
    if (Status != Rc95LdrSuccess) return Status;
    if (OldValueCount > OldList.Size / 4u)
        return Rc95LdrCorruptData;
    for (Index = 0; Index < OldValueCount; ++Index)
    {
        if (Rc95LdrReadU32(OldList.Data + Index * 4u) == OldVkOffset)
            ++MatchCount;
    }
    if (MatchCount != 1u) return Rc95LdrCorruptData;

    if ((OldStoredLength & REG_VALUE_INLINE) == 0 &&
        (OldStoredLength & 0x7fffffffu) > 4u)
    {
        OldDataOffset = Rc95LdrReadU32(OldVkData + 8u);
        Status = Rc95BcdGetCell(&Hive, OldDataOffset, &OldDataCell);
        if (Status != Rc95LdrSuccess) return Status;
        if (OldDataCell.Size < (OldStoredLength & 0x7fffffffu))
            return Rc95LdrCorruptData;
    }

    memmove(Scratch, Image, ImageSize);

    if (DataLength > 4u)
    {
        Status = Rc95BcdAllocateCell(Scratch, ImageSize, DataLength,
                                     &NewDataOffset, &Capacity);
        if (Status != Rc95LdrSuccess) return Status;
        if (Capacity < DataLength) return Rc95LdrCorruptData;
        Status = Rc95BcdGetMutableCell(Scratch, ImageSize, NewDataOffset,
                                       &NewVkData, &Capacity);
        if (Status != Rc95LdrSuccess) return Status;
        if (Capacity < DataLength) return Rc95LdrCorruptData;
        memcpy(NewVkData, Data, DataLength);
    }

    Status = Rc95BcdAllocateCell(Scratch, ImageSize,
                                 REG_VK_FIXED_SIZE + OldNameLength,
                                 &NewVkOffset, &Capacity);
    if (Status != Rc95LdrSuccess) return Status;
    if (Capacity < REG_VK_FIXED_SIZE + OldNameLength)
        return Rc95LdrCorruptData;
    Status = Rc95BcdGetMutableCell(Scratch, ImageSize, NewVkOffset,
                                   &NewVkData, &Capacity);
    if (Status != Rc95LdrSuccess) return Status;
    if (Capacity < REG_VK_FIXED_SIZE + OldNameLength)
        return Rc95LdrCorruptData;
    memset(NewVkData, 0, Capacity);
    Rc95BcdWriteU16(NewVkData, VK_SIGNATURE);
    Rc95BcdWriteU16(NewVkData + 2u, (uint16_t)OldNameLength);
    if (DataLength <= 4u)
    {
        Rc95BcdWriteU32(NewVkData + 4u,
                        DataLength | REG_VALUE_INLINE);
        if (DataLength != 0)
            memcpy(NewVkData + 8u, Data, DataLength);
    }
    else
    {
        Rc95BcdWriteU32(NewVkData + 4u, DataLength);
        Rc95BcdWriteU32(NewVkData + 8u, NewDataOffset);
    }
    Rc95BcdWriteU32(NewVkData + 12u, Type);
    Rc95BcdWriteU16(NewVkData + 16u, OldFlags);
    if (OldNameLength != 0)
        memcpy(NewVkData + REG_VK_FIXED_SIZE, NameData, OldNameLength);

    /* Preserve list order and replace exactly one VK reference. */
    Status = Rc95BcdAllocateCell(Scratch, ImageSize,
                                 OldValueCount * 4u,
                                 &NewListOffset, &Capacity);
    if (Status != Rc95LdrSuccess) return Status;
    if (Capacity < OldValueCount * 4u) return Rc95LdrCorruptData;
    Status = Rc95BcdGetMutableCell(Scratch, ImageSize, NewListOffset,
                                   &NewListData, &Capacity);
    if (Status != Rc95LdrSuccess) return Status;
    if (Capacity < OldValueCount * 4u) return Rc95LdrCorruptData;
    memset(NewListData, 0, Capacity);
    for (Index = 0; Index < OldValueCount; ++Index)
    {
        uint32_t Entry = Rc95LdrReadU32(OldList.Data + Index * 4u);
        if (Entry == OldVkOffset) Entry = NewVkOffset;
        Rc95BcdWriteU32(NewListData + Index * 4u, Entry);
    }

    Status = Rc95BcdGetMutableCell(Scratch, ImageSize, KeyCell,
                                   &KeyData, &KeyCapacity);
    if (Status != Rc95LdrSuccess) return Status;
    if (KeyCapacity < REG_NK_FIXED_SIZE ||
        Rc95LdrReadU16(KeyData) != NK_SIGNATURE)
        return Rc95LdrCorruptData;
    Rc95BcdWriteU32(KeyData + 40u, NewListOffset);

    /* Keep the hive reopenable while obsolete cells are reclaimed. */
    Sequence = Rc95LdrReadU32((const uint8_t *)Scratch + 4u) + 1u;
    Rc95BcdWriteU32((uint8_t *)Scratch + 4u, Sequence);
    Rc95BcdWriteU32((uint8_t *)Scratch + 8u, Sequence);
    Rc95BcdWriteU32((uint8_t *)Scratch + 0x1fcu,
                    Rc95BcdChecksum((const uint8_t *)Scratch));

    /*
     * Reclaim in reference order: detach the old value list first, then the
     * VK cell that it referenced, then the external data cell referenced by
     * that VK. Each reclamation coalesces adjacent free cells, so this order
     * also ensures no later target is absorbed while still referenced.
     */
    Status = Rc95BcdReclaimDetachedCellInPlace(Scratch, ImageSize,
                                               OldValueList);
    if (Status != Rc95LdrSuccess) return Status;
    Status = Rc95BcdReclaimDetachedCellInPlace(Scratch, ImageSize,
                                               OldVkOffset);
    if (Status != Rc95LdrSuccess) return Status;
    if (OldDataOffset != 0xffffffffu)
    {
        Status = Rc95BcdReclaimDetachedCellInPlace(Scratch, ImageSize,
                                                   OldDataOffset);
        if (Status != Rc95LdrSuccess) return Status;
    }

    Status = Rc95BcdOpenHive(Scratch, ImageSize, &Hive);
    if (Status != Rc95LdrSuccess) return Status;
    Status = Rc95BcdFindValue(&Hive, KeyCell, ValueName, &CheckValue);
    if (Status != Rc95LdrSuccess ||
        CheckValue.ValueCell != NewVkOffset ||
        CheckValue.Type != Type || CheckValue.Length != DataLength ||
        (DataLength != 0 && memcmp(CheckValue.Data, Data, DataLength) != 0))
        return Status == Rc95LdrSuccess ? Rc95LdrCorruptData : Status;
    Status = Rc95BcdValidateHiveBins(Scratch, ImageSize);
    if (Status != Rc95LdrSuccess) return Status;

    memmove(Image, Scratch, ImageSize);
    *NewValueCell = NewVkOffset;
    if (NewDataCell != NULL) *NewDataCell = NewDataOffset;
    return Rc95LdrSuccess;
}


/* Public, bounded enumeration of registry subkeys. This is shared by
 * BCDEDIT and boot-policy consumers so callers never parse NK/LI/LF/LH/RI
 * structures themselves. */
static RC95LDR_STATUS
Rc95BcdEnumerateSubKeyIndex(const RC95BCD_HIVE *Hive, uint32_t ListOffset,
                            uint32_t Expected, uint32_t *Seen,
                            uint32_t Depth,
                            RC95BCD_KEY_ENUM_CALLBACK Callback,
                            void *Context)
{
    RC95BCD_CELL List;
    uint16_t Signature, Count;
    uint32_t Index, EntrySize;
    RC95LDR_STATUS Status;

    if (Depth > RC95BCD_MAX_INDEX_DEPTH || *Seen > Expected)
        return Rc95LdrCorruptData;
    Status = Rc95BcdGetCell(Hive, ListOffset, &List);
    if (Status != Rc95LdrSuccess)
        return Status;
    if (List.Size < 4u)
        return Rc95LdrCorruptData;

    Signature = Rc95LdrReadU16(List.Data);
    Count = Rc95LdrReadU16(List.Data + 2u);
    if (Count > REG_MAX_ENUM_ITEMS)
        return Rc95LdrCorruptData;

    if (Signature == RI_SIGNATURE)
    {
        if ((uint32_t)Count > (List.Size - 4u) / 4u)
            return Rc95LdrCorruptData;
        for (Index = 0; Index < Count; ++Index)
        {
            Status = Rc95BcdEnumerateSubKeyIndex(
                Hive, Rc95LdrReadU32(List.Data + 4u + Index * 4u),
                Expected, Seen, Depth + 1u, Callback, Context);
            if (Status != Rc95LdrSuccess)
                return Status;
        }
        return Rc95LdrSuccess;
    }

    if (Signature == LI_SIGNATURE)
        EntrySize = 4u;
    else if (Signature == LF_SIGNATURE || Signature == LH_SIGNATURE)
        EntrySize = 8u;
    else
        return Rc95LdrCorruptData;

    if ((uint32_t)Count > (List.Size - 4u) / EntrySize)
        return Rc95LdrCorruptData;

    for (Index = 0; Index < Count; ++Index)
    {
        uint32_t ChildOffset = Rc95LdrReadU32(
            List.Data + 4u + Index * EntrySize);
        RC95BCD_CELL Child;
        RC95BCD_KEY_INFO Key;
        uint16_t Flags;
        uint32_t SubKeyCount, SubKeyList, ValueCount, ValueList;

        if (*Seen >= Expected)
            return Rc95LdrCorruptData;
        Status = Rc95BcdGetKey(Hive, ChildOffset, &Child, &Flags,
                               &SubKeyCount, &SubKeyList,
                               &ValueCount, &ValueList);
        if (Status != Rc95LdrSuccess)
            return Status;

        Key.Cell = ChildOffset;
        Key.SubKeyCount = SubKeyCount;
        Key.ValueCount = ValueCount;
        Key.NameLength = Rc95LdrReadU16(Child.Data + 72u);
        if (Key.NameLength > Child.Size - REG_NK_FIXED_SIZE)
            return Rc95LdrCorruptData;
        Key.Name = Child.Data + REG_NK_FIXED_SIZE;
        Key.Flags = Flags;

        Status = Callback(&Key, Context);
        if (Status != Rc95LdrSuccess)
            return Status;
        ++*Seen;
    }

    return Rc95LdrSuccess;
}

RC95LDR_STATUS
Rc95BcdEnumerateSubKeys(const RC95BCD_HIVE *Hive, uint32_t ParentCell,
                        RC95BCD_KEY_ENUM_CALLBACK Callback, void *Context)
{
    RC95BCD_CELL Parent;
    uint16_t Flags;
    uint32_t SubKeyCount, SubKeyList, ValueCount, ValueList, Seen = 0;
    RC95LDR_STATUS Status;

    if (Hive == NULL || Callback == NULL)
        return Rc95LdrInvalidParameter;

    Status = Rc95BcdGetKey(Hive, ParentCell, &Parent, &Flags,
                           &SubKeyCount, &SubKeyList,
                           &ValueCount, &ValueList);
    if (Status != Rc95LdrSuccess)
        return Status;
    if (SubKeyCount == 0)
        return Rc95LdrSuccess;
    if (SubKeyCount > REG_MAX_ENUM_ITEMS || SubKeyList == 0xffffffffu)
        return Rc95LdrCorruptData;

    Status = Rc95BcdEnumerateSubKeyIndex(Hive, SubKeyList, SubKeyCount,
                                         &Seen, 0, Callback, Context);
    if (Status != Rc95LdrSuccess)
        return Status;
    return Seen == SubKeyCount ? Rc95LdrSuccess : Rc95LdrCorruptData;
}
