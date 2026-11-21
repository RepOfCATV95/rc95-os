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

#include <poppack.h>

extern HALP_APIC_INFO_TABLE HalpApicInfoTable;

#define MPS_CPU 0
#define MPS_BUS 1
#define MPS_IOAPIC 2
#define MPS_INTSRC 3
#define MPS_LINTSRC 4

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
HalpMpsParseTable(_In_ PHALP_MPS_HEADER Table)
{
    PUCHAR Entry, End;
    ULONG i, CpuCount = 0, IoCount = 0;
    UCHAR BootId = (UCHAR)KeGetCurrentPrcb()->InitialApicId;
    BOOLEAN BootFound = FALSE;

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
        } else if (Entry[0] == MPS_IOAPIC) {
            PHALP_MPS_IOAPIC Io = (PHALP_MPS_IOAPIC)Entry;
            if ((Io->Flags & 1) && IoCount < HALP_APIC_INFO_TABLE_IOAPIC_NUMBER)
                HalpApicInfoTable.IoApicPA[IoCount++] = Io->Address;
        } else if (Entry[0] == MPS_INTSRC) {
            /* MPS interrupt source overrides are retained for ISA routing. */
            if (Entry[1] == 0 && Entry[5] < HALP_ISA_IRQ_COUNT)
                HalpApicInfoTable.IsaIrqGsi[Entry[5]] = Entry[7];
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
    return TRUE;
}

VOID
HalpParseApicTables(_In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PHALP_MPS_FP Fp;
    UNREFERENCED_PARAMETER(LoaderBlock);

    Fp = HalpMpsFindFloatingPointer();
    if (!Fp || Fp->Feature[0] != 0 || Fp->TableAddress == 0 ||
        !HalpMpsParseTable((PHALP_MPS_HEADER)(ULONG_PTR)Fp->TableAddress)) {
        DPRINT1("HALMP: No usable Intel MPS configuration; continuing with BSP only\n");
        RtlZeroMemory(&HalpApicInfoTable, sizeof(HalpApicInfoTable));
        HalpMpsUseBspOnly();
    }
}

VOID
HalpPrintApicTables(VOID)
{
#if DBG
    ULONG i;
    DPRINT1("HALMP: Physical processor count: %lu\n",
            HalpApicInfoTable.ProcessorCount);
    for (i = 0; i < HalpApicInfoTable.ProcessorCount; ++i)
        DPRINT1(" CPU %lu: APIC ID %lu, BSP %u, Started %u, PRCB %p\n",
                i, HalpProcessorIdentity[i].LapicId,
                HalpProcessorIdentity[i].BSPCheck,
                HalpProcessorIdentity[i].ProcessorStarted,
                HalpProcessorIdentity[i].ProcessorPrcb);
#endif
}
