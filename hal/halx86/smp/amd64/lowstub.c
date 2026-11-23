/*
 * PROJECT:     ReactOS HAL
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     amd64 low stub for waking up from ACPI sleep states (S2/S3)
 *
 * HalpWakeBuildStub prepares the real-mode stub (wakestub.S) and the
 * temporary page tables in the low stub pages. HalpWakeSaveContext saves the
 * processor state like setjmp: it returns FALSE when it saved the state and
 * returns TRUE a second time when the stub has restored the state after the
 * system woke up.
 */

/* INCLUDES ******************************************************************/

#include <hal.h>

#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/

/* The stub (wakestub.S) */
extern UCHAR HalpWakeEntry16[];
extern UCHAR HalpWakeTramp64[];
extern UCHAR HalpWakeData[];
extern UCHAR HalpWakeEntry16End[];
extern VOID HalpWakeEntry64(VOID);

/* The data of the stub, in the low stub page (see HalpWakeData in wakestub.S) */
typedef struct _HALP_WAKE_STUB_DATA
{
    ULONG Jump64Offset;
    USHORT Jump64Selector;
    USHORT Reserved;
    ULONG PageTableRoot;
    USHORT GdtrPad;
    USHORT GdtrLimit;
    ULONG GdtrBase;
    ULONG Reserved2;
    ULONG64 StatePtr;
    ULONG64 EntryPtr;
    ULONG64 Gdt[3];
} HALP_WAKE_STUB_DATA, *PHALP_WAKE_STUB_DATA;

C_ASSERT(FIELD_OFFSET(HALP_WAKE_STUB_DATA, Jump64Offset) == 0x00);
C_ASSERT(FIELD_OFFSET(HALP_WAKE_STUB_DATA, PageTableRoot) == 0x08);
C_ASSERT(FIELD_OFFSET(HALP_WAKE_STUB_DATA, GdtrLimit) == 0x0E);
C_ASSERT(FIELD_OFFSET(HALP_WAKE_STUB_DATA, GdtrBase) == 0x10);
C_ASSERT(FIELD_OFFSET(HALP_WAKE_STUB_DATA, StatePtr) == 0x18);
C_ASSERT(FIELD_OFFSET(HALP_WAKE_STUB_DATA, EntryPtr) == 0x20);
C_ASSERT(FIELD_OFFSET(HALP_WAKE_STUB_DATA, Gdt) == 0x28);

/* The processor state; the offsets are used by wakestub.S */
typedef struct DECLSPEC_ALIGN(16) _HALP_WAKE_STATE
{
    volatile ULONG Resumed;         /* 000 set by the stub */
    ULONG Reserved1;                /* 004 */
    ULONG64 Cr0;                    /* 008 */
    ULONG64 Cr3;                    /* 010 */
    ULONG64 Cr4;                    /* 018 */
    ULONG64 Cr8;                    /* 020 */
    ULONG64 Efer;                   /* 028 */
    USHORT GdtrPad[3];              /* 030 */
    USHORT GdtrLimit;               /* 036 */
    ULONG64 GdtrBase;               /* 038 */
    USHORT IdtrPad[3];              /* 040 */
    USHORT IdtrLimit;               /* 046 */
    ULONG64 IdtrBase;               /* 048 */
    USHORT Tr;                      /* 050 */
    USHORT Ldtr;                    /* 052 */
    USHORT SegCs;                   /* 054 */
    USHORT SegDs;                   /* 056 */
    USHORT SegEs;                   /* 058 */
    USHORT SegFs;                   /* 05A */
    USHORT SegGs;                   /* 05C */
    USHORT SegSs;                   /* 05E */
    ULONG64 Dr0;                    /* 060 */
    ULONG64 Dr1;                    /* 068 */
    ULONG64 Dr2;                    /* 070 */
    ULONG64 Dr3;                    /* 078 */
    ULONG64 Dr6;                    /* 080 */
    ULONG64 Dr7;                    /* 088 */
    ULONG64 GsBase;                 /* 090 */
    ULONG64 GsSwap;                 /* 098 */
    ULONG64 Star;                   /* 0A0 */
    ULONG64 LStar;                  /* 0A8 */
    ULONG64 CStar;                  /* 0B0 */
    ULONG64 SfMask;                 /* 0B8 */
    ULONG64 Pat;                    /* 0C0 */
    ULONG64 Rbx;                    /* 0C8 */
    ULONG64 Rbp;                    /* 0D0 */
    ULONG64 Rdi;                    /* 0D8 */
    ULONG64 Rsi;                    /* 0E0 */
    ULONG64 R12;                    /* 0E8 */
    ULONG64 R13;                    /* 0F0 */
    ULONG64 R14;                    /* 0F8 */
    ULONG64 R15;                    /* 100 */
    ULONG64 Rsp;                    /* 108 */
    ULONG64 Rip;                    /* 110 */
    ULONG64 Rflags;                 /* 118 */
    ULONG64 Reserved2[2];           /* 120 */
    DECLSPEC_ALIGN(16) UCHAR FxState[512]; /* 130 */
} HALP_WAKE_STATE, *PHALP_WAKE_STATE;

C_ASSERT(FIELD_OFFSET(HALP_WAKE_STATE, Resumed) == 0x000);
C_ASSERT(FIELD_OFFSET(HALP_WAKE_STATE, Cr0) == 0x008);
C_ASSERT(FIELD_OFFSET(HALP_WAKE_STATE, Cr3) == 0x010);
C_ASSERT(FIELD_OFFSET(HALP_WAKE_STATE, Cr4) == 0x018);
C_ASSERT(FIELD_OFFSET(HALP_WAKE_STATE, Cr8) == 0x020);
C_ASSERT(FIELD_OFFSET(HALP_WAKE_STATE, Efer) == 0x028);
C_ASSERT(FIELD_OFFSET(HALP_WAKE_STATE, GdtrLimit) == 0x036);
C_ASSERT(FIELD_OFFSET(HALP_WAKE_STATE, GdtrBase) == 0x038);
C_ASSERT(FIELD_OFFSET(HALP_WAKE_STATE, IdtrLimit) == 0x046);
C_ASSERT(FIELD_OFFSET(HALP_WAKE_STATE, Tr) == 0x050);
C_ASSERT(FIELD_OFFSET(HALP_WAKE_STATE, Ldtr) == 0x052);
C_ASSERT(FIELD_OFFSET(HALP_WAKE_STATE, SegCs) == 0x054);
C_ASSERT(FIELD_OFFSET(HALP_WAKE_STATE, SegDs) == 0x056);
C_ASSERT(FIELD_OFFSET(HALP_WAKE_STATE, SegSs) == 0x05E);
C_ASSERT(FIELD_OFFSET(HALP_WAKE_STATE, Dr0) == 0x060);
C_ASSERT(FIELD_OFFSET(HALP_WAKE_STATE, Dr7) == 0x088);
C_ASSERT(FIELD_OFFSET(HALP_WAKE_STATE, GsBase) == 0x090);
C_ASSERT(FIELD_OFFSET(HALP_WAKE_STATE, SfMask) == 0x0B8);
C_ASSERT(FIELD_OFFSET(HALP_WAKE_STATE, Pat) == 0x0C0);
C_ASSERT(FIELD_OFFSET(HALP_WAKE_STATE, Rbx) == 0x0C8);
C_ASSERT(FIELD_OFFSET(HALP_WAKE_STATE, R15) == 0x100);
C_ASSERT(FIELD_OFFSET(HALP_WAKE_STATE, Rsp) == 0x108);
C_ASSERT(FIELD_OFFSET(HALP_WAKE_STATE, Rip) == 0x110);
C_ASSERT(FIELD_OFFSET(HALP_WAKE_STATE, Rflags) == 0x118);
C_ASSERT(FIELD_OFFSET(HALP_WAKE_STATE, FxState) == 0x130);

/* Used by HalpWakeSaveContext in wakestub.S */
HALP_WAKE_STATE HalpWakeState;

#define HALP_WAKE_MSR_PAT       0x277
#define HALP_WAKE_EFER_LMA      0x400
#define HALP_WAKE_RFLAGS_IF     0x200

/* FUNCTIONS *****************************************************************/

/* The pages of the stub: below 1MB, as the firmware enters it in real mode */
static PVOID HalpWakeLowStub;
static PHYSICAL_ADDRESS HalpWakeLowStubPhysicalAddress;

/*
 * Get the pages for the stub and the temporary page tables. Has to be called
 * at PASSIVE_LEVEL, before HalpWakeBuildStub is needed. The pages are kept.
 */
NTSTATUS
NTAPI
HalpWakeAllocateStub(VOID)
{
    PHYSICAL_ADDRESS HighestAddress, PhysicalAddress;
    PVOID LowStub;

    if (HalpWakeLowStub != NULL)
        return STATUS_SUCCESS;

    HighestAddress.QuadPart = 0xFFFFF;
    LowStub = MmAllocateContiguousMemory(HALP_LOW_STUB_SIZE_IN_PAGES * PAGE_SIZE,
                                         HighestAddress);
    if (LowStub == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    PhysicalAddress = MmGetPhysicalAddress(LowStub);
    if ((PhysicalAddress.QuadPart & (PAGE_SIZE - 1)) != 0 ||
        PhysicalAddress.QuadPart + HALP_LOW_STUB_SIZE_IN_PAGES * PAGE_SIZE > 0x100000)
    {
        MmFreeContiguousMemory(LowStub);
        return STATUS_NOT_SUPPORTED;
    }

    RtlZeroMemory(LowStub, HALP_LOW_STUB_SIZE_IN_PAGES * PAGE_SIZE);
    HalpWakeLowStubPhysicalAddress = PhysicalAddress;
    HalpWakeLowStub = LowStub;
    return STATUS_SUCCESS;
}

/*
 * Page tables for the stub: the PML4 of the kernel with the first entry
 * replaced by a chain to a page table that maps the stub to itself.
 * Pages: 0 stub, 1 PML4, 2 PDPT, 3 PD, 4 PT.
 */
static
NTSTATUS
HalpWakeSetupTemporaryMappings(
    _Out_ PULONG PageTableRoot)
{
    PHYSICAL_ADDRESS PhysicalAddress;
    PULONG64 CurrentRoot, Pml4, Pdpt, Pd, Pt;
    ULONG64 StubPhysical = HalpWakeLowStubPhysicalAddress.QuadPart;
    ULONG StartPti, i;

    Pml4 = (PULONG64)((PUCHAR)HalpWakeLowStub + 1 * PAGE_SIZE);
    Pdpt = (PULONG64)((PUCHAR)HalpWakeLowStub + 2 * PAGE_SIZE);
    Pd = (PULONG64)((PUCHAR)HalpWakeLowStub + 3 * PAGE_SIZE);
    Pt = (PULONG64)((PUCHAR)HalpWakeLowStub + 4 * PAGE_SIZE);

    /* Copy the PML4 that is in use */
    PhysicalAddress.QuadPart = __readcr3() & ~(ULONG64)(PAGE_SIZE - 1);
    CurrentRoot = HalpMapPhysicalMemory64(PhysicalAddress, 1);
    if (CurrentRoot == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlCopyMemory(Pml4, CurrentRoot, PAGE_SIZE);
    HalpUnmapVirtualAddress(CurrentRoot, 1);

    /* The lowest 2MB: only the pages of the stub, mapped to themselves */
    StartPti = (ULONG)(StubPhysical >> PAGE_SHIFT);
    ASSERT(StartPti + HALP_LOW_STUB_SIZE_IN_PAGES < 512);
    RtlZeroMemory(Pdpt, PAGE_SIZE);
    RtlZeroMemory(Pd, PAGE_SIZE);
    RtlZeroMemory(Pt, PAGE_SIZE);
    for (i = 0; i < HALP_LOW_STUB_SIZE_IN_PAGES; i++)
        Pt[StartPti + i] = (StubPhysical + (ULONG64)i * PAGE_SIZE) | 0x3;
    Pd[0] = (StubPhysical + 4 * PAGE_SIZE) | 0x3;
    Pdpt[0] = (StubPhysical + 3 * PAGE_SIZE) | 0x3;
    Pml4[0] = (StubPhysical + 2 * PAGE_SIZE) | 0x3;

    *PageTableRoot = (ULONG)(StubPhysical + PAGE_SIZE);
    return STATUS_SUCCESS;
}

/*
 * Prepare the stub. Returns the physical address of its entry point, the
 * real-mode waking vector. Needs the low stub of HalpSetupAcpiPhase0.
 */
NTSTATUS
NTAPI
HalpWakeBuildStub(
    _Out_ PULONG StubPhysicalAddress)
{
    PVOID LowStub = HalpWakeLowStub;
    PHALP_WAKE_STUB_DATA Data;
    ULONG PageTableRoot;
    NTSTATUS Status;

    *StubPhysicalAddress = 0;

    /* Real mode: the entry point has to be below 1MB */
    if (LowStub == NULL ||
        HalpWakeLowStubPhysicalAddress.QuadPart == 0 ||
        HalpWakeLowStubPhysicalAddress.QuadPart + HALP_LOW_STUB_SIZE_IN_PAGES * PAGE_SIZE > 0x100000 ||
        (HalpWakeLowStubPhysicalAddress.QuadPart & (PAGE_SIZE - 1)) != 0)
    {
        return STATUS_NOT_SUPPORTED;
    }

    ASSERT((ULONG_PTR)HalpWakeEntry16End - (ULONG_PTR)HalpWakeEntry16 < PAGE_SIZE);

    Status = HalpWakeSetupTemporaryMappings(&PageTableRoot);
    if (!NT_SUCCESS(Status))
        return Status;

    RtlCopyMemory(LowStub,
                  HalpWakeEntry16,
                  (ULONG_PTR)HalpWakeEntry16End - (ULONG_PTR)HalpWakeEntry16);

    Data = (PHALP_WAKE_STUB_DATA)((PUCHAR)HalpWakeLowStub +
                                  ((ULONG_PTR)HalpWakeData - (ULONG_PTR)HalpWakeEntry16));

    /* Linear address of the trampoline, the page is mapped to itself */
    Data->Jump64Offset = (ULONG)HalpWakeLowStubPhysicalAddress.QuadPart +
                         (ULONG)((ULONG_PTR)HalpWakeTramp64 - (ULONG_PTR)HalpWakeEntry16);
    Data->Jump64Selector = 8;
    Data->PageTableRoot = PageTableRoot;
    Data->StatePtr = (ULONG64)(ULONG_PTR)&HalpWakeState;
    Data->EntryPtr = (ULONG64)(ULONG_PTR)HalpWakeEntry64;

    /* GDT of the stub: null, 64-bit code, data */
    Data->Gdt[0] = 0;
    Data->Gdt[1] = 0x00209A0000000000ULL;
    Data->Gdt[2] = 0x0000920000000000ULL;
    Data->GdtrLimit = sizeof(Data->Gdt) - 1;
    Data->GdtrBase = (ULONG)HalpWakeLowStubPhysicalAddress.QuadPart +
                     (ULONG)((ULONG_PTR)HalpWakeData - (ULONG_PTR)HalpWakeEntry16) +
                     FIELD_OFFSET(HALP_WAKE_STUB_DATA, Gdt);

    *StubPhysicalAddress = (ULONG)HalpWakeLowStubPhysicalAddress.QuadPart;
    return STATUS_SUCCESS;
}

/*
 * Save the control registers, the MSRs and the FPU/SSE state, the part of
 * HalpWakeSaveContext (wakestub.S) that is easier to write in C. That routine
 * saves the registers, the stack pointer and the return address of its caller
 * and returns FALSE; when the machine wakes up the stub restores the state
 * and the routine returns TRUE to the same caller. Interrupts have to be
 * disabled while it is called, and the caller's frame has to be alive.
 */
VOID
HalpWakeSaveControlState(VOID)
{
    PHALP_WAKE_STATE State = &HalpWakeState;

    State->Cr0 = __readcr0();
    State->Cr3 = __readcr3();
    State->Cr4 = __readcr4();
    State->Cr8 = __readcr8();
    State->Efer = __readmsr(X86_MSR_EFER) & ~(ULONG64)HALP_WAKE_EFER_LMA;
    /* GDTR, IDTR, TR and LDTR are saved by HalpWakeSaveContext */
    State->Dr0 = __readdr(0);
    State->Dr1 = __readdr(1);
    State->Dr2 = __readdr(2);
    State->Dr3 = __readdr(3);
    State->Dr6 = __readdr(6);
    State->Dr7 = __readdr(7);
    State->GsBase = __readmsr(X86_MSR_GSBASE);
    State->GsSwap = __readmsr(X86_MSR_KERNEL_GSBASE);
    State->Star = __readmsr(X86_MSR_STAR);
    State->LStar = __readmsr(X86_MSR_LSTAR);
    State->CStar = __readmsr(X86_MSR_CSTAR);
    State->SfMask = __readmsr(X86_MSR_SFMASK);
    State->Pat = __readmsr(HALP_WAKE_MSR_PAT);
    _fxsave64(State->FxState);
}
