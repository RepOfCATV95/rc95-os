#include "rc95ldr_ntfs_record.h"
#include <string.h>

static int
NtfsRange(size_t Length, size_t Offset, size_t Count)
{
    return Offset <= Length && Count <= Length - Offset;
}

RC95LDR_STATUS
Rc95LdrNtfsFixupFileRecord(
    void *Record, size_t RecordLength, uint32_t BytesPerSector,
    RC95LDR_NTFS_FILE_RECORD_INFO *Info)
{
    uint8_t *Bytes = (uint8_t *)Record;
    uint16_t UsaOffset, UsaCount, Sequence;
    uint32_t FirstAttribute, BytesInUse, BytesAllocated;
    size_t SectorCount, Index, UsaBytes;

    if (!Bytes || !Info || BytesPerSector < 256u ||
        (BytesPerSector & (BytesPerSector - 1u)) != 0 ||
        RecordLength < 48u || RecordLength % BytesPerSector != 0)
        return Rc95LdrInvalidParameter;
    if (memcmp(Bytes, "FILE", 4) != 0)
        return Rc95LdrInvalidImage;

    UsaOffset = Rc95LdrReadU16(Bytes + 4);
    UsaCount = Rc95LdrReadU16(Bytes + 6);
    FirstAttribute = Rc95LdrReadU16(Bytes + 20);
    BytesInUse = Rc95LdrReadU32(Bytes + 24);
    BytesAllocated = Rc95LdrReadU32(Bytes + 28);
    SectorCount = RecordLength / BytesPerSector;

    if (SectorCount >= UINT16_MAX || UsaCount != SectorCount + 1u ||
        !Rc95LdrMultiplySize(UsaCount, 2u, &UsaBytes) ||
        !NtfsRange(RecordLength, UsaOffset, UsaBytes) ||
        UsaOffset < 48u || UsaOffset + UsaBytes > FirstAttribute ||
        FirstAttribute < 48u || (FirstAttribute & 7u) != 0 ||
        BytesInUse < FirstAttribute + 8u ||
        BytesInUse > BytesAllocated || BytesAllocated != RecordLength ||
        (BytesInUse & 7u) != 0 ||
        !NtfsRange(RecordLength, FirstAttribute, BytesInUse - FirstAttribute))
        return Rc95LdrCorruptData;

    Sequence = Rc95LdrReadU16(Bytes + UsaOffset);
    if (!Sequence)
        return Rc95LdrCorruptData;

    /* Validate every trailer before modifying any of them. */
    for (Index = 1; Index <= SectorCount; ++Index)
    {
        size_t Trailer = Index * BytesPerSector - 2u;
        if (Rc95LdrReadU16(Bytes + Trailer) != Sequence)
            return Rc95LdrCorruptData;
    }
    for (Index = 1; Index <= SectorCount; ++Index)
    {
        size_t Trailer = Index * BytesPerSector - 2u;
        uint16_t Replacement = Rc95LdrReadU16(Bytes + UsaOffset + Index * 2u);
        Bytes[Trailer] = (uint8_t)Replacement;
        Bytes[Trailer + 1u] = (uint8_t)(Replacement >> 8);
    }

    Info->SequenceNumber = Rc95LdrReadU16(Bytes + 16);
    Info->Flags = Rc95LdrReadU16(Bytes + 22);
    Info->FirstAttributeOffset = FirstAttribute;
    Info->BytesInUse = BytesInUse;
    Info->BytesAllocated = BytesAllocated;
    Info->NextAttributeId = Rc95LdrReadU16(Bytes + 40);
    Info->BaseFileReference = Rc95LdrReadU64(Bytes + 32);
    Info->RecordNumber = Rc95LdrReadU32(Bytes + 44);
    return Rc95LdrSuccess;
}

RC95LDR_STATUS
Rc95LdrNtfsGetAttribute(
    const void *Record, size_t RecordLength, uint32_t Offset,
    RC95LDR_NTFS_ATTRIBUTE_INFO *Info, uint32_t *NextOffset)
{
    const uint8_t *Bytes = (const uint8_t *)Record;
    uint32_t Type, Length, NameBytes, BytesInUse;
    uint8_t NonResident, NameLength;
    uint16_t NameOffset;

    if (!Bytes || !Info || !NextOffset || RecordLength < 48u)
        return Rc95LdrInvalidParameter;
    if (memcmp(Bytes, "FILE", 4) != 0)
        return Rc95LdrInvalidImage;
    BytesInUse = Rc95LdrReadU32(Bytes + 24);
    if (BytesInUse < 48u || BytesInUse > RecordLength ||
        Offset > BytesInUse || BytesInUse - Offset < 4u)
        return Rc95LdrCorruptData;

    Type = Rc95LdrReadU32(Bytes + Offset);
    if (Type == RC95LDR_NTFS_ATTRIBUTE_END)
    {
        *NextOffset = Offset + 4u;
        return Rc95LdrNotFound;
    }
    if (BytesInUse - Offset < 16u)
        return Rc95LdrCorruptData;
    Length = Rc95LdrReadU32(Bytes + Offset + 4u);
    if (Length < 16u || (Length & 7u) != 0 ||
        !NtfsRange(BytesInUse, Offset, Length))
        return Rc95LdrCorruptData;

    NonResident = Bytes[Offset + 8u];
    NameLength = Bytes[Offset + 9u];
    NameOffset = Rc95LdrReadU16(Bytes + Offset + 10u);
    if (NonResident > 1u)
        return Rc95LdrCorruptData;
    NameBytes = (uint32_t)NameLength * 2u;
    if (NameLength &&
        (NameOffset < 16u || (NameOffset & 1u) != 0 ||
         NameOffset > Length || NameBytes > Length - NameOffset))
        return Rc95LdrCorruptData;

    memset(Info, 0, sizeof(*Info));
    Info->Type = Type;
    Info->Length = Length;
    Info->Instance = Rc95LdrReadU16(Bytes + Offset + 14u);
    Info->NameLength = NameLength;
    Info->NameOffset = NameOffset;
    Info->NonResident = NonResident;

    if (!NonResident)
    {
        uint32_t ValueLength;
        uint16_t ValueOffset;
        if (Length < 24u)
            return Rc95LdrCorruptData;
        ValueLength = Rc95LdrReadU32(Bytes + Offset + 16u);
        ValueOffset = Rc95LdrReadU16(Bytes + Offset + 20u);
        if (ValueOffset < 24u || ValueOffset > Length ||
            ValueLength > Length - ValueOffset)
            return Rc95LdrCorruptData;
        if (NameLength && ValueLength &&
            ValueOffset < NameOffset + NameBytes &&
            NameOffset < ValueOffset + ValueLength)
            return Rc95LdrCorruptData;
        Info->ValueOffset = ValueOffset;
        Info->ValueLength = ValueLength;
    }
    else
    {
        if (Length < 64u)
            return Rc95LdrCorruptData;
        Info->LowestVcn = Rc95LdrReadU64(Bytes + Offset + 16u);
        Info->HighestVcn = Rc95LdrReadU64(Bytes + Offset + 24u);
        Info->MappingPairsOffset = Rc95LdrReadU16(Bytes + Offset + 32u);
        if (Info->HighestVcn < Info->LowestVcn ||
            Info->MappingPairsOffset < 64u ||
            (NameLength && Info->MappingPairsOffset < NameOffset + NameBytes) ||
            Info->MappingPairsOffset >= Length ||
            (Info->MappingPairsOffset & 1u) != 0)
            return Rc95LdrCorruptData;
    }

    *NextOffset = Offset + Length;
    return Rc95LdrSuccess;
}

 
RC95LDR_STATUS
Rc95LdrNtfsGetAttributeListEntry(
    const void *List, size_t ListLength, uint32_t Offset,
    RC95LDR_NTFS_ATTRIBUTE_LIST_ENTRY *Entry, uint32_t *NextOffset)
{
    const uint8_t *Bytes = (const uint8_t *)List;
    uint16_t Length;
    uint8_t NameLength, NameOffset;
    size_t NameBytes;

    if (!Bytes || !Entry || !NextOffset)
        return Rc95LdrInvalidParameter;
    if (Offset > ListLength || ListLength - Offset < 26u)
        return Rc95LdrCorruptData;

    Length = Rc95LdrReadU16(Bytes + Offset + 4u);
    NameLength = Bytes[Offset + 6u];
    NameOffset = Bytes[Offset + 7u];
    NameBytes = (size_t)NameLength * 2u;

    if (Length < 26u || (Length & 7u) != 0 ||
        Length > ListLength - Offset)
        return Rc95LdrCorruptData;
    if (NameLength &&
        (NameOffset < 26u || (NameOffset & 1u) != 0 ||
         NameOffset > Length || NameBytes > (size_t)Length - NameOffset))
        return Rc95LdrCorruptData;
    if (!NameLength && NameOffset != 0u)
        return Rc95LdrCorruptData;

    Entry->Type = Rc95LdrReadU32(Bytes + Offset);
    Entry->Length = Length;
    Entry->NameLength = NameLength;
    Entry->NameOffset = NameOffset;
    Entry->LowestVcn = Rc95LdrReadU64(Bytes + Offset + 8u);
    Entry->FileReference = Rc95LdrReadU64(Bytes + Offset + 16u);
    Entry->Instance = Rc95LdrReadU16(Bytes + Offset + 24u);
    *NextOffset = Offset + Length;
    return Rc95LdrSuccess;
}

 
RC95LDR_STATUS
Rc95LdrNtfsValidateAttributeListTarget(
    const void *List, size_t ListLength, uint32_t EntryOffset,
    uint64_t BaseFileReference,
    const void *ExtensionRecord, size_t RecordLength,
    const RC95LDR_NTFS_FILE_RECORD_INFO *ExtensionInfo,
    uint32_t AttributeOffset)
{
    const uint8_t *ListBytes = (const uint8_t *)List;
    const uint8_t *RecordBytes = (const uint8_t *)ExtensionRecord;
    RC95LDR_NTFS_ATTRIBUTE_LIST_ENTRY Entry;
    RC95LDR_NTFS_ATTRIBUTE_INFO Attribute;
    uint64_t ExpectedFileReference;
    uint32_t NextOffset;
    RC95LDR_STATUS Status;
    uint32_t Index;

    if (!ListBytes || !RecordBytes || !ExtensionInfo)
        return Rc95LdrInvalidParameter;
    if (RecordLength < 48u || memcmp(RecordBytes, "FILE", 4) != 0)
        return Rc95LdrInvalidImage;
    if (ExtensionInfo->SequenceNumber != Rc95LdrReadU16(RecordBytes + 16u) ||
        ExtensionInfo->BaseFileReference != Rc95LdrReadU64(RecordBytes + 32u) ||
        ExtensionInfo->RecordNumber != Rc95LdrReadU32(RecordBytes + 44u))
        return Rc95LdrCorruptData;

    Status = Rc95LdrNtfsGetAttributeListEntry(
        List, ListLength, EntryOffset, &Entry, &NextOffset);
    if (Status != Rc95LdrSuccess)
        return Status;

    ExpectedFileReference =
        ((uint64_t)ExtensionInfo->SequenceNumber << 48) |
        ((uint64_t)ExtensionInfo->RecordNumber & UINT64_C(0x0000ffffffffffff));
    if (Entry.FileReference != ExpectedFileReference ||
        ExtensionInfo->BaseFileReference != BaseFileReference)
        return Rc95LdrCorruptData;

    Status = Rc95LdrNtfsGetAttribute(
        ExtensionRecord, RecordLength, AttributeOffset, &Attribute, &NextOffset);
    if (Status != Rc95LdrSuccess)
        return Status;

    if (Entry.Type != Attribute.Type ||
        Entry.Instance != Attribute.Instance ||
        Entry.LowestVcn != Attribute.LowestVcn ||
        Entry.NameLength != Attribute.NameLength)
        return Rc95LdrNotFound;

    for (Index = 0; Index < Entry.NameLength; ++Index)
    {
        uint16_t ListCharacter =
            Rc95LdrReadU16(ListBytes + EntryOffset + Entry.NameOffset + Index * 2u);
        uint16_t AttributeCharacter =
            Rc95LdrReadU16(RecordBytes + AttributeOffset +
                           Attribute.NameOffset + Index * 2u);
        if (ListCharacter != AttributeCharacter)
            return Rc95LdrNotFound;
    }
    return Rc95LdrSuccess;
}
