/*
 * PROJECT:     CATV 95 kernel-mode tests
 * LICENSE:     LGPL-2.1+
 * PURPOSE:     Hibernation ATA IDENTIFY and LBA task-file regression tests
 */

#include <kmt_test.h>
#include "../../../../ntoskrnl/po/hibdump_ata.h"

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

START_TEST(PoHiber)
{
    TestAtaIdentify();
    TestAtaAddress();
}
