/*
 * PROJECT:   CATV 95 Kernel
 * LICENSE:   GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:   Hibernation dump stack: writes the hibernation image to disk
 *            without the I/O manager, the file system or interrupts
 *
 * This file is included by hiber.c (it is not built on its own, so that the
 * build list does not need to change).
 *
 * Writing the image through ZwWriteFile needs PASSIVE_LEVEL and interrupts,
 * so memory keeps changing while it is serialized. The dump stack writes the
 * sectors of the hibernation file directly, by polling, so that the whole
 * image can be written at DISPATCH_LEVEL with interrupts disabled.
 *
 * It has two parts:
 *  - Preparation at PASSIVE_LEVEL: the on-disk extents of hiberfil.sys are
 *    queried (FSCTL_GET_RETRIEVAL_POINTERS) and translated to disk sectors.
 *  - A polled ATA (PIO, LBA28/LBA48) writer for the legacy IDE ports.
 *
 * The dump stack is only for disks on a legacy IDE channel, that is an IDE
 * controller (also a SATA one in IDE mode) in compatibility mode, which
 * decodes the fixed ports 1F0h/3F6h and 170h/376h. This is checked first,
 * from the PCI class code and programming interface of the controllers;
 * without such a channel nothing is touched and the image is written the
 * usual way, at PASSIVE_LEVEL through the file system. The same goes for
 * disks on any other kind of controller (AHCI, NVMe, SCSI, USB, native mode
 * IDE, ...), and for volumes that are not directly on such a disk.
 *
 * The disk is not taken from the storage stack. Instead, unique test data is
 * written to the file the normal way, flushed, and the devices of the legacy
 * channels are searched for the one that returns the same data at the
 * computed sectors. Only a device that passes this test is ever written to.
 * If anything does not check out (a file system that can't report extents,
 * ...) the dump stack is not used either.
 *
 * Limits: 512-byte sectors, legacy IDE ports, one disk per volume, a file
 * whose clusters are all allocated. Windows' dump stack loads a port and a
 * miniport driver for the disk instead; ReactOS has no such interface yet.
 */

#if defined(_M_IX86) || defined(_M_AMD64)

#include <ntdddisk.h>
#include "hibdump_ata.h"

#define PO_DUMP_SIGNATURE       'pmDP'
#define PO_DUMP_TAG             'pDoP'
#define PO_DUMP_SECTOR_SIZE     512
#define PO_DUMP_MAX_RUN         128         /* sectors per ATA command */
#define PO_DUMP_MAX_EXTENTS     16384
#define PO_DUMP_RP_EXTENTS      64
#define PO_DUMP_TEST_SECTORS    5

#define PO_DUMP_IDLE_TIMEOUT_US     1000000     /* 1 s */
#define PO_DUMP_CMD_TIMEOUT_US      5000000     /* 5 s */
#define PO_DUMP_FLUSH_TIMEOUT_US    30000000    /* 30 s */
#define PO_DUMP_PROBE_TIMEOUT_US    100000      /* 100 ms */

#define ATA_STATUS_ERR  0x01
#define ATA_STATUS_DRQ  0x08
#define ATA_STATUS_DF   0x20
#define ATA_STATUS_BSY  0x80

#define ATA_CMD_READ        0x20
#define ATA_CMD_READ_EXT    0x24
#define ATA_CMD_WRITE       0x30
#define ATA_CMD_WRITE_EXT   0x34
#define ATA_CMD_FLUSH       0xE7
#define ATA_CMD_FLUSH_EXT   0xEA
#define ATA_CMD_IDENTIFY    0xEC

typedef struct _PO_DUMP_EXTENT
{
    ULONGLONG FileSector;
    ULONGLONG DiskSector;
    ULONGLONG Count;
} PO_DUMP_EXTENT, *PPO_DUMP_EXTENT;

typedef struct _PO_DUMP_STACK
{
    ULONG Signature;

    /* The ATA device (valid once the test passed) */
    PUCHAR CommandBase;
    PUCHAR ControlBase;
    ULONG Device;
    BOOLEAN Lba48;
    BOOLEAN FlushCache;
    BOOLEAN FlushCacheExt;
    ULONGLONG TotalSectors;

    /* The hibernation file */
    ULONG ExtentCount;
    ULONG ExtentCapacity;
    PPO_DUMP_EXTENT Extents;
    ULONG LastExtent;
    ULONGLONG FileSectors;      /* sectors the image may use */

    /* Sequential writes of less than a sector are merged here */
    ULONGLONG HighWater;        /* sectors below were written in this run */
    BOOLEAN CacheValid;
    BOOLEAN CacheDirty;
    ULONGLONG CacheSector;
    UCHAR Cache[PO_DUMP_SECTOR_SIZE];
} PO_DUMP_STACK, *PPO_DUMP_STACK;

/* ATA ************************************************************************/

static
UCHAR
PopAtaAltStatus(
    _In_ PPO_DUMP_STACK Stack)
{
    return READ_PORT_UCHAR(Stack->ControlBase);
}

static
VOID
PopAtaDelay(
    _In_ PPO_DUMP_STACK Stack)
{
    ULONG i;

    /* 400ns after selecting a device or writing a command */
    for (i = 0; i < 4; i++)
        (VOID)PopAtaAltStatus(Stack);
}

/* Wait for BSY to clear. With Drq, also for DRQ. Errors are reported. */
static
NTSTATUS
PopAtaPoll(
    _In_ PPO_DUMP_STACK Stack,
    _In_ ULONG TimeoutUs,
    _In_ BOOLEAN WantDrq,
    _In_ BOOLEAN FailOnError)
{
    ULONG Elapsed;
    UCHAR Status;

    for (Elapsed = 0; Elapsed < TimeoutUs; Elapsed += 10)
    {
        Status = PopAtaAltStatus(Stack);
        if (!(Status & ATA_STATUS_BSY))
        {
            if (FailOnError && (Status & (ATA_STATUS_ERR | ATA_STATUS_DF)))
                return STATUS_IO_DEVICE_ERROR;
            if (!WantDrq || (Status & ATA_STATUS_DRQ))
                return STATUS_SUCCESS;
        }
        KeStallExecutionProcessor(10);
    }

    return STATUS_IO_TIMEOUT;
}

static
NTSTATUS
PopAtaSelect(
    _In_ PPO_DUMP_STACK Stack,
    _In_ ULONG TimeoutUs)
{
    NTSTATUS Status;

    WRITE_PORT_UCHAR(Stack->CommandBase + 6, (UCHAR)(0xA0 | (Stack->Device << 4)));
    PopAtaDelay(Stack);

    Status = PopAtaPoll(Stack, TimeoutUs, FALSE, FALSE);
    if (!NT_SUCCESS(Status))
        return Status;

    /* Somebody is in the middle of a PIO transfer: do not touch it */
    if (PopAtaAltStatus(Stack) & ATA_STATUS_DRQ)
        return STATUS_DEVICE_BUSY;

    return STATUS_SUCCESS;
}

static
NTSTATUS
PopAtaCheckRange(
    _In_ PPO_DUMP_STACK Stack,
    _In_ ULONGLONG Lba,
    _In_ ULONG Count)
{
    if (Count == 0 || Count > PO_DUMP_MAX_RUN)
        return STATUS_INVALID_PARAMETER;
    if (Lba >= Stack->TotalSectors || Count > Stack->TotalSectors - Lba)
        return STATUS_DISK_FULL;
    /*
     * LBA28 addresses sectors 0 through 0x0FFFFFFF inclusive. Count is a
     * sector count, so compare the final addressed sector rather than the
     * exclusive end; the old comparison incorrectly rejected the last
     * representable LBA even for a one-sector request.
     */
    if (!Stack->Lba48 &&
        (Lba > 0x0FFFFFFFULL ||
         (ULONGLONG)(Count - 1) > 0x0FFFFFFFULL - Lba))
    {
        return STATUS_DISK_FULL;
    }
    return STATUS_SUCCESS;
}

static
VOID
PopAtaLoadAddress(
    _In_ PPO_DUMP_STACK Stack,
    _In_ ULONGLONG Lba,
    _In_ ULONG Count)
{
    PUCHAR Base = Stack->CommandBase;

    PO_DUMP_ATA_ADDRESS Address;

    ASSERT(PopDumpAtaBuildAddress(Stack->Lba48,
                                  Stack->Device,
                                  Lba,
                                  Count,
                                  &Address));

    WRITE_PORT_UCHAR(Base + 6, Address.DeviceHead);
    if (Stack->Lba48)
    {
        /* ATA LBA48 requires high-order task-file bytes first. */
        WRITE_PORT_UCHAR(Base + 1, Address.HighFeatures);
        WRITE_PORT_UCHAR(Base + 2, Address.HighCount);
        WRITE_PORT_UCHAR(Base + 3, Address.HighLbaLow);
        WRITE_PORT_UCHAR(Base + 4, Address.HighLbaMid);
        WRITE_PORT_UCHAR(Base + 5, Address.HighLbaHigh);
    }
    WRITE_PORT_UCHAR(Base + 1, Address.LowFeatures);
    WRITE_PORT_UCHAR(Base + 2, Address.LowCount);
    WRITE_PORT_UCHAR(Base + 3, Address.LowLbaLow);
    WRITE_PORT_UCHAR(Base + 4, Address.LowLbaMid);
    WRITE_PORT_UCHAR(Base + 5, Address.LowLbaHigh);
}

static
NTSTATUS
PopAtaReadSectors(
    _In_ PPO_DUMP_STACK Stack,
    _In_ ULONGLONG Lba,
    _In_ ULONG Count,
    _Out_writes_bytes_(Count * PO_DUMP_SECTOR_SIZE) PUCHAR Buffer)
{
    NTSTATUS Status;
    ULONG Sector, Word;

    Status = PopAtaCheckRange(Stack, Lba, Count);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = PopAtaSelect(Stack, PO_DUMP_IDLE_TIMEOUT_US);
    if (!NT_SUCCESS(Status))
        return Status;

    PopAtaLoadAddress(Stack, Lba, Count);
    WRITE_PORT_UCHAR(Stack->CommandBase + 7,
                     Stack->Lba48 ? ATA_CMD_READ_EXT : ATA_CMD_READ);
    PopAtaDelay(Stack);

    for (Sector = 0; Sector < Count; Sector++)
    {
        Status = PopAtaPoll(Stack, PO_DUMP_CMD_TIMEOUT_US, TRUE, TRUE);
        if (!NT_SUCCESS(Status))
        {
            (VOID)READ_PORT_UCHAR(Stack->CommandBase + 7);
            return Status;
        }

        for (Word = 0; Word < PO_DUMP_SECTOR_SIZE / sizeof(USHORT); Word++)
        {
            USHORT Data = READ_PORT_USHORT((PUSHORT)Stack->CommandBase);
            Buffer[0] = (UCHAR)Data;
            Buffer[1] = (UCHAR)(Data >> 8);
            Buffer += sizeof(USHORT);
        }
    }

    /* Reading the status register acknowledges the device interrupt */
    (VOID)READ_PORT_UCHAR(Stack->CommandBase + 7);
    return STATUS_SUCCESS;
}

static
NTSTATUS
PopAtaWriteSectors(
    _In_ PPO_DUMP_STACK Stack,
    _In_ ULONGLONG Lba,
    _In_ ULONG Count,
    _In_reads_bytes_(Count * PO_DUMP_SECTOR_SIZE) const UCHAR *Buffer)
{
    NTSTATUS Status;
    ULONG Sector, Word;

    Status = PopAtaCheckRange(Stack, Lba, Count);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = PopAtaSelect(Stack, PO_DUMP_IDLE_TIMEOUT_US);
    if (!NT_SUCCESS(Status))
        return Status;

    PopAtaLoadAddress(Stack, Lba, Count);
    WRITE_PORT_UCHAR(Stack->CommandBase + 7,
                     Stack->Lba48 ? ATA_CMD_WRITE_EXT : ATA_CMD_WRITE);
    PopAtaDelay(Stack);

    for (Sector = 0; Sector < Count; Sector++)
    {
        Status = PopAtaPoll(Stack, PO_DUMP_CMD_TIMEOUT_US, TRUE, TRUE);
        if (!NT_SUCCESS(Status))
        {
            (VOID)READ_PORT_UCHAR(Stack->CommandBase + 7);
            return Status;
        }

        for (Word = 0; Word < PO_DUMP_SECTOR_SIZE / sizeof(USHORT); Word++)
        {
            USHORT Data = (USHORT)(Buffer[0] | (Buffer[1] << 8));
            WRITE_PORT_USHORT((PUSHORT)Stack->CommandBase, Data);
            Buffer += sizeof(USHORT);
        }
    }

    /* Wait for the device to finish writing the last sector */
    Status = PopAtaPoll(Stack, PO_DUMP_CMD_TIMEOUT_US, FALSE, TRUE);
    (VOID)READ_PORT_UCHAR(Stack->CommandBase + 7);
    return Status;
}

static
NTSTATUS
PopAtaFlushCache(
    _In_ PPO_DUMP_STACK Stack)
{
    NTSTATUS Status;

    Status = PopAtaSelect(Stack, PO_DUMP_IDLE_TIMEOUT_US);
    if (!NT_SUCCESS(Status))
        return Status;

    /*
     * FLUSH CACHE and FLUSH CACHE EXT are independent optional features.
     * Prefer the extended command, but never issue an unsupported command.
     */
    if (!Stack->FlushCacheExt && !Stack->FlushCache)
        return STATUS_NOT_SUPPORTED;

    WRITE_PORT_UCHAR(Stack->CommandBase + 7,
                     Stack->FlushCacheExt ? ATA_CMD_FLUSH_EXT : ATA_CMD_FLUSH);
    PopAtaDelay(Stack);

    Status = PopAtaPoll(Stack, PO_DUMP_FLUSH_TIMEOUT_US, FALSE, TRUE);
    (VOID)READ_PORT_UCHAR(Stack->CommandBase + 7);
    return Status;
}

/* Look for an ATA disk at the given legacy channel/device */
static
NTSTATUS
PopAtaIdentify(
    _Inout_ PPO_DUMP_STACK Stack)
{
    USHORT Id[256];
    UCHAR Status;
    ULONG i;

    WRITE_PORT_UCHAR(Stack->CommandBase + 6, (UCHAR)(0xA0 | (Stack->Device << 4)));
    PopAtaDelay(Stack);
    Status = PopAtaAltStatus(Stack);
    if (Status == 0xFF || Status == 0x00)
        return STATUS_NO_SUCH_DEVICE;   /* floating bus: nothing there */

    if (!NT_SUCCESS(PopAtaSelect(Stack, PO_DUMP_PROBE_TIMEOUT_US)))
        return STATUS_NO_SUCH_DEVICE;

    WRITE_PORT_UCHAR(Stack->CommandBase + 2, 0);
    WRITE_PORT_UCHAR(Stack->CommandBase + 3, 0);
    WRITE_PORT_UCHAR(Stack->CommandBase + 4, 0);
    WRITE_PORT_UCHAR(Stack->CommandBase + 5, 0);
    WRITE_PORT_UCHAR(Stack->CommandBase + 7, ATA_CMD_IDENTIFY);
    PopAtaDelay(Stack);

    if (!NT_SUCCESS(PopAtaPoll(Stack, PO_DUMP_PROBE_TIMEOUT_US, TRUE, TRUE)))
    {
        (VOID)READ_PORT_UCHAR(Stack->CommandBase + 7);
        return STATUS_NO_SUCH_DEVICE;   /* ATAPI or not a disk */
    }

    for (i = 0; i < 256; i++)
        Id[i] = READ_PORT_USHORT((PUSHORT)Stack->CommandBase);
    (VOID)READ_PORT_UCHAR(Stack->CommandBase + 7);

    if (Id[0] & 0x8000)
        return STATUS_NO_SUCH_DEVICE;   /* not an ATA device */

    /* Logical sectors that are not 512 bytes are not supported */
    if ((Id[106] & 0xC000) == 0x4000 && (Id[106] & 0x1000))
    {
        if (((ULONG)Id[117] | ((ULONG)Id[118] << 16)) != 256)
            return STATUS_NOT_SUPPORTED;
    }

    {
        PO_DUMP_ATA_INFO AtaInfo;

        if (!PopDumpAtaParseIdentify(Id, &AtaInfo))
            return STATUS_NO_SUCH_DEVICE;

        Stack->Lba48 = AtaInfo.Lba48;
        Stack->FlushCache = AtaInfo.FlushCache;
        Stack->FlushCacheExt = AtaInfo.FlushCacheExt;
        Stack->TotalSectors = AtaInfo.TotalSectors;
        if (!Stack->FlushCache && !Stack->FlushCacheExt)
            return STATUS_NOT_SUPPORTED;
    }

    return Stack->TotalSectors != 0 ? STATUS_SUCCESS : STATUS_NO_SUCH_DEVICE;
}

/* Legacy IDE detection *******************************************************/

#define PO_DUMP_IDE_PRIMARY     0x1
#define PO_DUMP_IDE_SECONDARY   0x2
#define PO_DUMP_PCI_MAX_BUS     16

/*
 * Channels of an IDE controller that are in compatibility mode (they use the
 * legacy ports). ProgIf bit 0 and 2 say that the primary/secondary channel
 * is in native PCI mode.
 */
static
ULONG
PopDumpCompatChannels(
    _In_ UCHAR ProgIf)
{
    ULONG Mask = 0;

    if (!(ProgIf & 0x01))
        Mask |= PO_DUMP_IDE_PRIMARY;
    if (!(ProgIf & 0x04))
        Mask |= PO_DUMP_IDE_SECONDARY;
    return Mask;
}

/*
 * Legacy channels that exist: those of a PCI IDE controller (class 01h,
 * subclass 01h) that decodes I/O space and is in compatibility mode. On a
 * machine without any PCI device both channels are assumed to be present.
 */
static
ULONG
PopDumpLegacyIdeChannels(VOID)
{
    PCI_COMMON_HEADER Header;
    PCI_SLOT_NUMBER Slot;
    ULONG Bus, Device, Function, Functions, Bytes;
    ULONG Mask = 0;
    BOOLEAN PciPresent = FALSE;

    for (Bus = 0; Bus < PO_DUMP_PCI_MAX_BUS; Bus++)
    {
        for (Device = 0; Device < PCI_MAX_DEVICES; Device++)
        {
            Functions = 1;
            for (Function = 0; Function < Functions; Function++)
            {
                Slot.u.AsULONG = 0;
                Slot.u.bits.DeviceNumber = Device;
                Slot.u.bits.FunctionNumber = Function;

                Bytes = HalGetBusDataByOffset(PCIConfiguration,
                                              Bus,
                                              Slot.u.AsULONG,
                                              &Header,
                                              0,
                                              sizeof(Header));
                if (Bytes < FIELD_OFFSET(PCI_COMMON_HEADER, LatencyTimer) ||
                    Header.VendorID == PCI_INVALID_VENDORID)
                {
                    /* Nothing here; the other functions need function 0 */
                    break;
                }

                PciPresent = TRUE;
                if (Function == 0 && (Header.HeaderType & PCI_MULTIFUNCTION))
                    Functions = PCI_MAX_FUNCTION;

                if (Header.BaseClass == PCI_CLASS_MASS_STORAGE_CTLR &&
                    Header.SubClass == PCI_SUBCLASS_MSC_IDE_CTLR &&
                    (Header.Command & PCI_ENABLE_IO_SPACE))
                {
                    Mask |= PopDumpCompatChannels(Header.ProgIf);
                }
            }
        }
    }

    return PciPresent ? Mask : (PO_DUMP_IDE_PRIMARY | PO_DUMP_IDE_SECONDARY);
}

/* File to disk mapping *******************************************************/

static
PPO_DUMP_EXTENT
PopDumpFindExtent(
    _In_ PPO_DUMP_STACK Stack,
    _In_ ULONGLONG FileSector)
{
    ULONG Low, High, Mid;
    PPO_DUMP_EXTENT Extent;

    if (Stack == NULL || Stack->Extents == NULL ||
        Stack->ExtentCount == 0 || Stack->LastExtent >= Stack->ExtentCount)
    {
        return NULL;
    }

    Low = 0;
    High = Stack->ExtentCount;

    /* Writes are mostly sequential */
    Extent = &Stack->Extents[Stack->LastExtent];
    if (FileSector >= Extent->FileSector &&
        FileSector - Extent->FileSector < Extent->Count)
    {
        return Extent;
    }

    while (Low < High)
    {
        Mid = Low + (High - Low) / 2;
        Extent = &Stack->Extents[Mid];
        if (FileSector < Extent->FileSector)
        {
            High = Mid;
        }
        else if (FileSector - Extent->FileSector >= Extent->Count)
        {
            Low = Mid + 1;
        }
        else
        {
            Stack->LastExtent = Mid;
            return Extent;
        }
    }

    return NULL;
}

static
NTSTATUS
PopDumpFileToDisk(
    _In_ PPO_DUMP_STACK Stack,
    _In_ ULONGLONG FileSector,
    _Out_ PULONGLONG DiskSector,
    _Out_ PULONG Contiguous)
{
    PPO_DUMP_EXTENT Extent;
    ULONGLONG Offset, Left;

    if (Stack == NULL || DiskSector == NULL || Contiguous == NULL ||
        Stack->Extents == NULL || Stack->ExtentCount == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Extent = PopDumpFindExtent(Stack, FileSector);
    if (Extent == NULL || FileSector >= Stack->FileSectors)
        return STATUS_DISK_FULL;

    Offset = FileSector - Extent->FileSector;
    Left = Extent->Count - Offset;
    if (Left > Stack->FileSectors - FileSector)
        Left = Stack->FileSectors - FileSector;

    *DiskSector = Extent->DiskSector + Offset;
    *Contiguous = Left > PO_DUMP_MAX_RUN ? PO_DUMP_MAX_RUN : (ULONG)Left;
    return STATUS_SUCCESS;
}

/* Writing ********************************************************************/

static
NTSTATUS
PopDumpFlushCacheSector(
    _Inout_ PPO_DUMP_STACK Stack)
{
    ULONGLONG DiskSector;
    ULONG Run;
    NTSTATUS Status;

    if (!Stack->CacheValid || !Stack->CacheDirty)
        return STATUS_SUCCESS;

    Status = PopDumpFileToDisk(Stack, Stack->CacheSector, &DiskSector, &Run);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = PopAtaWriteSectors(Stack, DiskSector, 1, Stack->Cache);
    if (NT_SUCCESS(Status))
        Stack->CacheDirty = FALSE;
    return Status;
}

/*
 * Write Length bytes at the byte Offset of the hibernation file.
 * Callable at any IRQL with interrupts disabled; does not allocate or wait.
 */
static
NTSTATUS
PopDumpStackWrite(
    _Inout_ PPO_DUMP_STACK Stack,
    _In_ ULONGLONG Offset,
    _In_reads_bytes_(Length) const VOID *Buffer,
    _In_ ULONG Length)
{
    const UCHAR *Data = Buffer;
    ULONGLONG Sector, DiskSector;
    ULONG InSector, Chunk, Run;
    NTSTATUS Status;

    if (Stack == NULL || Stack->Signature != PO_DUMP_SIGNATURE ||
        (Length != 0 && Buffer == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (Stack->FileSectors > (~0ULL / PO_DUMP_SECTOR_SIZE) ||
        Offset + Length < Offset ||
        Offset + Length > Stack->FileSectors * PO_DUMP_SECTOR_SIZE)
    {
        return STATUS_DISK_FULL;
    }

    while (Length != 0)
    {
        Sector = Offset / PO_DUMP_SECTOR_SIZE;
        InSector = (ULONG)(Offset % PO_DUMP_SECTOR_SIZE);

        if (InSector == 0 && Length >= PO_DUMP_SECTOR_SIZE)
        {
            /* Whole sectors go straight to the disk */
            Status = PopDumpFileToDisk(Stack, Sector, &DiskSector, &Run);
            if (!NT_SUCCESS(Status))
                return Status;
            if (Run > Length / PO_DUMP_SECTOR_SIZE)
                Run = Length / PO_DUMP_SECTOR_SIZE;

            /* The data being written replaces a cached sector */
            if (Stack->CacheValid &&
                Stack->CacheSector >= Sector &&
                Stack->CacheSector < Sector + Run)
            {
                Stack->CacheValid = FALSE;
                Stack->CacheDirty = FALSE;
            }

            Status = PopAtaWriteSectors(Stack, DiskSector, Run, Data);
            if (!NT_SUCCESS(Status))
                return Status;

            if (Sector + Run > Stack->HighWater)
                Stack->HighWater = Sector + Run;

            Chunk = Run * PO_DUMP_SECTOR_SIZE;
        }
        else
        {
            /* A part of a sector: merge it in the cached sector */
            Chunk = PO_DUMP_SECTOR_SIZE - InSector;
            if (Chunk > Length)
                Chunk = Length;

            if (!Stack->CacheValid || Stack->CacheSector != Sector)
            {
                Status = PopDumpFlushCacheSector(Stack);
                if (!NT_SUCCESS(Status))
                    return Status;

                Stack->CacheValid = FALSE;
                if (Sector < Stack->HighWater)
                {
                    /* Written before: keep what is there */
                    Status = PopDumpFileToDisk(Stack, Sector, &DiskSector, &Run);
                    if (!NT_SUCCESS(Status))
                        return Status;
                    Status = PopAtaReadSectors(Stack, DiskSector, 1, Stack->Cache);
                    if (!NT_SUCCESS(Status))
                        return Status;
                }
                else
                {
                    RtlZeroMemory(Stack->Cache, sizeof(Stack->Cache));
                    Stack->HighWater = Sector + 1;
                }

                Stack->CacheSector = Sector;
                Stack->CacheValid = TRUE;
                Stack->CacheDirty = FALSE;
            }

            RtlCopyMemory(Stack->Cache + InSector, Data, Chunk);
            Stack->CacheDirty = TRUE;
        }

        Data += Chunk;
        Offset += Chunk;
        Length -= Chunk;
    }

    return STATUS_SUCCESS;
}

/* Write what is still cached and make the disk write it out */
static
NTSTATUS
PopDumpStackFlush(
    _Inout_ PPO_DUMP_STACK Stack)
{
    NTSTATUS Status;

    if (Stack == NULL || Stack->Signature != PO_DUMP_SIGNATURE)
        return STATUS_INVALID_PARAMETER;

    Status = PopDumpFlushCacheSector(Stack);
    if (!NT_SUCCESS(Status))
        return Status;

    return PopAtaFlushCache(Stack);
}

/* Preparation (PASSIVE_LEVEL) ************************************************/

static
NTSTATUS
PopDumpIoctl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG IoControlCode,
    _Out_writes_bytes_(OutputLength) PVOID Output,
    _In_ ULONG OutputLength)
{
    KEVENT Event;
    IO_STATUS_BLOCK IoStatusBlock;
    PIRP Irp;
    NTSTATUS Status;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoStatusBlock.Status = STATUS_UNSUCCESSFUL;
    IoStatusBlock.Information = 0;
    Irp = IoBuildDeviceIoControlRequest(IoControlCode,
                                        DeviceObject,
                                        NULL,
                                        0,
                                        Output,
                                        OutputLength,
                                        FALSE,
                                        &Event,
                                        &IoStatusBlock);
    if (Irp == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = IoCallDriver(DeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatusBlock.Status;
    }
    else if (NT_SUCCESS(Status))
    {
        Status = IoStatusBlock.Status;
    }

    return Status;
}

static
BOOLEAN
PopDumpStackEnabled(VOID)
{
    RTL_QUERY_REGISTRY_TABLE QueryTable[2];
    ULONG Enabled = 1;

    RtlZeroMemory(QueryTable, sizeof(QueryTable));
    QueryTable[0].Name = L"HiberDumpStack";
    QueryTable[0].EntryContext = &Enabled;
    QueryTable[0].DefaultType = REG_DWORD;
    QueryTable[0].DefaultData = &Enabled;
    QueryTable[0].DefaultLength = sizeof(Enabled);

    RtlQueryRegistryValues(RTL_REGISTRY_CONTROL,
                           L"Session Manager\\Power",
                           QueryTable,
                           NULL,
                           NULL);
    return Enabled != 0;
}

static
NTSTATUS
PopDumpAddExtent(
    _Inout_ PPO_DUMP_STACK Stack,
    _In_ ULONGLONG FileSector,
    _In_ ULONGLONG DiskSector,
    _In_ ULONGLONG Count)
{
    if (Stack == NULL || Stack->Extents == NULL ||
        Stack->ExtentCapacity == 0 ||
        Stack->ExtentCount > Stack->ExtentCapacity ||
        Count == 0 ||
        FileSector > (~0ULL - Count) ||
        DiskSector > (~0ULL - Count))
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    /*
     * Retrieval-pointer extents describe one contiguous file stream even
     * when their disk clusters are fragmented. Reject gaps, overlaps and
     * wrapped ranges before the dump stack can translate an image offset.
     */
    if (Stack->ExtentCount != 0)
    {
        PPO_DUMP_EXTENT Previous = &Stack->Extents[Stack->ExtentCount - 1];

        if (Previous->FileSector > (~0ULL - Previous->Count) ||
            FileSector != Previous->FileSector + Previous->Count)
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }
    }

    if (Stack->ExtentCount == Stack->ExtentCapacity)
    {
        PPO_DUMP_EXTENT NewExtents;
        ULONG NewCapacity;

        if (Stack->ExtentCapacity >= PO_DUMP_MAX_EXTENTS)
            return STATUS_BUFFER_OVERFLOW;

        NewCapacity = Stack->ExtentCapacity * 2;
        if (NewCapacity > PO_DUMP_MAX_EXTENTS)
            NewCapacity = PO_DUMP_MAX_EXTENTS;

        NewExtents = ExAllocatePoolWithTag(NonPagedPool,
                                           NewCapacity * sizeof(*NewExtents),
                                           PO_DUMP_TAG);
        if (NewExtents == NULL)
            return STATUS_INSUFFICIENT_RESOURCES;

        RtlCopyMemory(NewExtents,
                      Stack->Extents,
                      Stack->ExtentCount * sizeof(*NewExtents));
        ExFreePoolWithTag(Stack->Extents, PO_DUMP_TAG);
        Stack->Extents = NewExtents;
        Stack->ExtentCapacity = NewCapacity;
    }

    Stack->Extents[Stack->ExtentCount].FileSector = FileSector;
    Stack->Extents[Stack->ExtentCount].DiskSector = DiskSector;
    Stack->Extents[Stack->ExtentCount].Count = Count;
    Stack->ExtentCount++;
    return STATUS_SUCCESS;
}

static
NTSTATUS
PopDumpQueryExtents(
    _Inout_ PPO_DUMP_STACK Stack,
    _In_ HANDLE FileHandle,
    _In_ ULONGLONG PartitionStartSector,
    _In_ ULONG SectorsPerCluster,
    _In_ ULONGLONG EndOfFile)
{
    PRETRIEVAL_POINTERS_BUFFER Pointers;
    STARTING_VCN_INPUT_BUFFER Input;
    IO_STATUS_BLOCK IoStatusBlock;
    ULONG BufferSize, i;
    ULONGLONG Vcn = 0, Next, Lcn, Length;
    ULONGLONG Sectors = 0, RequiredSectors;
    NTSTATUS Status, QueryStatus;

    if (SectorsPerCluster == 0)
        return STATUS_FILE_CORRUPT_ERROR;

    BufferSize = FIELD_OFFSET(RETRIEVAL_POINTERS_BUFFER, Extents) +
                 PO_DUMP_RP_EXTENTS * sizeof(Pointers->Extents[0]);
    Pointers = ExAllocatePoolWithTag(PagedPool, BufferSize, PO_DUMP_TAG);
    if (Pointers == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    for (;;)
    {
        Input.StartingVcn.QuadPart = (LONGLONG)Vcn;
        Status = ZwFsControlFile(FileHandle,
                                 NULL,
                                 NULL,
                                 NULL,
                                 &IoStatusBlock,
                                 FSCTL_GET_RETRIEVAL_POINTERS,
                                 &Input,
                                 sizeof(Input),
                                 Pointers,
                                 BufferSize);
        QueryStatus = Status;
        if (QueryStatus != STATUS_SUCCESS &&
            QueryStatus != STATUS_BUFFER_OVERFLOW)
        {
            break;
        }
        if (IoStatusBlock.Information <
                FIELD_OFFSET(RETRIEVAL_POINTERS_BUFFER, Extents) ||
            IoStatusBlock.Information > BufferSize ||
            Pointers->ExtentCount == 0 ||
            Pointers->ExtentCount > PO_DUMP_RP_EXTENTS ||
            Pointers->ExtentCount >
                (IoStatusBlock.Information -
                 FIELD_OFFSET(RETRIEVAL_POINTERS_BUFFER, Extents)) /
                    sizeof(Pointers->Extents[0]) ||
            Pointers->StartingVcn.QuadPart < 0 ||
            (ULONGLONG)Pointers->StartingVcn.QuadPart != Vcn)
        {
            Status = STATUS_UNSUCCESSFUL;
            break;
        }

        for (i = 0; i < Pointers->ExtentCount; i++)
        {
            /*
             * Retrieval-pointer LCNs are signed: -1 denotes a sparse range.
             * Reject every negative value, not just -1, before converting it
             * to an unsigned disk address.  Check all cluster-to-sector and
             * partition-offset arithmetic before adding an extent; corrupt
             * filesystem metadata must never wrap into a different disk area.
             */
            if (Pointers->Extents[i].Lcn.QuadPart < 0 ||
                Pointers->Extents[i].NextVcn.QuadPart < 0)
            {
                Status = STATUS_NOT_SUPPORTED;      /* sparse or invalid */
                goto Done;
            }

            Next = (ULONGLONG)Pointers->Extents[i].NextVcn.QuadPart;
            Lcn = (ULONGLONG)Pointers->Extents[i].Lcn.QuadPart;
            if (Next <= Vcn ||
                Next > (~0ULL / SectorsPerCluster) ||
                (Next - Vcn) > (~0ULL / SectorsPerCluster) ||
                Lcn > ((~0ULL - PartitionStartSector) / SectorsPerCluster))
            {
                Status = STATUS_FILE_CORRUPT_ERROR;
                goto Done;
            }

            Length = (Next - Vcn) * SectorsPerCluster;
            if (Vcn > (~0ULL / SectorsPerCluster) ||
                Sectors > (~0ULL - Length))
            {
                Status = STATUS_FILE_CORRUPT_ERROR;
                goto Done;
            }

            Status = PopDumpAddExtent(Stack,
                                      Vcn * SectorsPerCluster,
                                      PartitionStartSector + Lcn * SectorsPerCluster,
                                      Length);
            if (!NT_SUCCESS(Status))
                goto Done;

            Sectors += Length;
            Vcn = Next;
        }

        /*
         * PopDumpAddExtent updates Status, so retain the original FSCTL
         * result separately to continue when the retrieval buffer overflowed.
         */
        if (QueryStatus == STATUS_SUCCESS)
            break;
    }

Done:
    ExFreePoolWithTag(Pointers, PO_DUMP_TAG);
    if (!NT_SUCCESS(Status))
        return Status;

    /*
     * Compute the rounded-up sector count without adding sector_size - 1
     * to EndOfFile: the latter can overflow for a corrupt or hostile size.
     */
    RequiredSectors = EndOfFile / PO_DUMP_SECTOR_SIZE;
    if ((EndOfFile % PO_DUMP_SECTOR_SIZE) != 0)
        ++RequiredSectors;

    /* Every byte up to the end of the file must be on disk. */
    if (Sectors < RequiredSectors)
        return STATUS_NOT_SUPPORTED;

    Stack->FileSectors = RequiredSectors;
    return STATUS_SUCCESS;
}

static
VOID
PopDumpMakePattern(
    _Out_writes_bytes_(PO_DUMP_SECTOR_SIZE) PUCHAR Pattern,
    _In_ ULONG Index)
{
    ULONGLONG State;
    ULONG i;

    State = (ULONGLONG)KeQueryPerformanceCounter(NULL).QuadPart ^
            ((ULONGLONG)KeQueryInterruptTime() << 7) ^
            ((ULONGLONG)(Index + 1) * 0x9E3779B97F4A7C15ULL);
    if (State == 0)
        State = 0x2545F4914F6CDD1DULL;

    for (i = 0; i < PO_DUMP_SECTOR_SIZE; i++)
    {
        State ^= State << 13;
        State ^= State >> 7;
        State ^= State << 17;
        Pattern[i] = (UCHAR)(State >> 24);
    }
}

/*
 * Check the file to disk mapping and find the disk: Pattern[i] was written to
 * TestSectors[i] of the file through the file system. Called with interrupts
 * disabled.
 */
static
NTSTATUS
PopDumpFindDisk(
    _Inout_ PPO_DUMP_STACK Stack,
    _In_ const ULONGLONG *TestSectors,
    _In_ ULONG TestCount,
    _In_reads_(TestCount * PO_DUMP_SECTOR_SIZE) const UCHAR *Patterns,
    _Out_writes_bytes_(PO_DUMP_SECTOR_SIZE) PUCHAR ReadBuffer,
    _In_ ULONG ChannelMask)
{
    static const ULONG Ports[2][2] = { { 0x1F0, 0x3F6 }, { 0x170, 0x376 } };
    ULONG Channel, Device, i;
    ULONGLONG DiskSector;
    ULONG Run;
    BOOLEAN Match;

    for (Channel = 0; Channel < 2; Channel++)
    {
        /* Only ports that belong to a controller in compatibility mode */
        if (!(ChannelMask & (1UL << Channel)))
            continue;

        for (Device = 0; Device < 2; Device++)
        {
            Stack->CommandBase = (PUCHAR)(ULONG_PTR)Ports[Channel][0];
            Stack->ControlBase = (PUCHAR)(ULONG_PTR)Ports[Channel][1];
            Stack->Device = Device;

            if (!NT_SUCCESS(PopAtaIdentify(Stack)))
                continue;

            Match = TRUE;
            for (i = 0; i < TestCount && Match; i++)
            {
                if (!NT_SUCCESS(PopDumpFileToDisk(Stack,
                                                  TestSectors[i],
                                                  &DiskSector,
                                                  &Run)) ||
                    !NT_SUCCESS(PopAtaReadSectors(Stack,
                                                  DiskSector,
                                                  1,
                                                  ReadBuffer)) ||
                    RtlCompareMemory(ReadBuffer,
                                     Patterns + i * PO_DUMP_SECTOR_SIZE,
                                     PO_DUMP_SECTOR_SIZE) != PO_DUMP_SECTOR_SIZE)
                {
                    Match = FALSE;
                }
            }

            if (Match)
                return STATUS_SUCCESS;
        }
    }

    return STATUS_NO_SUCH_DEVICE;
}

static
VOID
PopDumpStackFree(
    _Inout_ PPOP_HIBER_CONTEXT Context)
{
    PPO_DUMP_STACK Stack = Context->DumpStack;

    Context->DumpStackActive = FALSE;
    Context->DumpStack = NULL;
    if (Stack == NULL)
        return;

    if (Stack->Extents != NULL)
        ExFreePoolWithTag(Stack->Extents, PO_DUMP_TAG);
    Stack->Signature = 0;
    ExFreePoolWithTag(Stack, PO_DUMP_TAG);
}

/*
 * Prepare the dump stack for the open hibernation file. PASSIVE_LEVEL.
 * On failure the dump stack is left unset and the image is written the
 * usual way.
 */
static
NTSTATUS
PopDumpStackInitialize(
    _Inout_ PPOP_HIBER_CONTEXT Context)
{
    PPO_DUMP_STACK Stack = NULL;
    PFILE_OBJECT FileObject = NULL;
    PDEVICE_OBJECT PartitionDevice = NULL;
    FILE_FS_SIZE_INFORMATION SizeInfo;
    FILE_STANDARD_INFORMATION StandardInfo;
    FILE_ALLOCATION_INFORMATION AllocationInfo;
    PARTITION_INFORMATION_EX PartitionInfo;
    IO_STATUS_BLOCK IoStatusBlock;
    ULONGLONG TestSectors[PO_DUMP_TEST_SECTORS];
    PUCHAR Patterns = NULL;
    ULONG TestCount = 0, i;
    ULONG ClusterBytes;
    ULONG ChannelMask;
    ULONGLONG EndOfFile, LastSector;
    KIRQL OldIrql;
    BOOLEAN Interrupts;
    NTSTATUS Status;

    PAGED_CODE();
    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

    PopDumpStackFree(Context);

    if (Context->LinkFileHandle == NULL || !PopDumpStackEnabled())
        return STATUS_NOT_SUPPORTED;

    /*
     * Legacy IDE only. Without a channel on the legacy ports the dump stack
     * can't be used, so leave the disk alone and keep the normal path.
     */
    ChannelMask = PopDumpLegacyIdeChannels();
    if (ChannelMask == 0)
    {
        DPRINT1("Hibernation dump stack not used: no legacy IDE channel\n");
        return STATUS_NOT_SUPPORTED;
    }

    Status = ObReferenceObjectByHandle(Context->LinkFileHandle,
                                       FILE_READ_DATA,
                                       IoFileObjectType,
                                       KernelMode,
                                       (PVOID *)&FileObject,
                                       NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    if (FileObject->Vpb == NULL || FileObject->Vpb->RealDevice == NULL)
    {
        Status = STATUS_NOT_SUPPORTED;
        goto Quit;
    }

    PartitionDevice = IoGetAttachedDeviceReference(FileObject->Vpb->RealDevice);

    Status = ZwQueryVolumeInformationFile(Context->LinkFileHandle,
                                          &IoStatusBlock,
                                          &SizeInfo,
                                          sizeof(SizeInfo),
                                          FileFsSizeInformation);
    if (!NT_SUCCESS(Status))
        goto Quit;
    if (SizeInfo.BytesPerSector != PO_DUMP_SECTOR_SIZE ||
        SizeInfo.SectorsPerAllocationUnit == 0 ||
        SizeInfo.SectorsPerAllocationUnit >
            MAXULONG / SizeInfo.BytesPerSector)
    {
        Status = STATUS_NOT_SUPPORTED;
        goto Quit;
    }
    ClusterBytes = SizeInfo.BytesPerSector * SizeInfo.SectorsPerAllocationUnit;

    Status = ZwQueryInformationFile(Context->LinkFileHandle,
                                    &IoStatusBlock,
                                    &StandardInfo,
                                    sizeof(StandardInfo),
                                    FileStandardInformation);
    if (!NT_SUCCESS(Status))
        goto Quit;
    EndOfFile = (ULONGLONG)StandardInfo.EndOfFile.QuadPart;
    if (EndOfFile == 0 || EndOfFile > 0x7FFFFFFFFFFFFFFFULL - ClusterBytes)
    {
        Status = STATUS_NOT_SUPPORTED;
        goto Quit;
    }

    /* All clusters must exist on disk, the file system may allocate lazily */
    AllocationInfo.AllocationSize.QuadPart =
        (LONGLONG)(((EndOfFile + ClusterBytes - 1) / ClusterBytes) * ClusterBytes);
    (VOID)ZwSetInformationFile(Context->LinkFileHandle,
                               &IoStatusBlock,
                               &AllocationInfo,
                               sizeof(AllocationInfo),
                               FileAllocationInformation);

    Status = PopDumpIoctl(PartitionDevice,
                          IOCTL_DISK_GET_PARTITION_INFO_EX,
                          &PartitionInfo,
                          sizeof(PartitionInfo));
    if (!NT_SUCCESS(Status))
        goto Quit;
    if (PartitionInfo.StartingOffset.QuadPart < 0 ||
        (PartitionInfo.StartingOffset.QuadPart % PO_DUMP_SECTOR_SIZE) != 0)
    {
        Status = STATUS_NOT_SUPPORTED;
        goto Quit;
    }

    Stack = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Stack), PO_DUMP_TAG);
    if (Stack == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Quit;
    }
    RtlZeroMemory(Stack, sizeof(*Stack));
    Stack->Signature = PO_DUMP_SIGNATURE;
    Stack->ExtentCapacity = 16;
    Stack->Extents = ExAllocatePoolWithTag(NonPagedPool,
                                           Stack->ExtentCapacity *
                                               sizeof(*Stack->Extents),
                                           PO_DUMP_TAG);
    if (Stack->Extents == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Quit;
    }

    /* Make sure that nothing of the file is waiting in the cache */
    Status = ZwFlushBuffersFile(Context->LinkFileHandle, &IoStatusBlock);
    if (!NT_SUCCESS(Status))
        goto Quit;

    Status = PopDumpQueryExtents(Stack,
                                 Context->LinkFileHandle,
                                 (ULONGLONG)PartitionInfo.StartingOffset.QuadPart /
                                     PO_DUMP_SECTOR_SIZE,
                                 SizeInfo.SectorsPerAllocationUnit,
                                 EndOfFile);
    if (!NT_SUCCESS(Status))
        goto Quit;

    /* Test sectors: the start of the first extents and the last full sector */
    LastSector = EndOfFile / PO_DUMP_SECTOR_SIZE;   /* full sectors */
    if (LastSector == 0)
    {
        Status = STATUS_NOT_SUPPORTED;
        goto Quit;
    }
    for (i = 0; i < Stack->ExtentCount && TestCount < PO_DUMP_TEST_SECTORS - 1; i++)
    {
        if (Stack->Extents[i].FileSector < LastSector)
            TestSectors[TestCount++] = Stack->Extents[i].FileSector;
    }
    /*
     * PopDumpQueryExtents guarantees coverage of the file, but keep this
     * boundary explicit: never index the test-sector array when no extent
     * contributed a sector before the final sector.
     */
    if (TestCount == 0)
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Quit;
    }

    if (TestSectors[TestCount - 1] != LastSector - 1)
        TestSectors[TestCount++] = LastSector - 1;

    Patterns = ExAllocatePoolWithTag(NonPagedPool,
                                     (TestCount + 1) * PO_DUMP_SECTOR_SIZE,
                                     PO_DUMP_TAG);
    if (Patterns == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Quit;
    }

    for (i = 0; i < TestCount; i++)
    {
        LARGE_INTEGER FileOffset;

        PopDumpMakePattern(Patterns + i * PO_DUMP_SECTOR_SIZE, i);
        FileOffset.QuadPart = (LONGLONG)(TestSectors[i] * PO_DUMP_SECTOR_SIZE);
        Status = ZwWriteFile(Context->LinkFileHandle,
                             NULL,
                             NULL,
                             NULL,
                             &IoStatusBlock,
                             Patterns + i * PO_DUMP_SECTOR_SIZE,
                             PO_DUMP_SECTOR_SIZE,
                             &FileOffset,
                             NULL);
        if (!NT_SUCCESS(Status))
            goto Quit;
    }

    Status = ZwFlushBuffersFile(Context->LinkFileHandle, &IoStatusBlock);
    if (!NT_SUCCESS(Status))
        goto Quit;

    /* Poll the disk with nobody else touching the controller */
    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    Interrupts = KeDisableInterrupts();
    Status = PopDumpFindDisk(Stack,
                             TestSectors,
                             TestCount,
                             Patterns,
                             Patterns + TestCount * PO_DUMP_SECTOR_SIZE,
                             ChannelMask);
    KeRestoreInterrupts(Interrupts);
    KeLowerIrql(OldIrql);
    if (!NT_SUCCESS(Status))
        goto Quit;

    Context->DumpStack = Stack;
    Stack = NULL;
    Status = STATUS_SUCCESS;

Quit:
    if (Patterns != NULL)
        ExFreePoolWithTag(Patterns, PO_DUMP_TAG);
    if (Stack != NULL)
    {
        Context->DumpStack = Stack;
        PopDumpStackFree(Context);
    }
    if (PartitionDevice != NULL)
        ObDereferenceObject(PartitionDevice);
    if (FileObject != NULL)
        ObDereferenceObject(FileObject);

    if (!NT_SUCCESS(Status))
        DPRINT1("Hibernation dump stack not used: 0x%08lx\n", Status);
    return Status;
}

#else /* !(x86 || amd64) */

static NTSTATUS PopDumpStackInitialize(PPOP_HIBER_CONTEXT Context)
{
    UNREFERENCED_PARAMETER(Context);
    return STATUS_NOT_SUPPORTED;
}
static VOID PopDumpStackFree(PPOP_HIBER_CONTEXT Context)
{
    UNREFERENCED_PARAMETER(Context);
}
static NTSTATUS PopDumpStackWrite(PVOID Stack, ULONGLONG Offset, const VOID *Buffer, ULONG Length)
{
    UNREFERENCED_PARAMETER(Stack);
    UNREFERENCED_PARAMETER(Offset);
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(Length);
    return STATUS_NOT_SUPPORTED;
}
static NTSTATUS PopDumpStackFlush(PVOID Stack)
{
    UNREFERENCED_PARAMETER(Stack);
    return STATUS_NOT_SUPPORTED;
}

#endif
