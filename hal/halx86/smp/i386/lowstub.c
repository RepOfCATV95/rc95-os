/*
 * PROJECT:     ReactOS HAL
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     i386 low stub for waking up from ACPI sleep states (S2/S3)
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

extern PHYSICAL_ADDRESS HalpLowStubPhysicalAddress;
extern PVOID HalpLowStub;

/* The stub (wakestub.S) */
extern UCHAR HalpWakeEntry16[];
extern UCHAR HalpWakeData[];
extern UCHAR HalpWakeEntry16End[];
extern VOID HalpWakeEntry32(VOID);

/* The data of the stub, in the low stub page (see WakeJump32... in wakestub.S) */
typedef struct _HALP_WAKE_STUB_DATA
{
    ULONG Jump32Offset;
    USHORT Jump32Selector;
    USHORT Reserved;
    ULONG PageTableRoot;
    ULONG StatePtr;
    USHORT GdtrPad;
    USHORT GdtrLimit;
    ULONG GdtrBase;
    USHORT IdtrPad;
    USHORT IdtrLimit;
    ULONG IdtrBase;
} HALP_WAKE_STUB_DATA, *PHALP_WAKE_STUB_DATA;

C_ASSERT(FIELD_OFFSET(HALP_WAKE_STUB_DATA, Jump32Offset) == 0x00);
C_ASSERT(FIELD_OFFSET(HALP_WAKE_STUB_DATA, PageTableRoot) == 0x08);
C_ASSERT(FIELD_OFFSET(HALP_WAKE_STUB_DATA, StatePtr) == 0x0C);
C_ASSERT(FIELD_OFFSET(HALP_WAKE_STUB_DATA, GdtrLimit) == 0x12);
C_ASSERT(FIELD_OFFSET(HALP_WAKE_STUB_DATA, IdtrLimit) == 0x1A);
C_ASSERT(sizeof(HALP_WAKE_STUB_DATA) == 0x20);

/* The processor state; the offsets are used by wakestub.S */
typedef struct DECLSPEC_ALIGN(16) _HALP_WAKE_STATE
{
    volatile ULONG Resumed;         /* 00 set by the stub */
    ULONG Cr0;                      /* 04 */
    ULONG Cr3;                      /* 08 */
    ULONG Cr4;                      /* 0C */
    ULONG Dr0;                      /* 10 */
    ULONG Dr1;                      /* 14 */
    ULONG Dr2;                      /* 18 */
    ULONG Dr3;                      /* 1C */
    ULONG Dr6;                      /* 20 */
    ULONG Dr7;                      /* 24 */
    USHORT Tr;                      /* 28 */
    USHORT Ldtr;                    /* 2A */
    ULONG GdtrBase;                 /* 2C */
    ULONG SegDs;                    /* 30 */
    ULONG SegEs;                    /* 34 */
    ULONG SegFs;                    /* 38 */
    ULONG SegGs;                    /* 3C */
    ULONG SegSs;                    /* 40 */
    ULONG Ebx;                      /* 44 */
    ULONG Ebp;                      /* 48 */
    ULONG Edi;                      /* 4C */
    ULONG Esi;                      /* 50 */
    ULONG Esp;                      /* 54 */
    ULONG Eip;                      /* 58 */
    ULONG EFlags;                   /* 5C */
    ULONG Reserved[4];              /* 60 */
    DECLSPEC_ALIGN(16) UCHAR FxState[512]; /* 70 */
} HALP_WAKE_STATE, *PHALP_WAKE_STATE;

C_ASSERT(FIELD_OFFSET(HALP_WAKE_STATE, Resumed) == 0x00);
C_ASSERT(FIELD_OFFSET(HALP_WAKE_STATE, Cr0) == 0x04);
C_ASSERT(FIELD_OFFSET(HALP_WAKE_STATE, Cr3) == 0x08);
C_ASSERT(FIELD_OFFSET(HALP_WAKE_STATE, Cr4) == 0x0C);
C_ASSERT(FIELD_OFFSET(HALP_WAKE_STATE, Dr0) == 0x10);
C_ASSERT(FIELD_OFFSET(HALP_WAKE_STATE, Dr6) == 0x20);
C_ASSERT(FIELD_OFFSET(HALP_WAKE_STATE, Dr7) == 0x24);
C_ASSERT(FIELD_OFFSET(HALP_WAKE_STATE, Tr) == 0x28);
C_ASSERT(FIELD_OFFSET(HALP_WAKE_STATE, Ldtr) == 0x2A);
C_ASSERT(FIELD_OFFSET(HALP_WAKE_STATE, GdtrBase) == 0x2C);
C_ASSERT(FIELD_OFFSET(HALP_WAKE_STATE, SegDs) == 0x30);
C_ASSERT(FIELD_OFFSET(HALP_WAKE_STATE, SegSs) == 0x40);
C_ASSERT(FIELD_OFFSET(HALP_WAKE_STATE, Ebx) == 0x44);
C_ASSERT(FIELD_OFFSET(HALP_WAKE_STATE, Esp) == 0x54);
C_ASSERT(FIELD_OFFSET(HALP_WAKE_STATE, Eip) == 0x58);
C_ASSERT(FIELD_OFFSET(HALP_WAKE_STATE, EFlags) == 0x5C);
C_ASSERT(FIELD_OFFSET(HALP_WAKE_STATE, FxState) == 0x70);

/* Used by HalpWakeSaveContext in wakestub.S */
HALP_WAKE_STATE HalpWakeState;

#define HALP_WAKE_CR4_PAE       0x00000020
#define HALP_WAKE_CR4_OSFXSR    0x00000200

/* FUNCTIONS *****************************************************************/

FORCEINLINE
VOID
HalpWakeSaveFpuState(
    _Out_ PUCHAR FxState)
{
#if defined(__GNUC__)
    Ke386FxSave((PFX_SAVE_AREA)FxState);
#else
    __fxsave((PFX_SAVE_AREA)FxState);
#endif
}

/* Page tables for the stub: the page directory of the kernel with the low 4MB replaced by the stub */
static
NTSTATUS
HalpWakeSetupTemporaryMappings(
    _Out_ PULONG PageTableRoot)
{
    PHYSICAL_ADDRESS PhysicalAddress;
    PULONG CurrentRoot, RootPageTable, LowPageTable;
    ULONG StartPti, i;

    RootPageTable = (PULONG)((PUCHAR)HalpLowStub + PAGE_SIZE);
    LowPageTable = (PULONG)((PUCHAR)HalpLowStub + 2 * PAGE_SIZE);

    /* Copy the page directory that is in use */
    PhysicalAddress.QuadPart = __readcr3() & ~(PAGE_SIZE - 1);
    CurrentRoot = HalpMapPhysicalMemory64(PhysicalAddress, 1);
    if (CurrentRoot == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlCopyMemory(RootPageTable, CurrentRoot, PAGE_SIZE);
    HalpUnmapVirtualAddress(CurrentRoot, 1);

    /* The first 4MB: only the pages of the stub, mapped to themselves */
    StartPti = (ULONG)(HalpLowStubPhysicalAddress.QuadPart >> PAGE_SHIFT);
    ASSERT(StartPti + HALP_LOW_STUB_SIZE_IN_PAGES < 1024);
    RtlZeroMemory(LowPageTable, PAGE_SIZE);
    for (i = 0; i < HALP_LOW_STUB_SIZE_IN_PAGES; i++)
    {
        LowPageTable[StartPti + i] =
            (ULONG)(HalpLowStubPhysicalAddress.QuadPart + i * PAGE_SIZE) | 0x3;
    }
    RootPageTable[0] =
        (ULONG)(HalpLowStubPhysicalAddress.QuadPart + 2 * PAGE_SIZE) | 0x3;

    *PageTableRoot = (ULONG)(HalpLowStubPhysicalAddress.QuadPart + PAGE_SIZE);
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
    PHALP_WAKE_STUB_DATA Data;
    ULONG PageTableRoot;
    NTSTATUS Status;

    *StubPhysicalAddress = 0;

    /* Real mode: the entry point has to be below 1MB */
    if (HalpLowStub == NULL ||
        HalpLowStubPhysicalAddress.QuadPart == 0 ||
        HalpLowStubPhysicalAddress.QuadPart + HALP_LOW_STUB_SIZE_IN_PAGES * PAGE_SIZE > 0x100000 ||
        (HalpLowStubPhysicalAddress.QuadPart & (PAGE_SIZE - 1)) != 0)
    {
        return STATUS_NOT_SUPPORTED;
    }

    /* No PAE page tables, and the FPU state is saved with FXSAVE */
    if ((__readcr4() & HALP_WAKE_CR4_PAE) || !(__readcr4() & HALP_WAKE_CR4_OSFXSR))
        return STATUS_NOT_SUPPORTED;

    ASSERT((ULONG_PTR)HalpWakeEntry16End - (ULONG_PTR)HalpWakeEntry16 < PAGE_SIZE);

    Status = HalpWakeSetupTemporaryMappings(&PageTableRoot);
    if (!NT_SUCCESS(Status))
        return Status;

    RtlCopyMemory(HalpLowStub,
                  HalpWakeEntry16,
                  (ULONG_PTR)HalpWakeEntry16End - (ULONG_PTR)HalpWakeEntry16);

    Data = (PHALP_WAKE_STUB_DATA)((PUCHAR)HalpLowStub +
                                  ((ULONG_PTR)HalpWakeData - (ULONG_PTR)HalpWakeEntry16));
    Data->Jump32Offset = (ULONG)(ULONG_PTR)HalpWakeEntry32;
    Data->Jump32Selector = KGDT_R0_CODE;
    Data->PageTableRoot = PageTableRoot;
    Data->StatePtr = (ULONG)(ULONG_PTR)&HalpWakeState;
    __sgdt(&Data->GdtrLimit);
    __sidt(&Data->IdtrLimit);

    *StubPhysicalAddress = (ULONG)HalpLowStubPhysicalAddress.QuadPart;
    return STATUS_SUCCESS;
}

/*
 * Save the control registers and the FPU/SSE state, the part of
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
    KDESCRIPTOR Gdtr;

    State->Cr0 = __readcr0();
    State->Cr3 = __readcr3();
    State->Cr4 = __readcr4();
    State->Dr0 = __readdr(0);
    State->Dr1 = __readdr(1);
    State->Dr2 = __readdr(2);
    State->Dr3 = __readdr(3);
    State->Dr6 = __readdr(6);
    State->Dr7 = __readdr(7);
    State->Tr = Ke386GetTr();
    Ke386GetLocalDescriptorTable(&State->Ldtr);
    Ke386GetGlobalDescriptorTable(&Gdtr.Limit);
    State->GdtrBase = Gdtr.Base;
    HalpWakeSaveFpuState(State->FxState);
}
