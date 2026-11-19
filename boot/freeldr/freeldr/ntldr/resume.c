/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Restore serialized x86 hibernation images.
 */

#include <freeldr.h>
#include <reactos/hiberfil.h>
#include "winldr.h"

#include <debug.h>

#define HIBER_MEMORY_CHUNK_SIZE  (64 * 1024)
#if defined(_M_IX86)
#define HIBER_RESTORE_WINDOW     0xFFC10000
#define HIBER_RESTORE_PTE_INDEX  ((HIBER_RESTORE_WINDOW - 0xFFC00000) >> MM_PAGE_SHIFT)
#define HIBER_SPECIAL_REGISTERS_OFFSET 0x2CC
#define HIBER_CR0_PG 0x80000000
#define HIBER_CR0_PE 0x00000001
#define HIBER_CR4_PAE 0x00000020
#define HIBER_EFLAGS_VM 0x00020000
#elif defined(_M_AMD64)
#define HIBER_RESTORE_WINDOW     0xFFFFF78000010000ULL
#define HIBER_CR0_PG 0x80000000
#define HIBER_CR0_PE 0x00000001
#define HIBER_CR4_PAE 0x00000020
#define HIBER_EFLAGS_VM 0x00020000
#define HIBER_AMD64_PHYSICAL_LIMIT (1ULL << 40)
#endif

typedef struct _WINLDR_RESUME_STATE
{
    KPROCESSOR_STATE ProcessorState;
    UCHAR FxState[512];
} WINLDR_RESUME_STATE, *PWINLDR_RESUME_STATE;

#if defined(_M_IX86)
C_ASSERT(sizeof(KPROCESSOR_STATE) == HIBER_SPECIAL_REGISTERS_OFFSET + 0x54);
#elif defined(_M_AMD64)
C_ASSERT(FIELD_OFFSET(KSPECIAL_REGISTERS, Gdtr) == 0x50);
C_ASSERT(FIELD_OFFSET(KSPECIAL_REGISTERS, Tr) == 0x70);
C_ASSERT(FIELD_OFFSET(KSPECIAL_REGISTERS, Cr8) == 0xA0);
C_ASSERT(FIELD_OFFSET(KSPECIAL_REGISTERS, MsrGsBase) == 0xA8);
C_ASSERT(FIELD_OFFSET(KPROCESSOR_STATE, ContextFrame) == 0xE0);
C_ASSERT(FIELD_OFFSET(CONTEXT, SegCs) == 0x38);
C_ASSERT(FIELD_OFFSET(CONTEXT, EFlags) == 0x44);
C_ASSERT(FIELD_OFFSET(CONTEXT, Rax) == 0x78);
C_ASSERT(FIELD_OFFSET(CONTEXT, Rsp) == 0x98);
C_ASSERT(FIELD_OFFSET(CONTEXT, Rip) == 0xF8);
C_ASSERT(FIELD_OFFSET(CONTEXT, FltSave) == 0x100);
C_ASSERT(sizeof(CONTEXT) == 0x4D0);
C_ASSERT(sizeof(KPROCESSOR_STATE) == 0x5B0);
#endif
C_ASSERT(FIELD_OFFSET(WINLDR_RESUME_STATE, FxState) == sizeof(KPROCESSOR_STATE));
C_ASSERT(sizeof(WINLDR_RESUME_STATE) <= HIBERFILE_CPU_STATE_MAX_SIZE);

#if defined(_M_IX86)
extern PHARDWARE_PTE HalPageTable;
#elif defined(_M_AMD64)
BOOLEAN
WinLdrMapHiberChunk(
    _In_ ULONGLONG PhysicalAddress,
    _In_ ULONG Length,
    _In_reads_bytes_(Length) const VOID *Buffer);
#endif

extern VOID NTAPI WinLdrResumeProcessorState(
    _In_ PKPROCESSOR_STATE State,
    _In_ ULONG FpuFlags);

static struct _PO_HIBER_IMAGE_HEADER WinLdrHiberHeader;
static DECLSPEC_ALIGN(16) WINLDR_RESUME_STATE WinLdrResumeState;
static UCHAR WinLdrHiberBuffer[HIBER_MEMORY_CHUNK_SIZE];

#define HIBER_TRAMPOLINE_HAS_FPU 0x01
#define HIBER_TRAMPOLINE_HAS_FX  0x02

static
ULONG
WinLdrHiberCalculateChecksum(
    _In_reads_bytes_(Length) const VOID *Buffer,
    _In_ ULONG Length
)
{
    const ULONG *Words = Buffer;
    ULONG CheckSum = 0;
    ULONG Index;

    for (Index = 0; Index < Length / sizeof(ULONG); Index++)
        CheckSum ^= Words[Index];

    return CheckSum;
}

static
BOOLEAN
WinLdrHiberReadExact(
    _In_ ULONG FileId,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length
)
{
    ULONG BytesRead = 0;
    return (ArcRead(FileId, Buffer, Length, &BytesRead) == ESUCCESS) &&
           (BytesRead == Length);
}

static
BOOLEAN
WinLdrHiberPhysicalRangeValid(
    _In_reads_(MemoryMapEntries) const PAGE_LOOKUP_TABLE_ITEM *MemoryMap,
    _In_ PFN_NUMBER MemoryMapEntries,
    _In_ ULONGLONG PhysicalAddress,
    _In_ ULONG Length
)
{
    ULONGLONG EndAddress;
    PFN_NUMBER FirstPage;
    PFN_NUMBER EndPage;
    PFN_NUMBER Page;

    if (Length == 0 ||
        (PhysicalAddress & (MM_PAGE_SIZE - 1)) != 0 ||
        (Length & (MM_PAGE_SIZE - 1)) != 0)
    {
        return FALSE;
    }

#if defined(_M_IX86)
    if (PhysicalAddress > 0xFFFFFFFFULL ||
        (ULONGLONG)Length > 0x100000000ULL - PhysicalAddress)
    {
        return FALSE;
    }
#elif defined(_M_AMD64)
    if (PhysicalAddress >= HIBER_AMD64_PHYSICAL_LIMIT ||
        (ULONGLONG)Length > HIBER_AMD64_PHYSICAL_LIMIT - PhysicalAddress)
    {
        return FALSE;
    }
#endif

    EndAddress = PhysicalAddress + Length;
    FirstPage = (PFN_NUMBER)(PhysicalAddress >> MM_PAGE_SHIFT);
    EndPage = (PFN_NUMBER)(EndAddress >> MM_PAGE_SHIFT);
    if (EndAddress == 0x100000000ULL)
        EndPage = 0x100000;

    if (EndPage > MemoryMapEntries)
        return FALSE;

    for (Page = FirstPage; Page < EndPage; Page++)
    {
        if (MemoryMap[Page].PageAllocated == LoaderBad ||
            MemoryMap[Page].PageAllocated == LoaderFirmwarePermanent)
        {
            return FALSE;
        }
    }

    return TRUE;
}

static
BOOLEAN
WinLdrHiberValidateProcessorState(
    _In_ const PO_HIBER_IMAGE_HEADER *Header,
    _Out_ PKPROCESSOR_STATE State,
    _In_reads_(MemoryMapEntries) const PAGE_LOOKUP_TABLE_ITEM *MemoryMap,
    _In_ PFN_NUMBER MemoryMapEntries
)
{
    if (Header->CpuStateVersion != HIBERFILE_CPU_STATE_VERSION ||
        Header->CpuStateSize != sizeof(WinLdrResumeState.ProcessorState) ||
        Header->ProcessorCount != 1 ||
        !(Header->Flags & HIBERFILE_FLAG_HAS_CPU_STATE) ||
        !(Header->Flags & HIBERFILE_FLAG_UNIPROCESSOR) ||
#if defined(_M_AMD64)
        (Header->Flags & (HIBERFILE_FLAG_HAS_FPU_STATE |
                          HIBERFILE_FLAG_HAS_FX_STATE)) !=
            (HIBERFILE_FLAG_HAS_FPU_STATE | HIBERFILE_FLAG_HAS_FX_STATE) ||
#endif
        (Header->ResumeFlagPhysicalAddress & (sizeof(ULONG) - 1)) != 0 ||
        ((Header->ResumeFlagPhysicalAddress & (MM_PAGE_SIZE - 1)) >
         MM_PAGE_SIZE - sizeof(ULONG)) ||
        ((Header->Flags & HIBERFILE_FLAG_HAS_FX_STATE) &&
         (!(Header->Flags & HIBERFILE_FLAG_HAS_FPU_STATE) ||
          Header->FxStateOffset != sizeof(WinLdrResumeState.ProcessorState) ||
          Header->FxStateSize != sizeof(WinLdrResumeState.FxState))) ||
        (!(Header->Flags & HIBERFILE_FLAG_HAS_FX_STATE) &&
         (Header->FxStateOffset != 0 || Header->FxStateSize != 0)) ||
        ((Header->Flags & HIBERFILE_FLAG_HAS_FPU_STATE) == 0 &&
         (Header->Flags & HIBERFILE_FLAG_HAS_FX_STATE) != 0))
    {
        return FALSE;
    }

    RtlZeroMemory(&WinLdrResumeState, sizeof(WinLdrResumeState));
    RtlCopyMemory(State,
                  Header->CpuState,
                  sizeof(*State));

    if (Header->Flags & HIBERFILE_FLAG_HAS_FX_STATE)
    {
        RtlCopyMemory(WinLdrResumeState.FxState,
                      Header->CpuState + Header->FxStateOffset,
                      sizeof(WinLdrResumeState.FxState));
    }

#if defined(_M_AMD64)
    if ((State->SpecialRegisters.Cr0 & (HIBER_CR0_PE | HIBER_CR0_PG)) !=
            (HIBER_CR0_PE | HIBER_CR0_PG) ||
        (State->SpecialRegisters.Cr4 & HIBER_CR4_PAE) == 0 ||
        (State->SpecialRegisters.Cr3 & (MM_PAGE_SIZE - 1)) != 0 ||
        (State->ContextFrame.EFlags & HIBER_EFLAGS_VM) != 0 ||
        State->ContextFrame.SegCs != 0x10 ||
        State->ContextFrame.SegSs != 0x18 ||
        State->ContextFrame.Rip < KSEG0_BASE ||
        State->ContextFrame.Rsp < KSEG0_BASE ||
        (ULONG_PTR)State->SpecialRegisters.Gdtr.Base < KSEG0_BASE ||
        (ULONG_PTR)State->SpecialRegisters.Idtr.Base < KSEG0_BASE ||
        State->SpecialRegisters.Tr != 0x40 ||
        (ULONG)(State->SpecialRegisters.Tr & 0xFFF8) + 2 * sizeof(KGDTENTRY64) - 1 >
            State->SpecialRegisters.Gdtr.Limit ||
        (State->SpecialRegisters.Ldtr != 0 &&
         ((State->SpecialRegisters.Ldtr & 0x0004) != 0 ||
          (ULONG)(State->SpecialRegisters.Ldtr & 0xFFF8) +
              sizeof(KGDTENTRY64) - 1 > State->SpecialRegisters.Gdtr.Limit)))
    {
        return FALSE;
    }
#else
    if ((State->SpecialRegisters.Cr0 & (HIBER_CR0_PE | HIBER_CR0_PG)) !=
            (HIBER_CR0_PE | HIBER_CR0_PG) ||
        (State->SpecialRegisters.Cr3 & (MM_PAGE_SIZE - 1)) != 0 ||
        (State->SpecialRegisters.Cr4 & HIBER_CR4_PAE) != 0 ||
        (State->ContextFrame.EFlags & HIBER_EFLAGS_VM) != 0 ||
        State->ContextFrame.SegCs != 0x08 ||
        State->ContextFrame.SegSs != 0x10 ||
        State->ContextFrame.Eip < KSEG0_BASE ||
        State->ContextFrame.Esp < KSEG0_BASE ||
        State->SpecialRegisters.Gdtr.Base < KSEG0_BASE ||
        State->SpecialRegisters.Idtr.Base < KSEG0_BASE ||
        State->SpecialRegisters.Tr == 0 ||
        (State->SpecialRegisters.Tr & 0x0004) != 0 ||
        (ULONG)(State->SpecialRegisters.Tr & 0xFFF8) + sizeof(KGDTENTRY) - 1 >
            State->SpecialRegisters.Gdtr.Limit ||
        (State->SpecialRegisters.Ldtr != 0 &&
         ((State->SpecialRegisters.Ldtr & 0x0004) != 0 ||
          (ULONG)(State->SpecialRegisters.Ldtr & 0xFFF8) +
              sizeof(KGDTENTRY) - 1 > State->SpecialRegisters.Gdtr.Limit)))
    {
        return FALSE;
    }
#endif

    if (!WinLdrHiberPhysicalRangeValid(MemoryMap,
                                      MemoryMapEntries,
                                      State->SpecialRegisters.Cr3,
                                      MM_PAGE_SIZE) ||
        !WinLdrHiberPhysicalRangeValid(
            MemoryMap,
            MemoryMapEntries,
            Header->ResumeFlagPhysicalAddress &
                ~((ULONGLONG)MM_PAGE_SIZE - 1),
            MM_PAGE_SIZE))
    {
        return FALSE;
    }

    return TRUE;
}

static
BOOLEAN
WinLdrHiberValidateImage(
    _In_ ULONG FileId,
    _In_ ULONGLONG FileSize,
    _In_reads_(MemoryMapEntries) const PAGE_LOOKUP_TABLE_ITEM *MemoryMap,
    _In_ PFN_NUMBER MemoryMapEntries
)
{
    LARGE_INTEGER Position;
    ULONGLONG PayloadSize;
    ULONGLONG BytesConsumed = 0;
    ULONGLONG PagesConsumed = 0;
    ULONGLONG PreviousEnd = 0;
    ULONG HeaderCheckSum;

    Position.QuadPart = 0;
    if (ArcSeek(FileId, &Position, SeekAbsolute) != ESUCCESS ||
        !WinLdrHiberReadExact(FileId, &WinLdrHiberHeader, sizeof(WinLdrHiberHeader)))
    {
        return FALSE;
    }

    HeaderCheckSum = WinLdrHiberHeader.CheckSum;
    WinLdrHiberHeader.CheckSum = 0;
    if (WinLdrHiberHeader.Signature != HIBERFILE_SIGNATURE ||
        WinLdrHiberHeader.Version != HIBERFILE_VERSION ||
        WinLdrHiberHeader.HeaderSize != sizeof(WinLdrHiberHeader) ||
        WinLdrHiberHeader.MachineType != HIBERFILE_MACHINE_TYPE ||
        WinLdrHiberHeader.PageSize != MM_PAGE_SIZE ||
        !(WinLdrHiberHeader.Flags & HIBERFILE_FLAG_IMAGE_COMPLETE) ||
        WinLdrHiberCalculateChecksum(&WinLdrHiberHeader,
                                     sizeof(WinLdrHiberHeader)) != HeaderCheckSum ||
        FileSize < sizeof(WinLdrHiberHeader))
    {
        return FALSE;
    }
    WinLdrHiberHeader.CheckSum = HeaderCheckSum;

    PayloadSize =
        ((ULONGLONG)WinLdrHiberHeader.PayloadSizeHigh << 32) |
        WinLdrHiberHeader.PayloadSizeLow;
    if (PayloadSize != FileSize - sizeof(WinLdrHiberHeader) ||
        !WinLdrHiberValidateProcessorState(&WinLdrHiberHeader,
                                           &WinLdrResumeState.ProcessorState,
                                           MemoryMap,
                                           MemoryMapEntries))
    {
        return FALSE;
    }

    while (BytesConsumed < PayloadSize)
    {
        PO_HIBER_MEMORY_CHUNK Chunk;
        ULONGLONG ChunkEnd;
        ULONGLONG ChunkSize;
        ULONGLONG ExpectedCheckSum;

        if (PayloadSize - BytesConsumed < sizeof(Chunk) ||
            !WinLdrHiberReadExact(FileId, &Chunk, sizeof(Chunk)))
        {
            return FALSE;
        }

        BytesConsumed += sizeof(Chunk);
        ChunkSize = Chunk.Length;
        if (ChunkSize == 0 ||
            ChunkSize > HIBER_MEMORY_CHUNK_SIZE ||
            (ChunkSize & (MM_PAGE_SIZE - 1)) != 0 ||
            ChunkSize > PayloadSize - BytesConsumed ||
            Chunk.PhysicalAddress < PreviousEnd ||
#if defined(_M_IX86)
            Chunk.PhysicalAddress > 0xFFFFFFFFULL)
#elif defined(_M_AMD64)
            Chunk.PhysicalAddress >= HIBER_AMD64_PHYSICAL_LIMIT)
#endif
        {
            return FALSE;
        }

        ChunkEnd = Chunk.PhysicalAddress + ChunkSize;
        if (!WinLdrHiberPhysicalRangeValid(MemoryMap,
                                           MemoryMapEntries,
                                           Chunk.PhysicalAddress,
                                           Chunk.Length))
        {
            return FALSE;
        }

        if (!WinLdrHiberReadExact(FileId, WinLdrHiberBuffer, Chunk.Length))
            return FALSE;

        ExpectedCheckSum =
            (ULONG)Chunk.PhysicalAddress ^
            (ULONG)(Chunk.PhysicalAddress >> 32) ^
            Chunk.Length ^
            WinLdrHiberCalculateChecksum(WinLdrHiberBuffer, Chunk.Length);
        if ((ULONG)ExpectedCheckSum != Chunk.CheckSum)
            return FALSE;

        BytesConsumed += ChunkSize;
        PagesConsumed += ChunkSize >> MM_PAGE_SHIFT;
        PreviousEnd = ChunkEnd;
    }

    return (BytesConsumed == PayloadSize) &&
           (PagesConsumed ==
            (((ULONGLONG)WinLdrHiberHeader.PageCountHigh << 32) |
             WinLdrHiberHeader.PageCountLow));
}

static
VOID
WinLdrHiberMapChunk(
    _In_ ULONGLONG PhysicalAddress,
    _In_ ULONG Length
)
{
#if defined(_M_IX86)
    ULONG Page;
    ULONG PageCount = Length >> MM_PAGE_SHIFT;

    for (Page = 0; Page < PageCount; Page++)
    {
        HalPageTable[HIBER_RESTORE_PTE_INDEX + Page].PageFrameNumber =
            (ULONG)((PhysicalAddress >> MM_PAGE_SHIFT) + Page);
        HalPageTable[HIBER_RESTORE_PTE_INDEX + Page].Valid = 1;
        HalPageTable[HIBER_RESTORE_PTE_INDEX + Page].Write = 1;
        HalPageTable[HIBER_RESTORE_PTE_INDEX + Page].CacheDisable = 0;
        HalPageTable[HIBER_RESTORE_PTE_INDEX + Page].WriteThrough = 0;
    }

    __writecr3(__readcr3());
    RtlCopyMemory((PVOID)HIBER_RESTORE_WINDOW, WinLdrHiberBuffer, Length);
#elif defined(_M_AMD64)
    if (!WinLdrMapHiberChunk(PhysicalAddress, Length, WinLdrHiberBuffer))
    {
        UiMessageBox("The hibernation image could not be mapped.");
        for (;;)
        {
        }
    }
#endif
}

static
VOID
WinLdrHiberRestoreImage(
    _In_ ULONG FileId,
    _In_ ULONGLONG PayloadSize
)
{
    LARGE_INTEGER Position;
    ULONGLONG BytesConsumed = 0;

    Position.QuadPart = sizeof(WinLdrHiberHeader);
    if (ArcSeek(FileId, &Position, SeekAbsolute) != ESUCCESS)
        goto Fatal;

    while (BytesConsumed < PayloadSize)
    {
        PO_HIBER_MEMORY_CHUNK Chunk;

        if (PayloadSize - BytesConsumed < sizeof(Chunk) ||
            !WinLdrHiberReadExact(FileId, &Chunk, sizeof(Chunk)))
        {
            goto Fatal;
        }

        BytesConsumed += sizeof(Chunk);
        if (Chunk.Length == 0 ||
            Chunk.Length > HIBER_MEMORY_CHUNK_SIZE ||
            (Chunk.Length & (MM_PAGE_SIZE - 1)) != 0 ||
            Chunk.Length > PayloadSize - BytesConsumed ||
            (Chunk.PhysicalAddress & (MM_PAGE_SIZE - 1)) != 0 ||
#if defined(_M_IX86)
            Chunk.PhysicalAddress > 0xFFFFFFFFULL ||
            Chunk.Length > 0x100000000ULL - Chunk.PhysicalAddress ||
#elif defined(_M_AMD64)
            Chunk.PhysicalAddress >= HIBER_AMD64_PHYSICAL_LIMIT ||
            Chunk.Length > HIBER_AMD64_PHYSICAL_LIMIT - Chunk.PhysicalAddress ||
#endif
            !WinLdrHiberReadExact(FileId, WinLdrHiberBuffer, Chunk.Length))
        {
            goto Fatal;
        }

        if (((ULONG)Chunk.PhysicalAddress ^
             (ULONG)(Chunk.PhysicalAddress >> 32) ^
             Chunk.Length ^
             WinLdrHiberCalculateChecksum(WinLdrHiberBuffer,
                                          Chunk.Length)) != Chunk.CheckSum)
        {
            goto Fatal;
        }

        WinLdrHiberMapChunk(Chunk.PhysicalAddress, Chunk.Length);
        BytesConsumed += Chunk.Length;
    }

    if (BytesConsumed != PayloadSize)
        goto Fatal;

    {
        ULONGLONG FlagPage =
            WinLdrHiberHeader.ResumeFlagPhysicalAddress &
            ~((ULONGLONG)MM_PAGE_SIZE - 1);
        ULONG FlagOffset =
            (ULONG)(WinLdrHiberHeader.ResumeFlagPhysicalAddress &
                    (MM_PAGE_SIZE - 1));

        WinLdrHiberMapChunk(FlagPage, MM_PAGE_SIZE);
        *(volatile ULONG *)((ULONG_PTR)HIBER_RESTORE_WINDOW + FlagOffset) = 1;
    }

    ArcClose(FileId);

    {
        typedef VOID (NTAPI *PRESUME_TRAMPOLINE)(
            PKPROCESSOR_STATE,
            ULONG);
        ULONG FpuFlags = 0;
        PRESUME_TRAMPOLINE Trampoline =
            (PRESUME_TRAMPOLINE)(KSEG0_BASE |
                                 (ULONG_PTR)WinLdrResumeProcessorState);

        if (WinLdrHiberHeader.Flags & HIBERFILE_FLAG_HAS_FPU_STATE)
            FpuFlags |= HIBER_TRAMPOLINE_HAS_FPU;
        if (WinLdrHiberHeader.Flags & HIBERFILE_FLAG_HAS_FX_STATE)
            FpuFlags |= HIBER_TRAMPOLINE_HAS_FX;

        Trampoline((PKPROCESSOR_STATE)(KSEG0_BASE |
                         (ULONG_PTR)&WinLdrResumeState.ProcessorState),
                   FpuFlags);
    }

Fatal:
    UiMessageBox("The hibernation image could not be restored.");
    for (;;)
    {
    }
}

VOID
WinLdrResumeFromHibernation(
    _In_ PCSTR SystemPartition
)
{
    ULONG FileId;
    FILEINFORMATION FileInformation;
    ARC_STATUS ArcStatus;
    PFN_NUMBER MemoryMapEntries;
    PPAGE_LOOKUP_TABLE_ITEM MemoryMap;
    ULONGLONG PayloadSize;

    ArcStatus = FsOpenFile("\\hiberfil.sys",
                           SystemPartition,
                           OpenReadOnly,
                           &FileId);
    if (ArcStatus != ESUCCESS)
        return;

    if (ArcGetFileInformation(FileId, &FileInformation) != ESUCCESS ||
        FileInformation.EndingAddress.QuadPart < 0)
    {
        ArcClose(FileId);
        return;
    }

    MemoryMap = MmGetMemoryMap(&MemoryMapEntries);
    if (MemoryMap == NULL)
    {
        ArcClose(FileId);
        return;
    }

    if (!WinLdrHiberValidateImage(FileId,
                                 (ULONGLONG)FileInformation.EndingAddress.QuadPart,
                                 MemoryMap,
                                 MemoryMapEntries))
    {
        ArcClose(FileId);
        return;
    }

    PayloadSize =
        ((ULONGLONG)WinLdrHiberHeader.PayloadSizeHigh << 32) |
        WinLdrHiberHeader.PayloadSizeLow;
    WinLdrHiberRestoreImage(FileId, PayloadSize);
}