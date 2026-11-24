/*
 * PROJECT:   CATV 95 Kernel
 * LICENSE:   GPL-2.0-or-later
 * PURPOSE:   Shared, side-effect-free ATA IDENTIFY and address helpers for
 *            the hibernation dump stack and its regression tests.
 */

#ifndef _PO_DUMP_ATA_H_
#define _PO_DUMP_ATA_H_

#define PO_DUMP_ATA_LBA28_MAX_SECTORS 0x10000000ULL
#define PO_DUMP_ATA_LBA48_MAX_SECTORS 0x0001000000000000ULL

typedef struct _PO_DUMP_ATA_INFO
{
    BOOLEAN Lba48;
    BOOLEAN FlushCache;
    BOOLEAN FlushCacheExt;
    ULONGLONG TotalSectors;
} PO_DUMP_ATA_INFO, *PPO_DUMP_ATA_INFO;

typedef struct _PO_DUMP_ATA_ADDRESS
{
    UCHAR DeviceHead;
    UCHAR HighFeatures;
    UCHAR LowFeatures;
    UCHAR HighCount;
    UCHAR HighLbaLow;
    UCHAR HighLbaMid;
    UCHAR HighLbaHigh;
    UCHAR LowCount;
    UCHAR LowLbaLow;
    UCHAR LowLbaMid;
    UCHAR LowLbaHigh;
} PO_DUMP_ATA_ADDRESS, *PPO_DUMP_ATA_ADDRESS;

/*
 * IDENTIFY words 83/86 contain supported/enabled command-set bits. Their
 * validity is advertised by words 83 and 87 respectively (bits 15:14 == 01b).
 */
static __inline
BOOLEAN
PopDumpAtaParseIdentify(
    _In_reads_(256) const USHORT *Id,
    _Out_ PPO_DUMP_ATA_INFO Info)
{
    BOOLEAN Lba48Supported;
    BOOLEAN Lba48Enabled;

    if (Id == NULL || Info == NULL)
        return FALSE;

    Info->Lba48 = FALSE;
    Info->FlushCache = FALSE;
    Info->FlushCacheExt = FALSE;
    Info->TotalSectors = 0;

    Lba48Supported = ((Id[83] & 0xC000) == 0x4000) &&
                     ((Id[83] & 0x0400) != 0);
    Lba48Enabled = ((Id[87] & 0xC000) == 0x4000) &&
                   ((Id[86] & 0x0400) != 0);

    if (Lba48Supported && Lba48Enabled)
    {
        /* Word 103 is reserved for the 48-bit sector count. */
        if (Id[103] != 0)
            return FALSE;

        Info->TotalSectors = (ULONGLONG)Id[100] |
                             ((ULONGLONG)Id[101] << 16) |
                             ((ULONGLONG)Id[102] << 32);
        if (Info->TotalSectors == 0 ||
            Info->TotalSectors > PO_DUMP_ATA_LBA48_MAX_SECTORS)
            return FALSE;

        Info->Lba48 = TRUE;
        Info->FlushCache =
            ((Id[83] & 0x1000) != 0) &&
            ((Id[86] & 0x1000) != 0);
        Info->FlushCacheExt =
            ((Id[83] & 0x2000) != 0) &&
            ((Id[86] & 0x2000) != 0);
        return TRUE;
    }

    /* Word 49 bit 9 advertises ordinary LBA support. */
    if (!(Id[49] & 0x0200))
        return FALSE;

    Info->TotalSectors = (ULONGLONG)Id[60] |
                         ((ULONGLONG)Id[61] << 16);
    if (Info->TotalSectors == 0 ||
        Info->TotalSectors > PO_DUMP_ATA_LBA28_MAX_SECTORS)
        return FALSE;

    Info->FlushCache = ((Id[83] & 0xC000) == 0x4000) &&
                       ((Id[83] & 0x1000) != 0) &&
                       ((Id[87] & 0xC000) == 0x4000) &&
                       ((Id[86] & 0x1000) != 0);
    Info->FlushCacheExt = FALSE;
    return TRUE;
}

/*
 * Produce the exact ATA task-file register values. The caller writes the
 * high-order LBA48 bytes before the low-order bytes, as required by ATA.
 */
static __inline
BOOLEAN
PopDumpAtaBuildAddress(
    _In_ BOOLEAN Lba48,
    _In_ ULONG Device,
    _In_ ULONGLONG Lba,
    _In_ ULONG Count,
    _Out_ PPO_DUMP_ATA_ADDRESS Address)
{
    if (Address == NULL || Device > 1 || Count == 0)
        return FALSE;

    if (Lba48)
    {
        if (Lba >= PO_DUMP_ATA_LBA48_MAX_SECTORS ||
            Count > 0x10000 ||
            (ULONGLONG)(Count - 1) >= PO_DUMP_ATA_LBA48_MAX_SECTORS - Lba)
            return FALSE;

        Address->DeviceHead = (UCHAR)(0x40 | (Device << 4));
        Address->HighFeatures = 0;
        Address->LowFeatures = 0;
        Address->HighCount = (UCHAR)(Count >> 8);
        Address->HighLbaLow = (UCHAR)(Lba >> 24);
        Address->HighLbaMid = (UCHAR)(Lba >> 32);
        Address->HighLbaHigh = (UCHAR)(Lba >> 40);
        Address->LowCount = (UCHAR)Count;
        Address->LowLbaLow = (UCHAR)Lba;
        Address->LowLbaMid = (UCHAR)(Lba >> 8);
        Address->LowLbaHigh = (UCHAR)(Lba >> 16);
    }
    else
    {
        if (Lba >= PO_DUMP_ATA_LBA28_MAX_SECTORS ||
            Count > 256 ||
            (ULONGLONG)(Count - 1) >= PO_DUMP_ATA_LBA28_MAX_SECTORS - Lba)
            return FALSE;

        Address->DeviceHead = (UCHAR)(0xE0 | (Device << 4) |
                                      ((ULONG)(Lba >> 24) & 0x0F));
        Address->HighFeatures = 0;
        Address->LowFeatures = 0;
        Address->HighCount = 0;
        Address->HighLbaLow = 0;
        Address->HighLbaMid = 0;
        Address->HighLbaHigh = 0;
        Address->LowCount = (UCHAR)Count;
        Address->LowLbaLow = (UCHAR)Lba;
        Address->LowLbaMid = (UCHAR)(Lba >> 8);
        Address->LowLbaHigh = (UCHAR)(Lba >> 16);
    }

    return TRUE;
}

#endif /* _PO_DUMP_ATA_H_ */
