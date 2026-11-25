/*
 * PROJECT:     CATV 95 kernel-mode tests
 * LICENSE:     LGPL-2.1+
 * PURPOSE:     Hibernation ATA IDENTIFY and LBA task-file regression tests
 */

#include <kmt_test.h>
#include "../../../../ntoskrnl/po/hibdump_ata.h"
#include "../../../../ntoskrnl/po/hibdump_btrfs.h"
#include "../../../../ntoskrnl/po/hibdump_btrfs_map.h"

static
VOID
TestAtaIdentify(VOID)
{
    USHORT Id[256];
    PO_DUMP_ATA_INFO Info;

    RtlZeroMemory(Id, sizeof(Id));
    Id[49] = 0x0200; /* LBA */
    Id[83] = 0x7400; /* valid + LBA48 + FLUSH CACHE + EXT */
    Id[86] = 0x3400; /* enabled LBA48 + FLUSH CACHE + EXT */
    Id[87] = 0x4000; /* enabled command-set words valid */
    Id[100] = 0x5678;
    Id[101] = 0x1234;
    Id[102] = 0x00AB;

    ok(PopDumpAtaParseIdentify(Id, &Info), "Valid LBA48 IDENTIFY rejected\n");
    ok(Info.Lba48, "LBA48 capability was not detected\n");
    ok(Info.FlushCache, "FLUSH CACHE capability was not detected\n");
    ok(Info.FlushCacheExt, "FLUSH CACHE EXT capability was not detected\n");
    ok(Info.TotalSectors == 0x00AB12345678ULL, "Unexpected LBA48 sector count: %I64u\n", Info.TotalSectors);

    /* A supported but disabled LBA48 feature must fall back to LBA28. */
    Id[86] = 0;
    Id[60] = 0xFFFF;
    Id[61] = 0x0FFF;
    ok(PopDumpAtaParseIdentify(Id, &Info), "LBA28 fallback rejected\n");
    ok(!Info.Lba48, "Disabled LBA48 was incorrectly selected\n");
    ok(!Info.FlushCacheExt, "LBA28 device selected FLUSH CACHE EXT\n");
    ok(Info.TotalSectors == 0x0FFFFFFFULL, "Unexpected LBA28 sector count: %I64u\n", Info.TotalSectors);

    /* Invalid word-83 validity bits must not advertise LBA48. */
    Id[83] = 0x0400;
    Id[86] = 0x0400;
    ok(PopDumpAtaParseIdentify(Id, &Info), "Valid LBA28 fallback rejected\n");
    ok(!Info.Lba48, "Invalid word-83 validity bits accepted\n");

    /* Word 87 validity is required before trusting enabled feature bits. */
    Id[83] = 0x4400;
    Id[86] = 0x0400;
    Id[87] = 0;
    ok(PopDumpAtaParseIdentify(Id, &Info),
       "LBA28 fallback with invalid word-87 rejected\n");
    ok(!Info.Lba48, "Invalid word-87 validity bits accepted\n");

    /* LBA48 can be active without the optional extended flush command. */
    Id[83] = 0x5400; /* valid + LBA48 + FLUSH CACHE, no EXT */
    Id[86] = 0x1400; /* enabled LBA48 + FLUSH CACHE, no EXT */
    Id[87] = 0x4000;
    Id[60] = 0xFFFF;
    Id[61] = 0x0FFF;
    ok(PopDumpAtaParseIdentify(Id, &Info),
       "LBA48 without FLUSH CACHE EXT rejected\n");
    ok(Info.Lba48, "Enabled LBA48 was not selected\n");
    ok(Info.FlushCache, "Ordinary FLUSH CACHE capability was not detected\n");
    ok(!Info.FlushCacheExt, "Unsupported FLUSH CACHE EXT selected\n");

    /* The full LBA28 address space contains exactly 2^28 sectors. */
    Id[60] = 0;
    Id[61] = 0x1000;
    ok(PopDumpAtaParseIdentify(Id, &Info), "Maximum LBA28 capacity rejected\n");
    ok(Info.TotalSectors == PO_DUMP_ATA_LBA28_MAX_SECTORS, "Maximum LBA28 capacity mismatch\n");

    Id[61] = 0x1001;
    ok(!PopDumpAtaParseIdentify(Id, &Info),
       "Out-of-range LBA28 capacity accepted\n");

    /* Reserved upper count word is rejected for an enabled LBA48 device. */
    Id[83] = 0x4400;
    Id[86] = 0x0400;
    Id[87] = 0x4000;
    Id[103] = 1;
    ok(!PopDumpAtaParseIdentify(Id, &Info),
       "Nonzero reserved LBA48 capacity word accepted\n");

    /* A device without LBA support cannot be used by this writer. */
    RtlZeroMemory(Id, sizeof(Id));
    ok(!PopDumpAtaParseIdentify(Id, &Info),
       "Device without LBA support accepted\n");
}

static
VOID
TestAtaAddress(VOID)
{
    PO_DUMP_ATA_ADDRESS Address;
    ULONGLONG Lba = 0x123456789ABCULL;

    ok(PopDumpAtaBuildAddress(TRUE, 1, Lba, 0x5A, &Address),
       "Valid LBA48 task file rejected\n");
    ok_eq_uint(0x50, Address.DeviceHead);
    ok_eq_uint(0x00, Address.HighFeatures);
    ok_eq_uint(0x00, Address.LowFeatures);
    ok_eq_uint(0x00, Address.HighCount);
    ok_eq_uint(0x12, Address.HighLbaLow);
    ok_eq_uint(0x34, Address.HighLbaMid);
    ok_eq_uint(0x56, Address.HighLbaHigh);
    ok_eq_uint(0x5A, Address.LowCount);
    ok_eq_uint(0xBC, Address.LowLbaLow);
    ok_eq_uint(0x9A, Address.LowLbaMid);
    ok_eq_uint(0x78, Address.LowLbaHigh);

    ok(PopDumpAtaBuildAddress(TRUE, 0,
                              PO_DUMP_ATA_LBA48_MAX_SECTORS - 1,
                              1, &Address),
       "Final LBA48 sector rejected\n");
    ok(!PopDumpAtaBuildAddress(TRUE, 0,
                               PO_DUMP_ATA_LBA48_MAX_SECTORS - 1,
                               2, &Address),
       "LBA48 request crossing the address limit accepted\n");
    ok(!PopDumpAtaBuildAddress(TRUE, 0, 0, 0, &Address),
       "Zero-sector LBA48 request accepted\n");
    ok(PopDumpAtaBuildAddress(TRUE, 0, 0x10000, 0x10000, &Address),
       "Maximum LBA48 sector count rejected\n");
    ok_eq_uint(0, Address.HighCount);
    ok_eq_uint(0, Address.LowCount); /* ATA encodes 65536 sectors as zero. */

    ok(PopDumpAtaBuildAddress(FALSE, 1, 0x0FFFFFFFULL, 1, &Address),
       "Final LBA28 sector rejected\n");
    ok_eq_uint(0xFF, Address.DeviceHead);
    ok(!PopDumpAtaBuildAddress(FALSE, 0, 0x0FFFFFFFULL, 2, &Address),
       "LBA28 request crossing the address limit accepted\n");
    ok(PopDumpAtaBuildAddress(FALSE, 0, 0x12345678, 256, &Address),
       "Maximum LBA28 sector count rejected\n");
    ok_eq_uint(0, Address.LowCount); /* ATA encodes 256 sectors as zero. */
    ok(!PopDumpAtaBuildAddress(FALSE, 0, 0, 257, &Address),
       "Oversized LBA28 sector count accepted\n");
    ok(!PopDumpAtaBuildAddress(TRUE, 2, 0, 1, &Address),
       "Invalid ATA device number accepted\n");
}

static
VOID
TestBtrfsDeviceList(VOID)
{
    union
    {
        ULONGLONG Alignment;
        UCHAR Buffer[512];
    } Storage;
    PO_BTRFS_DEVICE_ENTRY Entry;
    ULONG MemberCount;
    ULONG FixedEntrySize =
        (ULONG)(sizeof(PO_BTRFS_DEVICE_ENTRY) - sizeof(WCHAR));
    NTSTATUS Status;

    RtlZeroMemory(&Storage, sizeof(Storage));
    RtlZeroMemory(&Entry, sizeof(Entry));
    Entry.DeviceId = 1;
    Entry.Size = 1024 * 1024;
    Entry.MaxSize = 2 * 1024 * 1024;
    RtlCopyMemory(Storage.Buffer,
                  &Entry,
                  FIELD_OFFSET(PO_BTRFS_DEVICE_ENTRY, Name));

    MemberCount = 99;
    Status = PopDumpBtrfsValidateDeviceList(Storage.Buffer,
                                            sizeof(Storage.Buffer),
                                            &MemberCount);
    ok(Status == STATUS_SUCCESS, "Single Btrfs member rejected: 0x%08lx\n", Status);
    ok(MemberCount == 1, "Expected one member, got %lu\n", MemberCount);

    /* A valid next_entry means a second member and must disable the ATA path. */
    Entry.NextEntry = FixedEntrySize;
    RtlCopyMemory(Storage.Buffer,
                  &Entry,
                  FIELD_OFFSET(PO_BTRFS_DEVICE_ENTRY, Name));
    MemberCount = 0;
    Status = PopDumpBtrfsValidateDeviceList(Storage.Buffer,
                                            sizeof(Storage.Buffer),
                                            &MemberCount);
    ok(Status == STATUS_NOT_SUPPORTED,
       "Multi-member Btrfs list was not rejected: 0x%08lx\n", Status);
    ok(MemberCount == 2, "Expected at least two members, got %lu\n", MemberCount);

    Entry.NextEntry = FixedEntrySize - 1;
    RtlCopyMemory(Storage.Buffer,
                  &Entry,
                  FIELD_OFFSET(PO_BTRFS_DEVICE_ENTRY, Name));
    Status = PopDumpBtrfsValidateDeviceList(Storage.Buffer,
                                            sizeof(Storage.Buffer),
                                            &MemberCount);
    ok(Status == STATUS_FILE_CORRUPT_ERROR,
       "Short next_entry accepted: 0x%08lx\n", Status);

    Entry.NextEntry = sizeof(Storage.Buffer) + 1;
    RtlCopyMemory(Storage.Buffer,
                  &Entry,
                  FIELD_OFFSET(PO_BTRFS_DEVICE_ENTRY, Name));
    Status = PopDumpBtrfsValidateDeviceList(Storage.Buffer,
                                            sizeof(Storage.Buffer),
                                            &MemberCount);
    ok(Status == STATUS_FILE_CORRUPT_ERROR,
       "Out-of-buffer next_entry accepted: 0x%08lx\n", Status);

    Entry.NextEntry = 0;
    Entry.ReadOnly = TRUE;
    RtlCopyMemory(Storage.Buffer,
                  &Entry,
                  FIELD_OFFSET(PO_BTRFS_DEVICE_ENTRY, Name));
    Status = PopDumpBtrfsValidateDeviceList(Storage.Buffer,
                                            sizeof(Storage.Buffer),
                                            &MemberCount);
    ok(Status == STATUS_NOT_SUPPORTED,
       "Read-only member accepted for dump writes: 0x%08lx\n", Status);

    Entry.ReadOnly = FALSE;
    Entry.Missing = TRUE;
    RtlCopyMemory(Storage.Buffer,
                  &Entry,
                  FIELD_OFFSET(PO_BTRFS_DEVICE_ENTRY, Name));
    Status = PopDumpBtrfsValidateDeviceList(Storage.Buffer,
                                            sizeof(Storage.Buffer),
                                            &MemberCount);
    ok(Status == STATUS_NOT_SUPPORTED,
       "Missing member accepted for dump writes: 0x%08lx\n", Status);

    Entry.Missing = FALSE;
    Entry.NameLength = 1;
    RtlCopyMemory(Storage.Buffer,
                  &Entry,
                  FIELD_OFFSET(PO_BTRFS_DEVICE_ENTRY, Name));
    Status = PopDumpBtrfsValidateDeviceList(Storage.Buffer,
                                            sizeof(Storage.Buffer),
                                            &MemberCount);
    ok(Status == STATUS_FILE_CORRUPT_ERROR,
       "Odd UTF-16 device-name length accepted: 0x%08lx\n", Status);

    Entry.NameLength = 0;
    Entry.DeviceId = 0;
    RtlCopyMemory(Storage.Buffer,
                  &Entry,
                  FIELD_OFFSET(PO_BTRFS_DEVICE_ENTRY, Name));
    Status = PopDumpBtrfsValidateDeviceList(Storage.Buffer,
                                            sizeof(Storage.Buffer),
                                            &MemberCount);
    ok(Status == STATUS_FILE_CORRUPT_ERROR,
       "Zero device ID accepted: 0x%08lx\n", Status);

    Entry.DeviceId = 1;
    Entry.MaxSize = Entry.Size - 1;
    RtlCopyMemory(Storage.Buffer,
                  &Entry,
                  FIELD_OFFSET(PO_BTRFS_DEVICE_ENTRY, Name));
    Status = PopDumpBtrfsValidateDeviceList(Storage.Buffer,
                                            sizeof(Storage.Buffer),
                                            &MemberCount);
    ok(Status == STATUS_FILE_CORRUPT_ERROR,
       "Device size larger than physical capacity accepted: 0x%08lx\n", Status);

    Status = PopDumpBtrfsValidateDeviceList(Storage.Buffer,
                                            FixedEntrySize - 1,
                                            &MemberCount);
    ok(Status == STATUS_FILE_CORRUPT_ERROR,
       "Truncated device header accepted: 0x%08lx\n", Status);
    ok(MemberCount == 0, "Truncated header left a nonzero member count\n");
}


static
VOID
TestBtrfsChunkMapping(VOID)
{
    union
    {
        ULONGLONG Alignment;
        UCHAR Buffer[512];
    } Storage;
    PPO_BTRFS_HIBER_MAPPING Mapping;
    PO_BTRFS_HIBER_TARGET Targets[PO_BTRFS_HIBER_MAX_TARGETS];
    ULONG TargetCount, i;
    ULONGLONG MappedLength;
    NTSTATUS Status;

    RtlZeroMemory(&Storage, sizeof(Storage));
    Mapping = (PPO_BTRFS_HIBER_MAPPING)Storage.Buffer;
    Mapping->Version = PO_BTRFS_HIBER_MAPPING_VERSION;
    Mapping->HeaderSize = FIELD_OFFSET(PO_BTRFS_HIBER_MAPPING, Stripes);
    Mapping->StripeCount = 4;
    Mapping->Size = Mapping->HeaderSize +
                    Mapping->StripeCount * sizeof(PO_BTRFS_HIBER_STRIPE);
    Mapping->LogicalAddress = 0x100000;
    Mapping->ChunkLogical = 0x100000;
    Mapping->ChunkLength = 0x100000;
    Mapping->SegmentLength = 0x100000;
    Mapping->StripeLength = 0x10000;
    Mapping->SectorSize = 512;

    for (i = 0; i < Mapping->StripeCount; ++i)
    {
        Mapping->Stripes[i].DeviceId = i + 1;
        Mapping->Stripes[i].DeviceUuid[0] = (UCHAR)(i + 1);
        Mapping->Stripes[i].DeviceSize = 0x1000000;
        Mapping->Stripes[i].PhysicalOffset = 0x200000 + i * 0x100000;
    }

    Status = PopDumpBtrfsValidateMapping(Storage.Buffer,
                                         sizeof(Storage.Buffer), &Mapping);
    ok(Status == STATUS_SUCCESS, "Valid mapping header rejected: 0x%08lx\n", Status);

    Mapping->ChunkType = PO_BTRFS_BLOCK_RAID0;
    Status = PopDumpBtrfsMapLogicalRange(Mapping, 0x110200, 4096,
                                         Targets, &TargetCount, &MappedLength);
    ok(Status == STATUS_SUCCESS, "RAID0 mapping failed: 0x%08lx\n", Status);
    ok(TargetCount == 1 && Targets[0].StripeIndex == 1,
       "RAID0 selected wrong member/stripe\n");
    ok(Targets[0].PhysicalOffset == 0x300200,
       "RAID0 physical offset incorrect: %I64u\n", Targets[0].PhysicalOffset);
    ok(MappedLength == 4096, "RAID0 mapped length incorrect\n");

    Mapping->ChunkType = PO_BTRFS_BLOCK_RAID1;
    Mapping->StripeCount = 2;
    Status = PopDumpBtrfsMapLogicalRange(Mapping, 0x101000, 4096,
                                         Targets, &TargetCount, &MappedLength);
    ok(Status == STATUS_SUCCESS, "RAID1 mapping failed: 0x%08lx\n", Status);
    ok(TargetCount == 2, "RAID1 did not return both mirrors\n");
    ok(Targets[0].PhysicalOffset == 0x201000 &&
       Targets[1].PhysicalOffset == 0x301000,
       "RAID1 mirror offsets incorrect\n");

    Mapping->StripeCount = 4;
    Mapping->ChunkType = PO_BTRFS_BLOCK_RAID10;
    Mapping->SubStripes = 2;
    Status = PopDumpBtrfsMapLogicalRange(Mapping, 0x110200, 4096,
                                         Targets, &TargetCount, &MappedLength);
    ok(Status == STATUS_SUCCESS, "RAID10 mapping failed: 0x%08lx\n", Status);
    ok(TargetCount == 2 && Targets[0].StripeIndex == 2 &&
       Targets[1].StripeIndex == 3, "RAID10 selected wrong mirror group\n");
    ok(Targets[0].PhysicalOffset == 0x400200 &&
       Targets[1].PhysicalOffset == 0x500200,
       "RAID10 physical offsets incorrect\n");

    /* RAID0 requests are split at a stripe boundary. */
    Mapping->ChunkType = PO_BTRFS_BLOCK_RAID0;
    Status = PopDumpBtrfsMapLogicalRange(Mapping, 0x10FF00, 1024,
                                         Targets, &TargetCount, &MappedLength);
    ok(Status == STATUS_SUCCESS && MappedLength == 256,
       "RAID0 did not stop at the stripe boundary\n");

    Mapping->ChunkType = PO_BTRFS_BLOCK_RAID0;
    TargetCount = 99;
    MappedLength = 99;
    Status = PopDumpBtrfsMapLogicalRange(Mapping, 0x101000, 0,
                                         Targets, &TargetCount, &MappedLength);
    ok(Status == STATUS_INVALID_PARAMETER &&
       TargetCount == 0 && MappedLength == 0,
       "Zero-length request did not fail with cleared outputs\\n");

    Status = PopDumpBtrfsMapLogicalRange(Mapping, 0x101001, 512,
                                         Targets, &TargetCount, &MappedLength);
    ok(Status == STATUS_INVALID_PARAMETER,
       "Unaligned logical write was accepted\\n");

    Mapping->Flags = PO_BTRFS_HIBER_MAPPING_FLAG_DEGRADED;
    TargetCount = 99;
    MappedLength = 99;
    Status = PopDumpBtrfsMapLogicalRange(Mapping, 0x101000, 512,
                                         Targets, &TargetCount, &MappedLength);
    ok(Status == STATUS_NOT_SUPPORTED,
       "Degraded mapping was accepted\\n");
    ok(TargetCount == 0 && MappedLength == 0,
       "Failed mapping left stale target outputs\\n");
    Mapping->Flags = 0;

    Mapping->ChunkType = PO_BTRFS_BLOCK_RAID1;
    Mapping->StripeCount = 3;
    Status = PopDumpBtrfsMapLogicalRange(Mapping, 0x101000, 512,
                                         Targets, &TargetCount, &MappedLength);
    ok(Status == STATUS_NOT_SUPPORTED,
       "RAID1 with invalid stripe topology was accepted\\n");
    Mapping->StripeCount = 4;

    /* SINGLE is valid only with one stripe. */
    Mapping->ChunkType = 0;
    Mapping->StripeCount = 1;
    Status = PopDumpBtrfsMapLogicalRange(Mapping, 0x101000, 512,
                                         Targets, &TargetCount, &MappedLength);
    ok(Status == STATUS_SUCCESS && TargetCount == 1,
       "Valid SINGLE mapping rejected\\n");
    Mapping->StripeCount = 4;

    /* DUP mirrors must identify the same physical member. */
    Mapping->ChunkType = PO_BTRFS_BLOCK_DUP;
    Mapping->StripeCount = 2;
    Mapping->Stripes[1].DeviceId = Mapping->Stripes[0].DeviceId;
    Mapping->Stripes[1].DeviceUuid[0] = Mapping->Stripes[0].DeviceUuid[0];
    Status = PopDumpBtrfsMapLogicalRange(Mapping, 0x101000, 512,
                                         Targets, &TargetCount, &MappedLength);
    ok(Status == STATUS_SUCCESS && TargetCount == 2,
       "Valid DUP mapping rejected\\n");
    Mapping->Stripes[1].DeviceId = 2;
    Mapping->Stripes[1].DeviceUuid[0] = 2;
    Status = PopDumpBtrfsMapLogicalRange(Mapping, 0x101000, 512,
                                         Targets, &TargetCount, &MappedLength);
    ok(Status == STATUS_NOT_SUPPORTED,
       "DUP across different devices was incorrectly accepted\\n");

    Mapping->StripeCount = 4;
    Mapping->ChunkType = PO_BTRFS_BLOCK_RAID1C3;
    Mapping->StripeCount = 3;
    Status = PopDumpBtrfsMapLogicalRange(Mapping, 0x101000, 512,
                                         Targets, &TargetCount, &MappedLength);
    ok(Status == STATUS_SUCCESS && TargetCount == 3,
       "RAID1C3 mapping failed\\n");
    Mapping->ChunkType = PO_BTRFS_BLOCK_RAID1C4;
    Mapping->StripeCount = 4;
    Status = PopDumpBtrfsMapLogicalRange(Mapping, 0x101000, 512,
                                         Targets, &TargetCount, &MappedLength);
    ok(Status == STATUS_SUCCESS && TargetCount == 4,
       "RAID1C4 mapping failed\\n");
    Mapping->ChunkType = PO_BTRFS_BLOCK_RAID0 | PO_BTRFS_BLOCK_RAID1;
    Status = PopDumpBtrfsMapLogicalRange(Mapping, 0x101000, 512,
                                         Targets, &TargetCount, &MappedLength);
    ok(Status == STATUS_FILE_CORRUPT_ERROR,
       "Conflicting Btrfs profiles were accepted\\n");

    Mapping->ChunkType = PO_BTRFS_BLOCK_RAID5;
    Status = PopDumpBtrfsMapLogicalRange(Mapping, 0x101000, 512,
                                         Targets, &TargetCount, &MappedLength);
    ok(Status == STATUS_NOT_SUPPORTED,
       "RAID5 parity mapping was incorrectly enabled\n");

    Mapping->ChunkType = PO_BTRFS_BLOCK_RAID6;
    Status = PopDumpBtrfsMapLogicalRange(Mapping, 0x101000, 512,
                                         Targets, &TargetCount, &MappedLength);
    ok(Status == STATUS_NOT_SUPPORTED,
       "RAID6 parity mapping was incorrectly enabled\n");

    Mapping->ChunkType = 0;
    Status = PopDumpBtrfsMapLogicalRange(Mapping, 0x101000, 512,
                                         Targets, &TargetCount, &MappedLength);
    ok(Status == STATUS_NOT_SUPPORTED,
       "Multi-stripe SINGLE layout was incorrectly accepted\n");

    Mapping->ChunkType = PO_BTRFS_BLOCK_RAID1;
    Mapping->StripeCount = 3;
    Status = PopDumpBtrfsMapLogicalRange(Mapping, 0x101000, 512,
                                         Targets, &TargetCount, &MappedLength);
    ok(Status == STATUS_NOT_SUPPORTED,
       "RAID1 with invalid stripe topology was accepted\n");
    Mapping->StripeCount = 4;
    Mapping->ChunkType = PO_BTRFS_BLOCK_RAID0;
    Mapping->Stripes[1].Flags = PO_BTRFS_HIBER_STRIPE_MISSING;
    Status = PopDumpBtrfsMapLogicalRange(Mapping, 0x110200, 512,
                                         Targets, &TargetCount, &MappedLength);
    ok(Status == STATUS_NOT_SUPPORTED, "Missing RAID0 member accepted\n");

    Mapping->Stripes[1].Flags = 0;
    Mapping->Stripes[1].DeviceSize = 0x300000;
    Status = PopDumpBtrfsMapLogicalRange(Mapping, 0x110200, 4096,
                                         Targets, &TargetCount, &MappedLength);
    ok(Status == STATUS_DISK_FULL,
       "Out-of-range member write was accepted\n");

    Mapping->Stripes[1].DeviceSize = 0x1000000;

    Mapping->Stripes[0].PhysicalOffset++;
    Status = PopDumpBtrfsValidateMapping(Storage.Buffer,
                                         sizeof(Storage.Buffer), &Mapping);
    ok(Status == STATUS_FILE_CORRUPT_ERROR,
       "Unaligned member physical offset accepted\\n");
    Mapping->Stripes[0].PhysicalOffset--;

    Mapping->Stripes[0].DeviceUuid[0] = 0;
    Status = PopDumpBtrfsValidateMapping(Storage.Buffer,
                                         sizeof(Storage.Buffer), &Mapping);
    ok(Status == STATUS_FILE_CORRUPT_ERROR,
       "Zero member UUID accepted\\n");
    Mapping->Stripes[0].DeviceUuid[0] = 1;

    Mapping->Reserved2 = 1;
    Status = PopDumpBtrfsValidateMapping(Storage.Buffer,
                                         sizeof(Storage.Buffer), &Mapping);
    ok(Status == STATUS_FILE_CORRUPT_ERROR,
       "Nonzero mapping reserved field accepted\n");
}

START_TEST(PoHiber)
{
    TestAtaIdentify();
    TestAtaAddress();
    TestBtrfsDeviceList();
    TestBtrfsChunkMapping();
}
