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
#define HIBER_CHUNK_COMPRESSED 0x80000000UL
#define HIBER_CHUNK_LENGTH_MASK 0x7FFFFFFFUL
#define HIBER_LZ_MIN_MATCH 3
#define HIBER_LZ_MAX_MATCH 18
#define HIBER_LZ_OFFSET_MASK 0x0FFF
#define HIBER_LZ_INPUT_CAPACITY (HIBER_MEMORY_CHUNK_SIZE + (HIBER_MEMORY_CHUNK_SIZE / 8) + sizeof(ULONG) + 16)

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
static UCHAR WinLdrHiberEncodedBuffer[HIBER_LZ_INPUT_CAPACITY];

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
WinLdrHiberDecodeLz(
    _In_reads_bytes_(InputLength) const UCHAR *Input,
    _In_ ULONG InputLength,
    _Out_writes_bytes_(OutputCapacity) UCHAR *Output,
    _In_ ULONG OutputCapacity,
    _Out_ PULONG OutputLength
)
{
    ULONG InPos = 0, OutPos = 0;

    if (Input == NULL || Output == NULL || OutputLength == NULL)
        return FALSE;

    while (InPos < InputLength)
    {
        UCHAR Control = Input[InPos++];
        ULONG Token;

        for (Token = 0; Token < 8 && InPos < InputLength; ++Token)
        {
            if (Control & (1u << Token))
            {
                USHORT Encoded;
                ULONG Offset, MatchLength, CopyIndex;

                if (InputLength - InPos < sizeof(Encoded))
                    return FALSE;

                Encoded = (USHORT)(Input[InPos] |
                                   ((USHORT)Input[InPos + 1] << 8));
                InPos += sizeof(Encoded);
                Offset = (Encoded & HIBER_LZ_OFFSET_MASK) + 1;
                MatchLength = (Encoded >> 12) + HIBER_LZ_MIN_MATCH;

                if (Offset > OutPos ||
                    MatchLength > OutputCapacity - OutPos)
                {
                    return FALSE;
                }

                for (CopyIndex = 0; CopyIndex < MatchLength; ++CopyIndex)
                    Output[OutPos + CopyIndex] =
                        Output[OutPos + CopyIndex - Offset];
                OutPos += MatchLength;
            }
            else
            {
                if (OutPos >= OutputCapacity)
                    return FALSE;
                Output[OutPos++] = Input[InPos++];
            }
        }
    }

    *OutputLength = OutPos;
    return TRUE;
}

/*
 * Read and expand one chunk. The high bit of Length selects the LZ extension;
 * its first ULONG is the original byte count and the remaining bytes are the
 * bounded LZ token stream. The checksum is always over the expanded bytes.
 */
static
BOOLEAN
WinLdrHiberDecodeChunk(
    _In_ const PO_HIBER_MEMORY_CHUNK *Chunk,
    _In_ ULONG StoredLength,
    _In_reads_bytes_(StoredLength) const UCHAR *StoredData,
    _Out_ PULONG OriginalLength
)
{
    ULONG Length;

    if (Chunk->Length & HIBER_CHUNK_COMPRESSED)
    {
        if (StoredLength < sizeof(ULONG))
            return FALSE;

        RtlCopyMemory(&Length, StoredData, sizeof(Length));
        if (Length == 0 || Length > HIBER_MEMORY_CHUNK_SIZE ||
            (Length & (MM_PAGE_SIZE - 1)) != 0 ||
            !WinLdrHiberDecodeLz(StoredData + sizeof(ULONG),
                                 StoredLength - sizeof(ULONG),
                                 WinLdrHiberBuffer,
                                 HIBER_MEMORY_CHUNK_SIZE,
                                 OriginalLength) ||
            *OriginalLength != Length)
        {
            return FALSE;
        }
    }
    else
    {
        if (StoredLength == 0 || StoredLength > HIBER_MEMORY_CHUNK_SIZE ||
            (StoredLength & (MM_PAGE_SIZE - 1)) != 0)
        {
            return FALSE;
        }

        RtlCopyMemory(WinLdrHiberBuffer, StoredData, StoredLength);
        *OriginalLength = StoredLength;
    }

    return TRUE;
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
        ULONG ChunkOriginalLength;

        if (PayloadSize - BytesConsumed < sizeof(Chunk) ||
            !WinLdrHiberReadExact(FileId, &Chunk, sizeof(Chunk)))
        {
            return FALSE;
        }

        BytesConsumed += sizeof(Chunk);
        ChunkSize = Chunk.Length & HIBER_CHUNK_LENGTH_MASK;
        if (ChunkSize == 0 ||
            ChunkSize > HIBER_LZ_INPUT_CAPACITY ||
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

        if (!WinLdrHiberReadExact(FileId,
                                  (Chunk.Length & HIBER_CHUNK_COMPRESSED) ?
                                      WinLdrHiberEncodedBuffer : WinLdrHiberBuffer,
                                  (ULONG)ChunkSize))
        {
            return FALSE;
        }

        if (!WinLdrHiberDecodeChunk(&Chunk,
                                    (ULONG)ChunkSize,
                                    (Chunk.Length & HIBER_CHUNK_COMPRESSED) ?
                                        WinLdrHiberEncodedBuffer : WinLdrHiberBuffer,
                                    &ChunkOriginalLength))
        {
            return FALSE;
        }

        ChunkEnd = Chunk.PhysicalAddress + ChunkOriginalLength;
        if (!WinLdrHiberPhysicalRangeValid(MemoryMap,
                                           MemoryMapEntries,
                                           Chunk.PhysicalAddress,
                                           ChunkOriginalLength))
        {
            return FALSE;
        }

        ExpectedCheckSum =
            (ULONG)Chunk.PhysicalAddress ^
            (ULONG)(Chunk.PhysicalAddress >> 32) ^
            ChunkOriginalLength ^
            WinLdrHiberCalculateChecksum(WinLdrHiberBuffer, ChunkOriginalLength);
        if ((ULONG)ExpectedCheckSum != Chunk.CheckSum)
            return FALSE;

        BytesConsumed += ChunkSize;
        PagesConsumed += ChunkOriginalLength >> MM_PAGE_SHIFT;
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
        {
            ULONG StoredLength = Chunk.Length & HIBER_CHUNK_LENGTH_MASK;
            ULONG OriginalLength;

            if (StoredLength == 0 ||
                StoredLength > HIBER_LZ_INPUT_CAPACITY ||
                StoredLength > PayloadSize - BytesConsumed ||
                (Chunk.PhysicalAddress & (MM_PAGE_SIZE - 1)) != 0 ||
#if defined(_M_IX86)
                Chunk.PhysicalAddress > 0xFFFFFFFFULL ||
#elif defined(_M_AMD64)
                Chunk.PhysicalAddress >= HIBER_AMD64_PHYSICAL_LIMIT ||
#endif
                !WinLdrHiberReadExact(
                    FileId,
                    (Chunk.Length & HIBER_CHUNK_COMPRESSED) ?
                        WinLdrHiberEncodedBuffer : WinLdrHiberBuffer,
                    StoredLength) ||
                !WinLdrHiberDecodeChunk(
                    &Chunk,
                    StoredLength,
                    (Chunk.Length & HIBER_CHUNK_COMPRESSED) ?
                        WinLdrHiberEncodedBuffer : WinLdrHiberBuffer,
                    &OriginalLength) ||
#if defined(_M_IX86)
                OriginalLength > 0x100000000ULL - Chunk.PhysicalAddress ||
#elif defined(_M_AMD64)
                OriginalLength > HIBER_AMD64_PHYSICAL_LIMIT - Chunk.PhysicalAddress ||
#endif
                (((ULONG)Chunk.PhysicalAddress ^
                  (ULONG)(Chunk.PhysicalAddress >> 32) ^
                  OriginalLength ^
                  WinLdrHiberCalculateChecksum(WinLdrHiberBuffer,
                                               OriginalLength)) != Chunk.CheckSum))
            {
                goto Fatal;
            }

            WinLdrHiberMapChunk(Chunk.PhysicalAddress, OriginalLength);
            BytesConsumed += StoredLength;
        }
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