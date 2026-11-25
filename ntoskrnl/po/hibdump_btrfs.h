/*
 * PROJECT:   CATV 95 Kernel
 * LICENSE:   GPL-2.0-or-later
 * PURPOSE:   Side-effect-free validation of the Btrfs GET_DEVICES ABI used
 *            by the hibernation dump-stack preflight and regression tests.
 */

#ifndef _PO_DUMP_BTRFS_H_
#define _PO_DUMP_BTRFS_H_

/*
 * Keep this layout in lockstep with btrfsioctl.h's btrfs_device. In
 * particular, do not hard-code ULONGLONG offsets: the target compiler's
 * native structure alignment is part of this legacy variable-length ABI.
 */
typedef struct _PO_BTRFS_DEVICE_ENTRY
{
    ULONG NextEntry;
    ULONGLONG DeviceId;
    ULONGLONG Size;
    ULONGLONG MaxSize;
    BOOL ReadOnly;
    BOOL Missing;
    ULONG DeviceNumber;
    ULONG PartitionNumber;
    ULONGLONG Stats[5];
    USHORT NameLength;
    WCHAR Name[1];
} PO_BTRFS_DEVICE_ENTRY, *PPO_BTRFS_DEVICE_ENTRY;

/*
 * Validate the first record returned by FSCTL_BTRFS_GET_DEVICES. The kernel
 * ATA dump writer only supports one physical member, so a well-formed
 * NextEntry is enough to reject a multi-member filesystem without following
 * or trusting the rest of its variable-length list.
 *
 * get_devices() currently does not set IoStatus.Information. BufferLength is
 * therefore the actual allocated output capacity; the zeroed buffer makes
 * an unwritten/truncated first record fail the mandatory identity checks.
 */
static __inline
NTSTATUS
PopDumpBtrfsValidateDeviceList(
    _In_reads_bytes_(BufferLength) const VOID *Buffer,
    _In_ ULONG BufferLength,
    _Out_ PULONG MemberCount)
{
    const ULONG FixedEntrySize =
        sizeof(PO_BTRFS_DEVICE_ENTRY) - sizeof(WCHAR);
    PO_BTRFS_DEVICE_ENTRY Entry;
    ULONG NameBytes;

    if (Buffer == NULL || MemberCount == NULL)
        return STATUS_INVALID_PARAMETER;

    *MemberCount = 0;

    if (BufferLength < FixedEntrySize)
        return STATUS_FILE_CORRUPT_ERROR;

    RtlZeroMemory(&Entry, sizeof(Entry));
    RtlCopyMemory(&Entry,
                  Buffer,
                  FIELD_OFFSET(PO_BTRFS_DEVICE_ENTRY, Name));

    NameBytes = Entry.NameLength;
    if (Entry.DeviceId == 0 || Entry.Size == 0 ||
        Entry.MaxSize < Entry.Size ||
        (NameBytes % sizeof(WCHAR)) != 0 ||
        NameBytes > BufferLength - FixedEntrySize)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    if (Entry.ReadOnly || Entry.Missing)
        return STATUS_NOT_SUPPORTED;

    *MemberCount = 1;

    if (Entry.NextEntry != 0)
    {
        if (Entry.NextEntry < FixedEntrySize + NameBytes ||
            Entry.NextEntry > BufferLength)
        {
            *MemberCount = 0;
            return STATUS_FILE_CORRUPT_ERROR;
        }

        *MemberCount = 2; /* At least two members; the ATA path cannot use it. */
        return STATUS_NOT_SUPPORTED;
    }

    return STATUS_SUCCESS;
}

#endif /* _PO_DUMP_BTRFS_H_ */
