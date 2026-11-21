/*
 * PROJECT: ReactOS Kernel
 * LICENSE: GPL-2.0-or-later
 * PURPOSE: Intel MultiProcessor Specification discovery for HALMP.
 */
#include <hal.h>
#include <smp.h>
#define NDEBUG
#include <debug.h>
#include <pshpack1.h>

typedef struct _HALP_MPS_FP {
    CHAR Signature[4];
    ULONG TableAddress;
    UCHAR Length, Revision, Checksum, Feature[5];
} HALP_MPS_FP, *PHALP_MPS_FP;

typedef struct _HALP_MPS_HEADER {
    CHAR Signature[4];
    USHORT Length;
    UCHAR Revision, Checksum;
    CHAR OemId[8], ProductId[12];
    ULONG OemTable;
    USHORT OemLength, EntryCount;
    ULONG LocalApicAddress;
    USHORT ExtendedLength;
    UCHAR ExtendedChecksum, Reserved;
} HALP_MPS_HEADER, *PHALP_MPS_HEADER;

typedef struct _HALP_MPS_CPU {
    UCHAR Type, ApicId, ApicVersion, Flags;
    ULONG Signature, Features, Reserved[2];
} HALP_MPS_CPU, *PHALP_MPS_CPU;

typedef struct _HALP_MPS_IOAPIC {
    UCHAR Type, ApicId, Version, Flags;
    ULONG Address;
} HALP_MPS_IOAPIC, *PHALP_MPS_IOAPIC;
typedef struct _HALP_MPS_EXT_ADDRESS {
    UCHAR Type, BusId, AddressType, Reserved;
    ULONGLONG Base, Length;
} HALP_MPS_EXT_ADDRESS, *PHALP_MPS_EXT_ADDRESS;
typedef struct _HALP_MPS_EXT_HIERARCHY {
    UCHAR Type, BusId, BusInfo, ParentBus, Reserved[3];
} HALP_MPS_EXT_HIERARCHY, *PHALP_MPS_EXT_HIERARCHY;
typedef struct _HALP_MPS_EXT_COMPAT {
    UCHAR Type, BusId, Modifier, Reserved;
    ULONG RangeList;
} HALP_MPS_EXT_COMPAT, *PHALP_MPS_EXT_COMPAT;

#include <poppack.h>

extern HALP_APIC_INFO_TABLE HalpApicInfoTable;

#define MPS_CPU 0
#define MPS_BUS 1
#define MPS_IOAPIC 2
#define MPS_INTSRC 3
#define MPS_LINTSRC 4
#define MPS_EXT_ADDRESS 128
#define MPS_EXT_HIERARCHY 129
#define MPS_EXT_COMPAT 130

static BOOLEAN
HalpMpsChecksum(_In_reads_bytes_(Length) const UCHAR *Data, _In_ ULONG Length)
{
    UCHAR Sum = 0;
    ULONG i;
    for (i = 0; i < Length; ++i) Sum = (UCHAR)(Sum + Data[i]);
    return Sum == 0;
}

static PHALP_MPS_FP
HalpMpsScan(_In_ ULONG Base, _In_ ULONG Length)
{
    ULONG Offset;
    PHALP_MPS_FP Fp;
    for (Offset = 0; Offset + 16 <= Length; Offset += 16) {
        Fp = (PHALP_MPS_FP)(ULONG_PTR)(Base + Offset);
        if (RtlCompareMemory(Fp->Signature, "_MP_", 4) == 4 &&
            Fp->Length == 1 && (Fp->Revision == 1 || Fp->Revision == 4) &&
            HalpMpsChecksum((const UCHAR *)Fp, 16))
            return Fp;
    }
    return NULL;
}

static PHALP_MPS_FP
HalpMpsFindFloatingPointer(VOID)
{
    PHALP_MPS_FP Fp;
    USHORT ConventionalKb, EbdaSegment;
    Fp = HalpMpsScan(0, 0x400);
    if (Fp) return Fp;
    ConventionalKb = *(volatile PUSHORT)(ULONG_PTR)0x413;
    if (ConventionalKb && (ConventionalKb * 1024) >= 0x400) {
        Fp = HalpMpsScan((ConventionalKb * 1024) - 0x400, 0x400);
        if (Fp) return Fp;
    }
    EbdaSegment = *(volatile PUSHORT)(ULONG_PTR)0x40E;
    if (EbdaSegment) {
        Fp = HalpMpsScan((ULONG)EbdaSegment << 4, 0x400);
        if (Fp) return Fp;
    }
    return HalpMpsScan(0xF0000, 0x10000);
}

static VOID
HalpMpsUseBspOnly(VOID)
{
    RtlZeroMemory(HalpProcessorIdentity, sizeof(HalpProcessorIdentity));
    HalpProcessorIdentity[0].LapicId =
        (UCHAR)KeGetCurrentPrcb()->InitialApicId;
    HalpProcessorIdentity[0].ProcessorId = 0;
    HalpProcessorIdentity[0].BSPCheck = TRUE;
    HalpApicInfoTable.ProcessorCount = 1;
}


static BOOLEAN
HalpMpsParseExtendedTable(PUCHAR Data, USHORT Length, UCHAR Checksum)
{
    PUCHAR Entry = Data, End = Data + Length;
    UCHAR Sum = Checksum;
    ULONG i;
    for (i = 0; i < Length; ++i) Sum = (UCHAR)(Sum + Data[i]);
    if (Sum != 0) return FALSE;

    while (Entry < End) {
        ULONG Size;
        switch (Entry[0]) {
        case MPS_EXT_ADDRESS: Size = sizeof(HALP_MPS_EXT_ADDRESS); break;
        case MPS_EXT_HIERARCHY: Size = sizeof(HALP_MPS_EXT_HIERARCHY); break;
        case MPS_EXT_COMPAT: Size = sizeof(HALP_MPS_EXT_COMPAT); break;
        default: return FALSE;
        }
        if ((ULONG)(End - Entry) < Size) return FALSE;
        if (Entry[0] == MPS_EXT_ADDRESS) {
            PHALP_MPS_EXT_ADDRESS E = (PHALP_MPS_EXT_ADDRESS)Entry;
            ULONG n = HalpApicInfoTable.MpsExtendedAddressRangeCount;
            if (n >= HALP_MPS_MAX_EXT_ADDRESS_RANGES || E->AddressType > 2 ||
                E->Reserved != 0 || E->Length == 0 || E->Base + E->Length < E->Base) return FALSE;
            HalpApicInfoTable.MpsExtendedAddressRanges[n].BusId = E->BusId;
            HalpApicInfoTable.MpsExtendedAddressRanges[n].AddressType = E->AddressType;
            HalpApicInfoTable.MpsExtendedAddressRanges[n].BaseAddress = E->Base;
            HalpApicInfoTable.MpsExtendedAddressRanges[n].AddressLength = E->Length;
            ++HalpApicInfoTable.MpsExtendedAddressRangeCount;
        } else if (Entry[0] == MPS_EXT_HIERARCHY) {
            PHALP_MPS_EXT_HIERARCHY E = (PHALP_MPS_EXT_HIERARCHY)Entry;
            ULONG n = HalpApicInfoTable.MpsBusHierarchyCount;
            if (n >= HALP_MPS_MAX_BUS_HIERARCHIES || E->BusId == E->ParentBus ||
                E->Reserved[0] || E->Reserved[1] || E->Reserved[2]) return FALSE;
            HalpApicInfoTable.MpsBusHierarchies[n].BusId = E->BusId;
            HalpApicInfoTable.MpsBusHierarchies[n].BusInfo = E->BusInfo;
            HalpApicInfoTable.MpsBusHierarchies[n].ParentBus = E->ParentBus;
            ++HalpApicInfoTable.MpsBusHierarchyCount;
        } else {
            PHALP_MPS_EXT_COMPAT E = (PHALP_MPS_EXT_COMPAT)Entry;
            ULONG n = HalpApicInfoTable.MpsCompatibilityRangeCount;
            if (n >= HALP_MPS_MAX_COMPATIBILITY_RANGES || E->Modifier > 1 || E->Reserved) return FALSE;
            HalpApicInfoTable.MpsCompatibilityRanges[n].BusId = E->BusId;
            HalpApicInfoTable.MpsCompatibilityRanges[n].Modifier = E->Modifier;
            HalpApicInfoTable.MpsCompatibilityRanges[n].Address = E->RangeList;
            ++HalpApicInfoTable.MpsCompatibilityRangeCount;
        }
        Entry += Size;
    }
    return Entry == End;
}

static BOOLEAN
HalpMpsParseTable(_In_ PHALP_MPS_HEADER Table)
{
    PUCHAR Entry, End;
    ULONG i, CpuCount = 0, IoCount = 0;
    UCHAR BootId = (UCHAR)KeGetCurrentPrcb()->InitialApicId;
    BOOLEAN BootFound = FALSE;
    ULONG BusTypes[256] = {0};

    if (RtlCompareMemory(Table->Signature, "PCMP", 4) != 4 ||
        Table->Length < sizeof(*Table) ||
        (Table->Revision != 1 && Table->Revision != 4) ||
        !HalpMpsChecksum((const UCHAR *)Table, Table->Length))
        return FALSE;

    RtlZeroMemory(&HalpApicInfoTable, sizeof(HalpApicInfoTable));
    RtlZeroMemory(HalpProcessorIdentity, sizeof(HalpProcessorIdentity));
    HalpApicInfoTable.ApicMode = HALP_APIC_MODE_LEGACY;
    HalpApicInfoTable.LocalApicPA = Table->LocalApicAddress;
    Entry = (PUCHAR)(Table + 1);
    End = (PUCHAR)Table + Table->Length;

    for (i = 0; i < Table->EntryCount; ++i) {
        ULONG Size;
        if (Entry >= End) return FALSE;
        switch (Entry[0]) {
        case MPS_CPU: Size = sizeof(HALP_MPS_CPU); break;
        case MPS_BUS: Size = 8; break;
        case MPS_IOAPIC: Size = sizeof(HALP_MPS_IOAPIC); break;
        case MPS_INTSRC:
        case MPS_LINTSRC: Size = 8; break;
        default: return FALSE;
        }
        if ((ULONG)(End - Entry) < Size) return FALSE;

        if (Entry[0] == MPS_CPU) {
            PHALP_MPS_CPU Cpu = (PHALP_MPS_CPU)Entry;
            if (Cpu->Flags & 1) {
                if (CpuCount >= MAXIMUM_PROCESSORS) return FALSE;
                HalpProcessorIdentity[CpuCount].ProcessorId = Cpu->ApicId;
                HalpProcessorIdentity[CpuCount].LapicId = Cpu->ApicId;
                HalpProcessorIdentity[CpuCount].BSPCheck = (Cpu->Flags & 2) != 0;
                if (Cpu->ApicId == BootId) BootFound = TRUE;
                ++CpuCount;
            }
        } else if (Entry[0] == MPS_BUS) {
            if (RtlCompareMemory(Entry + 2, "ISA   ", 6) == 6) BusTypes[Entry[1]] = 1;
            else if (RtlCompareMemory(Entry + 2, "EISA  ", 6) == 6) BusTypes[Entry[1]] = 2;
            else if (RtlCompareMemory(Entry + 2, "PCI   ", 6) == 6) BusTypes[Entry[1]] = 3;
            else if (RtlCompareMemory(Entry + 2, "MCA   ", 6) == 6) BusTypes[Entry[1]] = 4;
        } else if (Entry[0] == MPS_IOAPIC) {
            PHALP_MPS_IOAPIC Io = (PHALP_MPS_IOAPIC)Entry;
            if ((Io->Flags & 1) && IoCount < HALP_APIC_INFO_TABLE_IOAPIC_NUMBER)
                HalpApicInfoTable.IoApicPA[IoCount] = Io->Address;
                HalpApicInfoTable.IoApicId[IoCount] = Io->ApicId;
                ++IoCount;
        } else if (Entry[0] == MPS_INTSRC) {
            HALP_MPS_INTERRUPT_ROUTE *Route;
            ULONG n = HalpApicInfoTable.MpsInterruptRouteCount;
            ULONG Polarity, Trigger;
            if (n >= HALP_MPS_MAX_INTERRUPT_ROUTES) return FALSE;
            Route = &HalpApicInfoTable.MpsInterruptRoutes[n];
            Route->InterruptType = Entry[1];
            Route->Flags = *(PUSHORT)(Entry + 2);
            if ((Route->Flags & 0xFFF0) ||
                ((Route->Flags & 3) == 2) || (((Route->Flags >> 2) & 3) == 2))
                return FALSE;
            Route->SourceBus = Entry[4];
            Route->SourceBusType = (UCHAR)BusTypes[Entry[4]];
            Route->SourceBusIrq = Entry[5];
            Route->DestinationApic = Entry[6];
            Route->DestinationInt = Entry[7];
            ++HalpApicInfoTable.MpsInterruptRouteCount;
            if ((BusTypes[Entry[4]] == 1 || BusTypes[Entry[4]] == 2) &&
                Entry[1] == 0 && Entry[5] < HALP_ISA_IRQ_COUNT) {
                Polarity = Route->Flags & 3;
                Trigger = (Route->Flags >> 2) & 3;
                HalpApicInfoTable.IsaIrqGsi[Entry[5]] = Entry[7]; /* translated to GSI after IOAPIC probing */
                HalpApicInfoTable.IsaIrqPolarity[Entry[5]] =
                    (Polarity == 3) ? InterruptActiveLow :
                    (Polarity == 1) ? InterruptActiveHigh :
                    InterruptPolarityUnknown;
                HalpApicInfoTable.IsaIrqTrigger[Entry[5]] =
                    (Trigger == 3) ? LevelSensitive : Latched;
            }
        }
        Entry += Size;
    }

    if (Entry != End || CpuCount == 0 || !BootFound) return FALSE;
    for (i = 0; i < CpuCount; ++i)
        if (HalpProcessorIdentity[i].LapicId == BootId) break;
    if (i == CpuCount) return FALSE;
    if (i) {
        PROCESSOR_IDENTITY Bsp = HalpProcessorIdentity[i];
        RtlMoveMemory(&HalpProcessorIdentity[1], &HalpProcessorIdentity[0],
                      i * sizeof(PROCESSOR_IDENTITY));
        HalpProcessorIdentity[0] = Bsp;
    }
    HalpProcessorIdentity[0].BSPCheck = TRUE;
    HalpApicInfoTable.ProcessorCount = CpuCount;
    HalpApicInfoTable.IOAPICCount = IoCount;
    return HalpMpsParseExtendedTable((PUCHAR)Table + Table->Length,
                                     Table->ExtendedLength,
                                     Table->ExtendedChecksum);
}

VOID
HalpParseApicTables(_In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PHALP_MPS_FP Fp;
    UNREFERENCED_PARAMETER(LoaderBlock);

    Fp = HalpMpsFindFloatingPointer();
    if (!Fp || Fp->Feature[0] != 0 || Fp->TableAddress == 0 ||
        !HalpMpsParseTable((PHALP_MPS_HEADER)(ULONG_PTR)Fp->TableAddress)) {
        DPRINT1("HALMP: No usable Intel MPS configuration; continuing with BSP only
");
        RtlZeroMemory(&HalpApicInfoTable, sizeof(HalpApicInfoTable));
        HalpMpsUseBspOnly();
    } else {
        HalpApicInfoTable.MpsImcrPresent = ((Fp->Feature[1] & 0x80) != 0);
    }
}

VOID
HalpPrintApicTables(VOID)
{
#if DBG
    ULONG i;
    DPRINT1("HALMP: Physical processor count: %lu
",
            HalpApicInfoTable.ProcessorCount);
    for (i = 0; i < HalpApicInfoTable.ProcessorCount; ++i)
        DPRINT1(" CPU %lu: APIC ID %lu, BSP %u, Started %u, PRCB %p
",
                i, HalpProcessorIdentity[i].LapicId,
                HalpProcessorIdentity[i].BSPCheck,
                HalpProcessorIdentity[i].ProcessorStarted,
                HalpProcessorIdentity[i].ProcessorPrcb);
#endif
}
