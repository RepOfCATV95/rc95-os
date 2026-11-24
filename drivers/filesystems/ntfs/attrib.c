/*
 *  ReactOS kernel
 *  Copyright (C) 2002,2003 ReactOS Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * COPYRIGHT:        See COPYING in the top level directory
 * PROJECT:          ReactOS kernel
 * FILE:             drivers/filesystem/ntfs/attrib.c
 * PURPOSE:          NTFS filesystem driver
 * PROGRAMMERS:      Eric Kohl
 *                   Valentin Verkhovsky
 *                   Hervé Poussineau (hpoussin@reactos.org)
 *                   Pierre Schweitzer (pierre@reactos.org)
 */

/* INCLUDES *****************************************************************/

#include "ntfs.h"
#include <ntintsafe.h>

#define NDEBUG
#include <debug.h>

/* Set to 1 to dump each data run list as it is encoded. Uses DbgPrint, so it
 * prints whatever NDEBUG is set to. */
#define NTFS_DUMP_DATA_RUNS 0

/* FUNCTIONS ****************************************************************/

/**
* @name AddBitmap
* @implemented
*
* Adds a $BITMAP attribute to a given FileRecord.
*
* @param Vcb
* Pointer to an NTFS_VCB for the destination volume.
*
* @param FileRecord
* Pointer to a complete file record to add the attribute to.
*
* @param AttributeAddress
* Pointer to the region of memory that will receive the $INDEX_ALLOCATION attribute.
* This address must reside within FileRecord. Must be aligned to an 8-byte boundary (relative to FileRecord).
*
* @param Name
* Pointer to a string of 16-bit Unicode characters naming the attribute. Most often L"$I30".
*
* @param NameLength
* The number of wide-characters in the name. L"$I30" Would use 4 here.
*
* @return
* STATUS_SUCCESS on success. STATUS_NOT_IMPLEMENTED if target address isn't at the end
* of the given file record, or if the file record isn't large enough for the attribute.
*
* @remarks
* Only adding the attribute to the end of the file record is supported; AttributeAddress must
* be of type AttributeEnd.
* This could be improved by adding an $ATTRIBUTE_LIST to the file record if there's not enough space.
*
*/
NTSTATUS
AddBitmap(PNTFS_VCB Vcb,
          PFILE_RECORD_HEADER FileRecord,
          PNTFS_ATTR_RECORD AttributeAddress,
          PCWSTR Name,
          USHORT NameLength)
{
    ULONG AttributeLength;
    // Calculate the header length
    ULONG ResidentHeaderLength = FIELD_OFFSET(NTFS_ATTR_RECORD, Resident.Reserved) + sizeof(UCHAR);
    ULONG FileRecordEnd = AttributeAddress->Length;
    ULONG NameOffset;
    ULONG ValueOffset;
    // We'll start out with 8 bytes of bitmap data
    ULONG ValueLength = 8;
    ULONG BytesAvailable;

    if (AttributeAddress->Type != AttributeEnd)
    {
        DPRINT1("FIXME: Can only add $BITMAP attribute to the end of a file record.\n");
        return STATUS_NOT_IMPLEMENTED;
    }

    NameOffset = ResidentHeaderLength;

    // Calculate ValueOffset, which will be aligned to a 4-byte boundary
    ValueOffset = ALIGN_UP_BY(NameOffset + (sizeof(WCHAR) * NameLength), VALUE_OFFSET_ALIGNMENT);

    // Calculate length of attribute
    AttributeLength = ValueOffset + ValueLength;
    AttributeLength = ALIGN_UP_BY(AttributeLength, ATTR_RECORD_ALIGNMENT);

    // Make sure the file record is large enough for the new attribute
    BytesAvailable = Vcb->NtfsInfo.BytesPerFileRecord - FileRecord->BytesInUse;
    if (BytesAvailable < AttributeLength)
    {
        DPRINT1("FIXME: Not enough room in file record for index allocation attribute!\n");
        return STATUS_NOT_IMPLEMENTED;
    }

    // Set Attribute fields
    RtlZeroMemory(AttributeAddress, AttributeLength);

    AttributeAddress->Type = AttributeBitmap;
    AttributeAddress->Length = AttributeLength;
    AttributeAddress->NameLength = NameLength;
    AttributeAddress->NameOffset = NameOffset;
    AttributeAddress->Instance = FileRecord->NextAttributeNumber++;

    AttributeAddress->Resident.ValueLength = ValueLength;
    AttributeAddress->Resident.ValueOffset = ValueOffset;

    // Set the name
    RtlCopyMemory((PCHAR)((ULONG_PTR)AttributeAddress + NameOffset), Name, NameLength * sizeof(WCHAR));

    // move the attribute-end and file-record-end markers to the end of the file record
    AttributeAddress = (PNTFS_ATTR_RECORD)((ULONG_PTR)AttributeAddress + AttributeAddress->Length);
    SetFileRecordEnd(FileRecord, AttributeAddress, FileRecordEnd);

    return STATUS_SUCCESS;
}

/**
* @name AddData
* @implemented
*
* Adds a $DATA attribute to a given FileRecord.
*
* @param FileRecord
* Pointer to a complete file record to add the attribute to. Caller is responsible for
* ensuring FileRecord is large enough to contain $DATA.
*
* @param AttributeAddress
* Pointer to the region of memory that will receive the $DATA attribute.
* This address must reside within FileRecord. Must be aligned to an 8-byte boundary (relative to FileRecord).
*
* @return
* STATUS_SUCCESS on success. STATUS_NOT_IMPLEMENTED if target address isn't at the end
* of the given file record.
*
* @remarks
* Only adding the attribute to the end of the file record is supported; AttributeAddress must
* be of type AttributeEnd.
* As it's implemented, this function is only intended to assist in creating new file records. It
* could be made more general-purpose by considering file records with an $ATTRIBUTE_LIST.
* It's the caller's responsibility to ensure the given file record has enough memory allocated
* for the attribute.
*/
NTSTATUS
AddData(PFILE_RECORD_HEADER FileRecord,
        PNTFS_ATTR_RECORD AttributeAddress)
{
    ULONG ResidentHeaderLength = FIELD_OFFSET(NTFS_ATTR_RECORD, Resident.Reserved) + sizeof(UCHAR);
    ULONG FileRecordEnd = AttributeAddress->Length;

    if (AttributeAddress->Type != AttributeEnd)
    {
        DPRINT1("FIXME: Can only add $DATA attribute to the end of a file record.\n");
        return STATUS_NOT_IMPLEMENTED;
    }

    AttributeAddress->Type = AttributeData;
    AttributeAddress->Length = ResidentHeaderLength;
    AttributeAddress->Length = ALIGN_UP_BY(AttributeAddress->Length, ATTR_RECORD_ALIGNMENT);
    AttributeAddress->Resident.ValueLength = 0;
    AttributeAddress->Resident.ValueOffset = ResidentHeaderLength;

    // for unnamed $DATA attributes, NameOffset equals header length
    AttributeAddress->NameOffset = ResidentHeaderLength;
    AttributeAddress->Instance = FileRecord->NextAttributeNumber++;

    // move the attribute-end and file-record-end markers to the end of the file record
    AttributeAddress = (PNTFS_ATTR_RECORD)((ULONG_PTR)AttributeAddress + AttributeAddress->Length);
    SetFileRecordEnd(FileRecord, AttributeAddress, FileRecordEnd);

    return STATUS_SUCCESS;
}

/**
* @name AddFileName
* @implemented
*
* Adds a $FILE_NAME attribute to a given FileRecord.
*
* @param FileRecord
* Pointer to a complete file record to add the attribute to. Caller is responsible for
* ensuring FileRecord is large enough to contain $FILE_NAME.
*
* @param AttributeAddress
* Pointer to the region of memory that will receive the $FILE_NAME attribute.
* This address must reside within FileRecord. Must be aligned to an 8-byte boundary (relative to FileRecord).
*
* @param DeviceExt
* Points to the target disk's DEVICE_EXTENSION.
*
* @param FileObject
* Pointer to the FILE_OBJECT which represents the new name.
* This parameter is used to determine the filename and parent directory.
*
* @param CaseSensitive
* Boolean indicating if the function should operate in case-sensitive mode. This will be TRUE
* if an application opened the file with the FILE_FLAG_POSIX_SEMANTICS flag.
*
* @param ParentMftIndex
* Pointer to a ULONGLONG which will receive the index of the parent directory.
*
* @return
* STATUS_SUCCESS on success. STATUS_NOT_IMPLEMENTED if target address isn't at the end
* of the given file record.
*
* @remarks
* Only adding the attribute to the end of the file record is supported; AttributeAddress must
* be of type AttributeEnd.
* As it's implemented, this function is only intended to assist in creating new file records. It
* could be made more general-purpose by considering file records with an $ATTRIBUTE_LIST.
* It's the caller's responsibility to ensure the given file record has enough memory allocated
* for the attribute.
*/
NTSTATUS
AddFileName(PFILE_RECORD_HEADER FileRecord,
            PNTFS_ATTR_RECORD AttributeAddress,
            PDEVICE_EXTENSION DeviceExt,
            PFILE_OBJECT FileObject,
            BOOLEAN CaseSensitive,
            PULONGLONG ParentMftIndex)
{
    ULONG ResidentHeaderLength = FIELD_OFFSET(NTFS_ATTR_RECORD, Resident.Reserved) + sizeof(UCHAR);
    PFILENAME_ATTRIBUTE FileNameAttribute;
    LARGE_INTEGER SystemTime;
    ULONG FileRecordEnd = AttributeAddress->Length;
    ULONGLONG CurrentMFTIndex = NTFS_FILE_ROOT;
    UNICODE_STRING Current, Remaining, FilenameNoPath;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG FirstEntry;

    if (AttributeAddress->Type != AttributeEnd)
    {
        DPRINT1("FIXME: Can only add $FILE_NAME attribute to the end of a file record.\n");
        return STATUS_NOT_IMPLEMENTED;
    }

    AttributeAddress->Type = AttributeFileName;
    AttributeAddress->Instance = FileRecord->NextAttributeNumber++;

    FileNameAttribute = (PFILENAME_ATTRIBUTE)((LONG_PTR)AttributeAddress + ResidentHeaderLength);

    // set timestamps
    KeQuerySystemTime(&SystemTime);
    FileNameAttribute->CreationTime = SystemTime.QuadPart;
    FileNameAttribute->ChangeTime = SystemTime.QuadPart;
    FileNameAttribute->LastWriteTime = SystemTime.QuadPart;
    FileNameAttribute->LastAccessTime = SystemTime.QuadPart;

    // Is this a directory?
    if(FileRecord->Flags & FRH_DIRECTORY)
        FileNameAttribute->FileAttributes = NTFS_FILE_TYPE_DIRECTORY;
    else
        FileNameAttribute->FileAttributes = NTFS_FILE_TYPE_ARCHIVE;

    // we need to extract the filename from the path
    DPRINT("Pathname: %wZ\n", &FileObject->FileName);

    FsRtlDissectName(FileObject->FileName, &Current, &Remaining);

    while (Remaining.Length != 0)
    {
        DPRINT("Current: %wZ\n", &Current);

        FirstEntry = 0;
        Status = NtfsFindMftRecord(DeviceExt,
                                   CurrentMFTIndex,
                                   &Current,
                                   &FirstEntry,
                                   FALSE,
                                   CaseSensitive,
                                   &CurrentMFTIndex);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("Path component '%wZ' of '%wZ' not found (Status %lx)\n",
                    &Current, &FileObject->FileName, Status);
            return Status;
        }

        FsRtlDissectName(Remaining, &Current, &Remaining);
    }

    /* Whatever is left over is the file's own name */
    FilenameNoPath = Current;

    DPRINT("MFT Index of parent: %I64u\n", CurrentMFTIndex);

    // set reference to parent directory
    FileNameAttribute->DirectoryFileReferenceNumber = CurrentMFTIndex;
    *ParentMftIndex = CurrentMFTIndex;

    DPRINT("SequenceNumber: 0x%02x\n", FileRecord->SequenceNumber);

    // The highest 2 bytes should be the sequence number, unless the parent happens to be root
    if (CurrentMFTIndex == NTFS_FILE_ROOT)
        FileNameAttribute->DirectoryFileReferenceNumber |= (ULONGLONG)NTFS_FILE_ROOT << 48;
    else
        FileNameAttribute->DirectoryFileReferenceNumber |= (ULONGLONG)FileRecord->SequenceNumber << 48;

    DPRINT("FileNameAttribute->DirectoryFileReferenceNumber: 0x%016I64x\n", FileNameAttribute->DirectoryFileReferenceNumber);

    FileNameAttribute->NameLength = FilenameNoPath.Length / sizeof(WCHAR);
    RtlCopyMemory(FileNameAttribute->Name, FilenameNoPath.Buffer, FilenameNoPath.Length);

    // For now, we're emulating the way Windows behaves when 8.3 name generation is disabled
    // TODO: add DOS Filename as needed
    if (!CaseSensitive && RtlIsNameLegalDOS8Dot3(&FilenameNoPath, NULL, NULL))
        FileNameAttribute->NameType = NTFS_FILE_NAME_WIN32_AND_DOS;
    else
        FileNameAttribute->NameType = NTFS_FILE_NAME_POSIX;

    FileRecord->LinkCount++;

    AttributeAddress->Length = ResidentHeaderLength +
        FIELD_OFFSET(FILENAME_ATTRIBUTE, Name) + FilenameNoPath.Length;
    AttributeAddress->Length = ALIGN_UP_BY(AttributeAddress->Length, ATTR_RECORD_ALIGNMENT);

    AttributeAddress->Resident.ValueLength = FIELD_OFFSET(FILENAME_ATTRIBUTE, Name) + FilenameNoPath.Length;
    AttributeAddress->Resident.ValueOffset = ResidentHeaderLength;
    AttributeAddress->Resident.Flags = RA_INDEXED;

    // move the attribute-end and file-record-end markers to the end of the file record
    AttributeAddress = (PNTFS_ATTR_RECORD)((ULONG_PTR)AttributeAddress + AttributeAddress->Length);
    SetFileRecordEnd(FileRecord, AttributeAddress, FileRecordEnd);

    return Status;
}

/**
* @name AddIndexAllocation
* @implemented
*
* Adds an $INDEX_ALLOCATION attribute to a given FileRecord.
*
* @param Vcb
* Pointer to an NTFS_VCB for the destination volume.
*
* @param FileRecord
* Pointer to a complete file record to add the attribute to.
*
* @param AttributeAddress
* Pointer to the region of memory that will receive the $INDEX_ALLOCATION attribute.
* This address must reside within FileRecord. Must be aligned to an 8-byte boundary (relative to FileRecord).
*
* @param Name
* Pointer to a string of 16-bit Unicode characters naming the attribute. Most often, this will be L"$I30".
*
* @param NameLength
* The number of wide-characters in the name. L"$I30" Would use 4 here.
*
* @return
* STATUS_SUCCESS on success. STATUS_NOT_IMPLEMENTED if target address isn't at the end
* of the given file record, or if the file record isn't large enough for the attribute.
*
* @remarks
* Only adding the attribute to the end of the file record is supported; AttributeAddress must
* be of type AttributeEnd.
* This could be improved by adding an $ATTRIBUTE_LIST to the file record if there's not enough space.
*
*/
NTSTATUS
AddIndexAllocation(PNTFS_VCB Vcb,
                   PFILE_RECORD_HEADER FileRecord,
                   PNTFS_ATTR_RECORD AttributeAddress,
                   PCWSTR Name,
                   USHORT NameLength)
{
    ULONG RecordLength;
    ULONG FileRecordEnd;
    ULONG NameOffset;
    ULONG DataRunOffset;
    ULONG BytesAvailable;

    if (AttributeAddress->Type != AttributeEnd)
    {
        DPRINT1("FIXME: Can only add $INDEX_ALLOCATION attribute to the end of a file record.\n");
        return STATUS_NOT_IMPLEMENTED;
    }

    // Calculate the name offset
    NameOffset = FIELD_OFFSET(NTFS_ATTR_RECORD, NonResident.CompressedSize);

    // Calculate the offset to the first data run
    DataRunOffset = (sizeof(WCHAR) * NameLength) + NameOffset;
    // The data run offset must be aligned to a 4-byte boundary
    DataRunOffset = ALIGN_UP_BY(DataRunOffset, DATA_RUN_ALIGNMENT);

    // Calculate the length of the new attribute; the empty data run will consist of a single byte
    RecordLength = DataRunOffset + 1;

    // The size of the attribute itself must be aligned to an 8 - byte boundary
    RecordLength = ALIGN_UP_BY(RecordLength, ATTR_RECORD_ALIGNMENT);

    // Back up the last 4-bytes of the file record (even though this value doesn't matter)
    FileRecordEnd = AttributeAddress->Length;

    // Make sure the file record can contain the new attribute
    BytesAvailable = Vcb->NtfsInfo.BytesPerFileRecord - FileRecord->BytesInUse;
    if (BytesAvailable < RecordLength)
    {
        DPRINT1("FIXME: Not enough room in file record for index allocation attribute!\n");
        return STATUS_NOT_IMPLEMENTED;
    }

    // Set fields of attribute header
    RtlZeroMemory(AttributeAddress, RecordLength);

    AttributeAddress->Type = AttributeIndexAllocation;
    AttributeAddress->Length = RecordLength;
    AttributeAddress->IsNonResident = TRUE;
    AttributeAddress->NameLength = NameLength;
    AttributeAddress->NameOffset = NameOffset;
    AttributeAddress->Instance = FileRecord->NextAttributeNumber++;

    AttributeAddress->NonResident.MappingPairsOffset = DataRunOffset;
    AttributeAddress->NonResident.HighestVCN = (LONGLONG)-1;

    // Set the name
    RtlCopyMemory((PCHAR)((ULONG_PTR)AttributeAddress + NameOffset), Name, NameLength * sizeof(WCHAR));

    // move the attribute-end and file-record-end markers to the end of the file record
    AttributeAddress = (PNTFS_ATTR_RECORD)((ULONG_PTR)AttributeAddress + AttributeAddress->Length);
    SetFileRecordEnd(FileRecord, AttributeAddress, FileRecordEnd);

    return STATUS_SUCCESS;
}

/**
* @name AddIndexRoot
* @implemented
*
* Adds an $INDEX_ROOT attribute to a given FileRecord.
*
* @param Vcb
* Pointer to an NTFS_VCB for the destination volume.
*
* @param FileRecord
* Pointer to a complete file record to add the attribute to. Caller is responsible for
* ensuring FileRecord is large enough to contain $INDEX_ROOT.
*
* @param AttributeAddress
* Pointer to the region of memory that will receive the $INDEX_ROOT attribute.
* This address must reside within FileRecord. Must be aligned to an 8-byte boundary (relative to FileRecord).
*
* @param NewIndexRoot
* Pointer to an INDEX_ROOT_ATTRIBUTE containing the index root that will be copied to the new attribute.
*
* @param RootLength
* The length of NewIndexRoot, in bytes.
*
* @param Name
* Pointer to a string of 16-bit Unicode characters naming the attribute. Most often, this will be L"$I30".
*
* @param NameLength
* The number of wide-characters in the name. L"$I30" Would use 4 here.
*
* @return
* STATUS_SUCCESS on success. STATUS_NOT_IMPLEMENTED if target address isn't at the end
* of the given file record.
*
* @remarks
* This function is intended to assist in creating new folders.
* Only adding the attribute to the end of the file record is supported; AttributeAddress must
* be of type AttributeEnd.
* It's the caller's responsibility to ensure the given file record has enough memory allocated
* for the attribute, and this memory must have been zeroed.
*/
NTSTATUS
AddIndexRoot(PNTFS_VCB Vcb,
             PFILE_RECORD_HEADER FileRecord,
             PNTFS_ATTR_RECORD AttributeAddress,
             PINDEX_ROOT_ATTRIBUTE NewIndexRoot,
             ULONG RootLength,
             PCWSTR Name,
             USHORT NameLength)
{
    ULONG AttributeLength;
    // Calculate the header length
    ULONG ResidentHeaderLength = FIELD_OFFSET(NTFS_ATTR_RECORD, Resident.Reserved) + sizeof(UCHAR);
    // Back up the file record's final ULONG (even though it doesn't matter)
    ULONG FileRecordEnd = AttributeAddress->Length;
    ULONG NameOffset;
    ULONG ValueOffset;
    ULONG BytesAvailable;

    if (AttributeAddress->Type != AttributeEnd)
    {
        DPRINT1("FIXME: Can only add $DATA attribute to the end of a file record.\n");
        return STATUS_NOT_IMPLEMENTED;
    }

    NameOffset = ResidentHeaderLength;

    // Calculate ValueOffset, which will be aligned to a 4-byte boundary
    ValueOffset = ALIGN_UP_BY(NameOffset + (sizeof(WCHAR) * NameLength), VALUE_OFFSET_ALIGNMENT);

    // Calculate length of attribute
    AttributeLength = ValueOffset + RootLength;
    AttributeLength = ALIGN_UP_BY(AttributeLength, ATTR_RECORD_ALIGNMENT);

    // Make sure the file record is large enough for the new attribute
    BytesAvailable = Vcb->NtfsInfo.BytesPerFileRecord - FileRecord->BytesInUse;
    if (BytesAvailable < AttributeLength)
    {
        DPRINT1("FIXME: Not enough room in file record for index allocation attribute!\n");
        return STATUS_NOT_IMPLEMENTED;
    }

    // Set Attribute fields
    RtlZeroMemory(AttributeAddress, AttributeLength);

    AttributeAddress->Type = AttributeIndexRoot;
    AttributeAddress->Length = AttributeLength;
    AttributeAddress->NameLength = NameLength;
    AttributeAddress->NameOffset = NameOffset;
    AttributeAddress->Instance = FileRecord->NextAttributeNumber++;

    AttributeAddress->Resident.ValueLength = RootLength;
    AttributeAddress->Resident.ValueOffset = ValueOffset;

    // Set the name
    RtlCopyMemory((PCHAR)((ULONG_PTR)AttributeAddress + NameOffset), Name, NameLength * sizeof(WCHAR));

    // Copy the index root attribute
    RtlCopyMemory((PCHAR)((ULONG_PTR)AttributeAddress + ValueOffset), NewIndexRoot, RootLength);

    // move the attribute-end and file-record-end markers to the end of the file record
    AttributeAddress = (PNTFS_ATTR_RECORD)((ULONG_PTR)AttributeAddress + AttributeAddress->Length);
    SetFileRecordEnd(FileRecord, AttributeAddress, FileRecordEnd);

    return STATUS_SUCCESS;
}

/**
* @name AddRun
* @implemented
*
* Adds a run of allocated clusters to a non-resident attribute.
*
* @param Vcb
* Pointer to an NTFS_VCB for the destination volume.
*
* @param AttrContext
* Pointer to an NTFS_ATTR_CONTEXT describing the destination attribute.
*
* @param AttrOffset
* Byte offset of the destination attribute relative to its file record.
*
* @param FileRecord
* Pointer to a complete copy of the file record containing the destination attribute. Must be at least
* Vcb->NtfsInfo.BytesPerFileRecord bytes long.
*
* @param NextAssignedCluster
* Logical cluster number of the start of the data run being added.
*
* @param RunLength
* How many clusters are in the data run being added. Can't be 0.
*
* @return
* STATUS_SUCCESS on success. STATUS_INVALID_PARAMETER if AttrContext describes a resident attribute.
* STATUS_INSUFFICIENT_RESOURCES if ConvertDataRunsToLargeMCB() fails or if we fail to allocate a
* buffer for the new data runs.
* STATUS_INSUFFICIENT_RESOURCES or STATUS_UNSUCCESSFUL if FsRtlAddLargeMcbEntry() fails.
* STATUS_NOT_IMPLEMENTED if we need to migrate the attribute to an attribute list (TODO).
* Any other failure from ConvertLargeMCBToDataRuns() is returned as-is.
*
* @remarks
* Clusters should have been allocated previously with NtfsAllocateClusters().
*
*
*/
static
NTSTATUS
NtfsAddRunAtVcn(PNTFS_VCB Vcb,
                PNTFS_ATTR_CONTEXT AttrContext,
                ULONG AttrOffset,
                PFILE_RECORD_HEADER FileRecord,
                ULONGLONG NextVBN,
                ULONGLONG NextAssignedCluster,
                ULONG RunLength,
                ULONGLONG AllocationSizeDelta)
{
    NTSTATUS Status;
    int DataRunMaxLength;
    PNTFS_ATTR_RECORD DestinationAttribute;
    ULONG NextAttributeOffset;
    ULONGLONG HighestVcn;
    ULONGLONG RunLastVcn;
    ULONGLONG ExistingHighestVcn;
    LONGLONG ExistingLcn;
    ULONGLONG CheckVcn;

    PUCHAR RunBuffer = NULL;
    ULONG RunBufferSize;
    PUCHAR OriginalFileRecord = NULL;
    PNTFS_ATTR_RECORD OriginalAttributeRecord = NULL;
    ULONG OriginalAttributeLength;
    BOOLEAN McbModified = FALSE;

    if (Vcb == NULL || AttrContext == NULL || AttrContext->pRecord == NULL ||
        FileRecord == NULL || RunLength == 0 ||
        Vcb->NtfsInfo.BytesPerCluster == 0 ||
        Vcb->NtfsInfo.BytesPerFileRecord == 0 ||
        AttrOffset > Vcb->NtfsInfo.BytesPerFileRecord ||
        AttrContext->pRecord->Length >
            Vcb->NtfsInfo.BytesPerFileRecord - AttrOffset ||
        !AttrContext->pRecord->IsNonResident)
    {
        return STATUS_INVALID_PARAMETER;
    }

    DestinationAttribute = (PNTFS_ATTR_RECORD)((ULONG_PTR)FileRecord + AttrOffset);
    NextAttributeOffset = AttrOffset + AttrContext->pRecord->Length;
    ExistingHighestVcn = AttrContext->pRecord->NonResident.HighestVCN;

    if (AllocationSizeDelta >
        (ULONGLONG)-1 - AttrContext->pRecord->NonResident.AllocatedSize)
        return STATUS_INTEGER_OVERFLOW;

    if (AttrContext->pRecord->Flags & (NTFS_ATTR_COMPRESSION_MASK | NTFS_ATTR_SPARSE))
    {
        ULONGLONG PhysicalBytes =
            (ULONGLONG)RunLength * Vcb->NtfsInfo.BytesPerCluster;

        if (PhysicalBytes >
            (ULONGLONG)-1 - AttrContext->pRecord->NonResident.CompressedSize)
            return STATUS_INTEGER_OVERFLOW;
    }

    /*
     * Validate the VCN range before updating the MCB. Mapping-pair VCNs
     * are inclusive at the high end, so NextVBN + RunLength must not wrap.
     */
    if (NextVBN > (ULONGLONG)MAXLONGLONG - RunLength)
        return STATUS_INTEGER_OVERFLOW;

    RunLastVcn = NextVBN + RunLength - 1;
    if (ExistingHighestVcn == (ULONGLONG)-1 ||
        (ExistingHighestVcn == 0 &&
         AttrContext->pRecord->NonResident.AllocatedSize == 0 &&
         AttrContext->pRecord->NonResident.DataSize == 0 &&
         FsRtlNumberOfRunsInLargeMcb(&AttrContext->DataRunsMCB) == 0))
    {
        /* Empty nonresident streams may carry a zero HighestVCN on disk. */
        if (NextVBN != 0)
            return STATUS_INVALID_PARAMETER;
        HighestVcn = RunLastVcn;
    }
    else if (NextVBN <= ExistingHighestVcn)
    {
        /* This entry must fill a hole wholly inside the existing stream. */
        if (RunLastVcn > ExistingHighestVcn)
            return STATUS_INVALID_PARAMETER;
        HighestVcn = ExistingHighestVcn;
    }
    else
    {
        if (ExistingHighestVcn >= MAXLONGLONG ||
            NextVBN != ExistingHighestVcn + 1)
            return STATUS_INVALID_PARAMETER;
        HighestVcn = RunLastVcn;
    }

    /* Never replace an already-backed VCN with a second physical extent. */
    for (CheckVcn = NextVBN; CheckVcn <= RunLastVcn; ++CheckVcn)
    {
        if (FsRtlLookupLargeMcbEntry(&AttrContext->DataRunsMCB,
                                     (LONGLONG)CheckVcn,
                                     &ExistingLcn,
                                     NULL, NULL, NULL, NULL) &&
            ExistingLcn != -1)
        {
            return STATUS_INVALID_PARAMETER;
        }
    }

    /*
     * Preserve the original in-memory state until the new MFT record is
     * committed. A failed or short write must not publish a mapping that is
     * not known to be durable.
     */
    OriginalAttributeLength = AttrContext->pRecord->Length;
    OriginalAttributeRecord = ExAllocatePoolWithTag(NonPagedPool,
                                                     OriginalAttributeLength,
                                                     TAG_NTFS);
    OriginalFileRecord = ExAllocatePoolWithTag(NonPagedPool,
                                                Vcb->NtfsInfo.BytesPerFileRecord,
                                                TAG_NTFS);
    if (OriginalAttributeRecord == NULL || OriginalFileRecord == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto CleanupSnapshots;
    }

    RtlCopyMemory(OriginalAttributeRecord, AttrContext->pRecord,
                  OriginalAttributeLength);
    RtlCopyMemory(OriginalFileRecord, FileRecord,
                  Vcb->NtfsInfo.BytesPerFileRecord);

    // Add newly-assigned clusters to MCB.
    McbModified = TRUE;
    Status = STATUS_SUCCESS;
    _SEH2_TRY
    {
        if (!FsRtlAddLargeMcbEntry(&AttrContext->DataRunsMCB,
                                   NextVBN,
                                   NextAssignedCluster,
                                   RunLength))
        {
            ExRaiseStatus(STATUS_UNSUCCESSFUL);
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (!NT_SUCCESS(Status))
        goto Rollback;
    RunBuffer = ExAllocatePoolWithTag(NonPagedPool, Vcb->NtfsInfo.BytesPerFileRecord, TAG_NTFS);
    if (!RunBuffer)
    {
        DPRINT1("ERROR: Couldn't allocate memory for data runs!\n");
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Rollback;
    }

    // Convert the map control block back to encoded data runs.
    Status = ConvertLargeMCBToDataRuns(&AttrContext->DataRunsMCB, HighestVcn + 1, RunBuffer, Vcb->NtfsInfo.BytesPerFileRecord, &RunBufferSize);
    if (Status == STATUS_BUFFER_TOO_SMALL)
    {
        // Runs won't fit in a single record; migrating to an $ATTRIBUTE_LIST isn't supported yet.
        DPRINT1("Data runs too large for one file record - $ATTRIBUTE_LIST needed (not implemented)\n");
        Status = STATUS_NOT_IMPLEMENTED;
        goto Rollback;
    }
    else if (!NT_SUCCESS(Status))
    {
        DPRINT1("ERROR: Failed to convert data runs! (Status %lx)\n", Status);
        goto Rollback;
    }

    // Get the amount of free space between the start of the of the first data run and the attribute end
    DataRunMaxLength = AttrContext->pRecord->Length - AttrContext->pRecord->NonResident.MappingPairsOffset;

    // Do we need to extend the attribute (or convert to attribute list)?
    if (DataRunMaxLength < RunBufferSize)
    {
        PNTFS_ATTR_RECORD NextAttribute = (PNTFS_ATTR_RECORD)((ULONG_PTR)FileRecord + NextAttributeOffset);
        PNTFS_ATTR_RECORD NewRecord;

        // Add free space at the end of the file record to DataRunMaxLength
        DataRunMaxLength += Vcb->NtfsInfo.BytesPerFileRecord - FileRecord->BytesInUse;

        // Can we resize the attribute?
        if (DataRunMaxLength < RunBufferSize)
        {
            DPRINT1("FIXME: Need to create attribute list! Max Data Run Length available: %d, RunBufferSize: %d\n", DataRunMaxLength, RunBufferSize);
            Status = STATUS_NOT_IMPLEMENTED;
            goto Rollback;
        }

        // Are there more attributes after the one we're resizing?
        if (NextAttribute->Type != AttributeEnd)
        {
            PNTFS_ATTR_RECORD FinalAttribute;

            // Calculate where to move the trailing attributes
            ULONG_PTR MoveTo = (ULONG_PTR)DestinationAttribute + AttrContext->pRecord->NonResident.MappingPairsOffset + RunBufferSize;
            MoveTo = ALIGN_UP_BY(MoveTo, ATTR_RECORD_ALIGNMENT);

            DPRINT("Moving attribute(s) after this one starting with type 0x%lx\n", NextAttribute->Type);

            // Move the trailing attributes; FinalAttribute will point to the end marker
            FinalAttribute = MoveAttributes(Vcb, NextAttribute, NextAttributeOffset, MoveTo);

            // set the file record end
            SetFileRecordEnd(FileRecord, FinalAttribute, FILE_RECORD_END);
        }

        // calculate position of end markers
        NextAttributeOffset = AttrOffset + AttrContext->pRecord->NonResident.MappingPairsOffset + RunBufferSize;
        NextAttributeOffset = ALIGN_UP_BY(NextAttributeOffset, ATTR_RECORD_ALIGNMENT);

        // Update the length of the destination attribute
        DestinationAttribute->Length = NextAttributeOffset - AttrOffset;

        // Create a new copy of the attribute record
        NewRecord = ExAllocatePoolWithTag(NonPagedPool, DestinationAttribute->Length, TAG_NTFS);
        if (NewRecord == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Rollback;
        }

        RtlCopyMemory(NewRecord, AttrContext->pRecord, AttrContext->pRecord->Length);
        NewRecord->Length = DestinationAttribute->Length;

        // Free the old copy of the attribute record, which won't be large enough
        ExFreePoolWithTag(AttrContext->pRecord, TAG_NTFS);

        // Set the attribute context's record to the new copy
        AttrContext->pRecord = NewRecord;

        // if NextAttribute is the AttributeEnd marker
        if (NextAttribute->Type == AttributeEnd)
        {
            // End the file record
            NextAttribute = (PNTFS_ATTR_RECORD)((ULONG_PTR)FileRecord + NextAttributeOffset);
            SetFileRecordEnd(FileRecord, NextAttribute, FILE_RECORD_END);
        }
    }

    // Update HighestVCN
    DestinationAttribute->NonResident.HighestVCN =
    AttrContext->pRecord->NonResident.HighestVCN = HighestVcn;

    /* Sparse and compressed streams also count the clusters they really own */
    if (AttrContext->pRecord->Flags & (NTFS_ATTR_COMPRESSION_MASK | NTFS_ATTR_SPARSE))
    {
        AttrContext->pRecord->NonResident.CompressedSize += (ULONGLONG)RunLength * Vcb->NtfsInfo.BytesPerCluster;
        DestinationAttribute->NonResident.CompressedSize = AttrContext->pRecord->NonResident.CompressedSize;
    }

    // Write data runs to destination attribute
    RtlCopyMemory((PVOID)((ULONG_PTR)DestinationAttribute + DestinationAttribute->NonResident.MappingPairsOffset),
                  RunBuffer,
                  RunBufferSize);

    // Update the attribute record in the attribute context
    RtlCopyMemory((PVOID)((ULONG_PTR)AttrContext->pRecord + AttrContext->pRecord->NonResident.MappingPairsOffset),
                  RunBuffer,
                  RunBufferSize);

    /*
     * A sparse-hole insertion commits its allocation-size delta together
     * with the new mapping pairs. Ordinary append callers retain their
     * existing accounting and pass a zero delta.
     */
    if (AllocationSizeDelta != 0)
    {
        AttrContext->pRecord->NonResident.AllocatedSize += AllocationSizeDelta;
        DestinationAttribute->NonResident.AllocatedSize =
            AttrContext->pRecord->NonResident.AllocatedSize;
    }

    // Commit the file record before publishing the new in-memory mapping.
    Status = UpdateFileRecord(Vcb, AttrContext->FileMFTIndex, FileRecord);
    ExFreePoolWithTag(RunBuffer, TAG_NTFS);
    RunBuffer = NULL;

    if (!NT_SUCCESS(Status))
        goto Rollback;

#if NTFS_DUMP_DATA_RUNS
    NtfsDumpDataRuns((PUCHAR)((ULONG_PTR)DestinationAttribute + DestinationAttribute->NonResident.MappingPairsOffset), 0);
#endif

    ExFreePoolWithTag(OriginalAttributeRecord, TAG_NTFS);
    ExFreePoolWithTag(OriginalFileRecord, TAG_NTFS);
    return STATUS_SUCCESS;

Rollback:
    if (RunBuffer != NULL)
        ExFreePoolWithTag(RunBuffer, TAG_NTFS);

    if (McbModified)
    {
        FsRtlRemoveLargeMcbEntry(&AttrContext->DataRunsMCB,
                                 (LONGLONG)NextVBN,
                                 (LONGLONG)RunLength);
    }

    RtlCopyMemory(FileRecord, OriginalFileRecord,
                  Vcb->NtfsInfo.BytesPerFileRecord);
    if (AttrContext->pRecord != NULL)
        ExFreePoolWithTag(AttrContext->pRecord, TAG_NTFS);
    AttrContext->pRecord = OriginalAttributeRecord;
    OriginalAttributeRecord = NULL;

CleanupSnapshots:
    if (OriginalAttributeRecord != NULL)
        ExFreePoolWithTag(OriginalAttributeRecord, TAG_NTFS);
    if (OriginalFileRecord != NULL)
        ExFreePoolWithTag(OriginalFileRecord, TAG_NTFS);
    return Status;
}

/*
 * Append a newly allocated physical run to the end of an attribute.
 */
NTSTATUS
AddRun(PNTFS_VCB Vcb,
       PNTFS_ATTR_CONTEXT AttrContext,
       ULONG AttrOffset,
       PFILE_RECORD_HEADER FileRecord,
       ULONGLONG NextAssignedCluster,
       ULONG RunLength)
{
    ULONGLONG NextVBN;

    if (AttrContext == NULL || AttrContext->pRecord == NULL)
        return STATUS_INVALID_PARAMETER;

    if (!(AttrContext->pRecord->Flags & NTFS_ATTR_SPARSE) &&
        AttrContext->pRecord->NonResident.AllocatedSize == 0)
    {
        /* Preserve the initial VCN for a newly created ordinary stream. */
        NextVBN = 0;
    }
    else if (AttrContext->pRecord->NonResident.HighestVCN == (ULONGLONG)-1)
    {
        NextVBN = 0;
    }
    else
    {
        if (AttrContext->pRecord->NonResident.HighestVCN >= MAXLONGLONG)
            return STATUS_FILE_CORRUPT_ERROR;
        NextVBN = AttrContext->pRecord->NonResident.HighestVCN + 1;
    }

    return NtfsAddRunAtVcn(Vcb,
                           AttrContext,
                           AttrOffset,
                           FileRecord,
                           NextVBN,
                           NextAssignedCluster,
                           RunLength,
                           0);
}

/*
 * Insert a physical run into a previously sparse VCN interval. The caller
 * must have reserved and zeroed the clusters before publishing this mapping.
 */
NTSTATUS
NtfsAddSparseRun(PNTFS_VCB Vcb,
                 PNTFS_ATTR_CONTEXT AttrContext,
                 ULONG AttrOffset,
                 PFILE_RECORD_HEADER FileRecord,
                 ULONGLONG Vcn,
                 ULONGLONG FirstLcn,
                 ULONG RunLength)
{
    if (Vcb == NULL || AttrContext == NULL || AttrContext->pRecord == NULL ||
        FileRecord == NULL || RunLength == 0 ||
        Vcb->NtfsInfo.BytesPerCluster == 0 ||
        FirstLcn >= Vcb->NtfsInfo.ClusterCount ||
        RunLength > Vcb->NtfsInfo.ClusterCount - FirstLcn ||
        AttrOffset > Vcb->NtfsInfo.BytesPerFileRecord ||
        AttrContext->pRecord->Length >
            Vcb->NtfsInfo.BytesPerFileRecord - AttrOffset)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if ((ULONGLONG)RunLength >
        (ULONGLONG)-1 / Vcb->NtfsInfo.BytesPerCluster)
        return STATUS_INTEGER_OVERFLOW;

    return NtfsAddRunAtVcn(Vcb,
                           AttrContext,
                           AttrOffset,
                           FileRecord,
                           Vcn,
                           FirstLcn,
                           RunLength,
                           (ULONGLONG)RunLength * Vcb->NtfsInfo.BytesPerCluster);
}

/**
* @name AddStandardInformation
* @implemented
*
* Adds a $STANDARD_INFORMATION attribute to a given FileRecord.
*
* @param FileRecord
* Pointer to a complete file record to add the attribute to. Caller is responsible for
* ensuring FileRecord is large enough to contain $STANDARD_INFORMATION.
*
* @param AttributeAddress
* Pointer to the region of memory that will receive the $STANDARD_INFORMATION attribute.
* This address must reside within FileRecord. Must be aligned to an 8-byte boundary (relative to FileRecord).
*
* @return
* STATUS_SUCCESS on success. STATUS_NOT_IMPLEMENTED if target address isn't at the end
* of the given file record.
*
* @remarks
* Only adding the attribute to the end of the file record is supported; AttributeAddress must
* be of type AttributeEnd.
* As it's implemented, this function is only intended to assist in creating new file records. It
* could be made more general-purpose by considering file records with an $ATTRIBUTE_LIST.
* It's the caller's responsibility to ensure the given file record has enough memory allocated
* for the attribute.
*/
NTSTATUS
AddStandardInformation(PFILE_RECORD_HEADER FileRecord,
                       PNTFS_ATTR_RECORD AttributeAddress)
{
    ULONG ResidentHeaderLength = FIELD_OFFSET(NTFS_ATTR_RECORD, Resident.Reserved) + sizeof(UCHAR);
    PSTANDARD_INFORMATION StandardInfo = (PSTANDARD_INFORMATION)((LONG_PTR)AttributeAddress + ResidentHeaderLength);
    LARGE_INTEGER SystemTime;
    ULONG FileRecordEnd = AttributeAddress->Length;

    if (AttributeAddress->Type != AttributeEnd)
    {
        DPRINT1("FIXME: Can only add $STANDARD_INFORMATION attribute to the end of a file record.\n");
        return STATUS_NOT_IMPLEMENTED;
    }

    AttributeAddress->Type = AttributeStandardInformation;
    AttributeAddress->Length = sizeof(STANDARD_INFORMATION) + ResidentHeaderLength;
    AttributeAddress->Length = ALIGN_UP_BY(AttributeAddress->Length, ATTR_RECORD_ALIGNMENT);
    AttributeAddress->Resident.ValueLength = sizeof(STANDARD_INFORMATION);
    AttributeAddress->Resident.ValueOffset = ResidentHeaderLength;
    AttributeAddress->Instance = FileRecord->NextAttributeNumber++;

    // set dates and times
    KeQuerySystemTime(&SystemTime);
    StandardInfo->CreationTime = SystemTime.QuadPart;
    StandardInfo->ChangeTime = SystemTime.QuadPart;
    StandardInfo->LastWriteTime = SystemTime.QuadPart;
    StandardInfo->LastAccessTime = SystemTime.QuadPart;
    StandardInfo->FileAttribute = NTFS_FILE_TYPE_ARCHIVE;

    // move the attribute-end and file-record-end markers to the end of the file record
    AttributeAddress = (PNTFS_ATTR_RECORD)((ULONG_PTR)AttributeAddress + AttributeAddress->Length);
    SetFileRecordEnd(FileRecord, AttributeAddress, FileRecordEnd);

    return STATUS_SUCCESS;
}

/**
* @name ConvertDataRunsToLargeMCB
* @implemented
*
* Converts binary data runs to a map control block.
*
* @param DataRun
* Pointer to the run data
*
* @param DataRunsMCB
* Pointer to an unitialized LARGE_MCB structure.
*
* @return
* STATUS_SUCCESS on success, STATUS_INSUFFICIENT_RESOURCES or STATUS_UNSUCCESSFUL if we fail to
* initialize the mcb or add an entry.
*
* @remarks
* Initializes the LARGE_MCB pointed to by DataRunsMCB. If this function succeeds, you
* need to call FsRtlUninitializeLargeMcb() when you're done with DataRunsMCB. This
* function will ensure the LargeMCB has been unitialized in case of failure.
*
*/
NTSTATUS
ConvertDataRunsToLargeMCB(PUCHAR DataRun,
                          ULONG DataRunLengthBytes,
                          ULONGLONG InitialLCN,
                          PLARGE_MCB DataRunsMCB,
                          PULONGLONG pNextVBN)
{
    LONGLONG  DataRunOffset;
    ULONGLONG DataRunLength;
    LONGLONG  DataRunStartLCN;
    ULONGLONG LastLCN = InitialLCN;
    BOOLEAN IsSparse;
    PUCHAR NextDataRun;
    ULONG RemainingBytes = DataRunLengthBytes;

    if (DataRun == NULL || DataRunsMCB == NULL || pNextVBN == NULL ||
        DataRunLengthBytes == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    // Initialize the MCB, potentially catch an exception
    _SEH2_TRY{
        FsRtlInitializeLargeMcb(DataRunsMCB, NonPagedPool);
    } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    } _SEH2_END;

    while (RemainingBytes != 0 && *DataRun != 0)
    {
        NextDataRun = DecodeRunBounded(DataRun, RemainingBytes,
                                       &DataRunOffset, &DataRunLength,
                                       &IsSparse);
        if (NextDataRun == NULL ||
            *pNextVBN > MAXLONGLONG ||
            DataRunLength > (ULONGLONG)MAXLONGLONG - *pNextVBN)
        {
            FsRtlUninitializeLargeMcb(DataRunsMCB);
            return STATUS_FILE_CORRUPT_ERROR;
        }
        RemainingBytes -= (ULONG)(NextDataRun - DataRun);
        DataRun = NextDataRun;

        if (!IsSparse)
        {
            // Normal data run; the MCB stores LCNs in signed 64-bit fields.
            if (LastLCN > MAXLONGLONG)
            {
                FsRtlUninitializeLargeMcb(DataRunsMCB);
                return STATUS_FILE_CORRUPT_ERROR;
            }

            if (DataRunOffset < 0)
            {
                ULONGLONG Magnitude =
                    (ULONGLONG)(-(DataRunOffset + 1)) + 1;
                if (Magnitude > LastLCN)
                {
                    FsRtlUninitializeLargeMcb(DataRunsMCB);
                    return STATUS_FILE_CORRUPT_ERROR;
                }
                DataRunStartLCN = (LONGLONG)(LastLCN - Magnitude);
            }
            else
            {
                if ((ULONGLONG)DataRunOffset >
                    (ULONGLONG)MAXLONGLONG - LastLCN)
                {
                    FsRtlUninitializeLargeMcb(DataRunsMCB);
                    return STATUS_FILE_CORRUPT_ERROR;
                }
                DataRunStartLCN = (LONGLONG)(LastLCN + DataRunOffset);
            }
            /*
             * The MCB stores the starting LCN and run length separately.
             * Ensure the complete physical interval remains representable
             * by its signed 64-bit LCN interface, not just its first cluster.
             */
            if (DataRunLength - 1 >
                (ULONGLONG)MAXLONGLONG - (ULONGLONG)DataRunStartLCN)
            {
                FsRtlUninitializeLargeMcb(DataRunsMCB);
                return STATUS_FILE_CORRUPT_ERROR;
            }

            LastLCN = (ULONGLONG)DataRunStartLCN;

            _SEH2_TRY{
                if (!FsRtlAddLargeMcbEntry(DataRunsMCB,
                                           *pNextVBN,
                                           DataRunStartLCN,
                                           DataRunLength))
                {
                    ExRaiseStatus(STATUS_UNSUCCESSFUL);
                }
            } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
                FsRtlUninitializeLargeMcb(DataRunsMCB);
                _SEH2_YIELD(return _SEH2_GetExceptionCode());
            } _SEH2_END;

        }

        *pNextVBN += DataRunLength;
    }

    if (RemainingBytes == 0 || *DataRun != 0)
    {
        FsRtlUninitializeLargeMcb(DataRunsMCB);
        return STATUS_FILE_CORRUPT_ERROR;
    }

    return STATUS_SUCCESS;
}

/**
* @name ConvertLargeMCBToDataRuns
* @implemented
*
* Converts a map control block to a series of encoded data runs (used by non-resident attributes).
*
* @param DataRunsMCB
* Pointer to a LARGE_MCB structure describing the data runs.
*
* @param VcnCount
* Number of VCNs the runs must describe, HighestVCN + 1. A hole the MCB doesn't
* reach, at the end of a sparse stream, is encoded up to this point.
*
* @param RunBuffer
* Pointer to the buffer that will receive the encoded data runs.
*
* @param MaxBufferSize
* Size of RunBuffer, in bytes.
*
* @param UsedBufferSize
* Pointer to a ULONG that will receive the size of the data runs in bytes. Can't be NULL.
*
* @return
* STATUS_SUCCESS on success, STATUS_BUFFER_TOO_SMALL if RunBuffer is too small to contain the
* complete output.
*
*/
NTSTATUS
ConvertLargeMCBToDataRuns(PLARGE_MCB DataRunsMCB,
                          ULONGLONG VcnCount,
                          PUCHAR RunBuffer,
                          ULONG MaxBufferSize,
                          PULONG UsedBufferSize)
{
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG RunBufferOffset = 0;
    LONGLONG  DataRunOffset = 0;
    LONGLONG LastLCN = 0;
    ULONGLONG NextVcn = 0;
    LONGLONG Vbn, Lbn, Count;
    ULONG i;
    BOOLEAN Tail = FALSE;

    /* FSRTL LARGE_MCB exposes signed VCN and LCN ranges. */
    if (DataRunsMCB == NULL || RunBuffer == NULL ||
        UsedBufferSize == NULL || MaxBufferSize == 0)
        return STATUS_INVALID_PARAMETER;

    *UsedBufferSize = 0;
    if (VcnCount > (ULONGLONG)MAXLONGLONG)
        return STATUS_FILE_CORRUPT_ERROR;

    DPRINT("\t[Vbn, Lbn, Count]\n");

    // convert each mcb entry to a data run, then the trailing hole if there is one
    for (i = 0; ; i++)
    {
        UCHAR DataRunOffsetSize = 0;
        UCHAR DataRunLengthSize = 0;
        UCHAR ControlByte = 0;

        if (!FsRtlGetNextLargeMcbEntry(DataRunsMCB, i, &Vbn, &Lbn, &Count))
        {
            if (NextVcn >= VcnCount)
                break;

            /* The MCB only knows mapped clusters, so a hole at the end isn't in it */
            Vbn = NextVcn;
            Lbn = -1;
            Count = VcnCount - NextVcn;
            Tail = TRUE;
        }

        /*
         * LARGE_MCB stores only physical mappings.  Materialize every VCN
         * gap as an explicit sparse run or the stream will be compacted when
         * converted back to mapping pairs.
         */
        if (Vbn < 0 || Count <= 0 || (ULONGLONG)Vbn < NextVcn ||
            (ULONGLONG)Vbn > VcnCount ||
            (ULONGLONG)Count > VcnCount - (ULONGLONG)Vbn ||
            Count > MAXLONGLONG - Vbn ||
            (Lbn < -1) ||
            (Lbn >= 0 && Count > MAXLONGLONG - Lbn))
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            break;
        }

        if ((ULONGLONG)Vbn > NextVcn)
        {
            ULONGLONG SparseLength = (ULONGLONG)Vbn - NextVcn;
            UCHAR SparseLengthSize = GetPackedByteCount(SparseLength, TRUE);

            if ((1 + SparseLengthSize + 1) > MaxBufferSize ||
                RunBufferOffset > MaxBufferSize - (1 + SparseLengthSize + 1))
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            RunBuffer[RunBufferOffset++] = SparseLengthSize;
            RtlCopyMemory(RunBuffer + RunBufferOffset,
                          &SparseLength,
                          SparseLengthSize);
            RunBufferOffset += SparseLengthSize;
            NextVcn = (ULONGLONG)Vbn;
        }

        // [vbn, lbn, count]
        DPRINT("\t[%I64d, %I64d,%I64d]\n", Vbn, Lbn, Count);

        // determine how to represent DataRunLengthSize with the minimum number of bytes
        DPRINT("Determining how many bytes needed to represent %I64x\n", Count);
        DataRunLengthSize = GetPackedByteCount(Count, TRUE);
        DPRINT("%d bytes needed.\n", DataRunLengthSize);
        /* A sparse run has no offset, and the next real run is relative to the last real one */
        if (Lbn != -1)
        {
            DataRunOffset = Lbn - LastLCN;
            LastLCN = Lbn;

            // now we need to determine how to represent DataRunOffset with the minimum number of bytes
            DPRINT("Determining how many bytes needed to represent %I64x\n", DataRunOffset);
            DataRunOffsetSize = GetPackedByteCount(DataRunOffset, TRUE);
            DPRINT("%d bytes needed.\n", DataRunOffsetSize);
        }

        // ensure the next data run + end marker would be <= Max buffer size
        if ((2 + DataRunLengthSize + DataRunOffsetSize) > MaxBufferSize ||
            RunBufferOffset > MaxBufferSize -
                              (2 + DataRunLengthSize + DataRunOffsetSize))
        {
            Status = STATUS_BUFFER_TOO_SMALL;
            DPRINT1("FIXME: Ran out of room in buffer for data runs!\n");
            break;
        }

        // pack and copy the control byte
        ControlByte = (DataRunOffsetSize << 4) + DataRunLengthSize;
        RunBuffer[RunBufferOffset++] = ControlByte;

        // copy DataRunLength
        RtlCopyMemory(RunBuffer + RunBufferOffset, &Count, DataRunLengthSize);
        RunBufferOffset += DataRunLengthSize;

        // copy DataRunOffset
        RtlCopyMemory(RunBuffer + RunBufferOffset, &DataRunOffset, DataRunOffsetSize);
        RunBufferOffset += DataRunOffsetSize;

        NextVcn = Vbn + Count;

        if (Tail)
            break;
    }

    // End of data runs.  Never write the terminator after a failed
    // capacity check or malformed run has exhausted the destination.
    if (RunBufferOffset >= MaxBufferSize)
    {
        if (NT_SUCCESS(Status))
            Status = STATUS_BUFFER_TOO_SMALL;
        return Status;
    }

    RunBuffer[RunBufferOffset++] = 0;

    if (NT_SUCCESS(Status))
        *UsedBufferSize = RunBufferOffset;
    DPRINT("New Size of DataRuns: %ld\n", *UsedBufferSize);

    return Status;
}

/*
 * Decode one mapping-pairs run without reading beyond the attribute's
 * mapping-pairs buffer.  A NULL return means the stream is malformed.
 */
PUCHAR
DecodeRunBounded(PUCHAR DataRun,
                 ULONG BytesAvailable,
                 PLONGLONG DataRunOffset,
                 PULONGLONG DataRunLength,
                 PBOOLEAN IsSparse)
{
    UCHAR Header, OffsetSize, LengthSize;
    ULONGLONG EncodedOffset = 0;
    ULONG i, Required;

    if (DataRun == NULL || DataRunOffset == NULL ||
        DataRunLength == NULL || IsSparse == NULL || BytesAvailable < 1)
        return NULL;

    Header = DataRun[0];
    if (Header == 0)
        return NULL;

    OffsetSize = (Header >> 4) & 0x0F;
    LengthSize = Header & 0x0F;
    if (LengthSize == 0 || LengthSize > sizeof(ULONGLONG) ||
        OffsetSize > sizeof(ULONGLONG))
        return NULL;

    Required = 1 + LengthSize + OffsetSize;
    if (Required > BytesAvailable)
        return NULL;

    *DataRunLength = 0;
    for (i = 0; i < LengthSize; ++i)
        *DataRunLength |= ((ULONGLONG)DataRun[1 + i]) << (i * 8);

    if (*DataRunLength == 0)
        return NULL;

    *IsSparse = (OffsetSize == 0);
    if (*IsSparse)
    {
        *DataRunOffset = 0;
    }
    else
    {
        PUCHAR OffsetBytes = DataRun + 1 + LengthSize;
        for (i = 0; i < OffsetSize; ++i)
            EncodedOffset |= ((ULONGLONG)OffsetBytes[i]) << (i * 8);

        if (OffsetSize < sizeof(ULONGLONG) &&
            (OffsetBytes[OffsetSize - 1] & 0x80))
            EncodedOffset |= (~0ULL) << (OffsetSize * 8);

        *DataRunOffset = (LONGLONG)EncodedOffset;
    }

    return DataRun + Required;
}

PUCHAR
DecodeRun(PUCHAR DataRun,
          LONGLONG *DataRunOffset,
          ULONGLONG *DataRunLength)
{
    UCHAR DataRunOffsetSize;
    UCHAR DataRunLengthSize;
    CHAR i;

    DataRunOffsetSize = (*DataRun >> 4) & 0xF;
    DataRunLengthSize = *DataRun & 0xF;
    *DataRunOffset = 0;
    *DataRunLength = 0;
    DataRun++;
    for (i = 0; i < DataRunLengthSize; i++)
    {
        *DataRunLength += ((ULONG64)*DataRun) << (i * 8);
        DataRun++;
    }

    /* NTFS 3+ sparse files */
    if (DataRunOffsetSize == 0)
    {
        *DataRunOffset = -1;
    }
    else
    {
        for (i = 0; i < DataRunOffsetSize - 1; i++)
        {
            *DataRunOffset += ((ULONG64)*DataRun) << (i * 8);
            DataRun++;
        }
        /* The last byte contains sign so we must process it different way. */
        *DataRunOffset = ((LONG64)(CHAR)(*(DataRun++)) << (i * 8)) + *DataRunOffset;
    }

    DPRINT("DataRunOffsetSize: %x\n", DataRunOffsetSize);
    DPRINT("DataRunLengthSize: %x\n", DataRunLengthSize);
    DPRINT("DataRunOffset: %x\n", *DataRunOffset);
    DPRINT("DataRunLength: %x\n", *DataRunLength);

    return DataRun;
}

BOOLEAN
FindRun(PNTFS_ATTR_RECORD NresAttr,
        ULONGLONG vcn,
        PULONGLONG lcn,
        PULONGLONG count)
{
    PUCHAR DataRun;
    ULONG BytesAvailable;
    ULONGLONG CurrentVcn;
    ULONGLONG RunLength;
    ULONGLONG Skip;
    LONGLONG CurrentLcn = 0;
    LONGLONG RunDelta;
    BOOLEAN IsSparse;

    if (NresAttr == NULL || lcn == NULL || count == NULL ||
        !NresAttr->IsNonResident ||
        NresAttr->Length < FIELD_OFFSET(NTFS_ATTR_RECORD, NonResident.MappingPairsOffset) +
                          sizeof(NresAttr->NonResident.MappingPairsOffset) ||
        NresAttr->NonResident.MappingPairsOffset >= NresAttr->Length ||
        vcn < NresAttr->NonResident.LowestVCN ||
        vcn > NresAttr->NonResident.HighestVCN)
    {
        return FALSE;
    }

    DataRun = (PUCHAR)NresAttr + NresAttr->NonResident.MappingPairsOffset;
    BytesAvailable = NresAttr->Length - NresAttr->NonResident.MappingPairsOffset;
    CurrentVcn = NresAttr->NonResident.LowestVCN;

    while (BytesAvailable != 0 && *DataRun != 0)
    {
        PUCHAR NextRun;

        NextRun = DecodeRunBounded(DataRun,
                                   BytesAvailable,
                                   &RunDelta,
                                   &RunLength,
                                   &IsSparse);
        /*
         * A mapping pair must describe a non-empty run wholly contained in
         * this extent's declared VCN interval.  Checking only arithmetic
         * overflow would allow a corrupt run to extend past HighestVCN.
         */
        if (NextRun == NULL ||
            RunLength == 0 ||
            CurrentVcn > NresAttr->NonResident.HighestVCN ||
            RunLength - 1 >
                NresAttr->NonResident.HighestVCN - CurrentVcn ||
            RunLength > (ULONGLONG)-1 - CurrentVcn)
        {
            return FALSE;
        }

        if (vcn < CurrentVcn + RunLength)
        {
            Skip = vcn - CurrentVcn;
            *count = RunLength - Skip;

            if (IsSparse)
            {
                *lcn = (ULONGLONG)-1;
                return TRUE;
            }

            if ((RunDelta > 0 && CurrentLcn > MAXLONGLONG - RunDelta) ||
                (RunDelta < 0 && CurrentLcn < ((LONGLONG)(-MAXLONGLONG - 1) - RunDelta)))
            {
                return FALSE;
            }

            CurrentLcn += RunDelta;
            if (CurrentLcn < 0 ||
                Skip > (ULONGLONG)MAXLONGLONG ||
                CurrentLcn > MAXLONGLONG - (LONGLONG)Skip)
            {
                return FALSE;
            }

            CurrentLcn += (LONGLONG)Skip;
            if (*count == 0 ||
                *count - 1 > (ULONGLONG)MAXLONGLONG -
                             (ULONGLONG)CurrentLcn)
            {
                return FALSE;
            }

            *lcn = (ULONGLONG)CurrentLcn;
            return TRUE;
        }

        if (!IsSparse)
        {
            if ((RunDelta > 0 && CurrentLcn > MAXLONGLONG - RunDelta) ||
                (RunDelta < 0 && CurrentLcn < ((LONGLONG)(-MAXLONGLONG - 1) - RunDelta)))
            {
                return FALSE;
            }
            CurrentLcn += RunDelta;
            if (CurrentLcn < 0)
                return FALSE;
        }

        CurrentVcn += RunLength;
        BytesAvailable -= (ULONG)(NextRun - DataRun);
        DataRun = NextRun;
    }

    return FALSE;
}

/**
* @name FreeClusters
* @implemented
*
* Shrinks the allocation size of a non-resident attribute by a given number of clusters.
* Frees the clusters from the volume's $BITMAP file as well as the attribute's data runs.
*
* @param Vcb
* Pointer to an NTFS_VCB for the destination volume.
*
* @param AttrContext
* Pointer to an NTFS_ATTR_CONTEXT describing the attribute from which the clusters will be freed.
*
* @param AttrOffset
* Byte offset of the destination attribute relative to its file record.
*
* @param FileRecord
* Pointer to a complete copy of the file record containing the attribute. Must be at least
* Vcb->NtfsInfo.BytesPerFileRecord bytes long.
*
* @param ClustersToFree
* Number of clusters that should be freed from the end of the data stream. Must be no more
* Than the number of clusters assigned to the attribute (HighestVCN + 1).
*
* @return
* STATUS_SUCCESS on success. STATUS_INVALID_PARAMETER if AttrContext describes a resident attribute,
* or if the caller requested more clusters be freed than the attribute has been allocated.
* STATUS_INSUFFICIENT_RESOURCES if allocating a buffer for the data runs fails or
* if ConvertDataRunsToLargeMCB() fails.
* STATUS_BUFFER_TOO_SMALL if ConvertLargeMCBToDataRuns() fails.
*
*
*/
NTSTATUS
FreeClusters(PNTFS_VCB Vcb,
             PNTFS_ATTR_CONTEXT AttrContext,
             ULONG AttrOffset,
             PFILE_RECORD_HEADER FileRecord,
             ULONG ClustersToFree)
{
    NTSTATUS Status = STATUS_SUCCESS;

    PNTFS_ATTR_RECORD DestinationAttribute;
    ULONG NextAttributeOffset;
    PNTFS_ATTR_RECORD NextAttribute;

    PUCHAR RunBuffer;
    PUCHAR OriginalFileRecord = NULL;
    ULONG RunBufferSize = 0;
    LARGE_MCB PreparedMCB;
    ULONGLONG PreparedVcnCount;
    ULONG RunIndex;

    ULONG LowestFreed = MAXULONG;
    ULONG HighestFreed = 0;
    ULONG Freed = 0;
    ULONGLONG LastVcn;
    ULONGLONG VcnToCheck;
    ULONG ClustersChecked;
    ULONG ClustersToRelease = 0;

    if (Vcb == NULL || AttrContext == NULL || AttrContext->pRecord == NULL ||
        FileRecord == NULL || !AttrContext->pRecord->IsNonResident)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (ClustersToFree == 0)
        return STATUS_SUCCESS;

    /*
     * Compressed and sparse streams account physical allocation in bytes.
     * A zero cluster size would make the later compressed-size validation
     * divide by zero and indicates invalid mounted-volume geometry.
     */
    if ((AttrContext->pRecord->Flags &
         (NTFS_ATTR_COMPRESSION_MASK | NTFS_ATTR_SPARSE)) &&
        Vcb->NtfsInfo.BytesPerCluster == 0)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    /*
     * FreeClusters() edits the attribute in the caller's FILE record. Verify
     * that the supplied offset identifies a complete attribute and that its
     * trailing end marker fits before forming pointers into the record.
     */
    if (FileRecord->BytesAllocated != Vcb->NtfsInfo.BytesPerFileRecord ||
        FileRecord->BytesInUse > FileRecord->BytesAllocated ||
        FileRecord->BytesInUse < sizeof(FILE_RECORD_HEADER) ||
        FileRecord->AttributeOffset < sizeof(FILE_RECORD_HEADER) ||
        FileRecord->AttributeOffset > FileRecord->BytesInUse ||
        (FileRecord->AttributeOffset & (ATTR_RECORD_ALIGNMENT - 1)) != 0 ||
        AttrOffset < FileRecord->AttributeOffset ||
        AttrOffset > FileRecord->BytesInUse ||
        AttrContext->pRecord->Length < sizeof(NTFS_ATTR_RECORD) ||
        AttrContext->pRecord->Length >
            FileRecord->BytesInUse - AttrOffset ||
        AttrContext->pRecord->Length >
            Vcb->NtfsInfo.BytesPerFileRecord - AttrOffset ||
        AttrContext->pRecord->NonResident.MappingPairsOffset <
            FIELD_OFFSET(NTFS_ATTR_RECORD, NonResident.MappingPairsOffset) +
                sizeof(USHORT) ||
        AttrContext->pRecord->NonResident.MappingPairsOffset >=
            AttrContext->pRecord->Length)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    NextAttributeOffset = AttrOffset + AttrContext->pRecord->Length;
    if (NextAttributeOffset > FileRecord->BytesInUse ||
        sizeof(ULONG) > FileRecord->BytesInUse - NextAttributeOffset ||
        NextAttributeOffset > Vcb->NtfsInfo.BytesPerFileRecord ||
        sizeof(ULONG) > Vcb->NtfsInfo.BytesPerFileRecord - NextAttributeOffset)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    DestinationAttribute = (PNTFS_ATTR_RECORD)((ULONG_PTR)FileRecord + AttrOffset);
    NextAttribute = (PNTFS_ATTR_RECORD)((ULONG_PTR)FileRecord + NextAttributeOffset);

    if (DestinationAttribute->Type != AttrContext->pRecord->Type ||
        DestinationAttribute->Length != AttrContext->pRecord->Length ||
        DestinationAttribute->Instance != AttrContext->pRecord->Instance ||
        DestinationAttribute->IsNonResident != TRUE)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    LastVcn = AttrContext->pRecord->NonResident.HighestVCN;

    /*
     * Validate the complete request before changing the bitmap or MCB.
     * Otherwise an oversized request can free part of the stream and only
     * then discover that no clusters remain.
     */
    /*
     * FSRTL LARGE_MCB uses signed 64-bit VCNs.  Reject an on-disk extent
     * whose inclusive highest VCN cannot be represented before calculating
     * its VCN count or passing values to the MCB interfaces.
     */
    if (LastVcn == (ULONGLONG)-1)
        return STATUS_INVALID_PARAMETER;

    if (LastVcn >= (ULONGLONG)MAXLONGLONG)
        return STATUS_FILE_CORRUPT_ERROR;

    if ((ULONGLONG)ClustersToFree > LastVcn + 1)
        return STATUS_INVALID_PARAMETER;

    VcnToCheck = LastVcn;
    for (ClustersChecked = 0;
         ClustersChecked < ClustersToFree;
         ++ClustersChecked, --VcnToCheck)
    {
        LONGLONG PhysicalLcn;

        if (FsRtlLookupLargeMcbEntry(&AttrContext->DataRunsMCB,
                                     VcnToCheck,
                                     &PhysicalLcn,
                                     NULL, NULL, NULL, NULL) &&
            (PhysicalLcn < -1 ||
             (PhysicalLcn >= 0 &&
              (ULONGLONG)PhysicalLcn >= Vcb->ClusterBitmap.SizeOfBitMap)))
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }
    }

    /*
     * Prepare the post-release mapping pairs before touching the live MCB or
     * allocation bitmap. LARGE_MCB stores physical runs only; conversion
     * materializes gaps as sparse mapping pairs.
     */
    PreparedVcnCount = LastVcn + 1 - ClustersToFree;
    _SEH2_TRY {
        FsRtlInitializeLargeMcb(&PreparedMCB, NonPagedPool);
    } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    } _SEH2_END;

    {
        ULONGLONG PreviousVcnEnd = 0;
        ULONG NumberOfRuns = FsRtlNumberOfRunsInLargeMcb(&AttrContext->DataRunsMCB);

        for (RunIndex = 0; RunIndex < NumberOfRuns; ++RunIndex)
        {
            LONGLONG Vbn, Lbn, Count;
            ULONGLONG CopyCount;
            ULONGLONG RunVcn, RunLcn, RunLength;

            if (!FsRtlGetNextLargeMcbEntry(&AttrContext->DataRunsMCB,
                                           RunIndex,
                                           &Vbn,
                                           &Lbn,
                                           &Count) ||
                Vbn < 0 || Lbn < -1 || Count <= 0 ||
                Count > MAXLONGLONG - Vbn ||
                (Lbn >= 0 && Count > MAXLONGLONG - Lbn))
            {
                FsRtlUninitializeLargeMcb(&PreparedMCB);
                return STATUS_FILE_CORRUPT_ERROR;
            }

            RunVcn = (ULONGLONG)Vbn;
            RunLcn = Lbn >= 0 ? (ULONGLONG)Lbn : 0;
            RunLength = (ULONGLONG)Count;

            /*
             * LARGE_MCB enumeration includes sparse gaps as runs with Lbn
             * equal to -1. Validate ordering and VCN bounds for every run,
             * but apply physical bitmap bounds only to backed extents. Sparse
             * gaps are recreated by leaving them between copied physical runs.
             */
            if (RunVcn < PreviousVcnEnd ||
                RunVcn > LastVcn ||
                RunLength > LastVcn + 1 - RunVcn ||
                (Lbn >= 0 &&
                 (RunLcn >= Vcb->ClusterBitmap.SizeOfBitMap ||
                  RunLength > (ULONGLONG)Vcb->ClusterBitmap.SizeOfBitMap - RunLcn)))
            {
                FsRtlUninitializeLargeMcb(&PreparedMCB);
                return STATUS_FILE_CORRUPT_ERROR;
            }

            PreviousVcnEnd = RunVcn + RunLength;

            if (RunVcn >= PreparedVcnCount || Lbn == -1)
                continue;

            CopyCount = min(RunLength, PreparedVcnCount - RunVcn);
            if (CopyCount != 0)
            {
                _SEH2_TRY {
                    if (!FsRtlAddLargeMcbEntry(&PreparedMCB,
                                               Vbn,
                                               Lbn,
                                               (LONGLONG)CopyCount))
                    {
                        ExRaiseStatus(STATUS_INSUFFICIENT_RESOURCES);
                    }
                } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
                    NTSTATUS ExceptionStatus = _SEH2_GetExceptionCode();
                    FsRtlUninitializeLargeMcb(&PreparedMCB);
                    _SEH2_YIELD(return ExceptionStatus);
                } _SEH2_END;
            }
        }
    }

    RunBuffer = ExAllocatePoolWithTag(NonPagedPool,
                                      Vcb->NtfsInfo.BytesPerFileRecord,
                                      TAG_NTFS);
    if (RunBuffer == NULL)
    {
        FsRtlUninitializeLargeMcb(&PreparedMCB);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = ConvertLargeMCBToDataRuns(&PreparedMCB,
                                       PreparedVcnCount,
                                       RunBuffer,
                                       Vcb->NtfsInfo.BytesPerFileRecord,
                                       &RunBufferSize);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(RunBuffer, TAG_NTFS);
        FsRtlUninitializeLargeMcb(&PreparedMCB);
        return Status;
    }

    if (RunBufferSize > DestinationAttribute->Length -
                        DestinationAttribute->NonResident.MappingPairsOffset)
    {
        ExFreePoolWithTag(RunBuffer, TAG_NTFS);
        FsRtlUninitializeLargeMcb(&PreparedMCB);
        return STATUS_FILE_CORRUPT_ERROR;
    }

    /* Same lock NtfsAllocateClusters() holds over the bitmap copy */
    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&Vcb->BitmapResource, TRUE);

    /*
     * A previous bitmap write may have partially reached disk.  Do not
     * persist another shortened file record when its cluster release cannot
     * safely be committed to the bitmap.
     */
    if (Vcb->Flags & VCB_BITMAP_WRITE_FAILED)
    {
        ExReleaseResourceLite(&Vcb->BitmapResource);
        KeLeaveCriticalRegion();
        ExFreePoolWithTag(RunBuffer, TAG_NTFS);
        FsRtlUninitializeLargeMcb(&PreparedMCB);
        return STATUS_DEVICE_DATA_ERROR;
    }

    /*
     * FreeClusters reads and edits the RTL bitmap directly while holding
     * BitmapResource.  Verify that the descriptor still refers to the
     * volume-owned buffer and that its declared bit range fits that buffer
     * before any bit test or clear can reach it.
     */
    if (Vcb->BitmapBuffer == NULL ||
        Vcb->BitmapSize == 0 ||
        Vcb->ClusterBitmap.Buffer != Vcb->BitmapBuffer ||
        Vcb->ClusterBitmap.SizeOfBitMap == 0 ||
        (ULONGLONG)Vcb->ClusterBitmap.SizeOfBitMap >
            (ULONGLONG)Vcb->BitmapSize * 8 ||
        Vcb->FreeClusterCount > Vcb->ClusterBitmap.SizeOfBitMap)
    {
        ExReleaseResourceLite(&Vcb->BitmapResource);
        KeLeaveCriticalRegion();
        ExFreePoolWithTag(RunBuffer, TAG_NTFS);
        FsRtlUninitializeLargeMcb(&PreparedMCB);
        return STATUS_FILE_CORRUPT_ERROR;
    }

    /*
     * The prepared mapping pairs must still describe the same stream extent.
     * Do not apply them if the attribute changed while the temporary MCB was
     * being built.
     */
    if (AttrContext->pRecord->NonResident.HighestVCN != LastVcn)
    {
        ExReleaseResourceLite(&Vcb->BitmapResource);
        KeLeaveCriticalRegion();
        ExFreePoolWithTag(RunBuffer, TAG_NTFS);
        FsRtlUninitializeLargeMcb(&PreparedMCB);
        return STATUS_FILE_CORRUPT_ERROR;
    }

    /*
     * Validate the allocation state for the complete release range while
     * holding BitmapResource.  A clear bit means this extent is stale or
     * overlaps an allocation that has already been released; clearing it
     * again would inflate FreeClusterCount and could hide metadata damage.
     * Do this as a separate pass so a bad bit cannot leave a partially
     * released stream.
     */
    VcnToCheck = AttrContext->pRecord->NonResident.HighestVCN;
    for (ClustersChecked = 0;
         ClustersChecked < ClustersToFree;
         ++ClustersChecked, --VcnToCheck)
    {
        LONGLONG PhysicalLcn;

        if (FsRtlLookupLargeMcbEntry(&AttrContext->DataRunsMCB,
                                     VcnToCheck,
                                     &PhysicalLcn,
                                     NULL, NULL, NULL, NULL) &&
            PhysicalLcn != -1)
        {
            if (!RtlCheckBit(&Vcb->ClusterBitmap, (ULONG)PhysicalLcn))
            {
                ExReleaseResourceLite(&Vcb->BitmapResource);
                KeLeaveCriticalRegion();
                ExFreePoolWithTag(RunBuffer, TAG_NTFS);
                FsRtlUninitializeLargeMcb(&PreparedMCB);
                return STATUS_FILE_CORRUPT_ERROR;
            }

            if (ClustersToRelease == MAXULONG)
            {
                ExReleaseResourceLite(&Vcb->BitmapResource);
                KeLeaveCriticalRegion();
                ExFreePoolWithTag(RunBuffer, TAG_NTFS);
                FsRtlUninitializeLargeMcb(&PreparedMCB);
                return STATUS_FILE_CORRUPT_ERROR;
            }

            ClustersToRelease++;
        }
    }

    /*
     * Validate accounting before clearing any bits.  A corrupt compressed
     * size or free-cluster counter must not wrap after the bitmap changes.
     */
    if (ClustersToRelease > MAXULONG - Vcb->FreeClusterCount ||
        ((AttrContext->pRecord->Flags &
          (NTFS_ATTR_COMPRESSION_MASK | NTFS_ATTR_SPARSE)) &&
         (ULONGLONG)ClustersToRelease >
             AttrContext->pRecord->NonResident.CompressedSize /
                 Vcb->NtfsInfo.BytesPerCluster))
    {
        ExReleaseResourceLite(&Vcb->BitmapResource);
        KeLeaveCriticalRegion();
        ExFreePoolWithTag(RunBuffer, TAG_NTFS);
        FsRtlUninitializeLargeMcb(&PreparedMCB);
        return STATUS_FILE_CORRUPT_ERROR;
    }

    /*
     * Persist the shortened file record before releasing its clusters.  If
     * the bitmap write subsequently fails, the worst on-disk outcome is an
     * allocated-but-unreferenced cluster (a leak); writing the bitmap first
     * could instead leave a still-referenced cluster marked free.
     *
     * UpdateFileRecord can itself fail after a partial device write, so this
     * ordering is a safety improvement, not an NTFS transaction or rollback.
     */

    /*
     * Keep the caller's complete record intact if the MFT commit fails.
     * The bitmap and live MCB have not been changed at this point.
     */
    OriginalFileRecord = ExAllocatePoolWithTag(
        NonPagedPool,
        Vcb->NtfsInfo.BytesPerFileRecord,
        TAG_NTFS);
    if (OriginalFileRecord == NULL)
    {
        ExReleaseResourceLite(&Vcb->BitmapResource);
        KeLeaveCriticalRegion();
        ExFreePoolWithTag(RunBuffer, TAG_NTFS);
        FsRtlUninitializeLargeMcb(&PreparedMCB);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlCopyMemory(OriginalFileRecord,
                  FileRecord,
                  Vcb->NtfsInfo.BytesPerFileRecord);

    /* Mapping pairs were encoded from PreparedMCB before live metadata changed. */
    DestinationAttribute->NonResident.HighestVCN =
        (LONGLONG)(PreparedVcnCount ? PreparedVcnCount - 1 : (ULONGLONG)-1);

    if (AttrContext->pRecord->Flags &
        (NTFS_ATTR_COMPRESSION_MASK | NTFS_ATTR_SPARSE))
    {
        DestinationAttribute->NonResident.CompressedSize =
            AttrContext->pRecord->NonResident.CompressedSize -
            (ULONGLONG)ClustersToRelease * Vcb->NtfsInfo.BytesPerCluster;
    }

    RtlCopyMemory((PVOID)((ULONG_PTR)DestinationAttribute +
                          DestinationAttribute->NonResident.MappingPairsOffset),
                  RunBuffer,
                  RunBufferSize);

    if (NextAttribute->Type == AttributeEnd)
    {
        DestinationAttribute->Length =
            ALIGN_UP_BY(AttrContext->pRecord->NonResident.MappingPairsOffset +
                        RunBufferSize,
                        ATTR_RECORD_ALIGNMENT);

        ASSERT(DestinationAttribute->Length <= AttrContext->pRecord->Length);
        NextAttribute = (PNTFS_ATTR_RECORD)((ULONG_PTR)DestinationAttribute +
                                           DestinationAttribute->Length);
        SetFileRecordEnd(FileRecord, NextAttribute, FILE_RECORD_END);
    }

    Status = UpdateFileRecord(Vcb, AttrContext->FileMFTIndex, FileRecord);
    if (!NT_SUCCESS(Status))
    {
        RtlCopyMemory(FileRecord,
                      OriginalFileRecord,
                      Vcb->NtfsInfo.BytesPerFileRecord);
        ExFreePoolWithTag(OriginalFileRecord, TAG_NTFS);
        ExReleaseResourceLite(&Vcb->BitmapResource);
        KeLeaveCriticalRegion();
        ExFreePoolWithTag(RunBuffer, TAG_NTFS);
        FsRtlUninitializeLargeMcb(&PreparedMCB);
        return Status;
    }

    ExFreePoolWithTag(OriginalFileRecord, TAG_NTFS);

    /*
     * The shortened record is now written.  Bring the cached attribute and
     * MCB into line, then persist the corresponding bitmap release.  A failed
     * bitmap write deliberately does not reattach these clusters: the file
     * record no longer references them, and retaining allocated bits on disk
     * is safe (though it can leak space).
     */
    VcnToCheck = LastVcn;
    for (ClustersChecked = 0;
         ClustersChecked < ClustersToFree;
         ++ClustersChecked, --VcnToCheck)
    {
        LONGLONG LargeLbn;

        if (FsRtlLookupLargeMcbEntry(&AttrContext->DataRunsMCB,
                                     VcnToCheck,
                                     &LargeLbn,
                                     NULL, NULL, NULL, NULL) &&
            LargeLbn != -1)
        {
            RtlClearBits(&Vcb->ClusterBitmap, (ULONG)LargeLbn, 1);
            LowestFreed = min(LowestFreed, (ULONG)LargeLbn);
            HighestFreed = max(HighestFreed, (ULONG)LargeLbn);
            Freed++;
        }

        FsRtlTruncateLargeMcb(&AttrContext->DataRunsMCB, VcnToCheck);
    }

    AttrContext->pRecord->NonResident.HighestVCN =
        PreparedVcnCount ? PreparedVcnCount - 1 : (ULONGLONG)-1;

    if (AttrContext->pRecord->Flags &
        (NTFS_ATTR_COMPRESSION_MASK | NTFS_ATTR_SPARSE))
    {
        AttrContext->pRecord->NonResident.CompressedSize =
            DestinationAttribute->NonResident.CompressedSize;
    }

    if (NextAttribute->Type == AttributeEnd)
        AttrContext->pRecord->Length = DestinationAttribute->Length;

    if (Freed != 0)
    {
        NTSTATUS WriteStatus;

        WriteStatus = NtfsWriteVolumeBitmap(Vcb,
                                            LowestFreed,
                                            HighestFreed - LowestFreed + 1);
        if (NT_SUCCESS(WriteStatus))
            Vcb->FreeClusterCount += Freed;
        else
            Status = WriteStatus;
    }

    ExReleaseResourceLite(&Vcb->BitmapResource);
    KeLeaveCriticalRegion();

    ExFreePoolWithTag(RunBuffer, TAG_NTFS);
    FsRtlUninitializeLargeMcb(&PreparedMCB);

#if NTFS_DUMP_DATA_RUNS
    NtfsDumpDataRuns((PUCHAR)((ULONG_PTR)DestinationAttribute + DestinationAttribute->NonResident.MappingPairsOffset), 0);
#endif

    return Status;
}

static
NTSTATUS
InternalReadNonResidentAttributes(PFIND_ATTR_CONTXT Context)
{
    ULONGLONG ListSize;
    PNTFS_ATTR_RECORD Attribute;
    PNTFS_ATTR_CONTEXT ListContext;

    DPRINT("InternalReadNonResidentAttributes(%p)\n", Context);

    Attribute = Context->CurrAttr;
    ASSERT(Attribute->Type == AttributeAttributeList);

    if (Context->OnlyResident)
    {
        Context->NonResidentStart = NULL;
        Context->NonResidentEnd = NULL;
        return STATUS_SUCCESS;
    }

    if (Context->NonResidentStart != NULL)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    ListContext = PrepareAttributeContext(Attribute);
    if (ListContext == NULL || ListContext->pRecord == NULL)
    {
        if (ListContext != NULL)
            ReleaseAttributeContext(ListContext);
        return STATUS_FILE_CORRUPT_ERROR;
    }

    ListSize = AttributeDataLength(ListContext->pRecord);
    if (ListSize < FIELD_OFFSET(NTFS_ATTRIBUTE_LIST_ITEM, Instance) +
                   sizeof(((PNTFS_ATTRIBUTE_LIST_ITEM)0)->Instance) ||
        (ListSize & (ATTR_RECORD_ALIGNMENT - 1)) != 0 ||
        (ListContext->pRecord->IsNonResident &&
         ListSize > AttributeAllocatedLength(ListContext->pRecord)))
    {
        ReleaseAttributeContext(ListContext);
        return STATUS_FILE_CORRUPT_ERROR;
    }

    if (ListSize > MAXULONG)
    {
        ReleaseAttributeContext(ListContext);
        return STATUS_BUFFER_OVERFLOW;
    }

    Context->NonResidentStart = ExAllocatePoolWithTag(NonPagedPool, (ULONG)ListSize, TAG_NTFS);
    if (Context->NonResidentStart == NULL)
    {
        ReleaseAttributeContext(ListContext);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    if (ReadAttribute(Context->Vcb, ListContext, 0, (PCHAR)Context->NonResidentStart, (ULONG)ListSize) != ListSize)
    {
        ExFreePoolWithTag(Context->NonResidentStart, TAG_NTFS);
        Context->NonResidentStart = NULL;
        ReleaseAttributeContext(ListContext);
        return STATUS_FILE_CORRUPT_ERROR;
    }

    ReleaseAttributeContext(ListContext);
    Context->NonResidentEnd = (PNTFS_ATTRIBUTE_LIST_ITEM)((PCHAR)Context->NonResidentStart + ListSize);
    return STATUS_SUCCESS;
}

/*
 * Attribute-list entries are variable length.  Validate the fixed header
 * and optional UTF-16 name against the containing attribute-list buffer
 * before any caller dereferences the entry.
 */
static
BOOLEAN
InternalIsValidAttributeListItem(PFIND_ATTR_CONTXT Context,
                                 PNTFS_ATTRIBUTE_LIST_ITEM Item)
{
    ULONG HeaderLength = FIELD_OFFSET(NTFS_ATTRIBUTE_LIST_ITEM, Instance) +
                         sizeof(Item->Instance);
    ULONG NameEnd;

    if ((PCHAR)Item < (PCHAR)Context->NonResidentStart ||
        (PCHAR)Item > (PCHAR)Context->NonResidentEnd ||
        (ULONG_PTR)Context->NonResidentEnd - (ULONG_PTR)Item < HeaderLength)
    {
        return FALSE;
    }

    if (Item->Length < HeaderLength ||
        (Item->Length & (ATTR_RECORD_ALIGNMENT - 1)) != 0 ||
        Item->Length > (ULONG_PTR)Context->NonResidentEnd - (ULONG_PTR)Item)
    {
        return FALSE;
    }

    if (Item->NameLength != 0)
    {
        if (Item->NameOffset < HeaderLength ||
            (Item->NameOffset & (sizeof(WCHAR) - 1)) != 0)
            return FALSE;

        NameEnd = (ULONG)Item->NameOffset +
                  ((ULONG)Item->NameLength * sizeof(WCHAR));
        if (NameEnd > Item->Length)
            return FALSE;
    }
    else if (Item->NameOffset != 0)
    {
        /* An unnamed attribute-list entry has no name payload or offset. */
        return FALSE;
    }

    return TRUE;
}

static
BOOLEAN
InternalIsAttributeListItemOrdered(PFIND_ATTR_CONTXT Context,
                                   PNTFS_ATTRIBUTE_LIST_ITEM Previous,
                                   PNTFS_ATTRIBUTE_LIST_ITEM Current)
{
    PWCHAR PreviousName;
    PWCHAR CurrentName;
    ULONG CommonLength;
    ULONG i;

    if (Previous == NULL)
        return TRUE;

    if (Current->Type < Previous->Type)
        return FALSE;

    if (Current->Type != Previous->Type)
        return TRUE;

    /*
     * NTFS sorts attribute names using the volume's $UpCase table, then
     * sorts extents of the same named attribute by StartingVCN.  The table
     * is not available while the volume is initially loading $UpCase; in
     * that case retain the conservative raw-name check used by the loader.
     */
    PreviousName = (PWCHAR)((PCHAR)Previous + Previous->NameOffset);
    CurrentName = (PWCHAR)((PCHAR)Current + Current->NameOffset);
    CommonLength = min(Previous->NameLength, Current->NameLength);

    if (Context->Vcb->UpCaseTable != NULL)
    {
        for (i = 0; i < CommonLength; ++i)
        {
            WCHAR PreviousChar = Context->Vcb->UpCaseTable[PreviousName[i]];
            WCHAR CurrentChar = Context->Vcb->UpCaseTable[CurrentName[i]];

            if (CurrentChar < PreviousChar)
                return FALSE;
            if (CurrentChar > PreviousChar)
                return TRUE;
        }

        if (Current->NameLength != Previous->NameLength)
            return Current->NameLength > Previous->NameLength;
    }
    else
    {
        /*
         * During $UpCase bootstrap, retain deterministic ordinal UTF-16
         * ordering.  Accepting every different same-length name here would
         * allow an arbitrarily shuffled attribute list to pass validation.
         */
        for (i = 0; i < CommonLength; ++i)
        {
            if (CurrentName[i] < PreviousName[i])
                return FALSE;
            if (CurrentName[i] > PreviousName[i])
                return TRUE;
        }

        if (Current->NameLength != Previous->NameLength)
            return Current->NameLength > Previous->NameLength;
    }

    return Current->StartingVCN > Previous->StartingVCN;
}

static
PNTFS_ATTRIBUTE_LIST_ITEM
InternalGetNextAttributeListItem(PFIND_ATTR_CONTXT Context)
{
    PNTFS_ATTRIBUTE_LIST_ITEM NextItem;

    if (Context->NonResidentCur == (PVOID)-1)
        return NULL;

    if (Context->NonResidentCur == NULL ||
        !InternalIsValidAttributeListItem(Context, Context->NonResidentCur))
    {
        Context->NonResidentCorrupt = TRUE;
        Context->NonResidentCur = (PVOID)-1;
        return NULL;
    }

    NextItem = (PNTFS_ATTRIBUTE_LIST_ITEM)
        ((PCHAR)Context->NonResidentCur + Context->NonResidentCur->Length);

    if ((PCHAR)NextItem == (PCHAR)Context->NonResidentEnd)
    {
        Context->NonResidentCur = (PVOID)-1;
        return NULL;
    }

    if ((PCHAR)NextItem > (PCHAR)Context->NonResidentEnd ||
        (ULONG_PTR)Context->NonResidentEnd - (ULONG_PTR)NextItem <
            sizeof(NextItem->Type))
    {
        Context->NonResidentCorrupt = TRUE;
        Context->NonResidentCur = (PVOID)-1;
        return NULL;
    }

    /*
     * $ATTRIBUTE_LIST is a packed sequence of length-delimited entries; unlike
     * a file-record attribute array it has no AttributeEnd sentinel.  Treat an
     * end marker inside the value as corruption instead of silently truncating
     * the list and hiding later entries.
     */
    if (NextItem->Type == AttributeEnd)
    {
        Context->NonResidentCorrupt = TRUE;
        Context->NonResidentCur = (PVOID)-1;
        return NULL;
    }

    if (!InternalIsValidAttributeListItem(Context, NextItem))
    {
        DPRINT1("Malformed NTFS attribute-list entry\n");
        Context->NonResidentCorrupt = TRUE;
        Context->NonResidentCur = (PVOID)-1;
        return NULL;
    }

    if (!InternalIsAttributeListItemOrdered(Context,
                                            Context->NonResidentPrevious,
                                            NextItem))
    {
        DPRINT1("Out-of-order NTFS attribute-list entry\n");
        Context->NonResidentCorrupt = TRUE;
        Context->NonResidentCur = (PVOID)-1;
        return NULL;
    }

    Context->NonResidentPrevious = Context->NonResidentCur;
    Context->NonResidentCur = NextItem;
    return NextItem;
}

NTSTATUS
FindFirstAttributeListItem(PFIND_ATTR_CONTXT Context,
                           PNTFS_ATTRIBUTE_LIST_ITEM *Item)
{
    Context->NonResidentCorrupt = FALSE;

    if (Context->NonResidentStart == NULL ||
        (PCHAR)Context->NonResidentStart >= (PCHAR)Context->NonResidentEnd)
    {
        *Item = NULL;
        return STATUS_UNSUCCESSFUL;
    }

    /* A loaded attribute-list value must begin with a real list entry. */
    if (Context->NonResidentStart->Type == AttributeEnd)
    {
        Context->NonResidentCorrupt = TRUE;
        *Item = NULL;
        return STATUS_FILE_CORRUPT_ERROR;
    }

    if (!InternalIsValidAttributeListItem(Context, Context->NonResidentStart))
    {
        Context->NonResidentCorrupt = TRUE;
        *Item = NULL;
        return STATUS_FILE_CORRUPT_ERROR;
    }

    Context->NonResidentPrevious = NULL;
    Context->NonResidentCur = Context->NonResidentStart;
    *Item = Context->NonResidentCur;
    return STATUS_SUCCESS;
}

NTSTATUS
FindNextAttributeListItem(PFIND_ATTR_CONTXT Context,
                          PNTFS_ATTRIBUTE_LIST_ITEM *Item)
{
    *Item = InternalGetNextAttributeListItem(Context);
    if (*Item == NULL)
    {
        return Context->NonResidentCorrupt ?
               STATUS_FILE_CORRUPT_ERROR : STATUS_UNSUCCESSFUL;
    }
    return STATUS_SUCCESS;
}

/*
 * Validate an attribute's complete on-record envelope before any caller reads
 * the resident/nonresident union or forms a pointer into its payload.
 */
static
BOOLEAN
NtfsIsValidAttributeRecord(
    _In_ PNTFS_ATTR_RECORD Attribute,
    _In_ ULONG BytesRemaining)
{
    ULONG HeaderSize;
    ULONG NameEnd;

    if (Attribute == NULL ||
        BytesRemaining < FIELD_OFFSET(NTFS_ATTR_RECORD, Resident.ValueLength) ||
        Attribute->Length < FIELD_OFFSET(NTFS_ATTR_RECORD, Resident.ValueLength) ||
        Attribute->Length > BytesRemaining ||
        (Attribute->Length & (ATTR_RECORD_ALIGNMENT - 1)) != 0 ||
        Attribute->IsNonResident > 1)
    {
        return FALSE;
    }

    if (Attribute->IsNonResident)
    {
        /* CompressedSize is optional for ordinary nonresident attributes. */
        HeaderSize =
            FIELD_OFFSET(NTFS_ATTR_RECORD, NonResident.InitializedSize) +
            sizeof(Attribute->NonResident.InitializedSize);

        if (Attribute->Flags & (NTFS_ATTR_COMPRESSION_MASK | NTFS_ATTR_SPARSE))
        {
            HeaderSize =
                FIELD_OFFSET(NTFS_ATTR_RECORD, NonResident.CompressedSize) +
                sizeof(Attribute->NonResident.CompressedSize);
        }
    }
    else
    {
        HeaderSize =
            FIELD_OFFSET(NTFS_ATTR_RECORD, Resident.Reserved) +
            sizeof(Attribute->Resident.Reserved);
    }

    if (Attribute->Length < HeaderSize)
        return FALSE;

    if (Attribute->NameLength != 0)
    {
        if (Attribute->NameOffset < HeaderSize ||
            (Attribute->NameOffset & (sizeof(WCHAR) - 1)) != 0)
            return FALSE;

        NameEnd = (ULONG)Attribute->NameOffset +
                  (ULONG)Attribute->NameLength * sizeof(WCHAR);
        if (NameEnd > Attribute->Length)
            return FALSE;
    }

    if (!Attribute->IsNonResident)
    {
        if (Attribute->Resident.ValueOffset < HeaderSize ||
            Attribute->Resident.ValueOffset > Attribute->Length ||
            Attribute->Resident.ValueLength >
                Attribute->Length - Attribute->Resident.ValueOffset)
            return FALSE;
    }
    else
    {
        if (Attribute->NonResident.MappingPairsOffset < HeaderSize ||
            Attribute->NonResident.MappingPairsOffset >= Attribute->Length ||
            (Attribute->NonResident.HighestVCN != (ULONGLONG)-1 &&
             Attribute->NonResident.HighestVCN <
                 Attribute->NonResident.LowestVCN))
            return FALSE;
    }

    return TRUE;
}

static
PNTFS_ATTR_RECORD
InternalGetNextAttribute(PFIND_ATTR_CONTXT Context)
{
    PNTFS_ATTR_RECORD NextAttribute;

    if (Context->CurrAttr == (PVOID)-1)
    {
        return NULL;
    }

    if (Context->CurrAttr >= Context->FirstAttr &&
        Context->CurrAttr < Context->LastAttr)
    {
        if (!NtfsIsValidAttributeRecord(
                Context->CurrAttr,
                (ULONG)((ULONG_PTR)Context->LastAttr -
                        (ULONG_PTR)Context->CurrAttr)))
        {
            DPRINT1("Malformed NTFS attribute record\\n");
            Context->NonResidentCorrupt = TRUE;
            Context->CurrAttr = (PVOID)-1;
            return NULL;
        }

        NextAttribute = (PNTFS_ATTR_RECORD)
            ((PCHAR)Context->CurrAttr + Context->CurrAttr->Length);

        Context->Offset += Context->CurrAttr->Length;
        Context->CurrAttr = NextAttribute;

        if (Context->CurrAttr < Context->LastAttr)
        {
            if ((ULONG_PTR)Context->LastAttr - (ULONG_PTR)Context->CurrAttr <
                sizeof(Context->CurrAttr->Type))
            {
                DPRINT1("Truncated attribute type at end of file record!\n");
                Context->NonResidentCorrupt = TRUE;
                Context->CurrAttr = (PVOID)-1;
                return NULL;
            }

            if (Context->CurrAttr->Type != AttributeEnd)
                return Context->CurrAttr;
        }
    }

    if (Context->NonResidentStart == NULL)
    {
        Context->CurrAttr = (PVOID)-1;
        return NULL;
    }

    Context->CurrAttr = (PVOID)-1;
    return NULL;
}

NTSTATUS
FindFirstAttribute(PFIND_ATTR_CONTXT Context,
                   PDEVICE_EXTENSION Vcb,
                   PFILE_RECORD_HEADER FileRecord,
                   BOOLEAN OnlyResident,
                   PNTFS_ATTR_RECORD * Attribute)
{
    NTSTATUS Status;
    PNTFS_ATTR_RECORD ScanAttribute;
    PNTFS_ATTR_RECORD FirstAttribute;

    DPRINT("FindFirstAttribute(%p, %p, %p, %u, %p)\n", Context, Vcb, FileRecord, OnlyResident, Attribute);

    if (Context == NULL || Vcb == NULL || FileRecord == NULL || Attribute == NULL)
        return STATUS_INVALID_PARAMETER;

    *Attribute = NULL;

    /*
     * BytesInUse is the upper bound used by the attribute walker. Validate
     * the record's size fields and attribute offset before deriving pointers.
     */
    if (Vcb->NtfsInfo.BytesPerFileRecord < sizeof(FILE_RECORD_HEADER) ||
        FileRecord->BytesAllocated > Vcb->NtfsInfo.BytesPerFileRecord ||
        FileRecord->BytesAllocated < sizeof(FILE_RECORD_HEADER) ||
        FileRecord->BytesInUse > FileRecord->BytesAllocated ||
        FileRecord->BytesInUse < sizeof(FILE_RECORD_HEADER) ||
        FileRecord->AttributeOffset < sizeof(FILE_RECORD_HEADER) ||
        (FileRecord->AttributeOffset & (ATTR_RECORD_ALIGNMENT - 1)) != 0 ||
        FileRecord->AttributeOffset > FileRecord->BytesInUse ||
        FileRecord->BytesInUse - FileRecord->AttributeOffset < sizeof(ULONG))
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    Context->Vcb = Vcb;
    Context->OnlyResident = OnlyResident;
    Context->FirstAttr = (PNTFS_ATTR_RECORD)((ULONG_PTR)FileRecord + FileRecord->AttributeOffset);
    Context->CurrAttr = Context->FirstAttr;
    Context->LastAttr = (PNTFS_ATTR_RECORD)((ULONG_PTR)FileRecord + FileRecord->BytesInUse);
    Context->NonResidentStart = NULL;
    Context->NonResidentEnd = NULL;
    Context->NonResidentCur = NULL;
    Context->NonResidentPrevious = NULL;
    Context->NonResidentCorrupt = FALSE;
    Context->Offset = FileRecord->AttributeOffset;

    FirstAttribute = Context->FirstAttr;
    if (FirstAttribute >= Context->LastAttr ||
        (ULONG_PTR)Context->LastAttr - (ULONG_PTR)FirstAttribute <
            sizeof(FirstAttribute->Type))
    {
        Context->CurrAttr = (PVOID)-1;
        *Attribute = NULL;
        return STATUS_FILE_CORRUPT_ERROR;
    }

    if (FirstAttribute->Type == AttributeEnd)
    {
        Context->CurrAttr = (PVOID)-1;
        *Attribute = NULL;
        return STATUS_END_OF_FILE;
    }

    /*
     * $ATTRIBUTE_LIST sorts after $STANDARD_INFORMATION.  Locate it by
     * walking the complete local attribute sequence rather than assuming
     * it is the first record attribute.
     */
    if (!OnlyResident)
    {
        ScanAttribute = FirstAttribute;
        while (ScanAttribute < Context->LastAttr)
        {
            if ((ULONG_PTR)Context->LastAttr - (ULONG_PTR)ScanAttribute <
                sizeof(ScanAttribute->Type))
            {
                Context->CurrAttr = (PVOID)-1;
                return STATUS_FILE_CORRUPT_ERROR;
            }

            if (ScanAttribute->Type == AttributeEnd)
                break;

            if (!NtfsIsValidAttributeRecord(
                    ScanAttribute,
                    (ULONG)((ULONG_PTR)Context->LastAttr -
                            (ULONG_PTR)ScanAttribute)))
            {
                Context->CurrAttr = (PVOID)-1;
                return STATUS_FILE_CORRUPT_ERROR;
            }

            if (ScanAttribute->Type == AttributeAttributeList)
            {
                Context->CurrAttr = ScanAttribute;
                Status = InternalReadNonResidentAttributes(Context);
                if (!NT_SUCCESS(Status))
                {
                    Context->CurrAttr = (PVOID)-1;
                    return Status;
                }
                break;
            }

            ScanAttribute = (PNTFS_ATTR_RECORD)((PCHAR)ScanAttribute + ScanAttribute->Length);
        }
    }

    if (!NtfsIsValidAttributeRecord(
            FirstAttribute,
            (ULONG)((ULONG_PTR)Context->LastAttr -
                    (ULONG_PTR)FirstAttribute)))
    {
        Context->CurrAttr = (PVOID)-1;
        *Attribute = NULL;
        return STATUS_FILE_CORRUPT_ERROR;
    }

    Context->CurrAttr = FirstAttribute;
    Context->Offset = (ULONG)((PCHAR)FirstAttribute - (PCHAR)FileRecord);
    *Attribute = FirstAttribute;
    return STATUS_SUCCESS;
}

NTSTATUS
FindNextAttribute(PFIND_ATTR_CONTXT Context,
                  PNTFS_ATTR_RECORD * Attribute)
{
    DPRINT("FindNextAttribute(%p, %p)\n", Context, Attribute);

    *Attribute = InternalGetNextAttribute(Context);
    if (*Attribute == NULL)
    {
        return Context->NonResidentCorrupt ?
               STATUS_FILE_CORRUPT_ERROR : STATUS_END_OF_FILE;
    }

    return STATUS_SUCCESS;
}

VOID
FindCloseAttribute(PFIND_ATTR_CONTXT Context)
{
    if (Context->NonResidentStart != NULL)
    {
        ExFreePoolWithTag(Context->NonResidentStart, TAG_NTFS);
        Context->NonResidentStart = NULL;
    }
}

static
VOID
NtfsDumpFileNameAttribute(PNTFS_ATTR_RECORD Attribute)
{
    PFILENAME_ATTRIBUTE FileNameAttr;

    DbgPrint("  $FILE_NAME ");

//    DbgPrint(" Length %lu  Offset %hu ", Attribute->Resident.ValueLength, Attribute->Resident.ValueOffset);

    FileNameAttr = (PFILENAME_ATTRIBUTE)((ULONG_PTR)Attribute + Attribute->Resident.ValueOffset);
    DbgPrint(" (%x) '%.*S' ", FileNameAttr->NameType, FileNameAttr->NameLength, FileNameAttr->Name);
    DbgPrint(" '%x' \n", FileNameAttr->FileAttributes);
    DbgPrint(" AllocatedSize: %I64u\nDataSize: %I64u\n", FileNameAttr->AllocatedSize, FileNameAttr->DataSize);
    DbgPrint(" File reference: 0x%016I64x\n", FileNameAttr->DirectoryFileReferenceNumber);
}


static
VOID
NtfsDumpStandardInformationAttribute(PNTFS_ATTR_RECORD Attribute)
{
    PSTANDARD_INFORMATION StandardInfoAttr;

    DbgPrint("  $STANDARD_INFORMATION ");

//    DbgPrint(" Length %lu  Offset %hu ", Attribute->Resident.ValueLength, Attribute->Resident.ValueOffset);

    StandardInfoAttr = (PSTANDARD_INFORMATION)((ULONG_PTR)Attribute + Attribute->Resident.ValueOffset);
    DbgPrint(" '%x' ", StandardInfoAttr->FileAttribute);
}


static
VOID
NtfsDumpVolumeNameAttribute(PNTFS_ATTR_RECORD Attribute)
{
    PWCHAR VolumeName;

    DbgPrint("  $VOLUME_NAME ");

//    DbgPrint(" Length %lu  Offset %hu ", Attribute->Resident.ValueLength, Attribute->Resident.ValueOffset);

    VolumeName = (PWCHAR)((ULONG_PTR)Attribute + Attribute->Resident.ValueOffset);
    DbgPrint(" '%.*S' ", Attribute->Resident.ValueLength / sizeof(WCHAR), VolumeName);
}


static
VOID
NtfsDumpVolumeInformationAttribute(PNTFS_ATTR_RECORD Attribute)
{
    PVOLINFO_ATTRIBUTE VolInfoAttr;

    DbgPrint("  $VOLUME_INFORMATION ");

//    DbgPrint(" Length %lu  Offset %hu ", Attribute->Resident.ValueLength, Attribute->Resident.ValueOffset);

    VolInfoAttr = (PVOLINFO_ATTRIBUTE)((ULONG_PTR)Attribute + Attribute->Resident.ValueOffset);
    DbgPrint(" NTFS Version %u.%u  Flags 0x%04hx ",
             VolInfoAttr->MajorVersion,
             VolInfoAttr->MinorVersion,
             VolInfoAttr->Flags);
}


static
VOID
NtfsDumpIndexRootAttribute(PNTFS_ATTR_RECORD Attribute)
{
    PINDEX_ROOT_ATTRIBUTE IndexRootAttr;
    ULONG CurrentOffset;
    ULONG CurrentNode;

    IndexRootAttr = (PINDEX_ROOT_ATTRIBUTE)((ULONG_PTR)Attribute + Attribute->Resident.ValueOffset);

    if (IndexRootAttr->AttributeType == AttributeFileName)
        ASSERT(IndexRootAttr->CollationRule == COLLATION_FILE_NAME);

    DbgPrint("  $INDEX_ROOT (%u bytes per index record, %u clusters) ", IndexRootAttr->SizeOfEntry, IndexRootAttr->ClustersPerIndexRecord);

    if (IndexRootAttr->Header.Flags == INDEX_ROOT_SMALL)
    {
        DbgPrint(" (small)\n");
    }
    else
    {
        ASSERT(IndexRootAttr->Header.Flags == INDEX_ROOT_LARGE);
        DbgPrint(" (large)\n");
    }

    DbgPrint("   Offset to first index: 0x%lx\n   Total size of index entries: 0x%lx\n   Allocated size of node: 0x%lx\n",
             IndexRootAttr->Header.FirstEntryOffset,
             IndexRootAttr->Header.TotalSizeOfEntries,
             IndexRootAttr->Header.AllocatedSize);
    CurrentOffset = IndexRootAttr->Header.FirstEntryOffset;
    CurrentNode = 0;
    // print details of every node in the index
    while (CurrentOffset < IndexRootAttr->Header.TotalSizeOfEntries)
    {
        PINDEX_ENTRY_ATTRIBUTE currentIndexExtry = (PINDEX_ENTRY_ATTRIBUTE)((ULONG_PTR)IndexRootAttr + 0x10 + CurrentOffset);
        DbgPrint("   Index Node Entry %lu", CurrentNode++);
        if (BooleanFlagOn(currentIndexExtry->Flags, NTFS_INDEX_ENTRY_NODE))
            DbgPrint(" (Branch)");
        else
            DbgPrint(" (Leaf)");
        if (BooleanFlagOn(currentIndexExtry->Flags, NTFS_INDEX_ENTRY_END))
        {
            DbgPrint(" (Dummy Key)");
        }
        DbgPrint("\n    File Reference: 0x%016I64x\n", currentIndexExtry->Data.Directory.IndexedFile);
        DbgPrint("    Index Entry Length: 0x%x\n", currentIndexExtry->Length);
        DbgPrint("    Index Key Length: 0x%x\n", currentIndexExtry->KeyLength);

        // if this isn't the final (dummy) node, print info about the key (Filename attribute)
        if (!(currentIndexExtry->Flags & NTFS_INDEX_ENTRY_END))
        {
            UNICODE_STRING Name;
            DbgPrint("     Parent File Reference: 0x%016I64x\n", currentIndexExtry->FileName.DirectoryFileReferenceNumber);
            DbgPrint("     $FILENAME indexed: ");
            Name.Length = currentIndexExtry->FileName.NameLength * sizeof(WCHAR);
            Name.MaximumLength = Name.Length;
            Name.Buffer = currentIndexExtry->FileName.Name;
            DbgPrint("'%wZ'\n", &Name);
        }

        // if this node has a sub-node beneath it
        if (currentIndexExtry->Flags & NTFS_INDEX_ENTRY_NODE)
        {
            // Print the VCN of the sub-node
            PULONGLONG SubNodeVCN = (PULONGLONG)((ULONG_PTR)currentIndexExtry + currentIndexExtry->Length - sizeof(ULONGLONG));
            DbgPrint("    VCN of sub-node: 0x%llx\n", *SubNodeVCN);
        }

        CurrentOffset += currentIndexExtry->Length;
        ASSERT(currentIndexExtry->Length);
    }

}


static
VOID
NtfsDumpAttribute(PDEVICE_EXTENSION Vcb,
                  PNTFS_ATTR_RECORD Attribute)
{
    UNICODE_STRING Name;

    ULONGLONG lcn = 0;
    ULONGLONG runcount = 0;

    switch (Attribute->Type)
    {
        case AttributeFileName:
            NtfsDumpFileNameAttribute(Attribute);
            break;

        case AttributeStandardInformation:
            NtfsDumpStandardInformationAttribute(Attribute);
            break;

        case AttributeObjectId:
            DbgPrint("  $OBJECT_ID ");
            break;

        case AttributeSecurityDescriptor:
            DbgPrint("  $SECURITY_DESCRIPTOR ");
            break;

        case AttributeVolumeName:
            NtfsDumpVolumeNameAttribute(Attribute);
            break;

        case AttributeVolumeInformation:
            NtfsDumpVolumeInformationAttribute(Attribute);
            break;

        case AttributeData:
            DbgPrint("  $DATA ");
            //DataBuf = ExAllocatePool(NonPagedPool,AttributeLengthAllocated(Attribute));
            break;

        case AttributeIndexRoot:
            NtfsDumpIndexRootAttribute(Attribute);
            break;

        case AttributeIndexAllocation:
            DbgPrint("  $INDEX_ALLOCATION ");
            break;

        case AttributeBitmap:
            DbgPrint("  $BITMAP ");
            break;

        case AttributeReparsePoint:
            DbgPrint("  $REPARSE_POINT ");
            break;

        case AttributeEAInformation:
            DbgPrint("  $EA_INFORMATION ");
            break;

        case AttributeEA:
            DbgPrint("  $EA ");
            break;

        case AttributePropertySet:
            DbgPrint("  $PROPERTY_SET ");
            break;

        case AttributeLoggedUtilityStream:
            DbgPrint("  $LOGGED_UTILITY_STREAM ");
            break;

        default:
            DbgPrint("  Attribute %lx ",
                     Attribute->Type);
            break;
    }

    if (Attribute->Type != AttributeAttributeList)
    {
        if (Attribute->NameLength != 0)
        {
            Name.Length = Attribute->NameLength * sizeof(WCHAR);
            Name.MaximumLength = Name.Length;
            Name.Buffer = (PWCHAR)((ULONG_PTR)Attribute + Attribute->NameOffset);

            DbgPrint("'%wZ' ", &Name);
        }

        DbgPrint("(%s)\n",
                 Attribute->IsNonResident ? "non-resident" : "resident");

        if (Attribute->IsNonResident)
        {
            FindRun(Attribute,0,&lcn, &runcount);

            DbgPrint("  AllocatedSize %I64u  DataSize %I64u InitilizedSize %I64u\n",
                     Attribute->NonResident.AllocatedSize, Attribute->NonResident.DataSize, Attribute->NonResident.InitializedSize);
            DbgPrint("  logical clusters: %I64u - %I64u\n",
                     lcn, lcn + runcount - 1);
        }
        else
            DbgPrint("    %u bytes of data\n", Attribute->Resident.ValueLength);
    }
}


VOID NtfsDumpDataRunData(PUCHAR DataRun)
{
    UCHAR DataRunOffsetSize;
    UCHAR DataRunLengthSize;
    CHAR i;

    DbgPrint("%02x ", *DataRun);

    if (*DataRun == 0)
        return;

    DataRunOffsetSize = (*DataRun >> 4) & 0xF;
    DataRunLengthSize = *DataRun & 0xF;

    DataRun++;
    for (i = 0; i < DataRunLengthSize; i++)
    {
        DbgPrint("%02x ", *DataRun);
        DataRun++;
    }

    for (i = 0; i < DataRunOffsetSize; i++)
    {
        DbgPrint("%02x ", *DataRun);
        DataRun++;
    }

    NtfsDumpDataRunData(DataRun);
}


VOID
NtfsDumpDataRuns(PVOID StartOfRun,
                 ULONGLONG CurrentLCN)
{
    PUCHAR DataRun = StartOfRun;
    LONGLONG DataRunOffset;
    ULONGLONG DataRunLength;

    if (CurrentLCN == 0)
    {
        DPRINT("Dumping data runs.\n\tData:\n\t\t");
        NtfsDumpDataRunData(StartOfRun);
        DbgPrint("\n\tRuns:\n\t\tOff\t\tLCN\t\tLength\n");
    }

    DataRun = DecodeRun(DataRun, &DataRunOffset, &DataRunLength);

    if (DataRunOffset != -1)
        CurrentLCN += DataRunOffset;

    DbgPrint("\t\t%I64d\t", DataRunOffset);
    if (DataRunOffset < 99999)
        DbgPrint("\t");
    DbgPrint("%I64u\t", CurrentLCN);
    if (CurrentLCN < 99999)
        DbgPrint("\t");
    DbgPrint("%I64u\n", DataRunLength);

    if (*DataRun == 0)
        DbgPrint("\t\t00\n");
    else
        NtfsDumpDataRuns(DataRun, CurrentLCN);
}


VOID
NtfsDumpFileAttributes(PDEVICE_EXTENSION Vcb,
                       PFILE_RECORD_HEADER FileRecord)
{
    NTSTATUS Status;
    FIND_ATTR_CONTXT Context;
    PNTFS_ATTR_RECORD Attribute;

    Status = FindFirstAttribute(&Context, Vcb, FileRecord, FALSE, &Attribute);
    while (NT_SUCCESS(Status))
    {
        NtfsDumpAttribute(Vcb, Attribute);

        Status = FindNextAttribute(&Context, &Attribute);
    }

    FindCloseAttribute(&Context);
}

PFILENAME_ATTRIBUTE
GetFileNameFromRecord(PDEVICE_EXTENSION Vcb,
                      PFILE_RECORD_HEADER FileRecord,
                      UCHAR NameType)
{
    FIND_ATTR_CONTXT Context;
    PNTFS_ATTR_RECORD Attribute;
    PFILENAME_ATTRIBUTE Name;
    NTSTATUS Status;

    Status = FindFirstAttribute(&Context, Vcb, FileRecord, FALSE, &Attribute);
    while (NT_SUCCESS(Status))
    {
        if (Attribute->Type == AttributeFileName)
        {
            Name = (PFILENAME_ATTRIBUTE)((ULONG_PTR)Attribute + Attribute->Resident.ValueOffset);
            if (Name->NameType == NameType ||
                (Name->NameType == NTFS_FILE_NAME_WIN32_AND_DOS && NameType == NTFS_FILE_NAME_WIN32) ||
                (Name->NameType == NTFS_FILE_NAME_WIN32_AND_DOS && NameType == NTFS_FILE_NAME_DOS))
            {
                FindCloseAttribute(&Context);
                return Name;
            }
        }

        Status = FindNextAttribute(&Context, &Attribute);
    }

    FindCloseAttribute(&Context);
    return NULL;
}

/**
* GetPackedByteCount
* Returns the minimum number of bytes needed to represent the value of a
* 64-bit number. Used to encode data runs.
*/
UCHAR
GetPackedByteCount(LONGLONG NumberToPack,
                   BOOLEAN IsSigned)
{
    if (!IsSigned)
    {
        if (NumberToPack >= 0x0100000000000000)
            return 8;
        if (NumberToPack >= 0x0001000000000000)
            return 7;
        if (NumberToPack >= 0x0000010000000000)
            return 6;
        if (NumberToPack >= 0x0000000100000000)
            return 5;
        if (NumberToPack >= 0x0000000001000000)
            return 4;
        if (NumberToPack >= 0x0000000000010000)
            return 3;
        if (NumberToPack >= 0x0000000000000100)
            return 2;
        return 1;
    }

    if (NumberToPack > 0)
    {
        // we have to make sure the number that gets encoded won't be interpreted as negative
        if (NumberToPack >= 0x0080000000000000)
            return 8;
        if (NumberToPack >= 0x0000800000000000)
            return 7;
        if (NumberToPack >= 0x0000008000000000)
            return 6;
        if (NumberToPack >= 0x0000000080000000)
            return 5;
        if (NumberToPack >= 0x0000000000800000)
            return 4;
        if (NumberToPack >= 0x0000000000008000)
            return 3;
        if (NumberToPack >= 0x0000000000000080)
            return 2;
    }
    else
    {
        // negative number
        if (NumberToPack <= 0xff80000000000000)
            return 8;
        if (NumberToPack <= 0xffff800000000000)
            return 7;
        if (NumberToPack <= 0xffffff8000000000)
            return 6;
        if (NumberToPack <= 0xffffffff80000000)
            return 5;
        if (NumberToPack <= 0xffffffffff800000)
            return 4;
        if (NumberToPack <= 0xffffffffffff8000)
            return 3;
        if (NumberToPack <= 0xffffffffffffff80)
            return 2;
    }
    return 1;
}

NTSTATUS
GetLastClusterInDataRun(PDEVICE_EXTENSION Vcb, PNTFS_ATTR_RECORD Attribute, PULONGLONG LastCluster)
{
    LONGLONG DataRunOffset;
    ULONGLONG DataRunLength;
    LONGLONG DataRunStartLCN;

    ULONGLONG LastLCN = 0;
    PUCHAR DataRun = (PUCHAR)Attribute + Attribute->NonResident.MappingPairsOffset;

    if (!Attribute->IsNonResident)
        return STATUS_INVALID_PARAMETER;

    while (1)
    {
        DataRun = DecodeRun(DataRun, &DataRunOffset, &DataRunLength);

        if (DataRunOffset != -1)
        {
            // Normal data run.
            DataRunStartLCN = LastLCN + DataRunOffset;
            LastLCN = DataRunStartLCN;
            *LastCluster = LastLCN + DataRunLength - 1;
        }

        if (*DataRun == 0)
            break;
    }

    return STATUS_SUCCESS;
}

PSTANDARD_INFORMATION
GetStandardInformationFromRecord(PDEVICE_EXTENSION Vcb,
                                 PFILE_RECORD_HEADER FileRecord)
{
    NTSTATUS Status;
    FIND_ATTR_CONTXT Context;
    PNTFS_ATTR_RECORD Attribute;
    PSTANDARD_INFORMATION StdInfo;

    Status = FindFirstAttribute(&Context, Vcb, FileRecord, FALSE, &Attribute);
    while (NT_SUCCESS(Status))
    {
        if (Attribute->Type == AttributeStandardInformation)
        {
            StdInfo = (PSTANDARD_INFORMATION)((ULONG_PTR)Attribute + Attribute->Resident.ValueOffset);
            FindCloseAttribute(&Context);
            return StdInfo;
        }

        Status = FindNextAttribute(&Context, &Attribute);
    }

    FindCloseAttribute(&Context);
    return NULL;
}

/**
* @name GetFileNameAttributeLength
* @implemented
*
* Returns the size of a given FILENAME_ATTRIBUTE, in bytes.
*
* @param FileNameAttribute
* Pointer to a FILENAME_ATTRIBUTE to determine the size of.
*
* @remarks
* The length of a FILENAME_ATTRIBUTE is variable and is dependent on the length of the file name stored at the end.
* This function operates on the FILENAME_ATTRIBUTE proper, so don't try to pass it a PNTFS_ATTR_RECORD.
*/
ULONG GetFileNameAttributeLength(PFILENAME_ATTRIBUTE FileNameAttribute)
{
    ULONG Length = FIELD_OFFSET(FILENAME_ATTRIBUTE, Name) + (FileNameAttribute->NameLength * sizeof(WCHAR));
    return Length;
}

PFILENAME_ATTRIBUTE
GetBestFileNameFromRecord(PDEVICE_EXTENSION Vcb,
                          PFILE_RECORD_HEADER FileRecord)
{
    PFILENAME_ATTRIBUTE FileName;

    FileName = GetFileNameFromRecord(Vcb, FileRecord, NTFS_FILE_NAME_POSIX);
    if (FileName == NULL)
    {
        FileName = GetFileNameFromRecord(Vcb, FileRecord, NTFS_FILE_NAME_WIN32);
        if (FileName == NULL)
        {
            FileName = GetFileNameFromRecord(Vcb, FileRecord, NTFS_FILE_NAME_DOS);
        }
    }

    return FileName;
}

/* EOF */
