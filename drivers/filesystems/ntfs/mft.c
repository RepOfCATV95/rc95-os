/*
 *  ReactOS kernel
 *  Copyright (C) 2002, 2014 ReactOS Team
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
 * FILE:             drivers/filesystem/ntfs/mft.c
 * PURPOSE:          NTFS filesystem driver
 * PROGRAMMERS:      Eric Kohl
 *                   Valentin Verkhovsky
 *                   Pierre Schweitzer (pierre@reactos.org)
 *                   Hervé Poussineau (hpoussin@reactos.org)
 *                   Trevor Thompson
 */

/* INCLUDES *****************************************************************/

#include "ntfs.h"
#include <ntintsafe.h>

#define NDEBUG
#include <debug.h>

/* FUNCTIONS ****************************************************************/

PNTFS_ATTR_CONTEXT
PrepareAttributeContext(PNTFS_ATTR_RECORD AttrRecord)
{
    PNTFS_ATTR_CONTEXT Context;

    Context = ExAllocateFromNPagedLookasideList(&NtfsGlobalData->AttrCtxtLookasideList);
    if(!Context)
    {
        DPRINT1("Error: Unable to allocate memory for context!\n");
        return NULL;
    }

    // Allocate memory for a copy of the attribute
    Context->pRecord = ExAllocatePoolWithTag(NonPagedPool, AttrRecord->Length, TAG_NTFS);
    if(!Context->pRecord)
    {
        DPRINT1("Error: Unable to allocate memory for attribute record!\n");
        ExFreeToNPagedLookasideList(&NtfsGlobalData->AttrCtxtLookasideList, Context);
        return NULL;
    }

    // Copy the attribute
    RtlCopyMemory(Context->pRecord, AttrRecord, AttrRecord->Length);

    if (AttrRecord->IsNonResident)
    {
        LONGLONG DataRunOffset;
        ULONGLONG DataRunLength;
        BOOLEAN FirstRunSparse;
        ULONGLONG NextVBN = 0;
        ULONG MappingPairsLength;
        PUCHAR DataRun;

        if (Context->pRecord->NonResident.LowestVCN != 0 ||
            Context->pRecord->NonResident.HighestVCN == (ULONGLONG)-1 ||
            Context->pRecord->NonResident.MappingPairsOffset <
                FIELD_OFFSET(NTFS_ATTR_RECORD, NonResident.InitializedSize) +
                sizeof(Context->pRecord->NonResident.InitializedSize) ||
            Context->pRecord->NonResident.MappingPairsOffset >=
                Context->pRecord->Length)
        {
            ExFreePoolWithTag(Context->pRecord, TAG_NTFS);
            ExFreeToNPagedLookasideList(&NtfsGlobalData->AttrCtxtLookasideList, Context);
            return NULL;
        }

        DataRun = (PUCHAR)((ULONG_PTR)Context->pRecord +
                           Context->pRecord->NonResident.MappingPairsOffset);
        MappingPairsLength = Context->pRecord->Length -
                             Context->pRecord->NonResident.MappingPairsOffset;

        Context->CacheRun = DataRun;
        Context->CacheRunOffset = 0;
        {
            PUCHAR NextRun = DecodeRunBounded(Context->CacheRun,
                                              MappingPairsLength,
                                              &DataRunOffset,
                                              &DataRunLength,
                                              &FirstRunSparse);
            if (NextRun == NULL ||
                (!FirstRunSparse && DataRunOffset < 0))
            {
                ExFreePoolWithTag(Context->pRecord, TAG_NTFS);
                ExFreeToNPagedLookasideList(&NtfsGlobalData->AttrCtxtLookasideList, Context);
                return NULL;
            }
            MappingPairsLength -= (ULONG)(NextRun - Context->CacheRun);
            Context->CacheRun = NextRun;
        }
        Context->CacheRunLength = DataRunLength;
        if (!FirstRunSparse)
        {
            /* Normal run. */
            Context->CacheRunStartLCN =
            Context->CacheRunLastLCN = DataRunOffset;
        }
        else
        {
            /* Sparse run. */
            Context->CacheRunStartLCN = -1;
            Context->CacheRunLastLCN = 0;
        }
        Context->CacheRunCurrentOffset = 0;

        /*
         * Keep every run in the MCB, including the first run cached above.
         * The read path rebuilds its run list from this MCB, so omitting the
         * first run here would shift every subsequent VCN and lose the head
         * of the stream.  Re-decode from the start with an initial LCN of
         * zero; the first physical run's delta is absolute by definition.
         */
        if (!NT_SUCCESS(ConvertDataRunsToLargeMCB(
                (PUCHAR)((ULONG_PTR)Context->pRecord +
                         Context->pRecord->NonResident.MappingPairsOffset),
                Context->pRecord->Length -
                    Context->pRecord->NonResident.MappingPairsOffset,
                0,
                &Context->DataRunsMCB,
                &NextVBN)) ||
            NextVBN != (ULONGLONG)Context->pRecord->NonResident.HighestVCN + 1)
        {
            DPRINT1("Invalid or incomplete NTFS mapping-pairs stream\n");
            ExFreePoolWithTag(Context->pRecord, TAG_NTFS);
            ExFreeToNPagedLookasideList(&NtfsGlobalData->AttrCtxtLookasideList, Context);
            return NULL;
        }
    }

    return Context;
}


VOID
ReleaseAttributeContext(PNTFS_ATTR_CONTEXT Context)
{
    if (Context->pRecord)
    {
        if (Context->pRecord->IsNonResident)
        {
            FsRtlUninitializeLargeMcb(&Context->DataRunsMCB);
        }

        ExFreePoolWithTag(Context->pRecord, TAG_NTFS);
    }

    ExFreeToNPagedLookasideList(&NtfsGlobalData->AttrCtxtLookasideList, Context);
}


/*
 * Attribute names use the volume's $UpCase mapping. Before that table is
 * loaded, the bootstrap lookup uses exact UTF-16 matching.
 */
static
BOOLEAN
NtfsAttributeNameEquals(PDEVICE_EXTENSION Vcb,
                        PCWSTR Candidate,
                        USHORT CandidateLength,
                        PCWSTR Name,
                        ULONG NameLength)
{
    ULONG Index;

    if (CandidateLength != NameLength)
        return FALSE;
    if (NameLength == 0)
        return TRUE;
    if (Candidate == NULL || Name == NULL)
        return FALSE;

    if (Vcb->UpCaseTable == NULL)
        return RtlCompareMemory(Candidate, Name,
                                NameLength * sizeof(WCHAR)) ==
               NameLength * sizeof(WCHAR);

    for (Index = 0; Index < NameLength; ++Index)
    {
        if (Vcb->UpCaseTable[Candidate[Index]] !=
            Vcb->UpCaseTable[Name[Index]])
            return FALSE;
    }
    return TRUE;
}

/**
* @name FindAttribute
* @implemented
*
* Searches a file record for an attribute matching the given type and name.
*
* @param Offset
* Optional pointer to a ULONG that will receive the offset of the found attribute
* from the beginning of the record. Can be set to NULL.
*/
NTSTATUS
FindAttribute(PDEVICE_EXTENSION Vcb,
              PFILE_RECORD_HEADER MftRecord,
              ULONG Type,
              PCWSTR Name,
              ULONG NameLength,
              PNTFS_ATTR_CONTEXT * AttrCtx,
              PULONG Offset)
{
    BOOLEAN Found;
    NTSTATUS Status;
    FIND_ATTR_CONTXT Context;
    PNTFS_ATTR_RECORD Attribute;
    PNTFS_ATTRIBUTE_LIST_ITEM AttrListItem;

    if (Vcb == NULL || MftRecord == NULL || AttrCtx == NULL ||
        (NameLength != 0 && Name == NULL) ||
        NameLength > (MAXUSHORT / sizeof(WCHAR)))
    {
        return STATUS_INVALID_PARAMETER;
    }

    *AttrCtx = NULL;
    if (Offset != NULL)
        *Offset = 0;

    DPRINT("FindAttribute(%p, %p, 0x%x, %S, %lu, %p, %p)\n", Vcb, MftRecord, Type, Name, NameLength, AttrCtx, Offset);

    Found = FALSE;
    Status = FindFirstAttribute(&Context, Vcb, MftRecord, FALSE, &Attribute);
    while (NT_SUCCESS(Status))
    {
        if (Attribute->Type == Type && Attribute->NameLength == NameLength)
        {
            if (NameLength != 0)
            {
                PWCHAR AttrName;

                ULONG NameEnd = (ULONG)Attribute->NameOffset +
                                Attribute->NameLength * sizeof(WCHAR);

                if (Attribute->NameOffset <
                        FIELD_OFFSET(NTFS_ATTR_RECORD, Resident.ValueLength) ||
                    NameEnd > Attribute->Length)
                {
                    FindCloseAttribute(&Context);
                    return STATUS_FILE_CORRUPT_ERROR;
                }

                AttrName = (PWCHAR)((PCHAR)Attribute + Attribute->NameOffset);
                Found = NtfsAttributeNameEquals(Vcb, AttrName,
                                                Attribute->NameLength,
                                                Name, NameLength);
            }
            else
            {
                Found = TRUE;
            }

            if (Found)
            {
                /* Found it, fill up the context and return. */
                DPRINT("Found context\n");
                *AttrCtx = PrepareAttributeContext(Attribute);
                if (*AttrCtx == NULL)
                {
                    FindCloseAttribute(&Context);
                    return STATUS_FILE_CORRUPT_ERROR;
                }

                (*AttrCtx)->FileMFTIndex = MftRecord->MFTRecordNumber;

                if (Offset != NULL)
                    *Offset = Context.Offset;

                FindCloseAttribute(&Context);
                return STATUS_SUCCESS;
            }
        }

        Status = FindNextAttribute(&Context, &Attribute);
    }

    /*
     * Only a clean end-of-attributes permits consulting the attribute list.
     * Do not turn a malformed local attribute sequence or failed attribute
     * list load into a misleading "not found" result.
     */
    if (Status != STATUS_END_OF_FILE)
    {
        FindCloseAttribute(&Context);
        return Status;
    }

    /* No local attribute found, check the attribute list for an extension. */
    Status = FindFirstAttributeListItem(&Context, &AttrListItem);
    while (NT_SUCCESS(Status))
    {
        if (AttrListItem->Type == Type && AttrListItem->NameLength == NameLength)
        {
            if (NameLength != 0)
            {
                PWCHAR AttrName;

                AttrName = (PWCHAR)((PCHAR)AttrListItem + AttrListItem->NameOffset);
                Found = NtfsAttributeNameEquals(Vcb, AttrName,
                                                AttrListItem->NameLength,
                                                Name, NameLength);
            }
            else
            {
                Found = TRUE;
            }

            if (Found == TRUE)
            {
                /* Get the MFT Index of attribute */
                ULONGLONG MftIndex;
                PFILE_RECORD_HEADER RemoteHdr;

                MftIndex = AttrListItem->MFTIndex & NTFS_MFT_MASK;
                RemoteHdr = ExAllocateFromNPagedLookasideList(&Vcb->FileRecLookasideList);

                if (RemoteHdr == NULL)
                {
                    FindCloseAttribute(&Context);
                    return STATUS_INSUFFICIENT_RESOURCES;
                }

                /* Check we are not reading ourselves */
                if (MftRecord->MFTRecordNumber == MftIndex)
                {
                    /*
                     * The base record was already searched locally.  A list
                     * entry that points back to it but names no local
                     * attribute is internally inconsistent.
                     */
                    DPRINT1("Attribute list references a missing base-record attribute\n");
                    ExFreeToNPagedLookasideList(&Vcb->FileRecLookasideList, RemoteHdr);
                    FindCloseAttribute(&Context);
                    return STATUS_FILE_CORRUPT_ERROR;
                }
                /*
                 * Read and validate the referenced extension record before
                 * searching it.  The sequence number is part of the NTFS file
                 * reference; checking it prevents a stale attribute-list
                 * entry from resolving to a record that has since been reused.
                 */
                Status = ReadFileRecord(Vcb, MftIndex, RemoteHdr);
                if (!NT_SUCCESS(Status))
                {
                    ExFreeToNPagedLookasideList(&Vcb->FileRecLookasideList, RemoteHdr);
                    FindCloseAttribute(&Context);
                    return Status;
                }

                if ((RemoteHdr->Flags & FRH_IN_USE) == 0 ||
                    RemoteHdr->MFTRecordNumber != MftIndex ||
                    RemoteHdr->SequenceNumber != (USHORT)(AttrListItem->MFTIndex >> 48) ||
                    (RemoteHdr->BaseFileRecord & NTFS_MFT_MASK) != MftRecord->MFTRecordNumber ||
                    (USHORT)(RemoteHdr->BaseFileRecord >> 48) != MftRecord->SequenceNumber)
                {
                    DPRINT1("Stale or unrelated MFT extension record referenced by attribute list\n");
                    ExFreeToNPagedLookasideList(&Vcb->FileRecLookasideList, RemoteHdr);
                    FindCloseAttribute(&Context);
                    return STATUS_FILE_CORRUPT_ERROR;
                }

                /*
                 * Match the exact attribute-list entry.  Looking up by type
                 * and name alone is insufficient because one stream can
                 * have several nonresident extents in the same MFT record.
                 */
                {
                    FIND_ATTR_CONTXT RemoteContext;
                    PNTFS_ATTR_RECORD RemoteAttribute;
                    BOOLEAN RemoteFound = FALSE;

                    Status = FindFirstAttribute(&RemoteContext, Vcb, RemoteHdr,
                                                FALSE, &RemoteAttribute);
                    while (NT_SUCCESS(Status))
                    {
                        BOOLEAN NameMatches = (RemoteAttribute->NameLength == NameLength);

                        if (NameMatches && NameLength != 0)
                        {
                            ULONG NameEnd = (ULONG)RemoteAttribute->NameOffset +
                                            NameLength * sizeof(WCHAR);
                            if (RemoteAttribute->NameOffset < FIELD_OFFSET(NTFS_ATTR_RECORD, Resident.ValueLength) ||
                                NameEnd > RemoteAttribute->Length)
                            {
                                Status = STATUS_FILE_CORRUPT_ERROR;
                                break;
                            }

                            NameMatches = NtfsAttributeNameEquals(
                                Vcb,
                                (PCWSTR)((PCHAR)RemoteAttribute +
                                         RemoteAttribute->NameOffset),
                                RemoteAttribute->NameLength,
                                Name,
                                NameLength);
                        }

                        if (RemoteAttribute->Type == Type &&
                            RemoteAttribute->Instance == AttrListItem->Instance &&
                            NameMatches &&
                            ((RemoteAttribute->IsNonResident &&
                              RemoteAttribute->NonResident.LowestVCN == AttrListItem->StartingVCN) ||
                             (!RemoteAttribute->IsNonResident &&
                              AttrListItem->StartingVCN == 0)))
                        {
                            *AttrCtx = PrepareAttributeContext(RemoteAttribute);
                            if (*AttrCtx == NULL)
                            {
                                Status = STATUS_FILE_CORRUPT_ERROR;
                                break;
                            }
                            (*AttrCtx)->FileMFTIndex = RemoteHdr->MFTRecordNumber;
                            if (Offset != NULL)
                                *Offset = RemoteContext.Offset;
                            RemoteFound = TRUE;
                            break;
                        }

                        Status = FindNextAttribute(&RemoteContext, &RemoteAttribute);
                    }

                    FindCloseAttribute(&RemoteContext);
                    /*
                     * Once an attribute-list entry has selected an extension
                     * record, absence of the exact type/name/instance/VCN
                     * tuple means the list and record disagree.
                     */
                    if (!RemoteFound &&
                        (Status == STATUS_END_OF_FILE || NT_SUCCESS(Status)))
                    {
                        Status = STATUS_FILE_CORRUPT_ERROR;
                    }
                }

                ExFreeToNPagedLookasideList(&Vcb->FileRecLookasideList, RemoteHdr);
                FindCloseAttribute(&Context);
                return Status;
            }
        }
        Status = FindNextAttributeListItem(&Context, &AttrListItem);
    }

    if (Status != STATUS_UNSUCCESSFUL)
    {
        FindCloseAttribute(&Context);
        return Status;
    }

    FindCloseAttribute(&Context);
    return STATUS_OBJECT_NAME_NOT_FOUND;
}


ULONGLONG
AttributeAllocatedLength(PNTFS_ATTR_RECORD AttrRecord)
{
    if (AttrRecord->IsNonResident)
        return AttrRecord->NonResident.AllocatedSize;
    else
        return ALIGN_UP_BY(AttrRecord->Resident.ValueLength, ATTR_RECORD_ALIGNMENT);
}


ULONGLONG
AttributeDataLength(PNTFS_ATTR_RECORD AttrRecord)
{
    if (AttrRecord->IsNonResident)
        return AttrRecord->NonResident.DataSize;
    else
        return AttrRecord->Resident.ValueLength;
}

/**
* @name IncreaseMftSize
* @implemented
*
* Increases the size of the master file table on a volume, increasing the space available for file records.
*
* @param Vcb
* Pointer to the VCB (DEVICE_EXTENSION) of the target volume.
*
*
* @param CanWait
* Boolean indicating if the function is allowed to wait for exclusive access to the master file table.
* This will only be relevant if the MFT doesn't have any free file records and needs to be enlarged.
*
* @return
* STATUS_SUCCESS on success.
* STATUS_INSUFFICIENT_RESOURCES if an allocation fails.
* STATUS_INVALID_PARAMETER if there was an error reading the Mft's bitmap.
* STATUS_CANT_WAIT if CanWait was FALSE and the function could not get immediate, exclusive access to the MFT.
*
* @remarks
* Increases the size of the Master File Table by 64 records. Bitmap entries for the new records are cleared,
* and the bitmap is also enlarged if needed. Mimicking Windows' behavior when enlarging the mft is still TODO.
* This function will wait for exlusive access to the volume fcb.
*/
NTSTATUS
IncreaseMftSize(PDEVICE_EXTENSION Vcb, BOOLEAN CanWait)
{
    PNTFS_ATTR_CONTEXT BitmapContext;
    LARGE_INTEGER BitmapSize;
    LARGE_INTEGER DataSize;
    LONGLONG BitmapSizeDifference;
    ULONG NewRecords = ATTR_RECORD_ALIGNMENT * 8;  // Allocate one new record for every bit of every byte we'll be adding to the bitmap
    ULONG DataSizeDifference = Vcb->NtfsInfo.BytesPerFileRecord * NewRecords;
    ULONG BitmapOffset;
    PUCHAR BitmapBuffer;
    ULONGLONG BitmapBytes;
    ULONGLONG NewBitmapSize;
    ULONGLONG FirstNewMftIndex;
    ULONG BytesRead;
    ULONG LengthWritten;
    PFILE_RECORD_HEADER BlankFileRecord;
    ULONG i;
    NTSTATUS Status;

    DPRINT("IncreaseMftSize(%p, %s)\n", Vcb, CanWait ? "TRUE" : "FALSE");

    // We need exclusive access to the mft while we change its size
    if (!ExAcquireResourceExclusiveLite(&(Vcb->DirResource), CanWait))
    {
        return STATUS_CANT_WAIT;
    }

    // Create a blank file record that will be used later
    BlankFileRecord = NtfsCreateEmptyFileRecord(Vcb);
    if (!BlankFileRecord)
    {
        DPRINT1("Error: Unable to create empty file record!\n");
        ExReleaseResourceLite(&(Vcb->DirResource));
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    // Clear the flags (file record is not in use)
    BlankFileRecord->Flags = 0;

    // Find the bitmap attribute of master file table
    Status = FindAttribute(Vcb, Vcb->MasterFileTable, AttributeBitmap, L"", 0, &BitmapContext, NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ERROR: Couldn't find $BITMAP attribute of Mft!\n");
        ExFreeToNPagedLookasideList(&Vcb->FileRecLookasideList, BlankFileRecord);
        ExReleaseResourceLite(&(Vcb->DirResource));
        return Status;
    }

    // Get size of Bitmap Attribute
    BitmapSize.QuadPart = AttributeDataLength(BitmapContext->pRecord);

    // Calculate the new mft size
    DataSize.QuadPart = AttributeDataLength(Vcb->MFTContext->pRecord) + DataSizeDifference;

    // Find the index of the first Mft entry that will be created
    FirstNewMftIndex = AttributeDataLength(Vcb->MFTContext->pRecord) / Vcb->NtfsInfo.BytesPerFileRecord;

    // Determine how many bytes will make up the bitmap
    BitmapBytes = DataSize.QuadPart / Vcb->NtfsInfo.BytesPerFileRecord / 8;
    if ((DataSize.QuadPart / Vcb->NtfsInfo.BytesPerFileRecord) % 8 != 0)
        BitmapBytes++;

    // Windows will always keep the number of bytes in a bitmap as a multiple of 8, so no bytes are wasted on slack
    BitmapBytes = ALIGN_UP_BY(BitmapBytes, ATTR_RECORD_ALIGNMENT);

    // Determine how much we need to adjust the bitmap size (it's possible we don't)
    BitmapSizeDifference = BitmapBytes - BitmapSize.QuadPart;
    NewBitmapSize = max(BitmapSize.QuadPart + BitmapSizeDifference, BitmapSize.QuadPart);

    // Allocate memory for the bitmap
    BitmapBuffer = ExAllocatePoolWithTag(NonPagedPool, NewBitmapSize, TAG_NTFS);
    if (!BitmapBuffer)
    {
        DPRINT1("ERROR: Unable to allocate memory for bitmap attribute!\n");
        ExFreeToNPagedLookasideList(&Vcb->FileRecLookasideList, BlankFileRecord);
        ExReleaseResourceLite(&(Vcb->DirResource));
        ReleaseAttributeContext(BitmapContext);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    // Zero the bytes we'll be adding
    RtlZeroMemory(BitmapBuffer, NewBitmapSize);

    // Read the bitmap attribute
    BytesRead = ReadAttribute(Vcb,
                              BitmapContext,
                              0,
                              (PCHAR)BitmapBuffer,
                              BitmapSize.LowPart);
    if (BytesRead != BitmapSize.LowPart)
    {
        DPRINT1("ERROR: Bytes read != Bitmap size!\n");
        ExFreeToNPagedLookasideList(&Vcb->FileRecLookasideList, BlankFileRecord);
        ExReleaseResourceLite(&(Vcb->DirResource));
        ExFreePoolWithTag(BitmapBuffer, TAG_NTFS);
        ReleaseAttributeContext(BitmapContext);
        return STATUS_INVALID_PARAMETER;
    }

    // Increase the mft size
    Status = SetNonResidentAttributeDataLength(Vcb, Vcb->MFTContext, Vcb->MftDataOffset, Vcb->MasterFileTable, &DataSize);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ERROR: Failed to set size of $MFT data attribute!\n");
        ExFreeToNPagedLookasideList(&Vcb->FileRecLookasideList, BlankFileRecord);
        ExReleaseResourceLite(&(Vcb->DirResource));
        ExFreePoolWithTag(BitmapBuffer, TAG_NTFS);
        ReleaseAttributeContext(BitmapContext);
        return Status;
    }

    // We'll need to find the bitmap again, because its offset will have changed after resizing the data attribute
    ReleaseAttributeContext(BitmapContext);
    Status = FindAttribute(Vcb, Vcb->MasterFileTable, AttributeBitmap, L"", 0, &BitmapContext, &BitmapOffset);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ERROR: Couldn't find $BITMAP attribute of Mft!\n");
        ExFreeToNPagedLookasideList(&Vcb->FileRecLookasideList, BlankFileRecord);
        ExReleaseResourceLite(&(Vcb->DirResource));
        return Status;
    }

    // If the bitmap grew
    if (BitmapSizeDifference > 0)
    {
        // Set the new bitmap size
        BitmapSize.QuadPart = NewBitmapSize;
        if (BitmapContext->pRecord->IsNonResident)
            Status = SetNonResidentAttributeDataLength(Vcb, BitmapContext, BitmapOffset, Vcb->MasterFileTable, &BitmapSize);
        else
            Status = SetResidentAttributeDataLength(Vcb, BitmapContext, BitmapOffset, Vcb->MasterFileTable, &BitmapSize);

        if (!NT_SUCCESS(Status))
        {
            DPRINT1("ERROR: Failed to set size of bitmap attribute!\n");
            ExFreeToNPagedLookasideList(&Vcb->FileRecLookasideList, BlankFileRecord);
            ExReleaseResourceLite(&(Vcb->DirResource));
            ExFreePoolWithTag(BitmapBuffer, TAG_NTFS);
            ReleaseAttributeContext(BitmapContext);
            return Status;
        }
    }

    NtfsDumpFileAttributes(Vcb, Vcb->MasterFileTable);

    // Update the file record with the new attribute sizes
    Status = UpdateFileRecord(Vcb, Vcb->VolumeFcb->MFTIndex, Vcb->MasterFileTable);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ERROR: Failed to update $MFT file record!\n");
        ExFreeToNPagedLookasideList(&Vcb->FileRecLookasideList, BlankFileRecord);
        ExReleaseResourceLite(&(Vcb->DirResource));
        ExFreePoolWithTag(BitmapBuffer, TAG_NTFS);
        ReleaseAttributeContext(BitmapContext);
        return Status;
    }

    // Write out the new bitmap
    Status = WriteAttribute(Vcb, BitmapContext, 0, BitmapBuffer, NewBitmapSize, &LengthWritten, Vcb->MasterFileTable);
    if (!NT_SUCCESS(Status))
    {
        ExFreeToNPagedLookasideList(&Vcb->FileRecLookasideList, BlankFileRecord);
        ExReleaseResourceLite(&(Vcb->DirResource));
        ExFreePoolWithTag(BitmapBuffer, TAG_NTFS);
        ReleaseAttributeContext(BitmapContext);
        DPRINT1("ERROR: Couldn't write to bitmap attribute of $MFT!\n");
        return Status;
    }

    // Create blank records for the new file record entries.
    for (i = 0; i < NewRecords; i++)
    {
        Status = UpdateFileRecord(Vcb, FirstNewMftIndex + i, BlankFileRecord);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("ERROR: Failed to write blank file record!\n");
            ExFreeToNPagedLookasideList(&Vcb->FileRecLookasideList, BlankFileRecord);
            ExReleaseResourceLite(&(Vcb->DirResource));
            ExFreePoolWithTag(BitmapBuffer, TAG_NTFS);
            ReleaseAttributeContext(BitmapContext);
            return Status;
        }
    }

    // Update the mft mirror
    Status = UpdateMftMirror(Vcb);

    // Cleanup
    ExFreeToNPagedLookasideList(&Vcb->FileRecLookasideList, BlankFileRecord);
    ExReleaseResourceLite(&(Vcb->DirResource));
    ExFreePoolWithTag(BitmapBuffer, TAG_NTFS);
    ReleaseAttributeContext(BitmapContext);

    return Status;
}

/**
* @name MoveAttributes
* @implemented
*
* Moves a block of attributes to a new location in the file Record. The attribute at FirstAttributeToMove
* and every attribute after that will be moved to MoveTo.
*
* @param DeviceExt
* Pointer to the DEVICE_EXTENSION (VCB) of the target volume.
*
* @param FirstAttributeToMove
* Pointer to the first NTFS_ATTR_RECORD that needs to be moved. This pointer must reside within a file record.
*
* @param FirstAttributeOffset
* Offset of FirstAttributeToMove relative to the beginning of the file record.
*
* @param MoveTo
* ULONG_PTR with the memory location that will be the new location of the first attribute being moved.
*
* @return
* The new location of the final attribute (i.e. AttributeEnd marker).
*/
PNTFS_ATTR_RECORD
MoveAttributes(PDEVICE_EXTENSION DeviceExt,
               PNTFS_ATTR_RECORD FirstAttributeToMove,
               ULONG FirstAttributeOffset,
               ULONG_PTR MoveTo)
{
    // Get the size of all attributes after this one
    ULONG MemBlockSize = 0;
    PNTFS_ATTR_RECORD CurrentAttribute = FirstAttributeToMove;
    ULONG CurrentOffset = FirstAttributeOffset;
    PNTFS_ATTR_RECORD FinalAttribute;

    while (CurrentAttribute->Type != AttributeEnd && CurrentOffset < DeviceExt->NtfsInfo.BytesPerFileRecord)
    {
        CurrentOffset += CurrentAttribute->Length;
        MemBlockSize += CurrentAttribute->Length;
        CurrentAttribute = (PNTFS_ATTR_RECORD)((ULONG_PTR)CurrentAttribute + CurrentAttribute->Length);
    }

    FinalAttribute = (PNTFS_ATTR_RECORD)(MoveTo + MemBlockSize);
    MemBlockSize += sizeof(ULONG) * 2;  // Add the AttributeEnd and file record end

    ASSERT(MemBlockSize % ATTR_RECORD_ALIGNMENT == 0);

    // Move the attributes after this one
    RtlMoveMemory((PCHAR)MoveTo, FirstAttributeToMove, MemBlockSize);

    return FinalAttribute;
}

NTSTATUS
InternalSetResidentAttributeLength(PDEVICE_EXTENSION DeviceExt,
                                   PNTFS_ATTR_CONTEXT AttrContext,
                                   PFILE_RECORD_HEADER FileRecord,
                                   ULONG AttrOffset,
                                   ULONG DataSize)
{
    PNTFS_ATTR_RECORD Destination = (PNTFS_ATTR_RECORD)((ULONG_PTR)FileRecord + AttrOffset);
    PNTFS_ATTR_RECORD NextAttribute = (PNTFS_ATTR_RECORD)((ULONG_PTR)Destination + Destination->Length);
    PNTFS_ATTR_RECORD FinalAttribute;
    ULONG OldAttributeLength = Destination->Length;
    ULONG NextAttributeOffset;

    DPRINT("InternalSetResidentAttributeLength( %p, %p, %p, %lu, %lu )\n", DeviceExt, AttrContext, FileRecord, AttrOffset, DataSize);

    ASSERT(!AttrContext->pRecord->IsNonResident);

    // Update ValueLength Field
    Destination->Resident.ValueLength = DataSize;

    // Calculate the record length and end marker offset
    Destination->Length = ALIGN_UP_BY(DataSize + AttrContext->pRecord->Resident.ValueOffset, ATTR_RECORD_ALIGNMENT);
    NextAttributeOffset = AttrOffset + Destination->Length;

    // Ensure NextAttributeOffset is aligned to an 8-byte boundary
    ASSERT(NextAttributeOffset % ATTR_RECORD_ALIGNMENT == 0);

    // Will the new attribute be larger than the old one?
    if (Destination->Length > OldAttributeLength)
    {
        // Free the old copy of the attribute in the context, as it will be the wrong length
        ExFreePoolWithTag(AttrContext->pRecord, TAG_NTFS);

        // Create a new copy of the attribute record for the context
        AttrContext->pRecord = ExAllocatePoolWithTag(NonPagedPool, Destination->Length, TAG_NTFS);
        if (!AttrContext->pRecord)
        {
            DPRINT1("Unable to allocate memory for attribute!\n");
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        RtlZeroMemory((PVOID)((ULONG_PTR)AttrContext->pRecord + OldAttributeLength), Destination->Length - OldAttributeLength);
        RtlCopyMemory(AttrContext->pRecord, Destination, OldAttributeLength);
    }

    // Are there attributes after this one that need to be moved?
    if (NextAttribute->Type != AttributeEnd)
    {
        // Move the attributes after this one
        FinalAttribute = MoveAttributes(DeviceExt, NextAttribute, NextAttributeOffset, (ULONG_PTR)Destination + Destination->Length);
    }
    else
    {
        // advance to the final "attribute," adjust for the changed length of the attribute we're resizing
        FinalAttribute = (PNTFS_ATTR_RECORD)((ULONG_PTR)NextAttribute - OldAttributeLength + Destination->Length);
    }

    // Update pRecord's length
    AttrContext->pRecord->Length = Destination->Length;
    AttrContext->pRecord->Resident.ValueLength = DataSize;

    // set the file record end
    SetFileRecordEnd(FileRecord, FinalAttribute, FILE_RECORD_END);

    //NtfsDumpFileRecord(DeviceExt, FileRecord);

    return STATUS_SUCCESS;
}

/**
*   @parameter FileRecord
*   Pointer to a file record. Must be a full record at least
*   Fcb->Vcb->NtfsInfo.BytesPerFileRecord bytes large, not just the header.
*/
NTSTATUS
SetAttributeDataLength(PFILE_OBJECT FileObject,
                       PNTFS_FCB Fcb,
                       PNTFS_ATTR_CONTEXT AttrContext,
                       ULONG AttrOffset,
                       PFILE_RECORD_HEADER FileRecord,
                       PLARGE_INTEGER DataSize)
{
    NTSTATUS Status = STATUS_SUCCESS;

    DPRINT("SetAttributeDataLength(%p, %p, %p, %lu, %p, %I64u)\n",
            FileObject,
            Fcb,
            AttrContext,
            AttrOffset,
            FileRecord,
            DataSize->QuadPart);

    // are we truncating the file?
    if (DataSize->QuadPart < AttributeDataLength(AttrContext->pRecord))
    {
        if (!MmCanFileBeTruncated(FileObject->SectionObjectPointer, DataSize))
        {
            DPRINT1("Can't truncate a memory-mapped file!\n");
            return STATUS_USER_MAPPED_FILE;
        }
    }

    if (AttrContext->pRecord->IsNonResident)
    {
        Status = SetNonResidentAttributeDataLength(Fcb->Vcb,
                                                   AttrContext,
                                                   AttrOffset,
                                                   FileRecord,
                                                   DataSize);
    }
    else
    {
        // resident attribute
        Status = SetResidentAttributeDataLength(Fcb->Vcb,
                                                AttrContext,
                                                AttrOffset,
                                                FileRecord,
                                                DataSize);
    }

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ERROR: Failed to set size of attribute!\n");
        return Status;
    }

    //NtfsDumpFileAttributes(Fcb->Vcb, FileRecord);

    // write the updated file record back to disk
    Status = UpdateFileRecord(Fcb->Vcb, Fcb->MFTIndex, FileRecord);

    if (NT_SUCCESS(Status))
    {
        if (AttrContext->pRecord->IsNonResident)
            Fcb->RFCB.AllocationSize.QuadPart = AttrContext->pRecord->NonResident.AllocatedSize;
        else
            Fcb->RFCB.AllocationSize = *DataSize;
        Fcb->RFCB.FileSize = *DataSize;
        Fcb->RFCB.ValidDataLength = *DataSize;
        CcSetFileSizes(FileObject, (PCC_FILE_SIZES)&Fcb->RFCB.AllocationSize);
    }

    return STATUS_SUCCESS;
}

/**
* @name SetFileRecordEnd
* @implemented
*
* This small function sets a new endpoint for the file record. It set's the final
* AttrEnd->Type to AttributeEnd and recalculates the bytes used by the file record.
*
* @param FileRecord
* Pointer to the file record whose endpoint (length) will be set.
*
* @param AttrEnd
* Pointer to section of memory that will receive the AttributeEnd marker. This must point
* to memory allocated for the FileRecord. Must be aligned to an 8-byte boundary (relative to FileRecord).
*
* @param EndMarker
* This value will be written after AttributeEnd but isn't critical at all. When Windows resizes
* a file record, it preserves the final ULONG that previously ended the record, even though this
* value is (to my knowledge) never used. We emulate this behavior.
*
*/
VOID
SetFileRecordEnd(PFILE_RECORD_HEADER FileRecord,
                 PNTFS_ATTR_RECORD AttrEnd,
                 ULONG EndMarker)
{
    // Ensure AttrEnd is aligned on an 8-byte boundary, relative to FileRecord
    ASSERT(((ULONG_PTR)AttrEnd - (ULONG_PTR)FileRecord) % ATTR_RECORD_ALIGNMENT == 0);

    // mark the end of attributes
    AttrEnd->Type = AttributeEnd;

    // Restore the "file-record-end marker." The value is never checked but this behavior is consistent with Win2k3.
    AttrEnd->Length = EndMarker;

    // recalculate bytes in use
    FileRecord->BytesInUse = (ULONG_PTR)AttrEnd - (ULONG_PTR)FileRecord + sizeof(ULONG) * 2;
}

/**
* @name NtfsExtendAllocation
* @implemented
*
* Assigns clusters to a non-resident attribute until its allocation reaches AllocationSize.
* Data size is left alone, so this also reserves room ahead of a growing file.
*
* @param Vcb
* Pointer to a DEVICE_EXTENSION describing the target disk.
*
* @param AttrContext
* PNTFS_ATTR_CONTEXT describing the attribute to extend.
*
* @param AttrOffset
* Offset, from the beginning of the record, of the attribute.
*
* @param FileRecord
* Pointer to a complete copy of the file record containing the attribute.
*
* @param AllocationSize
* New allocation in bytes. Must be a multiple of the cluster size.
*
* @return
* STATUS_SUCCESS on success, or the error from NtfsAllocateClusters() or AddRun().
*
* @remarks
* AddRun() writes the file record for each run it adds, but the new AllocatedSize
* only reaches the disk with the caller's next UpdateFileRecord().
*/
NTSTATUS
NtfsExtendAllocation(
    _In_ PDEVICE_EXTENSION Vcb,
    _In_ PNTFS_ATTR_CONTEXT AttrContext,
    _In_ ULONG AttrOffset,
    _In_ PFILE_RECORD_HEADER FileRecord,
    _In_ ULONGLONG AllocationSize)
{
    NTSTATUS Status;
    ULONG BytesPerCluster = Vcb->NtfsInfo.BytesPerCluster;
    PNTFS_ATTR_RECORD DestinationAttribute = (PNTFS_ATTR_RECORD)((ULONG_PTR)FileRecord + AttrOffset);
    ULONGLONG ExistingClusters = AttrContext->pRecord->NonResident.AllocatedSize / BytesPerCluster;
    ULONG ClustersNeeded;
    ULONG DesiredCluster = 0;
    LONGLONG LastVbn;
    LONGLONG LastLbn;
    ULONG NextAssignedCluster;
    ULONG AssignedClusters;

    ASSERT(AttrContext->pRecord->IsNonResident);
    ASSERT(AllocationSize % BytesPerCluster == 0);

    if (AttrContext->pRecord->NonResident.AllocatedSize >= AllocationSize)
        return STATUS_SUCCESS;

    ClustersNeeded = (ULONG)(AllocationSize / BytesPerCluster - ExistingClusters);

    /* Ask for the clusters right after the last one the stream owns. The
     * tail of a sparse stream is a hole, so look at the last mapping, not
     * at HighestVCN. */
    if (ExistingClusters != 0 &&
        FsRtlLookupLastLargeMcbEntry(&AttrContext->DataRunsMCB, &LastVbn, &LastLbn) &&
        LastLbn != -1)
    {
        DesiredCluster = (ULONG)LastLbn + 1;
    }

    DPRINT("DesiredCluster: %lu\n", DesiredCluster);
    DPRINT("Highest VCN of record: %I64u\n", AttrContext->pRecord->NonResident.HighestVCN);

    while (ClustersNeeded > 0)
    {
        Status = NtfsAllocateClusters(Vcb,
                                      DesiredCluster,
                                      ClustersNeeded,
                                      &NextAssignedCluster,
                                      &AssignedClusters);

        if (!NT_SUCCESS(Status))
        {
            DPRINT1("Error: Unable to allocate requested clusters!\n");
            return Status;
        }

        // now we need to add the clusters we allocated to the data run
        Status = AddRun(Vcb, AttrContext, AttrOffset, FileRecord, NextAssignedCluster, AssignedClusters);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("Error: Unable to add data run!\n");
            return Status;
        }

        /* AddRun() places the next run after HighestVCN only once the
         * attribute owns clusters, so count these before looping */
        AttrContext->pRecord->NonResident.AllocatedSize += (ULONGLONG)AssignedClusters * BytesPerCluster;

        ClustersNeeded -= AssignedClusters;
        DesiredCluster = NextAssignedCluster + AssignedClusters;
    }

    DestinationAttribute->NonResident.AllocatedSize = AttrContext->pRecord->NonResident.AllocatedSize;
    DestinationAttribute->NonResident.HighestVCN = AttrContext->pRecord->NonResident.HighestVCN;

    return STATUS_SUCCESS;
}

/**
* @name NtfsTrimAllocation
* @implemented
*
* Gives back clusters a growing file reserved past its end. Called when the last
* handle goes away.
*
* @param Fcb
* FCB of the file. The caller holds its MainResource exclusively.
*
* @return
* STATUS_SUCCESS on success, including when there's nothing to trim.
*/
NTSTATUS
NtfsTrimAllocation(
    _In_ PNTFS_FCB Fcb)
{
    PDEVICE_EXTENSION Vcb = Fcb->Vcb;
    ULONG BytesPerCluster = Vcb->NtfsInfo.BytesPerCluster;
    PFILE_RECORD_HEADER FileRecord;
    PNTFS_ATTR_CONTEXT DataContext;
    PNTFS_ATTR_RECORD DestinationAttribute;
    ULONG AttributeOffset;
    ULONGLONG NeededSize = 0;
    NTSTATUS Status;

    FileRecord = ExAllocateFromNPagedLookasideList(&Vcb->FileRecLookasideList);
    if (FileRecord == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    /* Paging writes look the runs up again; keep them out while the tail goes */
    ExAcquireResourceExclusiveLite(&Fcb->PagingIoResource, TRUE);

    Status = ReadFileRecord(Vcb, Fcb->MFTIndex, FileRecord);
    if (!NT_SUCCESS(Status))
        goto Quit;

    Status = FindAttribute(Vcb, FileRecord, AttributeData, Fcb->Stream, wcslen(Fcb->Stream), &DataContext, &AttributeOffset);
    if (!NT_SUCCESS(Status))
        goto Quit;

    if (DataContext->pRecord->IsNonResident)
        NeededSize = ROUND_UP(DataContext->pRecord->NonResident.DataSize, BytesPerCluster);

    /* Compressed and sparse streams round their allocation differently; leave them be */
    if (DataContext->pRecord->IsNonResident &&
        !(DataContext->pRecord->Flags & (NTFS_ATTR_COMPRESSION_MASK | NTFS_ATTR_SPARSE)) &&
        DataContext->pRecord->NonResident.AllocatedSize > NeededSize)
    {
        Status = FreeClusters(Vcb,
                              DataContext,
                              AttributeOffset,
                              FileRecord,
                              (ULONG)((DataContext->pRecord->NonResident.AllocatedSize - NeededSize) / BytesPerCluster));
        if (NT_SUCCESS(Status))
        {
            DestinationAttribute = (PNTFS_ATTR_RECORD)((ULONG_PTR)FileRecord + AttributeOffset);
            DestinationAttribute->NonResident.AllocatedSize = NeededSize;

            Status = UpdateFileRecord(Vcb, Fcb->MFTIndex, FileRecord);
            if (NT_SUCCESS(Status))
                Fcb->RFCB.AllocationSize.QuadPart = NeededSize;
        }
    }

    ReleaseAttributeContext(DataContext);

Quit:
    ExReleaseResourceLite(&Fcb->PagingIoResource);
    ExFreeToNPagedLookasideList(&Vcb->FileRecLookasideList, FileRecord);

    return Status;
}

/**
* @name NtfsResizeAllocation
* @implemented
*
* Sets how many clusters the data stream owns, leaving its size alone.
*
* @param Fcb
* FCB of the file. The caller holds its MainResource exclusively.
*
* @param FileObject
* File object the request came through
*
* @param CaseSensitive
* Whether the directory entries are looked up case sensitively
*
* @param AllocationSize
* New allocation in bytes, a multiple of the cluster size. It never goes below the data.
*
* @return
* STATUS_SUCCESS on success, including when there's nothing to change.
*/
NTSTATUS
NtfsResizeAllocation(
    _In_ PNTFS_FCB Fcb,
    _In_ PFILE_OBJECT FileObject,
    _In_ BOOLEAN CaseSensitive,
    _In_ ULONGLONG AllocationSize)
{
    PDEVICE_EXTENSION Vcb = Fcb->Vcb;
    ULONG BytesPerCluster = Vcb->NtfsInfo.BytesPerCluster;
    PFILE_RECORD_HEADER FileRecord;
    PNTFS_ATTR_CONTEXT DataContext;
    PNTFS_ATTR_RECORD DestinationAttribute;
    ULONG AttributeOffset;
    ULONGLONG CurrentSize;
    BOOLEAN Changed = FALSE;
    NTSTATUS Status;

    ASSERT(AllocationSize % BytesPerCluster == 0);

    FileRecord = ExAllocateFromNPagedLookasideList(&Vcb->FileRecLookasideList);
    if (FileRecord == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = ReadFileRecord(Vcb, Fcb->MFTIndex, FileRecord);
    if (!NT_SUCCESS(Status))
        goto Quit;

    Status = FindAttribute(Vcb, FileRecord, AttributeData, Fcb->Stream, wcslen(Fcb->Stream), &DataContext, &AttributeOffset);
    if (!NT_SUCCESS(Status))
        goto Quit;

    /* Resident data lives in the record, and compressed or sparse streams
     * round their allocation differently; there is nothing to do for those */
    if (!DataContext->pRecord->IsNonResident ||
        (DataContext->pRecord->Flags & (NTFS_ATTR_COMPRESSION_MASK | NTFS_ATTR_SPARSE)))
    {
        ReleaseAttributeContext(DataContext);
        goto Quit;
    }

    AllocationSize = max(AllocationSize, ROUND_UP(DataContext->pRecord->NonResident.DataSize, BytesPerCluster));
    CurrentSize = DataContext->pRecord->NonResident.AllocatedSize;

    if (AllocationSize < CurrentSize)
    {
        LARGE_INTEGER NewSize;

        NewSize.QuadPart = AllocationSize;
        if (!MmCanFileBeTruncated(FileObject->SectionObjectPointer, &NewSize))
        {
            ReleaseAttributeContext(DataContext);
            Status = STATUS_USER_MAPPED_FILE;
            goto Quit;
        }
    }

    /* Paging writes look the runs up again, keep them out while those change */
    ExAcquireResourceExclusiveLite(&Fcb->PagingIoResource, TRUE);

    if (AllocationSize > CurrentSize)
    {
        Status = NtfsExtendAllocation(Vcb, DataContext, AttributeOffset, FileRecord, AllocationSize);
        Changed = NT_SUCCESS(Status);
    }
    else if (AllocationSize < CurrentSize)
    {
        Status = FreeClusters(Vcb,
                              DataContext,
                              AttributeOffset,
                              FileRecord,
                              (ULONG)((CurrentSize - AllocationSize) / BytesPerCluster));
        if (NT_SUCCESS(Status))
        {
            DestinationAttribute = (PNTFS_ATTR_RECORD)((ULONG_PTR)FileRecord + AttributeOffset);
            DestinationAttribute->NonResident.AllocatedSize = AllocationSize;
            Changed = TRUE;
        }
    }

    if (Changed)
    {
        Status = UpdateFileRecord(Vcb, Fcb->MFTIndex, FileRecord);
        if (NT_SUCCESS(Status))
            Fcb->RFCB.AllocationSize.QuadPart = AllocationSize;
    }

    ExReleaseResourceLite(&Fcb->PagingIoResource);
    ReleaseAttributeContext(DataContext);

    if (Changed && NT_SUCCESS(Status))
    {
        CcSetFileSizes(FileObject, (PCC_FILE_SIZES)&Fcb->RFCB.AllocationSize);

        /* The directory entries carry the allocation too */
        NtfsUpdateDuplicatedInformation(Vcb,
                                        FileRecord,
                                        Fcb->MFTIndex,
                                        NTFS_FILENAME_UPDATE_SIZES,
                                        CaseSensitive);
    }

Quit:
    ExFreeToNPagedLookasideList(&Vcb->FileRecLookasideList, FileRecord);

    return Status;
}

/**
* @name SetNonResidentAttributeDataLength
* @implemented
*
* Called by SetAttributeDataLength() and IncreaseMftSize() to set the size of a non-resident attribute. Doesn't update
* the file record.
*
* @param Vcb
* Pointer to a DEVICE_EXTENSION describing the target disk.
*
* @param AttrContext
* PNTFS_ATTR_CONTEXT describing the location of the attribute whose size is being set.
*
* @param AttrOffset
* Offset, from the beginning of the record, of the attribute being sized.
*
* @param FileRecord
* Pointer to a file record containing the attribute to be resized. Must be a complete file record,
* not just the header.
*
* @param DataSize
* Pointer to a LARGE_INTEGER describing the new size of the attribute's data.
*
* @return
* STATUS_SUCCESS on success;
* STATUS_INSUFFICIENT_RESOURCES if an allocation fails.
* STATUS_INVALID_PARAMETER if we can't find the last cluster in the data run.
*
* @remarks
* Called by SetAttributeDataLength() and IncreaseMftSize(). Use SetAttributeDataLength() unless you have a good
* reason to use this. Doesn't update the file record on disk. Doesn't inform the cache controller of changes with
* any associated files. Synchronization is the callers responsibility.
* Growing never gives back clusters reserved by NtfsExtendAllocation(); only a truncate does.
*/
NTSTATUS
SetNonResidentAttributeDataLength(PDEVICE_EXTENSION Vcb,
                                  PNTFS_ATTR_CONTEXT AttrContext,
                                  ULONG AttrOffset,
                                  PFILE_RECORD_HEADER FileRecord,
                                  PLARGE_INTEGER DataSize)
{
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG BytesPerCluster = Vcb->NtfsInfo.BytesPerCluster;
    ULONGLONG AllocationSize = ROUND_UP(DataSize->QuadPart, BytesPerCluster);
    PNTFS_ATTR_RECORD DestinationAttribute = (PNTFS_ATTR_RECORD)((ULONG_PTR)FileRecord + AttrOffset);
    ULONG ExistingClusters = AttrContext->pRecord->NonResident.AllocatedSize / BytesPerCluster;

    ASSERT(AttrContext->pRecord->IsNonResident);

    // do we need to increase the allocation size?
    if (AttrContext->pRecord->NonResident.AllocatedSize < AllocationSize)
    {
        Status = NtfsExtendAllocation(Vcb, AttrContext, AttrOffset, FileRecord, AllocationSize);
        if (!NT_SUCCESS(Status))
            return Status;
    }
    else if (AttrContext->pRecord->NonResident.AllocatedSize > AllocationSize &&
             (ULONGLONG)DataSize->QuadPart < AttrContext->pRecord->NonResident.DataSize)
    {
        // shrink allocation size
        ULONG ClustersToFree = ExistingClusters - (AllocationSize / BytesPerCluster);
        Status = FreeClusters(Vcb, AttrContext, AttrOffset, FileRecord, ClustersToFree);
        if (!NT_SUCCESS(Status))
            return Status;

        AttrContext->pRecord->NonResident.AllocatedSize = AllocationSize;
    }

    // TODO: is the file compressed, encrypted, or sparse?

    AttrContext->pRecord->NonResident.DataSize = DataSize->QuadPart;
    AttrContext->pRecord->NonResident.InitializedSize = DataSize->QuadPart;

    DestinationAttribute->NonResident.AllocatedSize = AttrContext->pRecord->NonResident.AllocatedSize;
    DestinationAttribute->NonResident.DataSize = DataSize->QuadPart;
    DestinationAttribute->NonResident.InitializedSize = DataSize->QuadPart;

    /* AddRun() and FreeClusters() keep HighestVCN on the last VCN the runs
     * describe, holes included, so it's carried over rather than recomputed */
    DestinationAttribute->NonResident.HighestVCN = AttrContext->pRecord->NonResident.HighestVCN;

    DPRINT("Allocated Size: %I64u\n", DestinationAttribute->NonResident.AllocatedSize);

    return Status;
}

/**
* @name SetResidentAttributeDataLength
* @implemented
*
* Called by SetAttributeDataLength() to set the size of a non-resident attribute. Doesn't update the file record.
*
* @param Vcb
* Pointer to a DEVICE_EXTENSION describing the target disk.
*
* @param AttrContext
* PNTFS_ATTR_CONTEXT describing the location of the attribute whose size is being set.
*
* @param AttrOffset
* Offset, from the beginning of the record, of the attribute being sized.
*
* @param FileRecord
* Pointer to a file record containing the attribute to be resized. Must be a complete file record,
* not just the header.
*
* @param DataSize
* Pointer to a LARGE_INTEGER describing the new size of the attribute's data.
*
* @return
* STATUS_SUCCESS on success;
* STATUS_INSUFFICIENT_RESOURCES if an allocation fails.
* STATUS_INVALID_PARAMETER if AttrContext describes a non-resident attribute.
* STATUS_NOT_IMPLEMENTED if requested to decrease the size of an attribute that isn't the
* last attribute listed in the file record.
*
* @remarks
* Called by SetAttributeDataLength() and IncreaseMftSize(). Use SetAttributeDataLength() unless you have a good
* reason to use this. Doesn't update the file record on disk. Doesn't inform the cache controller of changes with
* any associated files. Synchronization is the callers responsibility.
*/
NTSTATUS
SetResidentAttributeDataLength(PDEVICE_EXTENSION Vcb,
                               PNTFS_ATTR_CONTEXT AttrContext,
                               ULONG AttrOffset,
                               PFILE_RECORD_HEADER FileRecord,
                               PLARGE_INTEGER DataSize)
{
    NTSTATUS Status;

    // find the next attribute
    ULONG NextAttributeOffset = AttrOffset + AttrContext->pRecord->Length;
    PNTFS_ATTR_RECORD NextAttribute = (PNTFS_ATTR_RECORD)((PCHAR)FileRecord + NextAttributeOffset);

    ASSERT(!AttrContext->pRecord->IsNonResident);

    //NtfsDumpFileAttributes(Vcb, FileRecord);

    // Do we need to increase the data length?
    if (DataSize->QuadPart > AttrContext->pRecord->Resident.ValueLength)
    {
        // There's usually padding at the end of a record. Do we need to extend past it?
        ULONG MaxValueLength = AttrContext->pRecord->Length - AttrContext->pRecord->Resident.ValueOffset;
        if (MaxValueLength < (ULONGLONG)DataSize->QuadPart)
        {
            // If this is the last attribute, we could move the end marker to the very end of the file record
            MaxValueLength += Vcb->NtfsInfo.BytesPerFileRecord - NextAttributeOffset - (sizeof(ULONG) * 2);

            if (MaxValueLength < (ULONGLONG)DataSize->QuadPart || NextAttribute->Type != AttributeEnd)
            {
                // convert attribute to non-resident
                PNTFS_ATTR_RECORD Destination = (PNTFS_ATTR_RECORD)((ULONG_PTR)FileRecord + AttrOffset);
                PNTFS_ATTR_RECORD NewRecord;
                LARGE_INTEGER AttribDataSize;
                PVOID AttribData;
                ULONG NewRecordLength;
                ULONG EndAttributeOffset;
                ULONG LengthWritten;

                DPRINT("Converting attribute to non-resident.\n");

                AttribDataSize.QuadPart = AttrContext->pRecord->Resident.ValueLength;

                // Is there existing data we need to back-up?
                if (AttribDataSize.QuadPart > 0)
                {
                    AttribData = ExAllocatePoolWithTag(NonPagedPool, AttribDataSize.QuadPart, TAG_NTFS);
                    if (AttribData == NULL)
                    {
                        DPRINT1("ERROR: Couldn't allocate memory for attribute data. Can't migrate to non-resident!\n");
                        return STATUS_INSUFFICIENT_RESOURCES;
                    }

                    // read data to temp buffer
                    Status = ReadAttribute(Vcb, AttrContext, 0, AttribData, AttribDataSize.QuadPart);
                    if (!NT_SUCCESS(Status))
                    {
                        DPRINT1("ERROR: Unable to read attribute before migrating!\n");
                        ExFreePoolWithTag(AttribData, TAG_NTFS);
                        return Status;
                    }
                }

                // Start by turning this attribute into a 0-length, non-resident attribute, then enlarge it.

                // The size of a 0-length, non-resident attribute will be 0x41 + the size of the attribute name, aligned to an 8-byte boundary
                NewRecordLength = ALIGN_UP_BY(0x41 + (AttrContext->pRecord->NameLength * sizeof(WCHAR)), ATTR_RECORD_ALIGNMENT);

                // Create a new attribute record that will store the 0-length, non-resident attribute
                NewRecord = ExAllocatePoolWithTag(NonPagedPool, NewRecordLength, TAG_NTFS);

                // Zero out the NonResident structure
                RtlZeroMemory(NewRecord, NewRecordLength);

                // Copy the data that's common to both non-resident and resident attributes
                RtlCopyMemory(NewRecord, AttrContext->pRecord, FIELD_OFFSET(NTFS_ATTR_RECORD, Resident.ValueLength));

                // The name follows the non-resident header, not the resident one
                NewRecord->NameOffset = FIELD_OFFSET(NTFS_ATTR_RECORD, NonResident.CompressedSize);

                // if there's a name
                if (AttrContext->pRecord->NameLength != 0)
                {
                    // copy the name
                    // An attribute name will be located at offset 0x18 for a resident attribute, 0x40 for non-resident
                    RtlCopyMemory((PCHAR)((ULONG_PTR)NewRecord + 0x40),
                                  (PCHAR)((ULONG_PTR)AttrContext->pRecord + 0x18),
                                  AttrContext->pRecord->NameLength * sizeof(WCHAR));
                }

                // update the mapping pairs offset, which will be 0x40 (size of a non-resident header) + length in bytes of the name
                NewRecord->NonResident.MappingPairsOffset = 0x40 + (AttrContext->pRecord->NameLength * sizeof(WCHAR));

                // update the end of the file record
                // calculate position of end markers (1 byte for empty data run)
                EndAttributeOffset = AttrOffset + NewRecord->NonResident.MappingPairsOffset + 1;
                EndAttributeOffset = ALIGN_UP_BY(EndAttributeOffset, ATTR_RECORD_ALIGNMENT);

                // Update the length
                NewRecord->Length = EndAttributeOffset - AttrOffset;

                ASSERT(NewRecord->Length == NewRecordLength);

                // Copy the new attribute record into the file record
                RtlCopyMemory(Destination, NewRecord, NewRecord->Length);

                // Update the file record end
                SetFileRecordEnd(FileRecord,
                                 (PNTFS_ATTR_RECORD)((ULONG_PTR)FileRecord + EndAttributeOffset),
                                 FILE_RECORD_END);

                // Initialize the MCB, potentially catch an exception
                _SEH2_TRY
                {
                    FsRtlInitializeLargeMcb(&AttrContext->DataRunsMCB, NonPagedPool);
                }
                _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
                {
                    DPRINT1("Unable to create LargeMcb!\n");
                    if (AttribDataSize.QuadPart > 0)
                        ExFreePoolWithTag(AttribData, TAG_NTFS);
                    ExFreePoolWithTag(NewRecord, TAG_NTFS);
                    _SEH2_YIELD(return _SEH2_GetExceptionCode());
                } _SEH2_END;

                // Mark the attribute as non-resident (we wait until after we know the LargeMcb was initialized)
                NewRecord->IsNonResident = Destination->IsNonResident = 1;

                // Update file record on disk
                Status = UpdateFileRecord(Vcb, AttrContext->FileMFTIndex, FileRecord);
                if (!NT_SUCCESS(Status))
                {
                    DPRINT1("ERROR: Couldn't update file record to continue migration!\n");
                    if (AttribDataSize.QuadPart > 0)
                        ExFreePoolWithTag(AttribData, TAG_NTFS);
                    ExFreePoolWithTag(NewRecord, TAG_NTFS);
                    return Status;
                }

                // Now we need to free the old copy of the attribute record in the context and replace it with the new one
                ExFreePoolWithTag(AttrContext->pRecord, TAG_NTFS);
                AttrContext->pRecord = NewRecord;

                // Now we can treat the attribute as non-resident and enlarge it normally
                Status = SetNonResidentAttributeDataLength(Vcb, AttrContext, AttrOffset, FileRecord, DataSize);
                if (!NT_SUCCESS(Status))
                {
                    DPRINT1("ERROR: Unable to migrate resident attribute!\n");
                    if (AttribDataSize.QuadPart > 0)
                        ExFreePoolWithTag(AttribData, TAG_NTFS);
                    return Status;
                }

                // restore the back-up attribute, if we made one
                if (AttribDataSize.QuadPart > 0)
                {
                    Status = WriteAttribute(Vcb, AttrContext, 0, AttribData, AttribDataSize.QuadPart, &LengthWritten, FileRecord);
                    if (!NT_SUCCESS(Status))
                    {
                        DPRINT1("ERROR: Unable to write attribute data to non-resident clusters during migration!\n");
                        // TODO: Reverse migration so no data is lost
                        ExFreePoolWithTag(AttribData, TAG_NTFS);
                        return Status;
                    }

                    ExFreePoolWithTag(AttribData, TAG_NTFS);
                }
            }
        }
    }

    // set the new length of the resident attribute (if we didn't migrate it)
    if (!AttrContext->pRecord->IsNonResident)
        return InternalSetResidentAttributeLength(Vcb, AttrContext, FileRecord, AttrOffset, DataSize->LowPart);

    return STATUS_SUCCESS;
}

static
NTSTATUS
NtfsGetDataRunByteLength(ULONGLONG ClusterCount,
                         ULONG BytesPerCluster,
                         PULONGLONG ByteLength)
{
    if (ByteLength == NULL || BytesPerCluster == 0 || ClusterCount == 0 ||
        ClusterCount > (ULONGLONG)-1 / BytesPerCluster)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    *ByteLength = ClusterCount * BytesPerCluster;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NtfsResolveDataRunStartLCN(ULONGLONG LastLCN,
                           LONGLONG RelativeOffset,
                           PLONGLONG StartLCN)
{
    if (StartLCN == NULL || LastLCN > MAXLONGLONG)
        return STATUS_FILE_CORRUPT_ERROR;

    if (RelativeOffset < 0)
    {
        ULONGLONG Magnitude =
            (ULONGLONG)(-(RelativeOffset + 1)) + 1;

        if (Magnitude > LastLCN)
            return STATUS_FILE_CORRUPT_ERROR;

        *StartLCN = (LONGLONG)(LastLCN - Magnitude);
    }
    else
    {
        if ((ULONGLONG)RelativeOffset >
            (ULONGLONG)MAXLONGLONG - LastLCN)
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }

        *StartLCN = (LONGLONG)(LastLCN + RelativeOffset);
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
NtfsGetDataRunLbo(LONGLONG StartLCN,
                  ULONG BytesPerCluster,
                  ULONGLONG RunOffset,
                  ULONG RunLength,
                  PULONGLONG Lbo)
{
    ULONGLONG BaseLbo;

    if (Lbo == NULL || StartLCN < 0 || BytesPerCluster == 0 ||
        RunLength == 0 ||
        (ULONGLONG)StartLCN > (ULONGLONG)-1 / BytesPerCluster)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    BaseLbo = (ULONGLONG)StartLCN * BytesPerCluster;
    if (RunOffset > (ULONGLONG)-1 - BaseLbo)
        return STATUS_FILE_CORRUPT_ERROR;

    *Lbo = BaseLbo + RunOffset;
    if ((ULONGLONG)(RunLength - 1) > (ULONGLONG)-1 - *Lbo)
        return STATUS_FILE_CORRUPT_ERROR;

    return STATUS_SUCCESS;
}

/**
* @name NtfsMapAttributeRuns
* @implemented
*
* Maps a byte range of a non-resident attribute onto the volume, collecting the
* physically contiguous runs it spans into a run list.
*
* @param Vcb
* Volume the attribute lives on
*
* @param Context
* Attribute to map. Numeric run state is updated; its transient run pointer
* is invalidated before the temporary mapping buffer is released.
*
* @param Offset
* Byte offset into the attribute to start mapping at
*
* @param Length
* How many bytes to map
*
* @param RunList
* Receives the runs. Must have been initialized by NtfsInitIoRunList().
*
* @param MappedLength
* Receives how many bytes were actually mapped, which is less than Length if
* the attribute's allocation ends first.
*
* @return
* STATUS_SUCCESS if the range was mapped, STATUS_END_OF_FILE if Offset lies
* beyond the last allocated cluster, or STATUS_INSUFFICIENT_RESOURCES.
*
* @remarks Holes are recorded as NTFS_SPARSE_LBO runs rather than rejected
* here. Reads zero-fill them. The FCB-aware write paths must materialize
* touched holes before calling the physical write mapper.
*
*/
/*
 * Translate a byte range in a non-resident attribute into physical I/O runs.
 * All mapping-pair arithmetic is checked before it can become a disk offset.
 */
static
NTSTATUS
NtfsMapAttributeRuns(PDEVICE_EXTENSION Vcb,
                     PNTFS_ATTR_CONTEXT Context,
                     ULONGLONG Offset,
                     ULONG Length,
                     PNTFS_IO_RUN_LIST RunList,
                     PULONG MappedLength)
{
    ULONGLONG LastLCN = 0;
    ULONGLONG CurrentOffset = 0;
    ULONGLONG DataRunBytes = 0;
    ULONGLONG InRunOffset;
    ULONGLONG RunLbo;
    ULONGLONG HighestVCN;
    ULONGLONG RequestOffset = Offset;
    PUCHAR DataRun;
    PUCHAR NextDataRun;
    LONGLONG DataRunOffset;
    LONGLONG DataRunStartLCN = -1;
    ULONGLONG DataRunLength = 0;
    ULONG UsedBufferSize;
    ULONG RemainingBufferSize;
    ULONG RunLength;
    ULONG Mapped = 0;
    BOOLEAN IsSparse;
    NTSTATUS Status;
    PUCHAR TempBuffer = NULL;

    if (MappedLength == NULL)
        return STATUS_INVALID_PARAMETER;

    *MappedLength = 0;

    if (Vcb == NULL || Context == NULL || Context->pRecord == NULL ||
        RunList == NULL || Vcb->NtfsInfo.BytesPerCluster == 0 ||
        Vcb->NtfsInfo.BytesPerFileRecord == 0 ||
        Vcb->NtfsInfo.ClusterCount == 0 ||
        !Context->pRecord->IsNonResident)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Length == 0)
        return STATUS_SUCCESS;

    if (Offset > (ULONGLONG)-1 - Length ||
        Context->pRecord->NonResident.HighestVCN == (ULONGLONG)-1)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    HighestVCN = Context->pRecord->NonResident.HighestVCN + 1;
    TempBuffer = ExAllocatePoolWithTag(NonPagedPool,
                                       Vcb->NtfsInfo.BytesPerFileRecord,
                                       TAG_NTFS);
    if (TempBuffer == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(TempBuffer, Vcb->NtfsInfo.BytesPerFileRecord);

    Status = ConvertLargeMCBToDataRuns(&Context->DataRunsMCB,
                                       HighestVCN,
                                       TempBuffer,
                                       Vcb->NtfsInfo.BytesPerFileRecord,
                                       &UsedBufferSize);
    if (!NT_SUCCESS(Status) || UsedBufferSize == 0 ||
        UsedBufferSize > Vcb->NtfsInfo.BytesPerFileRecord)
    {
        if (NT_SUCCESS(Status))
            Status = STATUS_FILE_CORRUPT_ERROR;
        goto Cleanup;
    }

    DataRun = TempBuffer;
    RemainingBufferSize = UsedBufferSize;

    /* Find the run containing Offset. */
    for (;;)
    {
        ULONG Consumed;

        if (RemainingBufferSize == 0 || *DataRun == 0)
        {
            Status = STATUS_END_OF_FILE;
            goto Cleanup;
        }

        NextDataRun = DecodeRunBounded(DataRun,
                                       RemainingBufferSize,
                                       &DataRunOffset,
                                       &DataRunLength,
                                       &IsSparse);
        if (NextDataRun == NULL || NextDataRun <= DataRun ||
            (ULONG)(NextDataRun - DataRun) > RemainingBufferSize)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Cleanup;
        }

        Consumed = (ULONG)(NextDataRun - DataRun);
        RemainingBufferSize -= Consumed;
        DataRun = NextDataRun;

        Status = NtfsGetDataRunByteLength(DataRunLength,
                                          Vcb->NtfsInfo.BytesPerCluster,
                                          &DataRunBytes);
        if (!NT_SUCCESS(Status))
            goto Cleanup;

        if (!IsSparse)
        {
            Status = NtfsResolveDataRunStartLCN(LastLCN,
                                                DataRunOffset,
                                                &DataRunStartLCN);
            if (!NT_SUCCESS(Status))
                goto Cleanup;

            /* Validate the complete physical cluster interval. */
            if (DataRunLength - 1 >
                    (ULONGLONG)MAXLONGLONG - (ULONGLONG)DataRunStartLCN ||
                (ULONGLONG)DataRunStartLCN >= Vcb->NtfsInfo.ClusterCount ||
                DataRunLength > (ULONGLONG)Vcb->NtfsInfo.ClusterCount -
                                    (ULONGLONG)DataRunStartLCN)
            {
                Status = STATUS_FILE_CORRUPT_ERROR;
                goto Cleanup;
            }

            LastLCN = (ULONGLONG)DataRunStartLCN;
        }
        else
        {
            DataRunStartLCN = -1;
        }

        if (Offset >= CurrentOffset &&
            Offset - CurrentOffset < DataRunBytes)
        {
            break;
        }

        if (DataRunBytes > (ULONGLONG)-1 - CurrentOffset)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Cleanup;
        }
        CurrentOffset += DataRunBytes;
    }

    /* Collect the requested portion of this and subsequent runs. */
    for (;;)
    {
        ULONGLONG AvailableBytes;

        InRunOffset = Offset > CurrentOffset ? Offset - CurrentOffset : 0;
        if (InRunOffset >= DataRunBytes)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Cleanup;
        }

        AvailableBytes = DataRunBytes - InRunOffset;
        RunLength = (ULONG)min(AvailableBytes, (ULONGLONG)Length);
        if (RunLength == 0)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Cleanup;
        }

        if (IsSparse)
        {
            RunLbo = NTFS_SPARSE_LBO;
        }
        else
        {
            Status = NtfsGetDataRunLbo(DataRunStartLCN,
                                       Vcb->NtfsInfo.BytesPerCluster,
                                       InRunOffset,
                                       RunLength,
                                       &RunLbo);
            if (!NT_SUCCESS(Status))
                goto Cleanup;
        }

        Status = NtfsAddIoRun(RunList, RunLbo, RunLength);
        if (!NT_SUCCESS(Status))
            goto Cleanup;

        Length -= RunLength;
        Mapped += RunLength;
        if (Length == 0)
            break;

        /* A short final map is intentional when the attribute ends. */
        if (RunLength < AvailableBytes ||
            RemainingBufferSize == 0 ||
            *DataRun == 0)
        {
            break;
        }

        if (DataRunBytes > (ULONGLONG)-1 - CurrentOffset)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Cleanup;
        }
        CurrentOffset += DataRunBytes;

        NextDataRun = DecodeRunBounded(DataRun,
                                       RemainingBufferSize,
                                       &DataRunOffset,
                                       &DataRunLength,
                                       &IsSparse);
        if (NextDataRun == NULL || NextDataRun <= DataRun ||
            (ULONG)(NextDataRun - DataRun) > RemainingBufferSize)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Cleanup;
        }

        RemainingBufferSize -= (ULONG)(NextDataRun - DataRun);
        DataRun = NextDataRun;

        Status = NtfsGetDataRunByteLength(DataRunLength,
                                          Vcb->NtfsInfo.BytesPerCluster,
                                          &DataRunBytes);
        if (!NT_SUCCESS(Status))
            goto Cleanup;

        if (!IsSparse)
        {
            Status = NtfsResolveDataRunStartLCN(LastLCN,
                                                DataRunOffset,
                                                &DataRunStartLCN);
            if (!NT_SUCCESS(Status))
                goto Cleanup;

            if (DataRunLength - 1 >
                    (ULONGLONG)MAXLONGLONG - (ULONGLONG)DataRunStartLCN ||
                (ULONGLONG)DataRunStartLCN >= Vcb->NtfsInfo.ClusterCount ||
                DataRunLength > (ULONGLONG)Vcb->NtfsInfo.ClusterCount -
                                    (ULONGLONG)DataRunStartLCN)
            {
                Status = STATUS_FILE_CORRUPT_ERROR;
                goto Cleanup;
            }

            LastLCN = (ULONGLONG)DataRunStartLCN;
        }
        else
        {
            DataRunStartLCN = -1;
        }

        Offset = CurrentOffset;
    }

    Status = STATUS_SUCCESS;

Cleanup:
    /*
     * CacheRun must not retain a pointer into TempBuffer: the buffer is freed
     * below. The numeric cache fields remain useful to diagnostics/callers.
     */
    Context->CacheRun = NULL;
    Context->CacheRunOffset = RequestOffset + Mapped;
    Context->CacheRunStartLCN = DataRunStartLCN;
    Context->CacheRunLength = DataRunLength;
    Context->CacheRunLastLCN = LastLCN;
    Context->CacheRunCurrentOffset = CurrentOffset;

    ExFreePoolWithTag(TempBuffer, TAG_NTFS);
    *MappedLength = Mapped;
    return Status;
}

/*
 * Materialize sparse clusters touched by a write before mapping the write
 * into physical I/O runs. Newly claimed clusters are zeroed before their
 * mapping pairs are published, preserving zero semantics for partial-cluster
 * writes and preventing stale disk contents from becoming visible.
 */
NTSTATUS
NtfsAllocateSparseWriteRange(PDEVICE_EXTENSION Vcb,
                             PNTFS_ATTR_CONTEXT Context,
                             ULONGLONG Offset,
                             ULONG Length,
                             PFILE_RECORD_HEADER SuppliedFileRecord,
                             BOOLEAN AllowAllocation)
{
    PFILE_RECORD_HEADER FileRecord = SuppliedFileRecord;
    BOOLEAN OwnsFileRecord = FALSE;
    FIND_ATTR_CONTXT FindContext;
    PNTFS_ATTR_RECORD Attribute;
    PNTFS_ATTR_RECORD DestinationAttribute = NULL;
    ULONG AttributeOffset = 0;
    ULONG BytesPerCluster;
    PUCHAR ZeroBuffer = NULL;
    ULONGLONG EndOffset;
    ULONGLONG FirstVcn;
    ULONGLONG LastVcn;
    ULONGLONG CurrentVcn;
    NTSTATUS Status;
    BOOLEAN FoundAttribute = FALSE;

    if (Vcb == NULL || Context == NULL || Context->pRecord == NULL ||
        Length == 0 || !Context->pRecord->IsNonResident ||
        !(Context->pRecord->Flags & NTFS_ATTR_SPARSE) ||
        (Context->pRecord->Flags & NTFS_ATTR_COMPRESSION_MASK))
        return STATUS_INVALID_PARAMETER;

    BytesPerCluster = Vcb->NtfsInfo.BytesPerCluster;
    if (BytesPerCluster == 0 ||
        Offset > (ULONGLONG)-1 - ((ULONGLONG)Length - 1))
        return STATUS_INTEGER_OVERFLOW;

    EndOffset = Offset + Length - 1;
    FirstVcn = Offset / BytesPerCluster;
    LastVcn = EndOffset / BytesPerCluster;

    if (Context->pRecord->NonResident.HighestVCN == (ULONGLONG)-1 ||
        LastVcn > Context->pRecord->NonResident.HighestVCN)
        return STATUS_END_OF_FILE;

    if (FileRecord == NULL)
    {
        FileRecord = ExAllocateFromNPagedLookasideList(&Vcb->FileRecLookasideList);
        if (FileRecord == NULL)
            return STATUS_INSUFFICIENT_RESOURCES;

        OwnsFileRecord = TRUE;
        Status = ReadFileRecord(Vcb, Context->FileMFTIndex, FileRecord);
        if (!NT_SUCCESS(Status))
            goto Cleanup;
    }

    Status = FindFirstAttribute(&FindContext, Vcb, FileRecord, TRUE, &Attribute);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    while (Attribute != NULL)
    {
        if (Attribute->Type == Context->pRecord->Type &&
            Attribute->Instance == Context->pRecord->Instance)
        {
            DestinationAttribute = Attribute;
            AttributeOffset = (ULONG)((ULONG_PTR)Attribute - (ULONG_PTR)FileRecord);
            FoundAttribute = TRUE;
            break;
        }

        Status = FindNextAttribute(&FindContext, &Attribute);
        if (!NT_SUCCESS(Status))
        {
            Attribute = NULL;
            break;
        }
    }

    FindCloseAttribute(&FindContext);
    if (!FoundAttribute || DestinationAttribute == NULL ||
        !DestinationAttribute->IsNonResident ||
        !(DestinationAttribute->Flags & NTFS_ATTR_SPARSE))
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Cleanup;
    }

    CurrentVcn = FirstVcn;
    while (CurrentVcn <= LastVcn)
    {
        LONGLONG ExistingLcn;
        ULONG HoleClusters = 0;
        ULONG FirstAssignedCluster;
        ULONG AssignedClusters;
        ULONG ClusterIndex;
        if (FsRtlLookupLargeMcbEntry(&Context->DataRunsMCB,
                                     (LONGLONG)CurrentVcn,
                                     &ExistingLcn,
                                     NULL, NULL, NULL, NULL))
        {
            if (ExistingLcn < -1)
            {
                Status = STATUS_FILE_CORRUPT_ERROR;
                goto Cleanup;
            }

            if (ExistingLcn >= 0)
            {
                ++CurrentVcn;
                continue;
            }
        }

        /* Count the contiguous sparse interval, stopping at backed storage. */
        while (CurrentVcn + HoleClusters <= LastVcn)
        {
            LONGLONG Lcn;

            if (FsRtlLookupLargeMcbEntry(&Context->DataRunsMCB,
                                         (LONGLONG)(CurrentVcn + HoleClusters),
                                         &Lcn,
                                         NULL, NULL, NULL, NULL))
            {
                if (Lcn < -1)
                {
                    Status = STATUS_FILE_CORRUPT_ERROR;
                    goto Cleanup;
                }

                if (Lcn >= 0)
                    break;
            }

            if (HoleClusters == MAXULONG)
            {
                Status = STATUS_INTEGER_OVERFLOW;
                goto Cleanup;
            }
            ++HoleClusters;
        }

        if (HoleClusters == 0)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Cleanup;
        }

        if (!AllowAllocation)
        {
            Status = STATUS_NOT_IMPLEMENTED;
            goto Cleanup;
        }

        if (ZeroBuffer == NULL)
        {
            ZeroBuffer = ExAllocatePoolWithTag(NonPagedPool,
                                                BytesPerCluster,
                                                TAG_NTFS);
            if (ZeroBuffer == NULL)
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                goto Cleanup;
            }
            RtlZeroMemory(ZeroBuffer, BytesPerCluster);
        }

        Status = NtfsAllocateClusters(Vcb, 0, HoleClusters,
                                      &FirstAssignedCluster, &AssignedClusters);
        if (!NT_SUCCESS(Status))
            goto Cleanup;

        if (AssignedClusters == 0 || AssignedClusters > HoleClusters)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Cleanup;
        }

        /*
         * Zero the reservation before making it reachable through the MFT.
         * A failure intentionally leaks the reservation rather than
         * publishing a cluster containing stale data.
         */
        for (ClusterIndex = 0; ClusterIndex < AssignedClusters; ++ClusterIndex)
        {
            ULONGLONG Lcn = (ULONGLONG)FirstAssignedCluster + ClusterIndex;
            ULONGLONG Lbo;

            if (Lcn >= Vcb->NtfsInfo.ClusterCount ||
                Lcn > (ULONGLONG)MAXLONGLONG / BytesPerCluster)
            {
                Status = STATUS_FILE_CORRUPT_ERROR;
                goto Cleanup;
            }

            Lbo = Lcn * BytesPerCluster;
            Status = NtfsWriteDisk(Vcb->StorageDevice,
                                   (LONGLONG)Lbo,
                                   BytesPerCluster,
                                   Vcb->NtfsInfo.BytesPerSector,
                                   ZeroBuffer);
            if (!NT_SUCCESS(Status))
                goto Cleanup;
        }

        Status = NtfsAddSparseRun(Vcb, Context, AttributeOffset, FileRecord,
                                  CurrentVcn, FirstAssignedCluster, AssignedClusters);
        if (!NT_SUCCESS(Status))
        {
            /*
             * Do not clear the bitmap here: AddRun may have reached the MFT
             * write before the storage error was reported. Leaking clusters
             * is safer than freeing clusters that may now be referenced.
             */
            goto Cleanup;
        }

        CurrentVcn += AssignedClusters;
    }

    Status = STATUS_SUCCESS;

Cleanup:
    if (ZeroBuffer != NULL)
        ExFreePoolWithTag(ZeroBuffer, TAG_NTFS);
    if (OwnsFileRecord)
        ExFreeToNPagedLookasideList(&Vcb->FileRecLookasideList, FileRecord);

    return Status;
}

/**
* @name ReadAttributeToIrp
* @implemented
*
* Reads a range of an attribute, serving it out of the given IRP where the
* shape of the request allows it.
*
* @param Vcb
* Volume the attribute lives on
*
* @param Context
* Attribute to read from
*
* @param Offset
* Byte offset into the attribute to start reading at
*
* @param Buffer
* System-space buffer receiving the data
*
* @param Length
* How much to read, in bytes
*
* @param Irp
* The request being served, or NULL. When supplied and the range is a single
* aligned contiguous piece of the volume, the IRP goes straight to the storage
* stack and the disk transfers directly into the caller's pages.
*
* @return
* The number of bytes read, or zero on failure.
*
* @remarks An IRP may only be passed when Buffer is the start of that IRP's
* buffer and nothing has been realigned or bounced, since a forwarded transfer
* lands at the beginning of the IRP's MDL.
*
*/
ULONG
ReadAttributeToIrp(PDEVICE_EXTENSION Vcb,
                   PNTFS_ATTR_CONTEXT Context,
                   ULONGLONG Offset,
                   PCHAR Buffer,
                   ULONG Length,
                   PIRP Irp)
{
    ULONG AlreadyRead = 0;
    ULONG Transferred = 0;
    NTSTATUS Status;
    NTFS_IO_RUN_LIST RunList;

    if (Vcb == NULL || Context == NULL || Context->pRecord == NULL ||
        (Length != 0 && Buffer == NULL))
    {
        return 0;
    }

    if (!Context->pRecord->IsNonResident)
    {
        ULONG ValueOffset = Context->pRecord->Resident.ValueOffset;
        ULONG AttributeSize = Context->pRecord->Length;
        ULONG ValueLength = Context->pRecord->Resident.ValueLength;
        ULONG AvailableLength;

        // The resident value must remain inside its attribute, not merely
        // somewhere inside the larger owning MFT record.
        if (ValueOffset > AttributeSize || ValueLength > AttributeSize - ValueOffset)
        {
            DPRINT1("Invalid resident attribute range!\\n");
            return 0;
        }

        // Check before narrowing Offset or doing pointer arithmetic.
        if (Offset > ValueLength)
            return 0;

        AvailableLength = ValueLength - (ULONG)Offset;
        if (Length > AvailableLength)
            Length = AvailableLength;

        if (Length != 0)
        {
            RtlCopyMemory(Buffer,
                          (PVOID)((ULONG_PTR)Context->pRecord + ValueOffset + (ULONG)Offset),
                          Length);
        }
        return Length;
    }

    /*
     * Non-resident attribute: map the requested range onto the volume, then
     * issue every run it spans in a single parallel batch.
     */

    NtfsInitIoRunList(&RunList);

    Status = NtfsMapAttributeRuns(Vcb, Context, Offset, Length, &RunList, &AlreadyRead);

    if (NT_SUCCESS(Status) && AlreadyRead != 0)
    {
        Status = NtfsPerformIrpIoRuns(Vcb->StorageDevice,
                                      IRP_MJ_READ,
                                      Vcb->NtfsInfo.BytesPerSector,
                                      Irp,
                                      (PUCHAR)Buffer,
                                      &RunList,
                                      FALSE,
                                      &Transferred);
    }

    NtfsFreeIoRunList(&RunList);

    if (!NT_SUCCESS(Status))
    {
        /* Our callers read the return value as a byte count and treat zero as
         * failure, so we cannot report a partial transfer here. */
        DPRINT1("Read failure (Status %lx)\n", Status);
        return 0;
    }

    if (Transferred < AlreadyRead)
    {
        DPRINT1("Short read: expected %lu bytes, got %lu\n", AlreadyRead, Transferred);
        AlreadyRead = Transferred;
    }

    return AlreadyRead;
}

ULONG
ReadAttribute(PDEVICE_EXTENSION Vcb,
              PNTFS_ATTR_CONTEXT Context,
              ULONGLONG Offset,
              PCHAR Buffer,
              ULONG Length)
{
    return ReadAttributeToIrp(Vcb, Context, Offset, Buffer, Length, NULL);
}


/**
* @name WriteAttribute
* @implemented
*
* Writes an NTFS attribute to the disk. It presently borrows a lot of code from ReadAttribute(),
* and it still needs more documentation / cleaning up.
*
* @param Vcb
* Volume Control Block indicating which volume to write the attribute to
*
* @param Context
* Pointer to an NTFS_ATTR_CONTEXT that has information about the attribute
*
* @param Offset
* Offset, in bytes, from the beginning of the attribute indicating where to start
* writing data
*
* @param Buffer
* The data that's being written to the device
*
* @param Length
* How much data will be written, in bytes
*
* @param RealLengthWritten
* Pointer to a ULONG which will receive how much data was written, in bytes
*
* @param FileRecord
* Optional pointer to a FILE_RECORD_HEADER that contains a copy of the file record
* being written to. Can be NULL, in which case the file record will be read from disk.
* If not-null, WriteAttribute() will skip reading from disk, and FileRecord
* will be updated with the newly-written attribute before the function returns.
*
* @return
* STATUS_SUCCESS if successful, an error code otherwise. STATUS_NOT_IMPLEMENTED if
* a sparse hole reaches the low-level writer without first being materialized
* by the FCB-aware write path.
*
* @remarks Note that in this context the word "attribute" isn't referring read-only, hidden,
* etc. - the file's data is actually stored in an attribute in NTFS parlance.
*
*/

NTSTATUS
WriteAttributeFromIrp(PDEVICE_EXTENSION Vcb,
                      PNTFS_ATTR_CONTEXT Context,
                      ULONGLONG Offset,
                      const PUCHAR Buffer,
                      ULONG Length,
                      PULONG RealLengthWritten,
                      PFILE_RECORD_HEADER FileRecord,
                      PIRP Irp)
{
    ULONG MappedLength = 0;
    ULONG Transferred = 0;
    NTSTATUS Status;
    BOOLEAN FileRecordAllocated = FALSE;
    NTFS_IO_RUN_LIST RunList;


    DPRINT("WriteAttribute(%p, %p, %I64u, %p, %lu, %p, %p)\n", Vcb, Context, Offset, Buffer, Length, RealLengthWritten, FileRecord);

    if (Vcb == NULL || Context == NULL || Context->pRecord == NULL ||
        RealLengthWritten == NULL || (Length != 0 && Buffer == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }

    *RealLengthWritten = 0;
    if (Length == 0)
        return STATUS_SUCCESS;

    NtfsInitIoRunList(&RunList);

    // is this a resident attribute?
    if (!Context->pRecord->IsNonResident)
    {
        ULONG AttributeOffset;
        ULONG ValueOffset;
        ULONG RecordBytesRemaining;
        PNTFS_ATTR_CONTEXT FoundContext;
        PNTFS_ATTR_RECORD Destination;
        PUCHAR OriginalValue = NULL;

        // Subtraction-based bounds checks avoid wrapping Offset + Length.
        if (Offset > Context->pRecord->Resident.ValueLength ||
            Length > Context->pRecord->Resident.ValueLength - (ULONG)Offset)
        {
            DPRINT1("DRIVER ERROR: Attribute is too small!\\n");
            return STATUS_INVALID_PARAMETER;
        }

        // Do we need to read the file record?
        if (FileRecord == NULL)
        {
            FileRecord = ExAllocateFromNPagedLookasideList(&Vcb->FileRecLookasideList);
            if (!FileRecord)
            {
                DPRINT1("Error: Couldn't allocate file record!\\n");
                return STATUS_NO_MEMORY;
            }

            FileRecordAllocated = TRUE;

            // A failed read must never leave us editing an uninitialized record.
            Status = ReadFileRecord(Vcb, Context->FileMFTIndex, FileRecord);
            if (!NT_SUCCESS(Status))
            {
                ExFreeToNPagedLookasideList(&Vcb->FileRecLookasideList, FileRecord);
                return Status;
            }
        }

        // find where to write the attribute data to
        Status = FindAttribute(Vcb, FileRecord,
                               Context->pRecord->Type,
                               (PCWSTR)((ULONG_PTR)Context->pRecord + Context->pRecord->NameOffset),
                               Context->pRecord->NameLength,
                               &FoundContext,
                               &AttributeOffset);

        if (!NT_SUCCESS(Status))
        {
            DPRINT1("ERROR: Couldn't find matching attribute!\\n");
            if (FileRecordAllocated)
                ExFreeToNPagedLookasideList(&Vcb->FileRecLookasideList, FileRecord);
            return Status;
        }

        Destination = (PNTFS_ATTR_RECORD)((ULONG_PTR)FileRecord + AttributeOffset);
        ValueOffset = Destination->Resident.ValueOffset;

        // The destination value must fit inside the actual attribute extent.
        if (ValueOffset > Destination->Length ||
            Destination->Resident.ValueLength > Destination->Length - ValueOffset ||
            Offset > Destination->Resident.ValueLength ||
            Length > Destination->Resident.ValueLength - (ULONG)Offset)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto ResidentWriteCleanup;
        }

        // Validate the complete destination range without additive overflow.
        if (AttributeOffset > Vcb->NtfsInfo.BytesPerFileRecord)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto ResidentWriteCleanup;
        }

        RecordBytesRemaining = Vcb->NtfsInfo.BytesPerFileRecord - AttributeOffset;
        if (ValueOffset > RecordBytesRemaining)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto ResidentWriteCleanup;
        }

        RecordBytesRemaining -= ValueOffset;
        if (Offset > RecordBytesRemaining ||
            Length > RecordBytesRemaining - (ULONG)Offset)
        {
            DPRINT1("DRIVER ERROR: Data being written extends past end of file record!\\n");
            Status = STATUS_INVALID_PARAMETER;
            goto ResidentWriteCleanup;
        }

        // Keep a copy so a failed metadata write cannot leave the caller's
        // in-memory MFT record showing an update that was not committed.
        if (Length != 0)
        {
            OriginalValue = ExAllocatePoolWithTag(NonPagedPool, Length, TAG_NTFS);
            if (OriginalValue == NULL)
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                goto ResidentWriteCleanup;
            }

            RtlCopyMemory(OriginalValue,
                          (PCHAR)((ULONG_PTR)Destination + ValueOffset + (ULONG)Offset),
                          Length);
        }

        // The validated range is now safe to narrow to ULONG.
        RtlCopyMemory((PCHAR)((ULONG_PTR)Destination + ValueOffset + (ULONG)Offset),
                      Buffer,
                      Length);

        Status = UpdateFileRecord(Vcb, Context->FileMFTIndex, FileRecord);

        // Publish the new context only after the complete record write.
        if (NT_SUCCESS(Status))
        {
            ASSERT(Context->pRecord->Length == Destination->Length);
            RtlCopyMemory((PVOID)Context->pRecord, Destination, Context->pRecord->Length);
            *RealLengthWritten = Length;
        }
        else if (OriginalValue != NULL)
        {
            RtlCopyMemory((PCHAR)((ULONG_PTR)Destination + ValueOffset + (ULONG)Offset),
                          OriginalValue,
                          Length);
        }

ResidentWriteCleanup:
        if (OriginalValue != NULL)
            ExFreePoolWithTag(OriginalValue, TAG_NTFS);
        ReleaseAttributeContext(FoundContext);
        if (FileRecordAllocated)
            ExFreeToNPagedLookasideList(&Vcb->FileRecLookasideList, FileRecord);

        return Status;
    }

    Status = NtfsMapAttributeRuns(Vcb, Context, Offset, Length, &RunList, &MappedLength);

    if (NT_SUCCESS(Status) && MappedLength < Length)
    {
        // The attribute's allocation ends before the request does.
        // TODO: assign new clusters to the end of the file.
        // [We can reach here by creating a new file record when the MFT isn't large enough]
        DPRINT1("FIXME: Attribute needs to be enlarged (mapped %lu of %lu bytes).\n",
                MappedLength, Length);
        Status = STATUS_END_OF_FILE;
    }

    if (NT_SUCCESS(Status))
    {
        Status = NtfsPerformIrpIoRuns(Vcb->StorageDevice,
                                      IRP_MJ_WRITE,
                                      Vcb->NtfsInfo.BytesPerSector,
                                      Irp,
                                      Buffer,
                                      &RunList,
                                      FALSE,
                                      &Transferred);
    }

    NtfsFreeIoRunList(&RunList);

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Write failure (Status %lx)\n", Status);
        *RealLengthWritten = 0;
        return Status;
    }

    *RealLengthWritten = Transferred;

    if (Transferred < Length)
    {
        DPRINT1("Short write: expected %lu bytes, wrote %lu\n", Length, Transferred);
        Status = STATUS_UNEXPECTED_IO_ERROR;
    }

    return Status;
}

NTSTATUS
WriteAttribute(PDEVICE_EXTENSION Vcb,
               PNTFS_ATTR_CONTEXT Context,
               ULONGLONG Offset,
               const PUCHAR Buffer,
               ULONG Length,
               PULONG RealLengthWritten,
               PFILE_RECORD_HEADER FileRecord)
{
    return WriteAttributeFromIrp(Vcb,
                                 Context,
                                 Offset,
                                 Buffer,
                                 Length,
                                 RealLengthWritten,
                                 FileRecord,
                                 NULL);
}

/**
* @name NtfsReadUpCaseTable
* @implemented
*
* Loads the volume's $UpCase file, which defines how NTFS folds case.
*
* @param Vcb
* Volume to load the table for
*
* @return
* STATUS_SUCCESS if the table was loaded, otherwise the failure that stopped
* us. A volume without a usable $UpCase is not a valid NTFS volume.
*
* @remarks Every NTFS volume carries this file at a fixed MFT index. It is one
* upcased code point per UCS-2 value, so exactly 128 KB.
*
*/
static
NTSTATUS
NtfsReadUpCaseTable(PDEVICE_EXTENSION Vcb)
{
    PFILE_RECORD_HEADER FileRecord;
    PNTFS_ATTR_CONTEXT DataContext;
    NTSTATUS Status;
    ULONGLONG Size;
    ULONG Read;

    FileRecord = ExAllocateFromNPagedLookasideList(&Vcb->FileRecLookasideList);
    if (FileRecord == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = ReadFileRecord(Vcb, NTFS_FILE_UPCASE, FileRecord);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Couldn't read the $UpCase file record!\n");
        ExFreeToNPagedLookasideList(&Vcb->FileRecLookasideList, FileRecord);
        return Status;
    }

    Status = FindAttribute(Vcb, FileRecord, AttributeData, L"", 0, &DataContext, NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("$UpCase has no data stream!\n");
        ExFreeToNPagedLookasideList(&Vcb->FileRecLookasideList, FileRecord);
        return Status;
    }

    Size = AttributeDataLength(DataContext->pRecord);
    if (Size != (0x10000 * sizeof(WCHAR)))
    {
        DPRINT1("$UpCase is %I64u bytes, expected %u\n", Size, 0x10000 * sizeof(WCHAR));
        ReleaseAttributeContext(DataContext);
        ExFreeToNPagedLookasideList(&Vcb->FileRecLookasideList, FileRecord);
        return STATUS_UNRECOGNIZED_VOLUME;
    }

    Vcb->UpCaseTable = ExAllocatePoolWithTag(NonPagedPool, (ULONG)Size, TAG_NTFS);
    if (Vcb->UpCaseTable == NULL)
    {
        ReleaseAttributeContext(DataContext);
        ExFreeToNPagedLookasideList(&Vcb->FileRecLookasideList, FileRecord);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Read = ReadAttribute(Vcb, DataContext, 0, (PCHAR)Vcb->UpCaseTable, (ULONG)Size);

    ReleaseAttributeContext(DataContext);
    ExFreeToNPagedLookasideList(&Vcb->FileRecLookasideList, FileRecord);

    if (Read != Size)
    {
        DPRINT1("Short read of $UpCase: %lu of %I64u bytes\n", Read, Size);
        ExFreePoolWithTag(Vcb->UpCaseTable, TAG_NTFS);
        Vcb->UpCaseTable = NULL;
        return STATUS_UNSUCCESSFUL;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
NtfsLoadUpCaseTable(PDEVICE_EXTENSION Vcb)
{
    return NtfsReadUpCaseTable(Vcb);
}

NTSTATUS
ReadFileRecord(PDEVICE_EXTENSION Vcb,
               ULONGLONG index,
               PFILE_RECORD_HEADER file)
{
    ULONGLONG BytesRead;
    ULONGLONG RecordOffset;
    ULONGLONG MftDataSize;
    ULONG RecordSize;

    if (Vcb == NULL || file == NULL || Vcb->MFTContext == NULL ||
        Vcb->MFTContext->pRecord == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    RecordSize = Vcb->NtfsInfo.BytesPerFileRecord;
    if (RecordSize == 0 || index > (ULONGLONG)-1 / RecordSize)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    RecordOffset = index * RecordSize;
    MftDataSize = AttributeDataLength(Vcb->MFTContext->pRecord);

    /* Do not let a corrupt file reference wrap the byte offset or read a
     * record beyond the initialized MFT data stream. */
    if (RecordOffset > MftDataSize ||
        RecordSize > MftDataSize - RecordOffset)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    DPRINT("ReadFileRecord(%p, %I64x, %p)\n", Vcb, index, file);

    BytesRead = ReadAttribute(Vcb, Vcb->MFTContext, RecordOffset, (PCHAR)file, RecordSize);
    if (BytesRead != RecordSize)
    {
        DPRINT1("ReadFileRecord failed: %I64u read, %lu expected\n", BytesRead, RecordSize);
        return STATUS_PARTIAL_COPY;
    }

    /* Apply update sequence array fixups. */
    DPRINT("Sequence number: %u\n", file->SequenceNumber);
    return FixupUpdateSequenceArray(Vcb, &file->Ntfs);
}


/**
* @name NtfsUpdateDuplicatedInformation
* @implemented
*
* Pushes the fields NTFS keeps in more than one place out to all of them.
*
* @param Vcb
* Volume the file lives on
*
* @param FileRecord
* The file's record, already holding the new values in $STANDARD_INFORMATION
* and its $DATA attribute
*
* @param MFTIndex
* MFT index of the file
*
* @param Flags
* Which groups of fields changed; see NTFS_FILENAME_UPDATE_*
*
* @param CaseSensitive
* Whether the parent's index should be searched case-sensitively
*
* @return
* STATUS_SUCCESS once every copy agrees, or the status of the failing update.
*
* @remarks NTFS stores a file's sizes, timestamps and attributes three times:
* in $STANDARD_INFORMATION, in the record's own $FILE_NAME, and again in the
* parent directory's index entry. Windows funnels all three through one
* routine (NtfsUpdateDuplicateInfo) precisely so they cannot drift; this is
* that funnel. Callers change $STANDARD_INFORMATION and then say what moved.
*
* The file record is written back here, so callers need not write it again.
*
* The directory flag is the one field that does not come from
* $STANDARD_INFORMATION -- NTFS masks it out of there and keeps it in the
* record header -- so it is restored from the header on the way past.
*/
NTSTATUS
NtfsUpdateDuplicatedInformation(PDEVICE_EXTENSION Vcb,
                                PFILE_RECORD_HEADER FileRecord,
                                ULONGLONG MFTIndex,
                                ULONG Flags,
                                BOOLEAN CaseSensitive)
{
    NTFS_FILENAME_UPDATE Update;
    PSTANDARD_INFORMATION StdInfo;
    PFILENAME_ATTRIBUTE FileName;
    UNICODE_STRING Name;
    ULONGLONG ParentMFTIndex;
    ULONG DirFlag;
    NTSTATUS Status;

    if (Flags == 0)
        return STATUS_SUCCESS;

    StdInfo = GetStandardInformationFromRecord(Vcb, FileRecord);
    FileName = GetBestFileNameFromRecord(Vcb, FileRecord);
    if (StdInfo == NULL || FileName == NULL)
    {
        DPRINT1("Record %I64u is missing $STANDARD_INFORMATION or $FILE_NAME\n", MFTIndex);
        return STATUS_INVALID_PARAMETER;
    }

    DirFlag = (FileRecord->Flags & FRH_DIRECTORY) ? NTFS_FILE_TYPE_DIRECTORY : 0;

    RtlZeroMemory(&Update, sizeof(Update));
    Update.Flags = Flags;

    if (Flags & NTFS_FILENAME_UPDATE_TIMES)
    {
        Update.CreationTime = StdInfo->CreationTime;
        Update.LastWriteTime = StdInfo->LastWriteTime;
        Update.ChangeTime = StdInfo->ChangeTime;
        Update.LastAccessTime = StdInfo->LastAccessTime;

        FileName->CreationTime = StdInfo->CreationTime;
        FileName->LastWriteTime = StdInfo->LastWriteTime;
        FileName->ChangeTime = StdInfo->ChangeTime;
        FileName->LastAccessTime = StdInfo->LastAccessTime;
    }

    if (Flags & NTFS_FILENAME_UPDATE_ATTRS)
    {
        Update.FileAttributes = StdInfo->FileAttribute | DirFlag;
        FileName->FileAttributes = Update.FileAttributes;
    }

    if (Flags & NTFS_FILENAME_UPDATE_SIZES)
    {
        /* Sizes come from the unnamed $DATA attribute, the same way the times
         * come from $STANDARD_INFORMATION: one source of truth per field. */
        Update.DataSize = NtfsGetFileSize(Vcb, FileRecord, L"", 0, &Update.AllocatedSize);

        FileName->DataSize = Update.DataSize;
        FileName->AllocatedSize = Update.AllocatedSize;
    }

    /* Persist the $FILE_NAME just modified. Callers have no reason to write the record again,
     * and leaving it unwritten would let the copy in the parent index disagree with it. */
    Status = UpdateFileRecord(Vcb, MFTIndex, FileRecord);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to write file record %I64u (Status %lx)\n", MFTIndex, Status);
        return Status;
    }

    /* Now the parent's index entry, which carries the same values */
    ParentMFTIndex = FileName->DirectoryFileReferenceNumber & NTFS_MFT_MASK;

    Name.Buffer = FileName->Name;
    Name.Length = FileName->NameLength * sizeof(WCHAR);
    Name.MaximumLength = Name.Length;

    return UpdateFileNameRecord(Vcb, ParentMFTIndex, &Name, FALSE, &Update, CaseSensitive);
}

/**
* Searches a file's parent directory (given the parent's index in the mft)
* for the given file. Upon finding an index entry for that file, refreshes the
* fields of that entry's $FILE_NAME attribute named by Update->Flags.
*
* (Most of this code was copied from NtfsFindMftRecord)
*/
static
NTSTATUS
UpdateFileNameRecordWorker(PDEVICE_EXTENSION Vcb,
                           ULONGLONG ParentMFTIndex,
                           PUNICODE_STRING FileName,
                           BOOLEAN DirSearch,
                           PNTFS_FILENAME_UPDATE Update,
                           BOOLEAN CaseSensitive)
{
    PFILE_RECORD_HEADER MftRecord;
    PNTFS_ATTR_CONTEXT IndexRootCtx;
    PINDEX_ROOT_ATTRIBUTE IndexRoot;
    PCHAR IndexRecord;
    PINDEX_ENTRY_ATTRIBUTE IndexEntry, IndexEntryEnd;
    NTSTATUS Status;
    ULONG CurrentEntry = 0;

    DPRINT("UpdateFileNameRecord(%p, %I64d, %wZ, %s, %lx, %s)\n",
           Vcb,
           ParentMFTIndex,
           FileName,
           DirSearch ? "TRUE" : "FALSE",
           Update->Flags,
           CaseSensitive ? "TRUE" : "FALSE");

    if (Update->Flags == 0)
        return STATUS_SUCCESS;

    MftRecord = ExAllocateFromNPagedLookasideList(&Vcb->FileRecLookasideList);
    if (MftRecord == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = ReadFileRecord(Vcb, ParentMFTIndex, MftRecord);
    if (!NT_SUCCESS(Status))
    {
        ExFreeToNPagedLookasideList(&Vcb->FileRecLookasideList, MftRecord);
        return Status;
    }

    ASSERT(MftRecord->Ntfs.Type == NRH_FILE_TYPE);
    Status = FindAttribute(Vcb, MftRecord, AttributeIndexRoot, L"$I30", 4, &IndexRootCtx, NULL);
    if (!NT_SUCCESS(Status))
    {
        ExFreeToNPagedLookasideList(&Vcb->FileRecLookasideList, MftRecord);
        return Status;
    }

    IndexRecord = ExAllocatePoolWithTag(NonPagedPool, Vcb->NtfsInfo.BytesPerIndexRecord, TAG_NTFS);
    if (IndexRecord == NULL)
    {
        ReleaseAttributeContext(IndexRootCtx);
        ExFreeToNPagedLookasideList(&Vcb->FileRecLookasideList, MftRecord);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = ReadAttribute(Vcb, IndexRootCtx, 0, IndexRecord, AttributeDataLength(IndexRootCtx->pRecord));
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ERROR: Failed to read Index Root!\n");
        ExFreePoolWithTag(IndexRecord, TAG_NTFS);
        ReleaseAttributeContext(IndexRootCtx);
        ExFreeToNPagedLookasideList(&Vcb->FileRecLookasideList, MftRecord);
        return Status;
    }

    IndexRoot = (PINDEX_ROOT_ATTRIBUTE)IndexRecord;
    IndexEntry = (PINDEX_ENTRY_ATTRIBUTE)((PCHAR)&IndexRoot->Header + IndexRoot->Header.FirstEntryOffset);
    // Index root is always resident.
    /* TotalSizeOfEntries is measured from the index header, not from the
     * start of the attribute, and FirstEntryOffset above already is. */
    IndexEntryEnd = (PINDEX_ENTRY_ATTRIBUTE)((ULONG_PTR)&IndexRoot->Header + IndexRoot->Header.TotalSizeOfEntries);

    DPRINT("IndexRecordSize: %x IndexBlockSize: %x\n", Vcb->NtfsInfo.BytesPerIndexRecord, IndexRoot->SizeOfEntry);

    Status = UpdateIndexEntryFileName(Vcb,
                                      MftRecord,
                                      IndexRecord,
                                      IndexRoot->SizeOfEntry,
                                      IndexEntry,
                                      IndexEntryEnd,
                                      FileName,
                                      &CurrentEntry,
                                      &CurrentEntry,
                                      DirSearch,
                                      Update,
                                      CaseSensitive);

    if (Status == STATUS_PENDING)
    {
        // we need to write the index root attribute back to disk
        ULONG LengthWritten;
        Status = WriteAttribute(Vcb, IndexRootCtx, 0, (PUCHAR)IndexRecord, AttributeDataLength(IndexRootCtx->pRecord), &LengthWritten, MftRecord);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("ERROR: Couldn't update Index Root!\n");
        }

    }

    ReleaseAttributeContext(IndexRootCtx);
    ExFreePoolWithTag(IndexRecord, TAG_NTFS);
    ExFreeToNPagedLookasideList(&Vcb->FileRecLookasideList, MftRecord);

    return Status;
}

/* The worker rewrites the directory record from a copy it reads, so it
 * must not overlap any other update of a directory index */
NTSTATUS
UpdateFileNameRecord(PDEVICE_EXTENSION Vcb,
                     ULONGLONG ParentMFTIndex,
                     PUNICODE_STRING FileName,
                     BOOLEAN DirSearch,
                     PNTFS_FILENAME_UPDATE Update,
                     BOOLEAN CaseSensitive)
{
    NTSTATUS Status;

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&Vcb->IndexResource, TRUE);
    Status = UpdateFileNameRecordWorker(Vcb, ParentMFTIndex, FileName, DirSearch, Update, CaseSensitive);
    ExReleaseResourceLite(&Vcb->IndexResource);
    KeLeaveCriticalRegion();

    return Status;
}

/**
* Recursively searches a directory index and applies the requested update to the $FILE_NAME
* attribute of the proper index entry.
* (Heavily based on BrowseIndexEntries)
*/
NTSTATUS
UpdateIndexEntryFileName(PDEVICE_EXTENSION Vcb,
                         PFILE_RECORD_HEADER MftRecord,
                         PCHAR IndexRecord,
                         ULONG IndexBlockSize,
                         PINDEX_ENTRY_ATTRIBUTE FirstEntry,
                         PINDEX_ENTRY_ATTRIBUTE LastEntry,
                         PUNICODE_STRING FileName,
                         PULONG StartEntry,
                         PULONG CurrentEntry,
                         BOOLEAN DirSearch,
                         PNTFS_FILENAME_UPDATE Update,
                         BOOLEAN CaseSensitive)
{
    NTSTATUS Status;
    ULONGLONG RecordOffset;
    ULONG BytesRead;
    PINDEX_ENTRY_ATTRIBUTE IndexEntry;
    PNTFS_ATTR_CONTEXT IndexAllocationCtx;
    ULONGLONG IndexAllocationSize;
    PINDEX_BUFFER IndexBuffer;

    DPRINT("UpdateIndexEntryFileName(%p, %p, %p, %lu, %p, %p, %wZ, %lu, %lu, %s, %lx, %s)\n",
           Vcb,
           MftRecord,
           IndexRecord,
           IndexBlockSize,
           FirstEntry,
           LastEntry,
           FileName,
           *StartEntry,
           *CurrentEntry,
           DirSearch ? "TRUE" : "FALSE",
           Update->Flags,
           CaseSensitive ? "TRUE" : "FALSE");

    // find the index entry responsible for the file we're trying to update
    IndexEntry = FirstEntry;
    while (IndexEntry < LastEntry &&
           !(IndexEntry->Flags & NTFS_INDEX_ENTRY_END))
    {
        if ((IndexEntry->Data.Directory.IndexedFile & NTFS_MFT_MASK) > NTFS_FILE_FIRST_USER_FILE &&
            *CurrentEntry >= *StartEntry &&
            IndexEntry->FileName.NameType != NTFS_FILE_NAME_DOS &&
            CompareFileName(Vcb, FileName, IndexEntry, DirSearch, CaseSensitive))
        {
            *StartEntry = *CurrentEntry;

            if (Update->Flags & NTFS_FILENAME_UPDATE_SIZES)
            {
                IndexEntry->FileName.DataSize = Update->DataSize;
                IndexEntry->FileName.AllocatedSize = Update->AllocatedSize;
            }

            if (Update->Flags & NTFS_FILENAME_UPDATE_TIMES)
            {
                IndexEntry->FileName.CreationTime = Update->CreationTime;
                IndexEntry->FileName.ChangeTime = Update->ChangeTime;
                IndexEntry->FileName.LastWriteTime = Update->LastWriteTime;
                IndexEntry->FileName.LastAccessTime = Update->LastAccessTime;
            }

            if (Update->Flags & NTFS_FILENAME_UPDATE_ATTRS)
            {
                IndexEntry->FileName.FileAttributes = Update->FileAttributes;
            }

            // indicate that the caller will still need to write the structure to the disk
            return STATUS_PENDING;
        }

        (*CurrentEntry) += 1;
        ASSERT(IndexEntry->Length >= sizeof(INDEX_ENTRY_ATTRIBUTE));
        IndexEntry = (PINDEX_ENTRY_ATTRIBUTE)((PCHAR)IndexEntry + IndexEntry->Length);
    }

    /* If we're already browsing a subnode */
    if (IndexRecord == NULL)
    {
        return STATUS_OBJECT_PATH_NOT_FOUND;
    }

    /* If there's no subnode */
    if (!(IndexEntry->Flags & NTFS_INDEX_ENTRY_NODE))
    {
        return STATUS_OBJECT_PATH_NOT_FOUND;
    }

    Status = FindAttribute(Vcb, MftRecord, AttributeIndexAllocation, L"$I30", 4, &IndexAllocationCtx, NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT("Corrupted filesystem!\n");
        return Status;
    }

    IndexAllocationSize = AttributeDataLength(IndexAllocationCtx->pRecord);
    if (Vcb->NtfsInfo.BytesPerSector == 0 ||
        IndexBlockSize == 0 ||
        IndexBlockSize % Vcb->NtfsInfo.BytesPerSector != 0 ||
        IndexAllocationSize < IndexBlockSize ||
        IndexAllocationSize % IndexBlockSize != 0)
    {
        ReleaseAttributeContext(IndexAllocationCtx);
        return STATUS_FILE_CORRUPT_ERROR;
    }

    Status = STATUS_OBJECT_PATH_NOT_FOUND;
    for (RecordOffset = 0;
         RecordOffset <= IndexAllocationSize - IndexBlockSize;
         RecordOffset += IndexBlockSize)
    {
        BytesRead = ReadAttribute(Vcb, IndexAllocationCtx, RecordOffset,
                                  IndexRecord, IndexBlockSize);
        if (BytesRead != IndexBlockSize)
        {
            Status = STATUS_DEVICE_DATA_ERROR;
            break;
        }

        Status = FixupUpdateSequenceArray(Vcb, &((PFILE_RECORD_HEADER)IndexRecord)->Ntfs);
        if (!NT_SUCCESS(Status))
        {
            break;
        }

        IndexBuffer = (PINDEX_BUFFER)IndexRecord;
        ASSERT(IndexBuffer->Ntfs.Type == NRH_INDX_TYPE);
        ASSERT(IndexBuffer->Header.AllocatedSize + FIELD_OFFSET(INDEX_BUFFER, Header) == IndexBlockSize);
        FirstEntry = (PINDEX_ENTRY_ATTRIBUTE)((ULONG_PTR)&IndexBuffer->Header + IndexBuffer->Header.FirstEntryOffset);
        LastEntry = (PINDEX_ENTRY_ATTRIBUTE)((ULONG_PTR)&IndexBuffer->Header + IndexBuffer->Header.TotalSizeOfEntries);
        ASSERT(LastEntry <= (PINDEX_ENTRY_ATTRIBUTE)((ULONG_PTR)IndexBuffer + IndexBlockSize));

        Status = UpdateIndexEntryFileName(NULL,
                                          NULL,
                                          NULL,
                                          0,
                                          FirstEntry,
                                          LastEntry,
                                          FileName,
                                          StartEntry,
                                          CurrentEntry,
                                          DirSearch,
                                          Update,
                                          CaseSensitive);
        if (Status == STATUS_PENDING)
        {
            // write the index record back to disk
            ULONG Written = 0;
            NTSTATUS RestoreStatus;

            // Encode the index block's update sequence array before writing.
            Status = AddFixupArray(Vcb, &((PFILE_RECORD_HEADER)IndexRecord)->Ntfs);
            if (!NT_SUCCESS(Status))
            {
                DPRINT1("Error: Failed to update fixup sequence array (Status %lx)!\n", Status);
                break;
            }

            Status = WriteAttribute(Vcb, IndexAllocationCtx, RecordOffset,
                                    (const PUCHAR)IndexRecord, IndexBlockSize,
                                    &Written, MftRecord);
            if (NT_SUCCESS(Status) && Written != IndexBlockSize)
            {
                DPRINT1("Short index block write: %lu written, %lu expected\n",
                        Written, IndexBlockSize);
                Status = STATUS_DEVICE_DATA_ERROR;
            }

            // Restore the in-memory index block even when the write fails.
            RestoreStatus = FixupUpdateSequenceArray(Vcb,
                                                     &((PFILE_RECORD_HEADER)IndexRecord)->Ntfs);
            if (!NT_SUCCESS(RestoreStatus))
            {
                DPRINT1("Unable to restore index block fixups (Status %lx)!\n",
                        RestoreStatus);
                if (NT_SUCCESS(Status))
                    Status = RestoreStatus;
            }

            if (!NT_SUCCESS(Status))
            {
                DPRINT1("ERROR Performing index block write (Status %lx)!\n", Status);
                break;
            }

            Status = STATUS_SUCCESS;
        }
        if (NT_SUCCESS(Status))
        {
            break;
        }
    }

    ReleaseAttributeContext(IndexAllocationCtx);
    return Status;
}

/**
* @name UpdateFileRecord
* @implemented
*
* Writes a file record to the master file table, at a given index.
*
* @param Vcb
* Pointer to the DEVICE_EXTENSION of the target drive being written to.
*
* @param MftIndex
* Target index in the master file table to store the file record.
*
* @param FileRecord
* Pointer to the complete file record which will be written to the master file table.
*
* @return
* STATUS_SUCCESS on a complete record write. Returns an I/O error or
* STATUS_DEVICE_DATA_ERROR when the lower layer reports a short write.
*
*/
NTSTATUS

UpdateFileRecord(PDEVICE_EXTENSION Vcb,
                 ULONGLONG MftIndex,
                 PFILE_RECORD_HEADER FileRecord)
{
    ULONG BytesWritten = 0;
    ULONGLONG RecordOffset;
    ULONGLONG MftDataSize;
    ULONG RecordSize;
    NTSTATUS Status;
    NTSTATUS RestoreStatus;

    if (Vcb == NULL ||
        FileRecord == NULL ||
        Vcb->MFTContext == NULL ||
        Vcb->MFTContext->pRecord == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    RecordSize = Vcb->NtfsInfo.BytesPerFileRecord;
    if (RecordSize == 0 || MftIndex > (ULONGLONG)-1 / RecordSize)
        return STATUS_INVALID_PARAMETER;

    RecordOffset = MftIndex * RecordSize;
    MftDataSize = AttributeDataLength(Vcb->MFTContext->pRecord);

    /* Keep writes within the current logical $MFT stream. Checking the
     * complete half-open range also protects the end offset from overflow. */
    if (RecordOffset > MftDataSize ||
        RecordSize > MftDataSize - RecordOffset)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    DPRINT("UpdateFileRecord(%p, 0x%I64x, %p)\n", Vcb, MftIndex, FileRecord);

    /* Add the fixup array to prepare the data for writing to disk. */
    Status = AddFixupArray(Vcb, &FileRecord->Ntfs);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Unable to prepare MFT record fixups (Status %lx)\n", Status);
        return Status;
    }

    /* Write the complete file record to the master file table. */
    Status = WriteAttribute(Vcb,
                            Vcb->MFTContext,
                            RecordOffset,
                            (const PUCHAR)FileRecord,
                            Vcb->NtfsInfo.BytesPerFileRecord,
                            &BytesWritten,
                            FileRecord);

    if (NT_SUCCESS(Status) &&
        BytesWritten != Vcb->NtfsInfo.BytesPerFileRecord)
    {
        DPRINT1("Short MFT record write: %lu written, %lu expected\n",
                BytesWritten, Vcb->NtfsInfo.BytesPerFileRecord);
        Status = STATUS_DEVICE_DATA_ERROR;
    }
    else if (!NT_SUCCESS(Status))
    {
        DPRINT1("MFT record write failed: %lu written, %lu expected (Status %lx)\n",
                BytesWritten, Vcb->NtfsInfo.BytesPerFileRecord, Status);
    }

    /* Always restore the caller's in-memory record after successful encoding. */
    RestoreStatus = FixupUpdateSequenceArray(Vcb, &FileRecord->Ntfs);
    if (!NT_SUCCESS(RestoreStatus))
    {
        DPRINT1("Unable to restore in-memory MFT record fixups (Status %lx)\n",
                RestoreStatus);
        if (NT_SUCCESS(Status))
            Status = RestoreStatus;
    }

    return Status;
}


/**
* @name AddNewMftEntry
* @implemented
*
* Adds a file record to the master file table of a given device.
*
* @param FileRecord
* Pointer to a complete file record which will be saved to disk.
*
* @param DeviceExt
* Pointer to the DEVICE_EXTENSION of the target drive.
*
* @param DestinationIndex
* Pointer to a ULONGLONG which will receive the MFT index where the file record was stored.
*
* @param CanWait
* Boolean indicating if the function is allowed to wait for exclusive access to the master file table.
* This will only be relevant if the MFT doesn't have any free file records and needs to be enlarged.
*
* @return
* STATUS_SUCCESS on success.
* STATUS_OBJECT_NAME_NOT_FOUND if we can't find the MFT's $Bitmap or if we weren't able
* to read the attribute.
* STATUS_INSUFFICIENT_RESOURCES if we can't allocate enough memory for a copy of $Bitmap.
* STATUS_CANT_WAIT if CanWait was FALSE and the function could not get immediate, exclusive access to the MFT.
*/
NTSTATUS
AddNewMftEntry(PFILE_RECORD_HEADER FileRecord,
               PDEVICE_EXTENSION DeviceExt,
               PULONGLONG DestinationIndex,
               BOOLEAN CanWait)
{
    NTSTATUS Status = STATUS_SUCCESS;
    ULONGLONG MftIndex;
    RTL_BITMAP Bitmap;
    ULONGLONG BitmapDataSize;
    ULONGLONG AttrBytesRead;
    PUCHAR BitmapData;
    PUCHAR BitmapBuffer;
    ULONG LengthWritten;
    PNTFS_ATTR_CONTEXT BitmapContext;
    LARGE_INTEGER BitmapBits;
    UCHAR SystemReservedBits;

    DPRINT("AddNewMftEntry(%p, %p, %p, %s)\n", FileRecord, DeviceExt, DestinationIndex, CanWait ? "TRUE" : "FALSE");

    // First, we have to read the mft's $Bitmap attribute

    // Find the attribute
    Status = FindAttribute(DeviceExt, DeviceExt->MasterFileTable, AttributeBitmap, L"", 0, &BitmapContext, NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ERROR: Couldn't find $Bitmap attribute of master file table!\n");
        return Status;
    }

    // Get size of bitmap
    BitmapDataSize = AttributeDataLength(BitmapContext->pRecord);

    // RtlInitializeBitmap wants a ULONG-aligned pointer, and wants the memory passed to it to be a ULONG-multiple
    // Allocate a buffer for the $Bitmap attribute plus enough to ensure we can get a ULONG-aligned pointer
    BitmapBuffer = ExAllocatePoolWithTag(NonPagedPool, BitmapDataSize + sizeof(ULONG), TAG_NTFS);
    if (!BitmapBuffer)
    {
        ReleaseAttributeContext(BitmapContext);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(BitmapBuffer, BitmapDataSize + sizeof(ULONG));

    // Get a ULONG-aligned pointer for the bitmap itself
    BitmapData = (PUCHAR)ALIGN_UP_BY((ULONG_PTR)BitmapBuffer, sizeof(ULONG));

    // read $Bitmap attribute
    AttrBytesRead = ReadAttribute(DeviceExt, BitmapContext, 0, (PCHAR)BitmapData, BitmapDataSize);

    if (AttrBytesRead != BitmapDataSize)
    {
        DPRINT1("ERROR: Unable to read $Bitmap attribute of master file table!\n");
        ExFreePoolWithTag(BitmapBuffer, TAG_NTFS);
        ReleaseAttributeContext(BitmapContext);
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    // We need to backup the bits for records 0x10 - 0x17 (3rd byte of bitmap) and mark these records
    // as in-use so we don't assign files to those indices. They're reserved for the system (e.g. ChkDsk).
    SystemReservedBits = BitmapData[2];
    BitmapData[2] = 0xff;

    // Calculate bit count
    BitmapBits.QuadPart = AttributeDataLength(DeviceExt->MFTContext->pRecord) /
                          DeviceExt->NtfsInfo.BytesPerFileRecord;
    if (BitmapBits.HighPart != 0)
    {
        DPRINT1("\tFIXME: bitmap sizes beyond 32bits are not yet supported! (Your NTFS volume is too large)\n");
        ExFreePoolWithTag(BitmapBuffer, TAG_NTFS);
        ReleaseAttributeContext(BitmapContext);
        return STATUS_NOT_IMPLEMENTED;
    }

    // convert buffer into bitmap
    RtlInitializeBitMap(&Bitmap, (PULONG)BitmapData, BitmapBits.LowPart);

    // set next available bit, preferrably after 23rd bit
    MftIndex = RtlFindClearBitsAndSet(&Bitmap, 1, 24);
    if ((LONG)MftIndex == -1)
    {
        DPRINT1("Couldn't find free space in MFT for file record, increasing MFT size.\n");

        ExFreePoolWithTag(BitmapBuffer, TAG_NTFS);
        ReleaseAttributeContext(BitmapContext);

        // Couldn't find a free record in the MFT, add some blank records and try again
        Status = IncreaseMftSize(DeviceExt, CanWait);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("ERROR: Couldn't find space in MFT for file or increase MFT size!\n");
            return Status;
        }

        return AddNewMftEntry(FileRecord, DeviceExt, DestinationIndex, CanWait);
    }

    DPRINT("Creating file record at MFT index: %I64u\n", MftIndex);

    // update file record with index
    FileRecord->MFTRecordNumber = MftIndex;

    // [BitmapData should have been updated via RtlFindClearBitsAndSet()]

    // Restore the system reserved bits
    BitmapData[2] = SystemReservedBits;

    // write the bitmap back to the MFT's $Bitmap attribute
    Status = WriteAttribute(DeviceExt, BitmapContext, 0, BitmapData, BitmapDataSize, &LengthWritten, FileRecord);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ERROR encountered when writing $Bitmap attribute!\n");
        ExFreePoolWithTag(BitmapBuffer, TAG_NTFS);
        ReleaseAttributeContext(BitmapContext);
        return Status;
    }

    // update the file record (write it to disk)
    Status = UpdateFileRecord(DeviceExt, MftIndex, FileRecord);

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ERROR: Unable to write file record!\n");
        ExFreePoolWithTag(BitmapBuffer, TAG_NTFS);
        ReleaseAttributeContext(BitmapContext);
        return Status;
    }

    *DestinationIndex = MftIndex;

    ExFreePoolWithTag(BitmapBuffer, TAG_NTFS);
    ReleaseAttributeContext(BitmapContext);

    return Status;
}

/**
* @name GetMaxIndexRootSize
* @implemented
*
* Calculates how many bytes of index entries the index root can hold, given the space
* left in the file record.
*
* @param DeviceExt
* Points to the target disk's DEVICE_EXTENSION.
*
* @param FileRecord
* Pointer to the file record containing the index root.
*
* @param IndexRootOffset
* Offset of the index root attribute within FileRecord.
*
* @param IndexRootRecord
* Pointer to the index root attribute record.
*
* @return
* The maximum size of the index root's entries, leaving room for any attributes that
* follow the index root.
*/
static
ULONG
GetMaxIndexRootSize(PDEVICE_EXTENSION DeviceExt,
                    PFILE_RECORD_HEADER FileRecord,
                    ULONG IndexRootOffset,
                    PNTFS_ATTR_RECORD IndexRootRecord)
{
    ULONG MaxIndexRootSize;
    PNTFS_ATTR_RECORD CurrentAttribute;

    // First, find the max index size assuming index root is the last attribute
    MaxIndexRootSize = DeviceExt->NtfsInfo.BytesPerFileRecord   // Start with the size of a file record
                       - IndexRootOffset                        // Subtract the length of everything that comes before index root
                       - IndexRootRecord->Resident.ValueOffset  // Subtract the length of the attribute header for index root
                       - sizeof(INDEX_ROOT_ATTRIBUTE)           // Subtract the length of the index root header
                       - (sizeof(ULONG) * 2);                   // Subtract the length of the file record end marker and padding

    // Leave room for any attributes after this one, not counting the end marker
    CurrentAttribute = (PNTFS_ATTR_RECORD)((ULONG_PTR)FileRecord + IndexRootOffset + IndexRootRecord->Length);
    while (CurrentAttribute->Type != AttributeEnd)
    {
        MaxIndexRootSize -= CurrentAttribute->Length;
        CurrentAttribute = (PNTFS_ATTR_RECORD)((ULONG_PTR)CurrentAttribute + CurrentAttribute->Length);
    }

    return MaxIndexRootSize;
}

/**
* @name NtfsAddFilenameToDirectory
* @implemented
*
* Adds a $FILE_NAME attribute to a given directory index.
*
* @param DeviceExt
* Points to the target disk's DEVICE_EXTENSION.
*
* @param DirectoryMftIndex
* Mft index of the parent directory which will receive the file.
*
* @param FileReferenceNumber
* File reference of the file to be added to the directory. This is a combination of the
* Mft index and sequence number.
*
* @param FilenameAttribute
* Pointer to the FILENAME_ATTRIBUTE of the file being added to the directory.
*
* @param CaseSensitive
* Boolean indicating if the function should operate in case-sensitive mode. This will be TRUE
* if an application created the file with the FILE_FLAG_POSIX_SEMANTICS flag.
*
* @return
* STATUS_SUCCESS on success.
* STATUS_INSUFFICIENT_RESOURCES if an allocation fails.
* STATUS_NOT_IMPLEMENTED if target address isn't at the end of the given file record.
*
* @remarks
* WIP - Can only support a few files in a directory.
* One FILENAME_ATTRIBUTE is added to the directory's index for each link to that file. So, each
* file which contains one FILENAME_ATTRIBUTE for a long name and another for the 8.3 name, will
* get both attributes added to its parent directory.
*/
static
NTSTATUS
AddFilenameToDirectoryWorker(PDEVICE_EXTENSION DeviceExt,
                             ULONGLONG DirectoryMftIndex,
                             ULONGLONG FileReferenceNumber,
                             PFILENAME_ATTRIBUTE FilenameAttribute,
                             BOOLEAN CaseSensitive)
{
    NTSTATUS Status = STATUS_SUCCESS;
    PFILE_RECORD_HEADER ParentFileRecord;
    PNTFS_ATTR_CONTEXT IndexRootContext;
    PINDEX_ROOT_ATTRIBUTE I30IndexRoot;
    ULONG IndexRootOffset;
    ULONGLONG I30IndexRootLength;
    ULONG LengthWritten;
    PINDEX_ROOT_ATTRIBUTE NewIndexRoot;
    ULONG AttributeLength;
    PB_TREE NewTree;
    ULONG BtreeIndexLength;
    ULONG MaxIndexRootSize;
    PB_TREE_KEY NewLeftKey;
    PB_TREE_FILENAME_NODE NewRightHandNode;
    LARGE_INTEGER MinIndexRootSize;
    ULONG NewMaxIndexRootSize;
    ULONG NodeSize;

    // Allocate memory for the parent directory
    ParentFileRecord = ExAllocateFromNPagedLookasideList(&DeviceExt->FileRecLookasideList);
    if (!ParentFileRecord)
    {
        DPRINT1("ERROR: Couldn't allocate memory for file record!\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    // Open the parent directory
    Status = ReadFileRecord(DeviceExt, DirectoryMftIndex, ParentFileRecord);
    if (!NT_SUCCESS(Status))
    {
        ExFreeToNPagedLookasideList(&DeviceExt->FileRecLookasideList, ParentFileRecord);
        DPRINT1("ERROR: Couldn't read parent directory with index %I64u\n",
                DirectoryMftIndex);
        return Status;
    }

#ifndef NDEBUG
    DPRINT1("Dumping old parent file record:\n");
    NtfsDumpFileRecord(DeviceExt, ParentFileRecord);
#endif

    // Find the index root attribute for the directory
    Status = FindAttribute(DeviceExt,
                           ParentFileRecord,
                           AttributeIndexRoot,
                           L"$I30",
                           4,
                           &IndexRootContext,
                           &IndexRootOffset);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ERROR: Couldn't find $I30 $INDEX_ROOT attribute for parent directory with MFT #: %I64u!\n",
                DirectoryMftIndex);
        ExFreeToNPagedLookasideList(&DeviceExt->FileRecLookasideList, ParentFileRecord);
        return Status;
    }

    // Find the maximum index size given what the file record can hold
    MaxIndexRootSize = GetMaxIndexRootSize(DeviceExt, ParentFileRecord, IndexRootOffset, IndexRootContext->pRecord);

    // Allocate memory for the index root data
    I30IndexRootLength = AttributeDataLength(IndexRootContext->pRecord);
    I30IndexRoot = ExAllocatePoolWithTag(NonPagedPool, I30IndexRootLength, TAG_NTFS);
    if (!I30IndexRoot)
    {
        DPRINT1("ERROR: Couldn't allocate memory for index root attribute!\n");
        ReleaseAttributeContext(IndexRootContext);
        ExFreeToNPagedLookasideList(&DeviceExt->FileRecLookasideList, ParentFileRecord);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    // Read the Index Root
    Status = ReadAttribute(DeviceExt, IndexRootContext, 0, (PCHAR)I30IndexRoot, I30IndexRootLength);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ERROR: Couln't read index root attribute for Mft index #%I64u\n", DirectoryMftIndex);
        ReleaseAttributeContext(IndexRootContext);
        ExFreePoolWithTag(I30IndexRoot, TAG_NTFS);
        ExFreeToNPagedLookasideList(&DeviceExt->FileRecLookasideList, ParentFileRecord);
        return Status;
    }

    // Convert the index to a B*Tree
    Status = CreateBTreeFromIndex(DeviceExt,
                                  ParentFileRecord,
                                  IndexRootContext,
                                  I30IndexRoot,
                                  &NewTree);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ERROR: Failed to create B-Tree from Index!\n");
        ReleaseAttributeContext(IndexRootContext);
        ExFreePoolWithTag(I30IndexRoot, TAG_NTFS);
        ExFreeToNPagedLookasideList(&DeviceExt->FileRecLookasideList, ParentFileRecord);
        return Status;
    }

#ifndef NDEBUG
    DumpBTree(NewTree);
#endif

    // Insert the key for the file we're adding
    Status = NtfsInsertKey(NewTree,
                           FileReferenceNumber,
                           FilenameAttribute,
                           NewTree->RootNode,
                           CaseSensitive,
                           MaxIndexRootSize,
                           I30IndexRoot->SizeOfEntry,
                           &NewLeftKey,
                           &NewRightHandNode);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ERROR: Failed to insert '%.*S' into the index of MFT record %I64u (Status %lx)\n",
                FilenameAttribute->NameLength,
                FilenameAttribute->Name,
                DirectoryMftIndex,
                Status);
        DestroyBTree(NewTree);
        ReleaseAttributeContext(IndexRootContext);
        ExFreePoolWithTag(I30IndexRoot, TAG_NTFS);
        ExFreeToNPagedLookasideList(&DeviceExt->FileRecLookasideList, ParentFileRecord);
        return Status;
    }

#ifndef NDEBUG
    DumpBTree(NewTree);
#endif

    // The root node can't be split
    ASSERT(NewLeftKey == NULL);
    ASSERT(NewRightHandNode == NULL);

    // Convert B*Tree back to Index

    // Updating the index allocation can change the size available for the index root,
    // And if the index root is demoted, the index allocation will need to be updated again,
    // which may change the size available for index root... etc.
    // My solution is to decrease index root to the size it would be if it was demoted,
    // then UpdateIndexAllocation will have an accurate representation of the maximum space
    // it can use in the file record. There's still a chance that the act of allocating an
    // index node after demoting the index root will increase the size of the file record beyond
    // it's limit, but if that happens, an attribute-list will most definitely be needed.
    // This a bit hacky, but it seems to be functional.

    // Calculate the minimum size of the index root attribute, considering one dummy key and one VCN
    MinIndexRootSize.QuadPart = sizeof(INDEX_ROOT_ATTRIBUTE) // size of the index root headers
                                + 0x18; // Size of dummy key with a VCN for a subnode
    ASSERT(MinIndexRootSize.QuadPart % ATTR_RECORD_ALIGNMENT == 0);

    // Temporarily shrink the index root to it's minimal size
    AttributeLength = MinIndexRootSize.LowPart;
    AttributeLength += sizeof(INDEX_ROOT_ATTRIBUTE);


    // FIXME: IndexRoot will probably be invalid until we're finished. If we fail before we finish, the directory will probably be toast.
    // The potential for catastrophic data-loss exists!!! :)

    // Update the length of the attribute in the file record of the parent directory
    Status = InternalSetResidentAttributeLength(DeviceExt,
                                                IndexRootContext,
                                                ParentFileRecord,
                                                IndexRootOffset,
                                                AttributeLength);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ERROR: Unable to set length of index root!\n");
        DestroyBTree(NewTree);
        ReleaseAttributeContext(IndexRootContext);
        ExFreePoolWithTag(I30IndexRoot, TAG_NTFS);
        ExFreeToNPagedLookasideList(&DeviceExt->FileRecLookasideList, ParentFileRecord);
        return Status;
    }

    // Update the index allocation
    Status = UpdateIndexAllocation(DeviceExt, NewTree, I30IndexRoot->SizeOfEntry, ParentFileRecord);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ERROR: Failed to update index allocation from B-Tree!\n");
        DestroyBTree(NewTree);
        ReleaseAttributeContext(IndexRootContext);
        ExFreePoolWithTag(I30IndexRoot, TAG_NTFS);
        ExFreeToNPagedLookasideList(&DeviceExt->FileRecLookasideList, ParentFileRecord);
        return Status;
    }

#ifndef NDEBUG
    DPRINT1("Index Allocation updated\n");
    DumpBTree(NewTree);
#endif

    // Find the maximum index root size given what the file record can hold
    NewMaxIndexRootSize = GetMaxIndexRootSize(DeviceExt, ParentFileRecord, IndexRootOffset, IndexRootContext->pRecord);

    // The index allocation and index bitmap may have grown, leaving less room for the index root,
    // so now we need to double-check that index root isn't too large
    NodeSize = GetSizeOfIndexEntries(NewTree->RootNode);
    if (NodeSize > NewMaxIndexRootSize)
    {
        DPRINT("Demoting index root.\nNodeSize: 0x%lx\nNewMaxIndexRootSize: 0x%lx\n", NodeSize, NewMaxIndexRootSize);

        Status = DemoteBTreeRoot(NewTree);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("ERROR: Failed to demote index root!\n");
            DestroyBTree(NewTree);
            ReleaseAttributeContext(IndexRootContext);
            ExFreePoolWithTag(I30IndexRoot, TAG_NTFS);
            ExFreeToNPagedLookasideList(&DeviceExt->FileRecLookasideList, ParentFileRecord);
            return Status;
        }

        // We need to update the index allocation once more
        Status = UpdateIndexAllocation(DeviceExt, NewTree, I30IndexRoot->SizeOfEntry, ParentFileRecord);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("ERROR: Failed to update index allocation from B-Tree!\n");
            DestroyBTree(NewTree);
            ReleaseAttributeContext(IndexRootContext);
            ExFreePoolWithTag(I30IndexRoot, TAG_NTFS);
            ExFreeToNPagedLookasideList(&DeviceExt->FileRecLookasideList, ParentFileRecord);
            return Status;
        }

        // re-recalculate max size of index root
        NewMaxIndexRootSize = GetMaxIndexRootSize(DeviceExt, ParentFileRecord, IndexRootOffset, IndexRootContext->pRecord);
    }

    // Create the Index Root from the B*Tree
    Status = CreateIndexRootFromBTree(DeviceExt, NewTree, NewMaxIndexRootSize, &NewIndexRoot, &BtreeIndexLength);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ERROR: Failed to create Index root from B-Tree!\n");
        DestroyBTree(NewTree);
        ReleaseAttributeContext(IndexRootContext);
        ExFreePoolWithTag(I30IndexRoot, TAG_NTFS);
        ExFreeToNPagedLookasideList(&DeviceExt->FileRecLookasideList, ParentFileRecord);
        return Status;
    }

    // We're done with the B-Tree now
    DestroyBTree(NewTree);

    // Write back the new index root attribute to the parent directory file record

    // First, we need to resize the attribute.
    // CreateIndexRootFromBTree() should have verified that the index root fits within MaxIndexSize.
    // We can't set the size as we normally would, because $INDEX_ROOT must always be resident.
    AttributeLength = NewIndexRoot->Header.AllocatedSize + FIELD_OFFSET(INDEX_ROOT_ATTRIBUTE, Header);

    if (AttributeLength != IndexRootContext->pRecord->Resident.ValueLength)
    {
        // Update the length of the attribute in the file record of the parent directory
        Status = InternalSetResidentAttributeLength(DeviceExt,
                                                    IndexRootContext,
                                                    ParentFileRecord,
                                                    IndexRootOffset,
                                                    AttributeLength);
        if (!NT_SUCCESS(Status))
        {
            ExFreePoolWithTag(NewIndexRoot, TAG_NTFS);
            ReleaseAttributeContext(IndexRootContext);
            ExFreePoolWithTag(I30IndexRoot, TAG_NTFS);
            ExFreeToNPagedLookasideList(&DeviceExt->FileRecLookasideList, ParentFileRecord);
            DPRINT1("ERROR: Unable to set resident attribute length!\n");
            return Status;
        }

    }

    NT_ASSERT(ParentFileRecord->BytesInUse <= DeviceExt->NtfsInfo.BytesPerFileRecord);

    Status = UpdateFileRecord(DeviceExt, DirectoryMftIndex, ParentFileRecord);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ERROR: Failed to update file record of directory with index: %llx\n", DirectoryMftIndex);
        ExFreeToNPagedLookasideList(&DeviceExt->FileRecLookasideList, ParentFileRecord);
        ExFreePoolWithTag(NewIndexRoot, TAG_NTFS);
        ReleaseAttributeContext(IndexRootContext);
        ExFreePoolWithTag(I30IndexRoot, TAG_NTFS);
        return Status;
    }

    // Write the new index root to disk
    Status = WriteAttribute(DeviceExt,
                            IndexRootContext,
                            0,
                            (PUCHAR)NewIndexRoot,
                            AttributeLength,
                            &LengthWritten,
                            ParentFileRecord);
    if (!NT_SUCCESS(Status) || LengthWritten != AttributeLength)
    {
        DPRINT1("ERROR: Unable to write new index root attribute to parent directory!\n");
        ExFreePoolWithTag(NewIndexRoot, TAG_NTFS);
        ReleaseAttributeContext(IndexRootContext);
        ExFreePoolWithTag(I30IndexRoot, TAG_NTFS);
        ExFreeToNPagedLookasideList(&DeviceExt->FileRecLookasideList, ParentFileRecord);
        return Status;
    }

    // re-read the parent file record, so we can dump it
    Status = ReadFileRecord(DeviceExt, DirectoryMftIndex, ParentFileRecord);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ERROR: Couldn't read parent directory after messing with it!\n");
    }
    else
    {
#ifndef NDEBUG
        DPRINT1("Dumping new B-Tree:\n");

        Status = CreateBTreeFromIndex(DeviceExt, ParentFileRecord, IndexRootContext, NewIndexRoot, &NewTree);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("ERROR: Couldn't re-create b-tree\n");
            return Status;
        }

        DumpBTree(NewTree);

        DestroyBTree(NewTree);

        NtfsDumpFileRecord(DeviceExt, ParentFileRecord);
#endif
    }

    // Cleanup
    ExFreePoolWithTag(NewIndexRoot, TAG_NTFS);
    ReleaseAttributeContext(IndexRootContext);
    ExFreePoolWithTag(I30IndexRoot, TAG_NTFS);
    ExFreeToNPagedLookasideList(&DeviceExt->FileRecLookasideList, ParentFileRecord);

    return Status;
}

/* The worker rewrites the directory record from a copy it reads, so it
 * must not overlap any other update of a directory index */
NTSTATUS
NtfsAddFilenameToDirectory(PDEVICE_EXTENSION DeviceExt,
                           ULONGLONG DirectoryMftIndex,
                           ULONGLONG FileReferenceNumber,
                           PFILENAME_ATTRIBUTE FilenameAttribute,
                           BOOLEAN CaseSensitive)
{
    NTSTATUS Status;

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&DeviceExt->IndexResource, TRUE);
    Status = AddFilenameToDirectoryWorker(DeviceExt, DirectoryMftIndex, FileReferenceNumber, FilenameAttribute, CaseSensitive);
    ExReleaseResourceLite(&DeviceExt->IndexResource);
    KeLeaveCriticalRegion();

    return Status;
}

/**
* @name NtfsRemoveFilenameFromDirectory
* @implemented
*
* Removes a $FILE_NAME attribute from a given directory index.
*
* @param DeviceExt
* Points to the target disk's DEVICE_EXTENSION.
*
* @param DirectoryMftIndex
* Mft index of the parent directory the file is being removed from.
*
* @param FilenameAttribute
* Pointer to the FILENAME_ATTRIBUTE of the file being removed from the directory.
*
* @param CaseSensitive
* Boolean indicating if the function should operate in case-sensitive mode. This will be TRUE
* if an application created the file with the FILE_FLAG_POSIX_SEMANTICS flag.
*
* @return
* STATUS_SUCCESS on success.
* STATUS_OBJECT_NAME_NOT_FOUND if the name isn't in the directory's index.
* STATUS_INSUFFICIENT_RESOURCES if an allocation fails.
*
* @remarks
* The inverse of NtfsAddFilenameToDirectory(). One $FILE_NAME attribute is indexed per link, so
* a file carrying both a long name and an 8.3 name needs one call for each.
*/
static
NTSTATUS
RemoveFilenameFromDirectoryWorker(PDEVICE_EXTENSION DeviceExt,
                                  ULONGLONG DirectoryMftIndex,
                                  PFILENAME_ATTRIBUTE FilenameAttribute,
                                  BOOLEAN CaseSensitive)
{
    NTSTATUS Status;
    PFILE_RECORD_HEADER ParentFileRecord;
    PNTFS_ATTR_CONTEXT IndexRootContext;
    PINDEX_ROOT_ATTRIBUTE I30IndexRoot;
    PINDEX_ROOT_ATTRIBUTE NewIndexRoot;
    ULONG IndexRootOffset;
    ULONGLONG I30IndexRootLength;
    ULONG LengthWritten;
    ULONG AttributeLength;
    ULONG BtreeIndexLength;
    ULONG MaxIndexRootSize;
    PB_TREE Tree;
    PB_TREE_KEY SearchKey;

    DPRINT("NtfsRemoveFilenameFromDirectory(%p, %I64u, %p, %s)\n",
           DeviceExt, DirectoryMftIndex, FilenameAttribute, CaseSensitive ? "TRUE" : "FALSE");

    ParentFileRecord = ExAllocateFromNPagedLookasideList(&DeviceExt->FileRecLookasideList);
    if (!ParentFileRecord)
    {
        DPRINT1("ERROR: Couldn't allocate memory for file record!\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = ReadFileRecord(DeviceExt, DirectoryMftIndex, ParentFileRecord);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ERROR: Couldn't read parent directory with index %I64u\n", DirectoryMftIndex);
        ExFreeToNPagedLookasideList(&DeviceExt->FileRecLookasideList, ParentFileRecord);
        return Status;
    }

    Status = FindAttribute(DeviceExt,
                           ParentFileRecord,
                           AttributeIndexRoot,
                           L"$I30",
                           4,
                           &IndexRootContext,
                           &IndexRootOffset);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ERROR: Couldn't find $I30 $INDEX_ROOT attribute for parent directory with MFT #: %I64u!\n",
                DirectoryMftIndex);
        ExFreeToNPagedLookasideList(&DeviceExt->FileRecLookasideList, ParentFileRecord);
        return Status;
    }

    I30IndexRootLength = AttributeDataLength(IndexRootContext->pRecord);
    I30IndexRoot = ExAllocatePoolWithTag(NonPagedPool, I30IndexRootLength, TAG_NTFS);
    if (!I30IndexRoot)
    {
        DPRINT1("ERROR: Couldn't allocate memory for index root attribute!\n");
        ReleaseAttributeContext(IndexRootContext);
        ExFreeToNPagedLookasideList(&DeviceExt->FileRecLookasideList, ParentFileRecord);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = ReadAttribute(DeviceExt, IndexRootContext, 0, (PCHAR)I30IndexRoot, I30IndexRootLength);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ERROR: Couldn't read index root attribute for Mft index #%I64u\n", DirectoryMftIndex);
        ReleaseAttributeContext(IndexRootContext);
        ExFreePoolWithTag(I30IndexRoot, TAG_NTFS);
        ExFreeToNPagedLookasideList(&DeviceExt->FileRecLookasideList, ParentFileRecord);
        return Status;
    }

    Status = CreateBTreeFromIndex(DeviceExt, ParentFileRecord, IndexRootContext, I30IndexRoot, &Tree);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ERROR: Failed to create B-Tree from Index!\n");
        ReleaseAttributeContext(IndexRootContext);
        ExFreePoolWithTag(I30IndexRoot, TAG_NTFS);
        ExFreeToNPagedLookasideList(&DeviceExt->FileRecLookasideList, ParentFileRecord);
        return Status;
    }

    // NtfsRemoveKey() compares filenames, so the file reference here is only a placeholder
    SearchKey = CreateBTreeKeyFromFilename(0, FilenameAttribute);
    if (!SearchKey)
    {
        DestroyBTree(Tree);
        ReleaseAttributeContext(IndexRootContext);
        ExFreePoolWithTag(I30IndexRoot, TAG_NTFS);
        ExFreeToNPagedLookasideList(&DeviceExt->FileRecLookasideList, ParentFileRecord);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = NtfsRemoveKey(Tree->RootNode, SearchKey, CaseSensitive);

    DestroyBTreeKey(SearchKey);

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ERROR: Failed to remove '%.*S' from the index of MFT record %I64u (Status %lx)\n",
                FilenameAttribute->NameLength,
                FilenameAttribute->Name,
                DirectoryMftIndex,
                Status);
        DestroyBTree(Tree);
        ReleaseAttributeContext(IndexRootContext);
        ExFreePoolWithTag(I30IndexRoot, TAG_NTFS);
        ExFreeToNPagedLookasideList(&DeviceExt->FileRecLookasideList, ParentFileRecord);
        return Status;
    }

    // Push the tree back out to the index allocation before sizing the index root, for the same
    // reason the insert path does: growing or shrinking the allocation changes how much of the
    // file record is left for the root.
    Status = UpdateIndexAllocation(DeviceExt, Tree, I30IndexRoot->SizeOfEntry, ParentFileRecord);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ERROR: Failed to update index allocation from B-Tree!\n");
        DestroyBTree(Tree);
        ReleaseAttributeContext(IndexRootContext);
        ExFreePoolWithTag(I30IndexRoot, TAG_NTFS);
        ExFreeToNPagedLookasideList(&DeviceExt->FileRecLookasideList, ParentFileRecord);
        return Status;
    }

    // Find the maximum index root size given what the file record can hold
    MaxIndexRootSize = GetMaxIndexRootSize(DeviceExt, ParentFileRecord, IndexRootOffset, IndexRootContext->pRecord);

    Status = CreateIndexRootFromBTree(DeviceExt, Tree, MaxIndexRootSize, &NewIndexRoot, &BtreeIndexLength);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ERROR: Failed to create Index root from B-Tree!\n");
        DestroyBTree(Tree);
        ReleaseAttributeContext(IndexRootContext);
        ExFreePoolWithTag(I30IndexRoot, TAG_NTFS);
        ExFreeToNPagedLookasideList(&DeviceExt->FileRecLookasideList, ParentFileRecord);
        return Status;
    }

    DestroyBTree(Tree);

    // $INDEX_ROOT must always be resident, so its length is set directly rather than through
    // the usual attribute-resizing path.
    AttributeLength = NewIndexRoot->Header.AllocatedSize + FIELD_OFFSET(INDEX_ROOT_ATTRIBUTE, Header);

    if (AttributeLength != IndexRootContext->pRecord->Resident.ValueLength)
    {
        Status = InternalSetResidentAttributeLength(DeviceExt,
                                                    IndexRootContext,
                                                    ParentFileRecord,
                                                    IndexRootOffset,
                                                    AttributeLength);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("ERROR: Unable to set resident attribute length!\n");
            ExFreePoolWithTag(NewIndexRoot, TAG_NTFS);
            ReleaseAttributeContext(IndexRootContext);
            ExFreePoolWithTag(I30IndexRoot, TAG_NTFS);
            ExFreeToNPagedLookasideList(&DeviceExt->FileRecLookasideList, ParentFileRecord);
            return Status;
        }
    }

    NT_ASSERT(ParentFileRecord->BytesInUse <= DeviceExt->NtfsInfo.BytesPerFileRecord);

    Status = UpdateFileRecord(DeviceExt, DirectoryMftIndex, ParentFileRecord);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ERROR: Failed to update file record of directory with index: %I64x\n", DirectoryMftIndex);
        ExFreePoolWithTag(NewIndexRoot, TAG_NTFS);
        ReleaseAttributeContext(IndexRootContext);
        ExFreePoolWithTag(I30IndexRoot, TAG_NTFS);
        ExFreeToNPagedLookasideList(&DeviceExt->FileRecLookasideList, ParentFileRecord);
        return Status;
    }

    Status = WriteAttribute(DeviceExt,
                            IndexRootContext,
                            0,
                            (PUCHAR)NewIndexRoot,
                            AttributeLength,
                            &LengthWritten,
                            ParentFileRecord);
    if (!NT_SUCCESS(Status) || LengthWritten != AttributeLength)
    {
        DPRINT1("ERROR: Unable to write new index root attribute to parent directory!\n");
        if (NT_SUCCESS(Status))
            Status = STATUS_UNSUCCESSFUL;
    }

    ExFreePoolWithTag(NewIndexRoot, TAG_NTFS);
    ReleaseAttributeContext(IndexRootContext);
    ExFreePoolWithTag(I30IndexRoot, TAG_NTFS);
    ExFreeToNPagedLookasideList(&DeviceExt->FileRecLookasideList, ParentFileRecord);

    return Status;
}

/* The worker rewrites the directory record from a copy it reads, so it
 * must not overlap any other update of a directory index */
NTSTATUS
NtfsRemoveFilenameFromDirectory(PDEVICE_EXTENSION DeviceExt,
                                ULONGLONG DirectoryMftIndex,
                                PFILENAME_ATTRIBUTE FilenameAttribute,
                                BOOLEAN CaseSensitive)
{
    NTSTATUS Status;

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&DeviceExt->IndexResource, TRUE);
    Status = RemoveFilenameFromDirectoryWorker(DeviceExt, DirectoryMftIndex, FilenameAttribute, CaseSensitive);
    ExReleaseResourceLite(&DeviceExt->IndexResource);
    KeLeaveCriticalRegion();

    return Status;
}

/**
* @name FreeMftEntry
* @implemented
*
* Releases an MFT record back to the volume, clearing its bit in the master file table's $Bitmap
* attribute and marking the record itself as no longer in use.
*
* @param DeviceExt
* Points to the target disk's DEVICE_EXTENSION.
*
* @param MftIndex
* Mft index of the record to release.
*
* @return
* STATUS_SUCCESS on success.
* STATUS_OBJECT_NAME_NOT_FOUND if the master file table has no $Bitmap attribute.
* STATUS_INSUFFICIENT_RESOURCES if an allocation fails.
*
* @remarks
* The record's sequence number is incremented before it's written back, so any file reference
* still pointing at the old incarnation can be recognised as stale.
*/
NTSTATUS
FreeMftEntry(PDEVICE_EXTENSION DeviceExt,
             ULONGLONG MftIndex)
{
    NTSTATUS Status;
    RTL_BITMAP Bitmap;
    ULONGLONG BitmapDataSize;
    ULONGLONG AttrBytesRead;
    PUCHAR BitmapData;
    PUCHAR BitmapBuffer;
    ULONG LengthWritten;
    PNTFS_ATTR_CONTEXT BitmapContext;
    LARGE_INTEGER BitmapBits;
    PFILE_RECORD_HEADER FileRecord;

    DPRINT("FreeMftEntry(%p, %I64u)\n", DeviceExt, MftIndex);

    if (DeviceExt == NULL ||
        DeviceExt->NtfsInfo.BytesPerFileRecord == 0 ||
        DeviceExt->MFTContext == NULL ||
        DeviceExt->MFTContext->pRecord == NULL ||
        DeviceExt->MasterFileTable == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    // Mark the record itself as free first, so a bitmap bit is never handed out for a record
    // that still looks in use.
    FileRecord = ExAllocateFromNPagedLookasideList(&DeviceExt->FileRecLookasideList);
    if (!FileRecord)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = ReadFileRecord(DeviceExt, MftIndex, FileRecord);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ERROR: Couldn't read file record %I64u to free it!\n", MftIndex);
        ExFreeToNPagedLookasideList(&DeviceExt->FileRecLookasideList, FileRecord);
        return Status;
    }

    FileRecord->Flags &= ~FRH_IN_USE;
    FileRecord->SequenceNumber++;

    // Sequence number zero is reserved to mean "no reference"
    if (FileRecord->SequenceNumber == 0)
        FileRecord->SequenceNumber = 1;

    Status = UpdateFileRecord(DeviceExt, MftIndex, FileRecord);
    ExFreeToNPagedLookasideList(&DeviceExt->FileRecLookasideList, FileRecord);

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ERROR: Couldn't write file record %I64u while freeing it!\n", MftIndex);
        return Status;
    }

    Status = FindAttribute(DeviceExt, DeviceExt->MasterFileTable, AttributeBitmap, L"", 0, &BitmapContext, NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ERROR: Couldn't find $Bitmap attribute of master file table!\n");
        return Status;
    }

    BitmapDataSize = AttributeDataLength(BitmapContext->pRecord);

    /*
     * RTL bitmap operations use ULONG bit counts and ULONG-sized storage.
     * Reject sizes that cannot be represented before narrowing the length.
     */
    if (BitmapDataSize > MAXULONG - sizeof(ULONG) ||
        BitmapDataSize > MAXULONG / 8)
    {
        ReleaseAttributeContext(BitmapContext);
        return STATUS_NOT_SUPPORTED;
    }

    if (AttributeDataLength(DeviceExt->MFTContext->pRecord) %
        DeviceExt->NtfsInfo.BytesPerFileRecord != 0)
    {
        ReleaseAttributeContext(BitmapContext);
        return STATUS_FILE_CORRUPT_ERROR;
    }

    BitmapBits.QuadPart = AttributeDataLength(DeviceExt->MFTContext->pRecord) /
                          DeviceExt->NtfsInfo.BytesPerFileRecord;
    if (BitmapBits.HighPart != 0 ||
        MftIndex >= BitmapBits.QuadPart ||
        BitmapBits.QuadPart > BitmapDataSize * 8)
    {
        DPRINT1("ERROR: MFT bitmap does not cover record %I64u!\n", MftIndex);
        ReleaseAttributeContext(BitmapContext);
        return STATUS_FILE_CORRUPT_ERROR;
    }

    // RtlInitializeBitmap wants a ULONG-aligned pointer.
    BitmapBuffer = ExAllocatePoolWithTag(NonPagedPool,
                                         (SIZE_T)BitmapDataSize + sizeof(ULONG),
                                         TAG_NTFS);
    if (!BitmapBuffer)
    {
        ReleaseAttributeContext(BitmapContext);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(BitmapBuffer, BitmapDataSize + sizeof(ULONG));

    BitmapData = (PUCHAR)ALIGN_UP_BY((ULONG_PTR)BitmapBuffer, sizeof(ULONG));

    AttrBytesRead = ReadAttribute(DeviceExt, BitmapContext, 0, (PCHAR)BitmapData, BitmapDataSize);
    if (AttrBytesRead != BitmapDataSize)
    {
        DPRINT1("ERROR: Unable to read $Bitmap attribute of master file table!\n");
        ExFreePoolWithTag(BitmapBuffer, TAG_NTFS);
        ReleaseAttributeContext(BitmapContext);
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    RtlInitializeBitMap(&Bitmap, (PULONG)BitmapData, BitmapBits.LowPart);
    RtlClearBits(&Bitmap, (ULONG)MftIndex, 1);

    Status = WriteAttribute(DeviceExt, BitmapContext, 0, BitmapData,
                            (ULONG)BitmapDataSize, &LengthWritten, NULL);
    if (NT_SUCCESS(Status) && LengthWritten != (ULONG)BitmapDataSize)
    {
        DPRINT1("ERROR: Short write while clearing MFT $Bitmap bit!\n");
        Status = STATUS_DEVICE_DATA_ERROR;
    }
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ERROR encountered when writing $Bitmap attribute!\n");
    }

    ExFreePoolWithTag(BitmapBuffer, TAG_NTFS);
    ReleaseAttributeContext(BitmapContext);

    return Status;
}

/**
* @name NtfsDeleteFileRecord
* @implemented
*
* Deletes a file or empty directory: releases every cluster its non-resident attributes hold,
* removes each of its names from the parent index, and frees its MFT record.
*
* @param DeviceExt
* Points to the target disk's DEVICE_EXTENSION.
*
* @param MftIndex
* Mft index of the file to delete.
*
* @param CaseSensitive
* Boolean indicating if the function should operate in case-sensitive mode. This will be TRUE
* if an application created the file with the FILE_FLAG_POSIX_SEMANTICS flag.
*
* @return
* STATUS_SUCCESS on success.
*
* @remarks
* Callers must have established that the file may be deleted, in particular that a directory
* is empty. Clusters are released before the names, so a failure part-way through leaves a
* reachable file rather than orphaned allocation.
*/
/*
 * Validate the complete in-memory FILE record before deletion performs any
 * directory-index or allocation mutations. All ranges are checked by
 * subtraction so malformed lengths cannot wrap an addition.
 */
static NTSTATUS
NtfsValidateFileRecordForDelete(PFILE_RECORD_HEADER FileRecord,
                                ULONG RecordSize)
{
    ULONG Offset;
    ULONG BytesInUse;
    ULONG AttributeOffset;

    if (RecordSize < sizeof(FILE_RECORD_HEADER) ||
        FileRecord->Ntfs.Type != NRH_FILE_TYPE ||
        !(FileRecord->Flags & FRH_IN_USE) ||
        FileRecord->BytesAllocated > RecordSize ||
        FileRecord->BytesAllocated < sizeof(FILE_RECORD_HEADER) ||
        FileRecord->BytesInUse < sizeof(FILE_RECORD_HEADER) + sizeof(ULONG) ||
        FileRecord->BytesInUse > FileRecord->BytesAllocated)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    BytesInUse = FileRecord->BytesInUse;
    AttributeOffset = FileRecord->AttributeOffset;

    if (AttributeOffset < sizeof(FILE_RECORD_HEADER) ||
        (AttributeOffset & (ATTR_RECORD_ALIGNMENT - 1)) != 0 ||
        AttributeOffset > BytesInUse - sizeof(ULONG))
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    Offset = AttributeOffset;
    while (Offset <= BytesInUse - sizeof(ULONG))
    {
        PNTFS_ATTR_RECORD Attribute =
            (PNTFS_ATTR_RECORD)((PUCHAR)FileRecord + Offset);
        ULONG Remaining = BytesInUse - Offset;
        ULONG HeaderLength;
        ULONG NameBytes;

        if (Attribute->Type == AttributeEnd)
        {
            /*
             * BytesInUse describes the used portion of the FILE record.
             * The end marker must terminate that portion; accepting hidden
             * trailing bytes would make the validated and consumed ranges
             * disagree.
             */
            return (Remaining == sizeof(ULONG)) ?
                STATUS_SUCCESS : STATUS_FILE_CORRUPT_ERROR;
        }

        if (Remaining < FIELD_OFFSET(NTFS_ATTR_RECORD, Resident.Reserved) +
                        sizeof(UCHAR))
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }

        HeaderLength = Attribute->IsNonResident ?
            FIELD_OFFSET(NTFS_ATTR_RECORD, NonResident.AllocatedSize) +
                sizeof(Attribute->NonResident.AllocatedSize) :
            FIELD_OFFSET(NTFS_ATTR_RECORD, Resident.Reserved) +
                sizeof(Attribute->Resident.Reserved);

        if (Attribute->IsNonResident > 1 ||
            Attribute->Length < HeaderLength ||
            (Attribute->Length & (ATTR_RECORD_ALIGNMENT - 1)) != 0 ||
            Attribute->Length > Remaining)
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }

        NameBytes = (ULONG)Attribute->NameLength * sizeof(WCHAR);
        if (Attribute->NameLength != 0 &&
            (Attribute->NameOffset < HeaderLength ||
             Attribute->NameOffset > Attribute->Length ||
             NameBytes > Attribute->Length - Attribute->NameOffset))
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }

        if (Attribute->IsNonResident)
        {
            ULONG RunOffset;

            if (Attribute->NonResident.MappingPairsOffset < HeaderLength ||
                Attribute->NonResident.MappingPairsOffset >= Attribute->Length ||
                Attribute->NonResident.HighestVCN <
                    Attribute->NonResident.LowestVCN)
            {
                return STATUS_FILE_CORRUPT_ERROR;
            }

            /*
             * Mapping pairs are a sequence of length/offset nibbles followed
             * by their payloads. Validate every payload boundary and require
             * the zero terminator before deletion can release any clusters.
             */
            {
                ULONGLONG ExpectedClusters;
                ULONGLONG TotalClusters = 0;

                /*
                 * HighestVCN is inclusive. Reject the one range whose
                 * inclusive cluster count cannot be represented in 64 bits.
                 */
                ExpectedClusters =
                    Attribute->NonResident.HighestVCN -
                    Attribute->NonResident.LowestVCN;
                if (ExpectedClusters == (ULONGLONG)-1)
                    return STATUS_FILE_CORRUPT_ERROR;
                ExpectedClusters++;

                RunOffset = Attribute->NonResident.MappingPairsOffset;
                for (;;)
                {
                    UCHAR RunHeader;
                    ULONG LengthBytes;
                    ULONG OffsetBytes;
                    ULONGLONG RunClusters = 0;
                    ULONG Index;

                    if (RunOffset >= Attribute->Length)
                        return STATUS_FILE_CORRUPT_ERROR;

                    RunHeader = *((PUCHAR)Attribute + RunOffset++);
                    if (RunHeader == 0)
                        break;

                    LengthBytes = RunHeader & 0x0F;
                    OffsetBytes = (RunHeader >> 4) & 0x0F;

                    if (LengthBytes == 0 ||
                        LengthBytes > sizeof(ULONGLONG) ||
                        OffsetBytes > sizeof(LONGLONG) ||
                        LengthBytes + OffsetBytes >
                            Attribute->Length - RunOffset)
                    {
                        return STATUS_FILE_CORRUPT_ERROR;
                    }

                    for (Index = 0; Index < LengthBytes; Index++)
                    {
                        RunClusters |=
                            (ULONGLONG)*((PUCHAR)Attribute + RunOffset + Index)
                            << (Index * 8);
                    }

                    if (RunClusters == 0 ||
                        RunClusters > ExpectedClusters - TotalClusters)
                    {
                        return STATUS_FILE_CORRUPT_ERROR;
                    }

                    TotalClusters += RunClusters;
                    RunOffset += LengthBytes + OffsetBytes;
                }

                if (TotalClusters != ExpectedClusters)
                    return STATUS_FILE_CORRUPT_ERROR;
            }
        }
        else
        {
            ULONG ValueOffset = Attribute->Resident.ValueOffset;
            ULONG ValueLength = Attribute->Resident.ValueLength;

            if (ValueOffset < HeaderLength ||
                ValueOffset > Attribute->Length ||
                ValueLength > Attribute->Length - ValueOffset)
            {
                return STATUS_FILE_CORRUPT_ERROR;
            }

            if (Attribute->Type == AttributeFileName)
            {
                PFILENAME_ATTRIBUTE FileName;

                if (ValueLength < FIELD_OFFSET(FILENAME_ATTRIBUTE, Name))
                    return STATUS_FILE_CORRUPT_ERROR;

                FileName = (PFILENAME_ATTRIBUTE)
                    ((PUCHAR)Attribute + ValueOffset);

                if ((ULONG)FileName->NameLength >
                    (ValueLength - FIELD_OFFSET(FILENAME_ATTRIBUTE, Name)) /
                        sizeof(WCHAR))
                {
                    return STATUS_FILE_CORRUPT_ERROR;
                }
            }
        }

        Offset += Attribute->Length;
    }

    /* A valid record must contain its end marker inside BytesInUse. */
    return STATUS_FILE_CORRUPT_ERROR;
}

static
NTSTATUS
NtfsRestoreFileNames(PDEVICE_EXTENSION DeviceExt,
                     PFILE_RECORD_HEADER FileRecord,
                     ULONGLONG FileReference,
                     ULONG Count,
                     BOOLEAN CaseSensitive)
{
    PNTFS_ATTR_RECORD Attribute;
    ULONG Offset;
    ULONG RecordSize;
    ULONG BytesInUse;
    ULONG HeaderLength = FIELD_OFFSET(NTFS_ATTR_RECORD, Resident) +
                         sizeof(Attribute->Resident);
    NTSTATUS Status;

    if (DeviceExt == NULL || FileRecord == NULL ||
        DeviceExt->NtfsInfo.BytesPerFileRecord < sizeof(FILE_RECORD_HEADER) ||
        FileRecord->BytesAllocated > DeviceExt->NtfsInfo.BytesPerFileRecord ||
        FileRecord->BytesInUse < sizeof(FILE_RECORD_HEADER) + sizeof(ULONG) ||
        FileRecord->BytesInUse > FileRecord->BytesAllocated)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    RecordSize = DeviceExt->NtfsInfo.BytesPerFileRecord;
    BytesInUse = FileRecord->BytesInUse;
    Offset = FileRecord->AttributeOffset;
    if (FileRecord->AttributeOffset < sizeof(FILE_RECORD_HEADER) ||
        (FileRecord->AttributeOffset & (ATTR_RECORD_ALIGNMENT - 1)) != 0 ||
        FileRecord->AttributeOffset > BytesInUse - sizeof(ULONG))
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    while (Count != 0 && Offset <= BytesInUse - sizeof(ULONG))
    {
        Attribute = (PNTFS_ATTR_RECORD)((PUCHAR)FileRecord + Offset);
        if (Attribute->Type == AttributeEnd)
            break;

        if (Attribute->Length < sizeof(ULONG) ||
            Attribute->Length > BytesInUse - Offset ||
            Attribute->Length > RecordSize - Offset)
            return STATUS_FILE_CORRUPT_ERROR;

        if (Attribute->Type == AttributeFileName && !Attribute->IsNonResident)
        {
            PFILENAME_ATTRIBUTE Name;
            ULONG ValueOffset = Attribute->Resident.ValueOffset;

            if (Attribute->Length < HeaderLength ||
                ValueOffset < HeaderLength ||
                ValueOffset > Attribute->Length ||
                Attribute->Resident.ValueLength > Attribute->Length - ValueOffset ||
                Attribute->Resident.ValueLength < FIELD_OFFSET(FILENAME_ATTRIBUTE, Name))
                return STATUS_FILE_CORRUPT_ERROR;

            Name = (PFILENAME_ATTRIBUTE)((PUCHAR)Attribute + ValueOffset);
            if ((ULONG)Name->NameLength * sizeof(WCHAR) >
                Attribute->Resident.ValueLength - FIELD_OFFSET(FILENAME_ATTRIBUTE, Name))
                return STATUS_FILE_CORRUPT_ERROR;

            Status = NtfsAddFilenameToDirectory(
                DeviceExt,
                Name->DirectoryFileReferenceNumber & NTFS_MFT_MASK,
                FileReference,
                Name,
                CaseSensitive);
            if (!NT_SUCCESS(Status))
                return Status;

            Count--;
        }

        Offset += Attribute->Length;
    }

    return (Count == 0) ? STATUS_SUCCESS : STATUS_FILE_CORRUPT_ERROR;
}

/*
 * Preserve reachability if a later attribute-release operation aborts after
 * directory links have been removed. Report corruption if recovery fails.
 */
/*
 * Allocation release is not transactional: FreeClusters() may already have
 * cleared bitmap bits and rewritten an attribute runlist before a later
 * operation fails. Re-linking the filename at this point could expose a
 * partially deallocated file as if it were intact. Keep the MFT record
 * allocated but unlinked and report corruption so recovery can reconcile it.
 */
static NTSTATUS
NtfsAbortFileDelete(PDEVICE_EXTENSION DeviceExt,
                    PFILE_RECORD_HEADER FileRecord,
                    ULONGLONG FileReference,
                    ULONG RemovedFileNames,
                    BOOLEAN CaseSensitive,
                    NTSTATUS FailureStatus)
{
    UNREFERENCED_PARAMETER(DeviceExt);
    UNREFERENCED_PARAMETER(FileRecord);
    UNREFERENCED_PARAMETER(FileReference);
    UNREFERENCED_PARAMETER(RemovedFileNames);
    UNREFERENCED_PARAMETER(CaseSensitive);

    DPRINT1("ERROR: File deletion stopped after allocation release began; "
            "MFT record %I64u remains allocated but unlinked (0x%lx)!\n",
            FileReference & NTFS_MFT_MASK,
            FailureStatus);
    return STATUS_FILE_CORRUPT_ERROR;
}
NTSTATUS
NtfsDeleteFileRecord(PDEVICE_EXTENSION DeviceExt,
                     ULONGLONG MftIndex,
                     BOOLEAN CaseSensitive)
{
    NTSTATUS Status;
    PFILE_RECORD_HEADER FileRecord;
    PNTFS_ATTR_RECORD Attribute;
    PNTFS_ATTR_CONTEXT AttributeContext;
    ULONG AttributeOffset;
    ULONG RemovedFileNames = 0;
    ULONGLONG FileReference;

    DPRINT("NtfsDeleteFileRecord(%p, %I64u, %s)\n", DeviceExt, MftIndex, CaseSensitive ? "TRUE" : "FALSE");

    FileRecord = ExAllocateFromNPagedLookasideList(&DeviceExt->FileRecLookasideList);
    if (!FileRecord)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = ReadFileRecord(DeviceExt, MftIndex, FileRecord);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ERROR: Couldn't read file record %I64u to delete it!\n", MftIndex);
        ExFreeToNPagedLookasideList(&DeviceExt->FileRecLookasideList, FileRecord);
        return Status;
    }

    Status = NtfsValidateFileRecordForDelete(
        FileRecord, DeviceExt->NtfsInfo.BytesPerFileRecord);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ERROR: File record %I64u is malformed; refusing deletion.\n",
                MftIndex);
        ExFreeToNPagedLookasideList(&DeviceExt->FileRecLookasideList, FileRecord);
        return Status;
    }

    /*
     * Remove names before freeing allocation. If a later unlink fails, restore
     * all earlier index entries so the live MFT record remains reachable.
     */
    FileReference = MftIndex | ((ULONGLONG)FileRecord->SequenceNumber << 48);
    Attribute = (PNTFS_ATTR_RECORD)((ULONG_PTR)FileRecord + FileRecord->AttributeOffset);
    AttributeOffset = FileRecord->AttributeOffset;

    while (AttributeOffset < DeviceExt->NtfsInfo.BytesPerFileRecord &&
           Attribute->Type != AttributeEnd)
    {
        if (Attribute->Type == AttributeFileName && !Attribute->IsNonResident)
        {
            PFILENAME_ATTRIBUTE FileName =
                (PFILENAME_ATTRIBUTE)((ULONG_PTR)Attribute + Attribute->Resident.ValueOffset);

            Status = NtfsRemoveFilenameFromDirectory(DeviceExt,
                                                     FileName->DirectoryFileReferenceNumber & NTFS_MFT_MASK,
                                                     FileName,
                                                     CaseSensitive);
            if (!NT_SUCCESS(Status))
            {
                NTSTATUS RollbackStatus;

                DPRINT1("ERROR: Failed to remove '%.*S' from directory %I64u!\n",
                        FileName->NameLength,
                        FileName->Name,
                        FileName->DirectoryFileReferenceNumber & NTFS_MFT_MASK);
                RollbackStatus = NtfsRestoreFileNames(DeviceExt,
                                                       FileRecord,
                                                       FileReference,
                                                       RemovedFileNames,
                                                       CaseSensitive);
                ExFreeToNPagedLookasideList(&DeviceExt->FileRecLookasideList, FileRecord);
                if (!NT_SUCCESS(RollbackStatus))
                {
                    DPRINT1("ERROR: Filename rollback failed for MFT record %I64u (0x%lx)!\n",
                            MftIndex, RollbackStatus);
                    return STATUS_FILE_CORRUPT_ERROR;
                }
                return Status;
            }

            RemovedFileNames++;
        }

        if (Attribute->Length == 0)
            break;

        AttributeOffset += Attribute->Length;
        Attribute = (PNTFS_ATTR_RECORD)((ULONG_PTR)FileRecord + AttributeOffset);
    }

    // Release the clusters held by every non-resident attribute
    Attribute = (PNTFS_ATTR_RECORD)((ULONG_PTR)FileRecord + FileRecord->AttributeOffset);
    AttributeOffset = FileRecord->AttributeOffset;

    while (AttributeOffset < DeviceExt->NtfsInfo.BytesPerFileRecord &&
           Attribute->Type != AttributeEnd)
    {
        if (Attribute->IsNonResident)
        {
            Status = FindAttribute(DeviceExt,
                                   FileRecord,
                                   Attribute->Type,
                                   (PCWSTR)((ULONG_PTR)Attribute + Attribute->NameOffset),
                                   Attribute->NameLength,
                                   &AttributeContext,
                                   NULL);
            if (NT_SUCCESS(Status))
            {
                ULONGLONG ClusterCount;

                /*
                 * FreeClusters() rewrites the supplied FILE record and
                 * truncates the attribute runlist from its highest VCN.
                 * It cannot safely update a remote extension record or an
                 * extent whose VCN range does not begin at zero.
                 */
                if (AttributeContext->FileMFTIndex != MftIndex ||
                    AttributeContext->pRecord == NULL ||
                    !AttributeContext->pRecord->IsNonResident ||
                    AttributeContext->pRecord->Instance != Attribute->Instance ||
                    AttributeContext->pRecord->NonResident.LowestVCN !=
                        Attribute->NonResident.LowestVCN ||
                    AttributeContext->pRecord->NonResident.HighestVCN !=
                        Attribute->NonResident.HighestVCN ||
                    Attribute->NonResident.LowestVCN != 0)
                {
                    DPRINT1("ERROR: Unsupported or mismatched attribute extent while deleting file %I64u!\n",
                            MftIndex);
                    ReleaseAttributeContext(AttributeContext);
                    Status = NtfsAbortFileDelete(DeviceExt,
                                                 FileRecord,
                                                 FileReference,
                                                 RemovedFileNames,
                                                 CaseSensitive,
                                                 STATUS_FILE_CORRUPT_ERROR);
                    ExFreeToNPagedLookasideList(&DeviceExt->FileRecLookasideList, FileRecord);
                    return Status;
                }

                ClusterCount = Attribute->NonResident.HighestVCN -
                               Attribute->NonResident.LowestVCN + 1;
                if (ClusterCount == 0 || ClusterCount > MAXULONG)
                {
                    DPRINT1("ERROR: Attribute extent cluster count is not representable for file %I64u!\n",
                            MftIndex);
                    ReleaseAttributeContext(AttributeContext);
                    Status = NtfsAbortFileDelete(DeviceExt,
                                                 FileRecord,
                                                 FileReference,
                                                 RemovedFileNames,
                                                 CaseSensitive,
                                                 STATUS_FILE_CORRUPT_ERROR);
                    ExFreeToNPagedLookasideList(&DeviceExt->FileRecLookasideList, FileRecord);
                    return Status;
                }

                Status = FreeClusters(DeviceExt, AttributeContext, AttributeOffset,
                                      FileRecord, (ULONG)ClusterCount);
                if (!NT_SUCCESS(Status))
                {
                    DPRINT1("ERROR: Failed to free clusters of attribute 0x%lx of file %I64u!\n",
                            Attribute->Type, MftIndex);
                    ReleaseAttributeContext(AttributeContext);
                    Status = NtfsAbortFileDelete(DeviceExt,
                                                 FileRecord,
                                                 FileReference,
                                                 RemovedFileNames,
                                                 CaseSensitive,
                                                 Status);
                    ExFreeToNPagedLookasideList(&DeviceExt->FileRecLookasideList, FileRecord);
                    return Status;
                }

                ReleaseAttributeContext(AttributeContext);
            }
            else
            {
                DPRINT1("ERROR: Failed to locate non-resident attribute 0x%lx of file %I64u!\n",
                        Attribute->Type, MftIndex);
                Status = NtfsAbortFileDelete(DeviceExt,
                                             FileRecord,
                                             FileReference,
                                             RemovedFileNames,
                                             CaseSensitive,
                                             Status);
                ExFreeToNPagedLookasideList(&DeviceExt->FileRecLookasideList, FileRecord);
                return Status;
            }
        }

        if (Attribute->Length == 0)
            break;

        AttributeOffset += Attribute->Length;
        Attribute = (PNTFS_ATTR_RECORD)((ULONG_PTR)FileRecord + AttributeOffset);
    }

    /*
     * Keep the original record buffer until FreeMftEntry has completed. It
     * contains the names needed to recover directory reachability if freeing
     * the MFT record fails before the in-use flag is cleared.
     */
    Status = FreeMftEntry(DeviceExt, MftIndex);
    if (!NT_SUCCESS(Status))
    {
        NTSTATUS ReadStatus;
        NTSTATUS RollbackStatus;

        /*
         * FreeMftEntry writes the record's unused state before clearing its
         * bitmap bit. Re-read it to distinguish a still-live record (whose
         * names must be restored) from an already-retired record whose bitmap
         * update failed (which must not be re-linked).
         */
        ReadStatus = ReadFileRecord(DeviceExt, MftIndex, FileRecord);
        if (!NT_SUCCESS(ReadStatus))
        {
            DPRINT1("ERROR: Cannot determine MFT record state after delete failure for %I64u!\n",
                    MftIndex);
            ExFreeToNPagedLookasideList(&DeviceExt->FileRecLookasideList, FileRecord);
            return STATUS_FILE_CORRUPT_ERROR;
        }

        if ((FileRecord->Flags & FRH_IN_USE) &&
            FileRecord->SequenceNumber == (USHORT)(FileReference >> 48))
        {
            RollbackStatus = NtfsRestoreFileNames(DeviceExt,
                                                   FileRecord,
                                                   FileReference,
                                                   RemovedFileNames,
                                                   CaseSensitive);
            if (!NT_SUCCESS(RollbackStatus))
            {
                DPRINT1("ERROR: Filename rollback failed after MFT release error for %I64u (0x%lx)!\n",
                        MftIndex, RollbackStatus);
                Status = STATUS_FILE_CORRUPT_ERROR;
            }
        }
    }

    ExFreeToNPagedLookasideList(&DeviceExt->FileRecLookasideList, FileRecord);
    return Status;
}


/*
 * Resolve and validate the record geometry before touching any update
 * sequence array or sector trailer. FILE and INDX records have independent
 * sizes on NTFS volumes.
 */
static
NTSTATUS
NtfsGetFixupRecordSize(PDEVICE_EXTENSION Vcb,
                       PNTFS_RECORD_HEADER Record,
                       PULONG RecordSize)
{
    ULONG Size;
    ULONG SectorCount;
    ULONG UsaBytes;

    if (Vcb == NULL || Record == NULL || RecordSize == NULL ||
        Vcb->NtfsInfo.BytesPerSector < sizeof(USHORT))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Record->Type == NRH_FILE_TYPE)
        Size = Vcb->NtfsInfo.BytesPerFileRecord;
    else if (Record->Type == NRH_INDX_TYPE)
        Size = Vcb->NtfsInfo.BytesPerIndexRecord;
    else
        return STATUS_FILE_CORRUPT_ERROR;

    if (Size == 0 || (Size % Vcb->NtfsInfo.BytesPerSector) != 0)
        return STATUS_FILE_CORRUPT_ERROR;

    SectorCount = Size / Vcb->NtfsInfo.BytesPerSector;
    if (SectorCount == 0 || SectorCount >= 0xFFFF)
        return STATUS_FILE_CORRUPT_ERROR;

    if (Record->UsaCount != SectorCount + 1 ||
        Record->UsaOffset < sizeof(NTFS_RECORD_HEADER))
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    UsaBytes = (ULONG)Record->UsaCount * sizeof(USHORT);
    if (Record->UsaOffset > Vcb->NtfsInfo.BytesPerSector - sizeof(USHORT) ||
        UsaBytes > Vcb->NtfsInfo.BytesPerSector - sizeof(USHORT) -
                   Record->UsaOffset ||
        Record->UsaOffset > Size || UsaBytes > Size - Record->UsaOffset)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    *RecordSize = Size;
    return STATUS_SUCCESS;
}

NTSTATUS
AddFixupArray(PDEVICE_EXTENSION Vcb,
              PNTFS_RECORD_HEADER Record)
{
    NTSTATUS Status;
    ULONG RecordSize;
    ULONG SectorCount;
    ULONG Offset;
    ULONG i;
    USHORT *Usa;
    USHORT *Block;
    USHORT UpdateSequenceNumber;

    Status = NtfsGetFixupRecordSize(Vcb, Record, &RecordSize);
    if (!NT_SUCCESS(Status))
        return Status;

    SectorCount = Record->UsaCount - 1;
    Usa = (USHORT*)((PUCHAR)Record + Record->UsaOffset);
    UpdateSequenceNumber = (USHORT)(Usa[0] + 1);
    Usa[0] = UpdateSequenceNumber;

    for (i = 0; i < SectorCount; i++)
    {
        Offset = (i + 1) * Vcb->NtfsInfo.BytesPerSector - sizeof(USHORT);
        if (Offset > RecordSize - sizeof(USHORT))
            return STATUS_FILE_CORRUPT_ERROR;

        Block = (USHORT*)((PUCHAR)Record + Offset);
        Usa[i + 1] = *Block;
        *Block = UpdateSequenceNumber;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
FixupUpdateSequenceArray(PDEVICE_EXTENSION Vcb,
                         PNTFS_RECORD_HEADER Record)
{
    NTSTATUS Status;
    ULONG RecordSize;
    ULONG SectorCount;
    ULONG Offset;
    ULONG i;
    USHORT *Usa;
    USHORT *Block;
    USHORT UpdateSequenceNumber;

    Status = NtfsGetFixupRecordSize(Vcb, Record, &RecordSize);
    if (!NT_SUCCESS(Status))
        return Status;

    Usa = (USHORT*)((PUCHAR)Record + Record->UsaOffset);
    UpdateSequenceNumber = Usa[0];
    SectorCount = Record->UsaCount - 1;

    /*
     * Validate every sector trailer before restoring any of them. A corrupt
     * later trailer must not leave earlier sectors partially fixed up.
     */
    for (i = 0; i < SectorCount; i++)
    {
        Offset = (i + 1) * Vcb->NtfsInfo.BytesPerSector - sizeof(USHORT);
        if (Offset > RecordSize - sizeof(USHORT))
            return STATUS_FILE_CORRUPT_ERROR;

        Block = (USHORT*)((PUCHAR)Record + Offset);
        if (*Block != UpdateSequenceNumber)
        {
            DPRINT1("Mismatch with USA at sector %lu: %u read, %u expected\n",
                    i, *Block, UpdateSequenceNumber);
            return STATUS_FILE_CORRUPT_ERROR;
        }
    }

    for (i = 0; i < SectorCount; i++)
    {
        Offset = (i + 1) * Vcb->NtfsInfo.BytesPerSector - sizeof(USHORT);
        Block = (USHORT*)((PUCHAR)Record + Offset);
        *Block = Usa[i + 1];
    }

    return STATUS_SUCCESS;
}

NTSTATUS
ReadLCN(PDEVICE_EXTENSION Vcb,
        ULONGLONG lcn,
        ULONG count,
        PVOID buffer)
{
    LARGE_INTEGER DiskSector;

    DiskSector.QuadPart = lcn;

    return NtfsReadSectors(Vcb->StorageDevice,
                           DiskSector.u.LowPart * Vcb->NtfsInfo.SectorsPerCluster,
                           count * Vcb->NtfsInfo.SectorsPerCluster,
                           Vcb->NtfsInfo.BytesPerSector,
                           buffer,
                           FALSE);
}


BOOLEAN
CompareFileName(PDEVICE_EXTENSION Vcb,
                PUNICODE_STRING FileName,
                PINDEX_ENTRY_ATTRIBUTE IndexEntry,
                BOOLEAN DirSearch,
                BOOLEAN CaseSensitive)
{
    BOOLEAN Ret, Alloc = FALSE;
    UNICODE_STRING EntryName;

    EntryName.Buffer = IndexEntry->FileName.Name;
    EntryName.Length =
    EntryName.MaximumLength = IndexEntry->FileName.NameLength * sizeof(WCHAR);

    if (DirSearch)
    {
        UNICODE_STRING IntFileName;
        if (!CaseSensitive)
        {
            NT_VERIFY(NT_SUCCESS(RtlUpcaseUnicodeString(&IntFileName, FileName, TRUE)));
            Alloc = TRUE;
        }
        else
        {
            IntFileName = *FileName;
        }

        /* With a real upcase table the matcher folds both sides through it,
         * which is what NTFS means by case-insensitive. */
        Ret = FsRtlIsNameInExpression(&IntFileName, &EntryName, !CaseSensitive,
                                      Vcb ? Vcb->UpCaseTable : NULL);

        if (Alloc)
        {
            RtlFreeUnicodeString(&IntFileName);
        }

        return Ret;
    }
    else
    {
        return (RtlCompareUnicodeString(FileName, &EntryName, !CaseSensitive) == 0);
    }
}

/**
* @name UpdateMftMirror
* @implemented
*
* Backs-up the first ~4 master file table entries to the $MFTMirr file.
*
* @param Vcb
* Pointer to an NTFS_VCB for the volume whose Mft mirror is being updated.
*
* @return

* STATUS_SUCCESS on success.
* STATUS_INSUFFICIENT_RESOURCES if an allocation failed.
* STATUS_UNSUCCESSFUL if we couldn't read the master file table.
*
* @remarks
* NTFS maintains up-to-date copies of the first several mft entries in the $MFTMirr file. Usually, the first 4 file
* records from the mft are stored. The exact number of entries is determined by the size of $MFTMirr's $DATA.
* If $MFTMirr is not up-to-date, chkdsk will reject every change it can find prior to when $MFTMirr was last updated.
* Therefore, it's recommended to call this function if the volume changes considerably. For instance, IncreaseMftSize()
* relies on this function to keep chkdsk from deleting the mft entries it creates. Note that under most instances, creating
* or deleting a file will not affect the first ~four mft entries, and so will not require updating the mft mirror.
*/
NTSTATUS
UpdateMftMirror(PNTFS_VCB Vcb)
{
    PFILE_RECORD_HEADER MirrorFileRecord;
    PNTFS_ATTR_CONTEXT MirrDataContext;
    PNTFS_ATTR_CONTEXT MftDataContext;
    PCHAR DataBuffer;
    ULONGLONG DataLength;
    NTSTATUS Status;
    ULONG BytesRead;
    ULONG LengthWritten;

    // Allocate memory for the Mft mirror file record
    MirrorFileRecord = ExAllocateFromNPagedLookasideList(&Vcb->FileRecLookasideList);
    if (!MirrorFileRecord)
    {
        DPRINT1("Error: Failed to allocate memory for $MFTMirr!\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    // Read the Mft Mirror file record
    Status = ReadFileRecord(Vcb, NTFS_FILE_MFTMIRR, MirrorFileRecord);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ERROR: Failed to read $MFTMirr!\n");
        ExFreeToNPagedLookasideList(&Vcb->FileRecLookasideList, MirrorFileRecord);
        return Status;
    }

    // Find the $DATA attribute of $MFTMirr
    Status = FindAttribute(Vcb, MirrorFileRecord, AttributeData, L"", 0, &MirrDataContext, NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ERROR: Couldn't find $DATA attribute!\n");
        ExFreeToNPagedLookasideList(&Vcb->FileRecLookasideList, MirrorFileRecord);
        return Status;
    }

    // Find the $DATA attribute of $MFT
    Status = FindAttribute(Vcb, Vcb->MasterFileTable, AttributeData, L"", 0, &MftDataContext, NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ERROR: Couldn't find $DATA attribute!\n");
        ReleaseAttributeContext(MirrDataContext);
        ExFreeToNPagedLookasideList(&Vcb->FileRecLookasideList, MirrorFileRecord);
        return Status;
    }

    // Get the size of the mirror's $DATA attribute
    DataLength = AttributeDataLength(MirrDataContext->pRecord);

    ASSERT(DataLength % Vcb->NtfsInfo.BytesPerFileRecord == 0);

    // Create buffer for the mirror's $DATA attribute
    DataBuffer = ExAllocatePoolWithTag(NonPagedPool, DataLength, TAG_NTFS);
    if (!DataBuffer)
    {
        DPRINT1("Error: Couldn't allocate memory for $DATA buffer!\n");
        ReleaseAttributeContext(MftDataContext);
        ReleaseAttributeContext(MirrDataContext);
        ExFreeToNPagedLookasideList(&Vcb->FileRecLookasideList, MirrorFileRecord);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    ASSERT(DataLength < ULONG_MAX);

    // Back up the first several entries of the Mft's $DATA Attribute
    BytesRead = ReadAttribute(Vcb, MftDataContext, 0, DataBuffer, (ULONG)DataLength);
    if (BytesRead != (ULONG)DataLength)
    {
        DPRINT1("Error: Failed to read $DATA for $MFTMirr!\n");
        ReleaseAttributeContext(MftDataContext);
        ReleaseAttributeContext(MirrDataContext);
        ExFreePoolWithTag(DataBuffer, TAG_NTFS);
        ExFreeToNPagedLookasideList(&Vcb->FileRecLookasideList, MirrorFileRecord);
        return STATUS_UNSUCCESSFUL;
    }

    // Write the mirror's $DATA attribute
    Status = WriteAttribute(Vcb,
                             MirrDataContext,
                             0,
                             (PUCHAR)DataBuffer,
                             DataLength,
                             &LengthWritten,
                             MirrorFileRecord);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ERROR: Failed to write $DATA attribute of $MFTMirr!\n");
    }

    // Cleanup
    ReleaseAttributeContext(MftDataContext);
    ReleaseAttributeContext(MirrDataContext);
    ExFreePoolWithTag(DataBuffer, TAG_NTFS);
    ExFreeToNPagedLookasideList(&Vcb->FileRecLookasideList, MirrorFileRecord);

    return Status;
}

#if 0
static
VOID
DumpIndexEntry(PINDEX_ENTRY_ATTRIBUTE IndexEntry)
{
    DPRINT1("Entry: %p\n", IndexEntry);
    DPRINT1("\tData.Directory.IndexedFile: %I64x\n", IndexEntry->Data.Directory.IndexedFile);
    DPRINT1("\tLength: %u\n", IndexEntry->Length);
    DPRINT1("\tKeyLength: %u\n", IndexEntry->KeyLength);
    DPRINT1("\tFlags: %x\n", IndexEntry->Flags);
    DPRINT1("\tReserved: %x\n", IndexEntry->Reserved);
    DPRINT1("\t\tDirectoryFileReferenceNumber: %I64x\n", IndexEntry->FileName.DirectoryFileReferenceNumber);
    DPRINT1("\t\tCreationTime: %I64u\n", IndexEntry->FileName.CreationTime);
    DPRINT1("\t\tChangeTime: %I64u\n", IndexEntry->FileName.ChangeTime);
    DPRINT1("\t\tLastWriteTime: %I64u\n", IndexEntry->FileName.LastWriteTime);
    DPRINT1("\t\tLastAccessTime: %I64u\n", IndexEntry->FileName.LastAccessTime);
    DPRINT1("\t\tAllocatedSize: %I64u\n", IndexEntry->FileName.AllocatedSize);
    DPRINT1("\t\tDataSize: %I64u\n", IndexEntry->FileName.DataSize);
    DPRINT1("\t\tFileAttributes: %x\n", IndexEntry->FileName.FileAttributes);
    DPRINT1("\t\tNameLength: %u\n", IndexEntry->FileName.NameLength);
    DPRINT1("\t\tNameType: %x\n", IndexEntry->FileName.NameType);
    DPRINT1("\t\tName: %.*S\n", IndexEntry->FileName.NameLength, IndexEntry->FileName.Name);
}
#endif

NTSTATUS
BrowseSubNodeIndexEntries(PNTFS_VCB Vcb,
                          PFILE_RECORD_HEADER MftRecord,
                          ULONG IndexBlockSize,
                          PUNICODE_STRING FileName,
                          PNTFS_ATTR_CONTEXT IndexAllocationContext,
                          PRTL_BITMAP Bitmap,
                          ULONGLONG VCN,
                          PULONG StartEntry,
                          PULONG CurrentEntry,
                          BOOLEAN DirSearch,
                          BOOLEAN CaseSensitive,
                          ULONGLONG *OutMFTIndex)
{
    PINDEX_BUFFER IndexRecord;
    ULONGLONG Offset;
    ULONG BytesRead;
    PINDEX_ENTRY_ATTRIBUTE FirstEntry;
    PINDEX_ENTRY_ATTRIBUTE LastEntry;
    PINDEX_ENTRY_ATTRIBUTE IndexEntry;
    ULONG NodeNumber;
    NTSTATUS Status;

    DPRINT("BrowseSubNodeIndexEntries(%p, %p, %lu, %wZ, %p, %p, %I64d, %lu, %lu, %s, %s, %p)\n",
           Vcb,
           MftRecord,
           IndexBlockSize,
           FileName,
           IndexAllocationContext,
           Bitmap,
           VCN,
           *StartEntry,
           *CurrentEntry,
           "FALSE",
           DirSearch ? "TRUE" : "FALSE",
           CaseSensitive ? "TRUE" : "FALSE",
           OutMFTIndex);

    // Calculate node number as VCN / Clusters per index record. An index record smaller than
    // a cluster is addressed in sectors, matching GetAllocationOffsetFromVCN().
    if (IndexBlockSize < Vcb->NtfsInfo.BytesPerCluster)
        NodeNumber = VCN / (IndexBlockSize / Vcb->NtfsInfo.BytesPerSector);
    else
        NodeNumber = VCN / (IndexBlockSize / Vcb->NtfsInfo.BytesPerCluster);

    // Is the bit for this node clear in the bitmap?
    if (!RtlCheckBit(Bitmap, NodeNumber))
    {
        DPRINT1("File system corruption detected, node with VCN %I64u is marked as deleted.\n", VCN);
        return STATUS_DATA_ERROR;
    }

    // Allocate memory for the index record
    IndexRecord = ExAllocatePoolWithTag(NonPagedPool, IndexBlockSize, TAG_NTFS);
    if (!IndexRecord)
    {
        DPRINT1("Unable to allocate memory for index record!\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    // Calculate offset of index record, using the same helper the write path does
    Offset = GetAllocationOffsetFromVCN(Vcb, IndexBlockSize, VCN);

    // Read the index record
    BytesRead = ReadAttribute(Vcb, IndexAllocationContext, Offset, (PCHAR)IndexRecord, IndexBlockSize);
    if (BytesRead != IndexBlockSize)
    {
        DPRINT1("Unable to read index record!\n");
        ExFreePoolWithTag(IndexRecord, TAG_NTFS);
        return STATUS_UNSUCCESSFUL;
    }

    if (IndexRecord->Ntfs.Type != NRH_INDX_TYPE)
    {
        DPRINT1("File system corruption detected, no index record at VCN %I64u of MFT record %I64u "
                "(offset 0x%I64x, block size %lu, cluster %lu, index record %lu, signature 0x%08lx)\n",
                VCN,
                MftRecord->MFTRecordNumber & NTFS_MFT_MASK,
                Offset,
                IndexBlockSize,
                Vcb->NtfsInfo.BytesPerCluster,
                Vcb->NtfsInfo.BytesPerIndexRecord,
                IndexRecord->Ntfs.Type);
        ExFreePoolWithTag(IndexRecord, TAG_NTFS);
        return STATUS_DATA_ERROR;
    }

    // Apply the fixup array to the index record
    Status = FixupUpdateSequenceArray(Vcb, &((PFILE_RECORD_HEADER)IndexRecord)->Ntfs);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(IndexRecord, TAG_NTFS);
        DPRINT1("Failed to apply fixup array!\n");
        return Status;
    }

    ASSERT(IndexRecord->Header.AllocatedSize + FIELD_OFFSET(INDEX_BUFFER, Header) == IndexBlockSize);
    FirstEntry = (PINDEX_ENTRY_ATTRIBUTE)((ULONG_PTR)&IndexRecord->Header + IndexRecord->Header.FirstEntryOffset);
    LastEntry = (PINDEX_ENTRY_ATTRIBUTE)((ULONG_PTR)&IndexRecord->Header + IndexRecord->Header.TotalSizeOfEntries);
    ASSERT(LastEntry <= (PINDEX_ENTRY_ATTRIBUTE)((ULONG_PTR)IndexRecord + IndexBlockSize));

    // Loop through all Index Entries of index, starting with FirstEntry
    IndexEntry = FirstEntry;
    while (IndexEntry <= LastEntry)
    {
        // Does IndexEntry have a sub-node?
        if (IndexEntry->Flags & NTFS_INDEX_ENTRY_NODE)
        {
            if (!(IndexRecord->Header.Flags & INDEX_NODE_LARGE) || !IndexAllocationContext)
            {
                DPRINT1("Filesystem corruption detected!\n");
            }
            else
            {
                Status = BrowseSubNodeIndexEntries(Vcb,
                                                   MftRecord,
                                                   IndexBlockSize,
                                                   FileName,
                                                   IndexAllocationContext,
                                                   Bitmap,
                                                   GetIndexEntryVCN(IndexEntry),
                                                   StartEntry,
                                                   CurrentEntry,
                                                   DirSearch,
                                                   CaseSensitive,
                                                   OutMFTIndex);
                if (NT_SUCCESS(Status))
                {
                    ExFreePoolWithTag(IndexRecord, TAG_NTFS);
                    return Status;
                }
            }
        }

        // Are we done?
        if (IndexEntry->Flags & NTFS_INDEX_ENTRY_END)
            break;

        // If we've found a file whose index is greater than or equal to StartEntry that matches the search criteria
        if ((IndexEntry->Data.Directory.IndexedFile & NTFS_MFT_MASK) >= NTFS_FILE_FIRST_USER_FILE &&
            *CurrentEntry >= *StartEntry &&
            IndexEntry->FileName.NameType != NTFS_FILE_NAME_DOS &&
            CompareFileName(Vcb, FileName, IndexEntry, DirSearch, CaseSensitive))
        {
            *StartEntry = *CurrentEntry;
            *OutMFTIndex = (IndexEntry->Data.Directory.IndexedFile & NTFS_MFT_MASK);
            ExFreePoolWithTag(IndexRecord, TAG_NTFS);
            return STATUS_SUCCESS;
        }

        // Advance to the next index entry
        (*CurrentEntry) += 1;
        ASSERT(IndexEntry->Length >= sizeof(INDEX_ENTRY_ATTRIBUTE));
        IndexEntry = (PINDEX_ENTRY_ATTRIBUTE)((PCHAR)IndexEntry + IndexEntry->Length);
    }

    ExFreePoolWithTag(IndexRecord, TAG_NTFS);

    return STATUS_OBJECT_PATH_NOT_FOUND;
}

NTSTATUS
BrowseIndexEntries(PDEVICE_EXTENSION Vcb,
                   PFILE_RECORD_HEADER MftRecord,
                   PINDEX_ROOT_ATTRIBUTE IndexRecord,
                   ULONG IndexBlockSize,
                   PINDEX_ENTRY_ATTRIBUTE FirstEntry,
                   PINDEX_ENTRY_ATTRIBUTE LastEntry,
                   PUNICODE_STRING FileName,
                   PULONG StartEntry,
                   PULONG CurrentEntry,
                   BOOLEAN DirSearch,
                   BOOLEAN CaseSensitive,
                   ULONGLONG *OutMFTIndex)
{
    NTSTATUS Status;
    PINDEX_ENTRY_ATTRIBUTE IndexEntry;
    PNTFS_ATTR_CONTEXT IndexAllocationContext;
    PNTFS_ATTR_CONTEXT BitmapContext;
    PCHAR *BitmapMem;
    ULONG *BitmapPtr;
    RTL_BITMAP  Bitmap;

    DPRINT("BrowseIndexEntries(%p, %p, %p, %lu, %p, %p, %wZ, %lu, %lu, %s, %s, %p)\n",
           Vcb,
           MftRecord,
           IndexRecord,
           IndexBlockSize,
           FirstEntry,
           LastEntry,
           FileName,
           *StartEntry,
           *CurrentEntry,
           DirSearch ? "TRUE" : "FALSE",
           CaseSensitive ? "TRUE" : "FALSE",
           OutMFTIndex);

    // Find the $I30 index allocation, if there is one
    Status = FindAttribute(Vcb, MftRecord, AttributeIndexAllocation, L"$I30", 4, &IndexAllocationContext, NULL);
    if (NT_SUCCESS(Status))
    {
        ULONGLONG BitmapLength;
        // Find the bitmap attribute for the index
        Status = FindAttribute(Vcb, MftRecord, AttributeBitmap, L"$I30", 4, &BitmapContext, NULL);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("Potential file system corruption detected!\n");
            ReleaseAttributeContext(IndexAllocationContext);
            return Status;
        }

        // Get the length of the bitmap attribute
        BitmapLength = AttributeDataLength(BitmapContext->pRecord);

        // Allocate memory for the bitmap, including some padding; RtlInitializeBitmap() wants a pointer
        // that's ULONG-aligned, and it wants the size of the memory allocated for it to be a ULONG-multiple.
        BitmapMem = ExAllocatePoolWithTag(NonPagedPool, BitmapLength + sizeof(ULONG), TAG_NTFS);
        if (!BitmapMem)
        {
            DPRINT1("Error: failed to allocate bitmap!");
            ReleaseAttributeContext(BitmapContext);
            ReleaseAttributeContext(IndexAllocationContext);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        RtlZeroMemory(BitmapMem, BitmapLength + sizeof(ULONG));

        // RtlInitializeBitmap() wants a pointer that's ULONG-aligned.
        BitmapPtr = (PULONG)ALIGN_UP_BY((ULONG_PTR)BitmapMem, sizeof(ULONG));

        // Read the existing bitmap data
        Status = ReadAttribute(Vcb, BitmapContext, 0, (PCHAR)BitmapPtr, BitmapLength);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("ERROR: Failed to read bitmap attribute!\n");
            ExFreePoolWithTag(BitmapMem, TAG_NTFS);
            ReleaseAttributeContext(BitmapContext);
            ReleaseAttributeContext(IndexAllocationContext);
            return Status;
        }

        // Initialize bitmap
        RtlInitializeBitMap(&Bitmap, BitmapPtr, BitmapLength * 8);
    }
    else
    {
        // Couldn't find an index allocation
        IndexAllocationContext = NULL;
    }


    // Loop through all Index Entries of index, starting with FirstEntry
    IndexEntry = FirstEntry;
    while (IndexEntry <= LastEntry)
    {
        // Does IndexEntry have a sub-node?
        if (IndexEntry->Flags & NTFS_INDEX_ENTRY_NODE)
        {
            if (!(IndexRecord->Header.Flags & INDEX_ROOT_LARGE) || !IndexAllocationContext)
            {
                DPRINT1("Filesystem corruption detected!\n");
            }
            else
            {
                Status = BrowseSubNodeIndexEntries(Vcb,
                                                   MftRecord,
                                                   IndexBlockSize,
                                                   FileName,
                                                   IndexAllocationContext,
                                                   &Bitmap,
                                                   GetIndexEntryVCN(IndexEntry),
                                                   StartEntry,
                                                   CurrentEntry,
                                                   DirSearch,
                                                   CaseSensitive,
                                                   OutMFTIndex);

                /* Anything other than "not in this subtree" means we gave up on
                 * a subtree that may well have held the entry */
                if (!NT_SUCCESS(Status) && Status != STATUS_OBJECT_PATH_NOT_FOUND)
                {
                    DPRINT1("Sub-node at VCN %I64u abandoned while looking for '%wZ' (Status %lx)\n",
                            GetIndexEntryVCN(IndexEntry), FileName, Status);
                }

                if (NT_SUCCESS(Status))
                {
                    ExFreePoolWithTag(BitmapMem, TAG_NTFS);
                    ReleaseAttributeContext(BitmapContext);
                    ReleaseAttributeContext(IndexAllocationContext);
                    return Status;
                }
            }
        }

        // Are we done?
        if (IndexEntry->Flags & NTFS_INDEX_ENTRY_END)
            break;

        // If we've found a file whose index is greater than or equal to StartEntry that matches the search criteria
        if ((IndexEntry->Data.Directory.IndexedFile & NTFS_MFT_MASK) >= NTFS_FILE_FIRST_USER_FILE &&
            *CurrentEntry >= *StartEntry &&
            IndexEntry->FileName.NameType != NTFS_FILE_NAME_DOS &&
            CompareFileName(Vcb, FileName, IndexEntry, DirSearch, CaseSensitive))
        {
            *StartEntry = *CurrentEntry;
            *OutMFTIndex = (IndexEntry->Data.Directory.IndexedFile & NTFS_MFT_MASK);
            if (IndexAllocationContext)
            {
                ExFreePoolWithTag(BitmapMem, TAG_NTFS);
                ReleaseAttributeContext(BitmapContext);
                ReleaseAttributeContext(IndexAllocationContext);
            }
            return STATUS_SUCCESS;
        }

        // Advance to the next index entry
        (*CurrentEntry) += 1;
        ASSERT(IndexEntry->Length >= sizeof(INDEX_ENTRY_ATTRIBUTE));
        IndexEntry = (PINDEX_ENTRY_ATTRIBUTE)((PCHAR)IndexEntry + IndexEntry->Length);
    }

    if (IndexAllocationContext)
    {
        DPRINT("'%wZ' not found after scanning %lu entries of MFT record %I64u "
               "(index allocation present)\n",
               FileName, *CurrentEntry, MftRecord->MFTRecordNumber & NTFS_MFT_MASK);

        ExFreePoolWithTag(BitmapMem, TAG_NTFS);
        ReleaseAttributeContext(BitmapContext);
        ReleaseAttributeContext(IndexAllocationContext);
    }

    return STATUS_OBJECT_PATH_NOT_FOUND;
}

NTSTATUS
NtfsFindMftRecord(PDEVICE_EXTENSION Vcb,
                  ULONGLONG MFTIndex,
                  PUNICODE_STRING FileName,
                  PULONG FirstEntry,
                  BOOLEAN DirSearch,
                  BOOLEAN CaseSensitive,
                  ULONGLONG *OutMFTIndex)
{
    PFILE_RECORD_HEADER MftRecord;
    PNTFS_ATTR_CONTEXT IndexRootCtx;
    PINDEX_ROOT_ATTRIBUTE IndexRoot;
    PCHAR IndexRecord;
    PINDEX_ENTRY_ATTRIBUTE IndexEntry, IndexEntryEnd;
    NTSTATUS Status;
    ULONG CurrentEntry = 0;

    DPRINT("NtfsFindMftRecord(%p, %I64d, %wZ, %lu, %s, %s, %p)\n",
           Vcb,
           MFTIndex,
           FileName,
           *FirstEntry,
           DirSearch ? "TRUE" : "FALSE",
           CaseSensitive ? "TRUE" : "FALSE",
           OutMFTIndex);

    MftRecord = ExAllocateFromNPagedLookasideList(&Vcb->FileRecLookasideList);
    if (MftRecord == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = ReadFileRecord(Vcb, MFTIndex, MftRecord);
    if (!NT_SUCCESS(Status))
    {
        ExFreeToNPagedLookasideList(&Vcb->FileRecLookasideList, MftRecord);
        return Status;
    }

    ASSERT(MftRecord->Ntfs.Type == NRH_FILE_TYPE);
    Status = FindAttribute(Vcb, MftRecord, AttributeIndexRoot, L"$I30", 4, &IndexRootCtx, NULL);
    if (!NT_SUCCESS(Status))
    {
        ExFreeToNPagedLookasideList(&Vcb->FileRecLookasideList, MftRecord);
        return Status;
    }

    IndexRecord = ExAllocatePoolWithTag(NonPagedPool, Vcb->NtfsInfo.BytesPerIndexRecord, TAG_NTFS);
    if (IndexRecord == NULL)
    {
        ReleaseAttributeContext(IndexRootCtx);
        ExFreeToNPagedLookasideList(&Vcb->FileRecLookasideList, MftRecord);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    ReadAttribute(Vcb, IndexRootCtx, 0, IndexRecord, Vcb->NtfsInfo.BytesPerIndexRecord);
    IndexRoot = (PINDEX_ROOT_ATTRIBUTE)IndexRecord;
    IndexEntry = (PINDEX_ENTRY_ATTRIBUTE)((PCHAR)&IndexRoot->Header + IndexRoot->Header.FirstEntryOffset);
    /* Index root is always resident. */
    /* TotalSizeOfEntries is measured from the index header, not from the
     * start of the attribute, and FirstEntryOffset above already is. */
    IndexEntryEnd = (PINDEX_ENTRY_ATTRIBUTE)((ULONG_PTR)&IndexRoot->Header + IndexRoot->Header.TotalSizeOfEntries);
    ReleaseAttributeContext(IndexRootCtx);

    DPRINT("IndexRecordSize: %x IndexBlockSize: %x\n", Vcb->NtfsInfo.BytesPerIndexRecord, IndexRoot->SizeOfEntry);

    Status = BrowseIndexEntries(Vcb,
                                MftRecord,
                                (PINDEX_ROOT_ATTRIBUTE)IndexRecord,
                                IndexRoot->SizeOfEntry,
                                IndexEntry,
                                IndexEntryEnd,
                                FileName,
                                FirstEntry,
                                &CurrentEntry,
                                DirSearch,
                                CaseSensitive,
                                OutMFTIndex);

    ExFreePoolWithTag(IndexRecord, TAG_NTFS);
    ExFreeToNPagedLookasideList(&Vcb->FileRecLookasideList, MftRecord);

    return Status;
}

NTSTATUS
NtfsLookupFileAt(PDEVICE_EXTENSION Vcb,
                 PUNICODE_STRING PathName,
                 BOOLEAN CaseSensitive,
                 PFILE_RECORD_HEADER *FileRecord,
                 PULONGLONG MFTIndex,
                 ULONGLONG CurrentMFTIndex)
{
    UNICODE_STRING Current, Remaining;
    NTSTATUS Status;
    ULONG FirstEntry = 0;

    DPRINT("NtfsLookupFileAt(%p, %wZ, %s, %p, %p, %I64x)\n",
           Vcb,
           PathName,
           CaseSensitive ? "TRUE" : "FALSE",
           FileRecord,
           MFTIndex,
           CurrentMFTIndex);

    FsRtlDissectName(*PathName, &Current, &Remaining);

    while (Current.Length != 0)
    {
        DPRINT("Current: %wZ\n", &Current);

        Status = NtfsFindMftRecord(Vcb, CurrentMFTIndex, &Current, &FirstEntry, FALSE, CaseSensitive, &CurrentMFTIndex);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }

        if (Remaining.Length == 0)
            break;

        /* Split what is left of the path, not the component we just resolved */
        FsRtlDissectName(Remaining, &Current, &Remaining);
    }

    *FileRecord = ExAllocateFromNPagedLookasideList(&Vcb->FileRecLookasideList);
    if (*FileRecord == NULL)
    {
        DPRINT("NtfsLookupFileAt: Can't allocate MFT record\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = ReadFileRecord(Vcb, CurrentMFTIndex, *FileRecord);
    if (!NT_SUCCESS(Status))
    {
        DPRINT("NtfsLookupFileAt: Can't read MFT record\n");
        ExFreeToNPagedLookasideList(&Vcb->FileRecLookasideList, *FileRecord);
        return Status;
    }

    *MFTIndex = CurrentMFTIndex;

    return STATUS_SUCCESS;
}

NTSTATUS
NtfsLookupFile(PDEVICE_EXTENSION Vcb,
               PUNICODE_STRING PathName,
               BOOLEAN CaseSensitive,
               PFILE_RECORD_HEADER *FileRecord,
               PULONGLONG MFTIndex)
{
    return NtfsLookupFileAt(Vcb, PathName, CaseSensitive, FileRecord, MFTIndex, NTFS_FILE_ROOT);
}

void
NtfsDumpData(ULONG_PTR Buffer, ULONG Length)
{
    ULONG i, j;

    // dump binary data, 8 bytes at a time
    for (i = 0; i < Length; i += 8)
    {
        // display current offset, in hex
        DbgPrint("\t%03x\t", i);

        // display hex value of each of the next 8 bytes
        for (j = 0; j < 8; j++)
            DbgPrint("%02x ", *(PUCHAR)(Buffer + i + j));
        DbgPrint("\n");
    }
}

/**
* @name NtfsDumpFileRecord
* @implemented
*
* Provides diagnostic information about a file record. Prints a hex dump
* of the entire record (based on the size reported by FileRecord->ByesInUse),
* then prints a dump of each attribute.
*
* @param Vcb
* Pointer to a DEVICE_EXTENSION describing the volume.
*
* @param FileRecord
* Pointer to the file record to be analyzed.
*
* @remarks
* FileRecord must be a complete file record at least FileRecord->BytesAllocated
* in size, and not just the header.
*
*/
VOID
NtfsDumpFileRecord(PDEVICE_EXTENSION Vcb,
                   PFILE_RECORD_HEADER FileRecord)
{
    ULONG i, j;

    // dump binary data, 8 bytes at a time
    for (i = 0; i < FileRecord->BytesInUse; i += 8)
    {
        // display current offset, in hex
        DbgPrint("\t%03x\t", i);

        // display hex value of each of the next 8 bytes
        for (j = 0; j < 8; j++)
            DbgPrint("%02x ", *(PUCHAR)((ULONG_PTR)FileRecord + i + j));
        DbgPrint("\n");
    }

    NtfsDumpFileAttributes(Vcb, FileRecord);
}

NTSTATUS
NtfsFindFileAt(PDEVICE_EXTENSION Vcb,
               PUNICODE_STRING SearchPattern,
               PULONG FirstEntry,
               PFILE_RECORD_HEADER *FileRecord,
               PULONGLONG MFTIndex,
               ULONGLONG CurrentMFTIndex,
               BOOLEAN CaseSensitive)
{
    NTSTATUS Status;

    DPRINT("NtfsFindFileAt(%p, %wZ, %lu, %p, %p, %I64x, %s)\n",
           Vcb,
           SearchPattern,
           *FirstEntry,
           FileRecord,
           MFTIndex,
           CurrentMFTIndex,
           (CaseSensitive ? "TRUE" : "FALSE"));

    Status = NtfsFindMftRecord(Vcb, CurrentMFTIndex, SearchPattern, FirstEntry, TRUE, CaseSensitive, &CurrentMFTIndex);
    if (!NT_SUCCESS(Status))
    {
        DPRINT("NtfsFindFileAt: NtfsFindMftRecord() failed with status 0x%08lx\n", Status);
        return Status;
    }

    *FileRecord = ExAllocateFromNPagedLookasideList(&Vcb->FileRecLookasideList);
    if (*FileRecord == NULL)
    {
        DPRINT("NtfsFindFileAt: Can't allocate MFT record\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = ReadFileRecord(Vcb, CurrentMFTIndex, *FileRecord);
    if (!NT_SUCCESS(Status))
    {
        DPRINT("NtfsFindFileAt: Can't read MFT record\n");
        ExFreeToNPagedLookasideList(&Vcb->FileRecLookasideList, *FileRecord);
        return Status;
    }

    *MFTIndex = CurrentMFTIndex;

    return STATUS_SUCCESS;
}


/* Deeper than any index this driver or Windows builds; past it the tree loops */
#define NTFS_MAX_INDEX_DEPTH 32

static
NTSTATUS
NtfsIndexEntriesAreEmpty(
    _In_ PDEVICE_EXTENSION Vcb,
    _In_ PNTFS_ATTR_CONTEXT IndexAllocationContext,
    _In_ ULONG IndexBlockSize,
    _In_ PINDEX_HEADER_ATTRIBUTE Header,
    _In_ ULONG Depth,
    _Out_ PBOOLEAN Empty);

/**
* @name NtfsIndexNodeIsEmpty
*
* Reads the index node at Vcn and reports whether its subtree holds any entry.
*/
static
NTSTATUS
NtfsIndexNodeIsEmpty(
    _In_ PDEVICE_EXTENSION Vcb,
    _In_ PNTFS_ATTR_CONTEXT IndexAllocationContext,
    _In_ ULONG IndexBlockSize,
    _In_ ULONGLONG Vcn,
    _In_ ULONG Depth,
    _Out_ PBOOLEAN Empty)
{
    PINDEX_BUFFER IndexBuffer;
    ULONG BytesRead;
    NTSTATUS Status;

    if (IndexAllocationContext == NULL || Depth >= NTFS_MAX_INDEX_DEPTH)
        return STATUS_FILE_CORRUPT_ERROR;

    IndexBuffer = ExAllocatePoolWithTag(NonPagedPool, IndexBlockSize, TAG_NTFS);
    if (IndexBuffer == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    BytesRead = ReadAttribute(Vcb,
                              IndexAllocationContext,
                              GetAllocationOffsetFromVCN(Vcb, IndexBlockSize, Vcn),
                              (PCHAR)IndexBuffer,
                              IndexBlockSize);
    if (BytesRead != IndexBlockSize || IndexBuffer->Ntfs.Type != NRH_INDX_TYPE)
    {
        DPRINT1("Unable to read index node at VCN %I64u\n", Vcn);
        ExFreePoolWithTag(IndexBuffer, TAG_NTFS);
        return STATUS_FILE_CORRUPT_ERROR;
    }

    Status = FixupUpdateSequenceArray(Vcb, &((PFILE_RECORD_HEADER)IndexBuffer)->Ntfs);
    if (NT_SUCCESS(Status) &&
        FIELD_OFFSET(INDEX_BUFFER, Header) + IndexBuffer->Header.TotalSizeOfEntries > IndexBlockSize)
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
    }

    if (NT_SUCCESS(Status))
    {
        Status = NtfsIndexEntriesAreEmpty(Vcb,
                                          IndexAllocationContext,
                                          IndexBlockSize,
                                          &IndexBuffer->Header,
                                          Depth + 1,
                                          Empty);
    }

    ExFreePoolWithTag(IndexBuffer, TAG_NTFS);
    return Status;
}

/**
* @name NtfsIndexEntriesAreEmpty
*
* Checks the entries under Header. Entries are sorted with the terminator last,
* so the first one decides: anything but the terminator is a child, and a
* terminator that points down only leads to more entries to check.
*/
static
NTSTATUS
NtfsIndexEntriesAreEmpty(
    _In_ PDEVICE_EXTENSION Vcb,
    _In_ PNTFS_ATTR_CONTEXT IndexAllocationContext,
    _In_ ULONG IndexBlockSize,
    _In_ PINDEX_HEADER_ATTRIBUTE Header,
    _In_ ULONG Depth,
    _Out_ PBOOLEAN Empty)
{
    PINDEX_ENTRY_ATTRIBUTE IndexEntry;

    /* A terminator has no key, so only the fixed header is guaranteed */
    if (Header->FirstEntryOffset + FIELD_OFFSET(INDEX_ENTRY_ATTRIBUTE, FileName) > Header->TotalSizeOfEntries)
        return STATUS_FILE_CORRUPT_ERROR;

    IndexEntry = (PINDEX_ENTRY_ATTRIBUTE)((ULONG_PTR)Header + Header->FirstEntryOffset);

    if (Header->FirstEntryOffset + IndexEntry->Length > Header->TotalSizeOfEntries)
        return STATUS_FILE_CORRUPT_ERROR;

    if (!(IndexEntry->Flags & NTFS_INDEX_ENTRY_END))
    {
        *Empty = FALSE;
        return STATUS_SUCCESS;
    }

    if (IndexEntry->Flags & NTFS_INDEX_ENTRY_NODE)
    {
        /* The child's VCN sits in the last 8 bytes of the entry */
        if (IndexEntry->Length < FIELD_OFFSET(INDEX_ENTRY_ATTRIBUTE, FileName) + sizeof(ULONGLONG))
            return STATUS_FILE_CORRUPT_ERROR;

        return NtfsIndexNodeIsEmpty(Vcb,
                                    IndexAllocationContext,
                                    IndexBlockSize,
                                    GetIndexEntryVCN(IndexEntry),
                                    Depth,
                                    Empty);
    }

    *Empty = TRUE;
    return STATUS_SUCCESS;
}

/**
* @name NtfsIsDirectoryEmpty
* @implemented
*
* Decides whether a directory may be deleted. Unlike a name lookup, a sub-node
* that can't be read is an error here rather than a miss, since it may hold children.
*
* @param Vcb
* Volume holding the directory.
*
* @param MftIndex
* MFT record of the directory.
*
* @param Empty
* Receives TRUE if the index holds no entries.
*
* @return
* STATUS_SUCCESS if Empty was set, or an error if the index couldn't be fully read.
*/
NTSTATUS
NtfsIsDirectoryEmpty(
    _In_ PDEVICE_EXTENSION Vcb,
    _In_ ULONGLONG MftIndex,
    _Out_ PBOOLEAN Empty)
{
    PFILE_RECORD_HEADER FileRecord;
    PNTFS_ATTR_CONTEXT IndexRootContext = NULL;
    PNTFS_ATTR_CONTEXT IndexAllocationContext = NULL;
    PINDEX_ROOT_ATTRIBUTE IndexRoot = NULL;
    ULONG IndexRootLength;
    NTSTATUS Status;

    *Empty = FALSE;

    FileRecord = ExAllocateFromNPagedLookasideList(&Vcb->FileRecLookasideList);
    if (FileRecord == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = ReadFileRecord(Vcb, MftIndex, FileRecord);
    if (!NT_SUCCESS(Status))
        goto Quit;

    Status = FindAttribute(Vcb, FileRecord, AttributeIndexRoot, L"$I30", 4, &IndexRootContext, NULL);
    if (!NT_SUCCESS(Status))
        goto Quit;

    IndexRootLength = (ULONG)AttributeDataLength(IndexRootContext->pRecord);
    if (IndexRootLength < sizeof(*IndexRoot))
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Quit;
    }

    IndexRoot = ExAllocatePoolWithTag(NonPagedPool, IndexRootLength, TAG_NTFS);
    if (IndexRoot == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Quit;
    }

    if (ReadAttribute(Vcb, IndexRootContext, 0, (PCHAR)IndexRoot, IndexRootLength) != IndexRootLength)
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Quit;
    }

    if (FIELD_OFFSET(INDEX_ROOT_ATTRIBUTE, Header) + IndexRoot->Header.TotalSizeOfEntries > IndexRootLength)
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Quit;
    }

    /* Only a root that points down needs the allocation */
    if (IndexRoot->Header.Flags & INDEX_ROOT_LARGE)
    {
        Status = FindAttribute(Vcb, FileRecord, AttributeIndexAllocation, L"$I30", 4, &IndexAllocationContext, NULL);
        if (!NT_SUCCESS(Status))
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Quit;
        }
    }

    Status = NtfsIndexEntriesAreEmpty(Vcb,
                                      IndexAllocationContext,
                                      IndexRoot->SizeOfEntry,
                                      &IndexRoot->Header,
                                      0,
                                      Empty);

Quit:
    if (IndexRoot != NULL)
        ExFreePoolWithTag(IndexRoot, TAG_NTFS);
    if (IndexAllocationContext != NULL)
        ReleaseAttributeContext(IndexAllocationContext);
    if (IndexRootContext != NULL)
        ReleaseAttributeContext(IndexRootContext);
    ExFreeToNPagedLookasideList(&Vcb->FileRecLookasideList, FileRecord);

    return Status;
}

/* EOF */
