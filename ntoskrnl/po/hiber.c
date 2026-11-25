/*
 * PROJECT:   CATV 95 Kernel
 * LICENSE:   GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:   Hibernation support
 * COPYRIGHT: Copyright 1997-2028 Republic of CATV 95 <catv95republic@proton.me>
 */

#include <ntoskrnl.h>
#include <intrin.h>
#define NDEBUG
#include <debug.h>
#include "inbv/logo.h"

#define HIBER_MAINTAG 'Hibe'
#define HIBER_WAKE_TAG 'WbiH'
#define HIBER_MEMORY_CHUNK_SIZE (64 * 1024)
#define HIBER_LZ_HASH_SIZE 4096
#define HIBER_LZ_MAX_MATCH 18
#define HIBER_LZ_MIN_MATCH 3
#define HIBER_LZ_OFFSET_MASK 0x0FFF
#define HIBER_CHUNK_COMPRESSED 0x80000000UL
#define HIBER_CHUNK_LENGTH_MASK 0x7FFFFFFFUL
#define HIBER_MAX_ULONGLONG ((ULONGLONG)~0ULL)
#define HIBER_CR4_PAE 0x00000020

C_ASSERT(sizeof(KPROCESSOR_STATE) + 15 + 512 <= HIBERFILE_CPU_STATE_MAX_SIZE);

PPOP_HIBER_CONTEXT PopHiberContext;
static KSPIN_LOCK PopHiberLock;
static BOOLEAN PopHiberInitialized;
extern PLOADER_PARAMETER_BLOCK KeLoaderBlock;

/* Polled disk writer for the image, see the file for details */
#include "hibdump.inc"

typedef struct _POP_HIBER_SERIALIZATION_CONTEXT
{
    PPOP_HIBER_CONTEXT HiberContext;
    PUCHAR Buffer;
} POP_HIBER_SERIALIZATION_CONTEXT, *PPOP_HIBER_SERIALIZATION_CONTEXT;

static
ULONG
PopHiberCalculateHeaderChecksum(
    _In_ const PO_HIBER_IMAGE_HEADER *Header
)
{
    const ULONG *Words = (const ULONG *)Header;
    ULONG CheckSum = 0;
    ULONG CheckSumIndex = FIELD_OFFSET(PO_HIBER_IMAGE_HEADER, CheckSum) /
                          sizeof(ULONG);
    ULONG Index;

    for (Index = 0; Index < sizeof(*Header) / sizeof(ULONG); Index++)
    {
        if (Index != CheckSumIndex)
            CheckSum ^= Words[Index];
    }

    return CheckSum;
}

static
ULONG
PopHiberCalculateMemoryChecksum(
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

/*
 * Small bounded LZ codec used only for hibernation memory chunks. The stream
 * consists of groups of up to eight tokens, preceded by a control byte. A
 * set control bit denotes a two-byte (offset,length) back-reference; a clear
 * bit denotes one literal byte. The offset is 1..4096 and the match length is
 * 3..18. This deliberately avoids depending on pageable RTL compression
 * entry points while the hibernation path is running.
 */
#if defined(HIBERFILE_SUPPORTS_LZ_CHUNKS)
static
ULONG
PopHiberLzHash(
    _In_reads_bytes_(3) const UCHAR *Input)
{
    ULONG Hash = ((ULONG)Input[0] * 251u) ^
                 ((ULONG)Input[1] * 31u) ^ Input[2];
    return Hash & (HIBER_LZ_HASH_SIZE - 1);
}

static
BOOLEAN
PopHiberLzCompress(
    _In_reads_bytes_(InputLength) const UCHAR *Input,
    _In_ ULONG InputLength,
    _Inout_updates_(HIBER_LZ_HASH_SIZE) PULONG HashTable,
    _Out_writes_bytes_(OutputCapacity) PUCHAR Output,
    _In_ ULONG OutputCapacity,
    _Out_ PULONG OutputLength)
{
    ULONG InPos = 0, OutPos = 0;

    if (Input == NULL || Output == NULL || HashTable == NULL ||
        OutputLength == NULL || InputLength == 0)
        return FALSE;

    RtlFillMemory(HashTable,
                  HIBER_LZ_HASH_SIZE * sizeof(*HashTable),
                  0xFF);

    while (InPos < InputLength)
    {
        ULONG ControlPosition;
        UCHAR Control = 0;
        ULONG Token;

        if (OutPos >= OutputCapacity)
            return FALSE;
        ControlPosition = OutPos++;

        for (Token = 0; Token < 8 && InPos < InputLength; ++Token)
        {
            ULONG MatchPosition = 0xFFFFFFFFUL;
            ULONG MatchLength = 0;
            ULONG Hash = 0;

            if (InPos + HIBER_LZ_MIN_MATCH <= InputLength)
            {
                Hash = PopHiberLzHash(Input + InPos);
                MatchPosition = HashTable[Hash];
                HashTable[Hash] = InPos;

                if (MatchPosition != 0xFFFFFFFFUL &&
                    InPos > MatchPosition &&
                    InPos - MatchPosition <= 4096)
                {
                    ULONG Limit = InputLength - InPos;
                    if (Limit > HIBER_LZ_MAX_MATCH)
                        Limit = HIBER_LZ_MAX_MATCH;
                    while (MatchLength < Limit &&
                           Input[MatchPosition + MatchLength] ==
                           Input[InPos + MatchLength])
                    {
                        ++MatchLength;
                    }
                }
            }

            if (MatchLength >= HIBER_LZ_MIN_MATCH)
            {
                ULONG Offset = InPos - MatchPosition;
                USHORT Encoded;

                if (OutPos > OutputCapacity - 2)
                    return FALSE;
                Encoded = (USHORT)(((Offset - 1) & HIBER_LZ_OFFSET_MASK) |
                         ((MatchLength - HIBER_LZ_MIN_MATCH) << 12));
                Output[OutPos++] = (UCHAR)Encoded;
                Output[OutPos++] = (UCHAR)(Encoded >> 8);

                /* Seed hashes for positions skipped by this match. */
                {
                    ULONG Advance = MatchLength;
                    while (Advance > 1 &&
                           InPos + 1 + HIBER_LZ_MIN_MATCH <= InputLength)
                    {
                        ++InPos;
                        --Advance;
                        HashTable[PopHiberLzHash(Input + InPos)] = InPos;
                    }
                    InPos += Advance;
                }
                Control |= (UCHAR)(1u << Token);
            }
            else
            {
                if (OutPos >= OutputCapacity)
                    return FALSE;
                Output[OutPos++] = Input[InPos++];
            }
        }

        Output[ControlPosition] = Control;
    }

    *OutputLength = OutPos;
    return TRUE;
}

#endif /* HIBERFILE_SUPPORTS_LZ_CHUNKS */

static
NTSTATUS
PopHiberWriteAt(
    _In_ PPOP_HIBER_CONTEXT Context,
    _In_reads_bytes_(Length) const VOID *Buffer,
    _In_ ULONG Length,
    _In_ ULONGLONG Offset
)
{
    IO_STATUS_BLOCK IoStatusBlock;
    LARGE_INTEGER FileOffset;
    NTSTATUS Status;

    if (Context == NULL || Context->LinkFileHandle == NULL ||
        (Length != 0 && Buffer == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (Offset > 0x7FFFFFFFFFFFFFFFULL ||
        (ULONGLONG)Length > 0x7FFFFFFFFFFFFFFFULL - Offset)
    {
        return STATUS_INTEGER_OVERFLOW;
    }

    /* At DISPATCH_LEVEL with interrupts off: no file system, no I/O manager */
    if (Context->DumpStackActive)
        return PopDumpStackWrite(Context->DumpStack, Offset, Buffer, Length);

    FileOffset.QuadPart = (LONGLONG)Offset;
    Status = ZwWriteFile(Context->LinkFileHandle,
                         NULL,
                         NULL,
                         NULL,
                         &IoStatusBlock,
                         (PVOID)Buffer,
                         Length,
                         &FileOffset,
                         NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    if (IoStatusBlock.Information != Length)
        return STATUS_DEVICE_DATA_ERROR;

    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
PopHiberMapPhysicalChunk(
    _In_ PVOID Context,
    _In_ PHYSICAL_ADDRESS PhysicalAddress,
    _In_ ULONG Length,
    _Out_ PVOID *MappedAddress
)
{
    UNREFERENCED_PARAMETER(Context);

    if (MappedAddress == NULL || Length == 0)
        return STATUS_INVALID_PARAMETER;

    *MappedAddress = MmMapIoSpace(PhysicalAddress, Length, MmCached);
    return *MappedAddress ? STATUS_SUCCESS : STATUS_INSUFFICIENT_RESOURCES;
}

static
VOID
NTAPI
PopHiberUnmapPhysicalChunk(
    _In_ PVOID Context,
    _In_ PVOID MappedAddress,
    _In_ ULONG Length
)
{
    UNREFERENCED_PARAMETER(Context);
    MmUnmapIoSpace(MappedAddress, Length);
}

static
NTSTATUS
NTAPI
PopHiberWriteMemoryChunk(
    _In_ PVOID Context,
    _In_ PHYSICAL_ADDRESS PhysicalAddress,
    _In_reads_bytes_(Length) const VOID *MappedAddress,
    _In_ ULONG Length
)
{
    PPOP_HIBER_SERIALIZATION_CONTEXT SerializationContext = Context;
    PPOP_HIBER_CONTEXT HiberContext;
    PO_HIBER_MEMORY_CHUNK ChunkHeader;
    ULONGLONG PayloadSize;
    ULONGLONG ChunkPayloadSize;
    ULONGLONG FileOffset;
    ULONGLONG PageCount;
    ULONG StoredLength = Length;
    PUCHAR StoredBuffer;
    BOOLEAN Compressed = FALSE;
    NTSTATUS Status;

    if (SerializationContext == NULL ||
        SerializationContext->HiberContext == NULL ||
        SerializationContext->Buffer == NULL ||
        MappedAddress == NULL || Length == 0 ||
        Length > HIBER_MEMORY_CHUNK_SIZE ||
        (Length & (PAGE_SIZE - 1)) != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    HiberContext = SerializationContext->HiberContext;
    if (HiberContext->LinkFileHandle == NULL)
        return STATUS_INVALID_PARAMETER;

    RtlCopyMemory(SerializationContext->Buffer, MappedAddress, Length);
    StoredBuffer = SerializationContext->Buffer;

#if defined(HIBERFILE_SUPPORTS_LZ_CHUNKS)
    /*
     * The on-disk chunk extension stores a 32-bit original length before the
     * LZ stream. The high bit of ChunkHeader.Length marks this representation;
     * the low 31 bits contain the stored payload length. The checksum always
     * covers the original uncompressed page bytes. Do not enable this define
     * until the boot/resume image reader understands this chunk encoding.
     */
    if (HiberContext->CompressionWorkspace != NULL &&
        HiberContext->CompressedWriteBuffer != NULL &&
        Length <= HIBER_MEMORY_CHUNK_SIZE)
    {
        ULONG EncodedLength = 0;
        PUCHAR Candidate = HiberContext->CompressedWriteBuffer + sizeof(ULONG);
        ULONG CandidateCapacity = HIBER_MEMORY_CHUNK_SIZE +
                                  (HIBER_MEMORY_CHUNK_SIZE / 8) + 16;

        if (PopHiberLzCompress(SerializationContext->Buffer,
                               Length,
                               (PULONG)HiberContext->CompressionWorkspace,
                               Candidate,
                               CandidateCapacity,
                               &EncodedLength) &&
            EncodedLength + sizeof(ULONG) + 8 < Length)
        {
            *(PULONG)HiberContext->CompressedWriteBuffer = Length;
            StoredBuffer = HiberContext->CompressedWriteBuffer;
            StoredLength = EncodedLength + sizeof(ULONG);
            Compressed = TRUE;
        }
    }
#endif /* HIBERFILE_SUPPORTS_LZ_CHUNKS */

    PageCount = Length >> PAGE_SHIFT;
    if (HiberContext->PagesOut > HIBER_MAX_ULONGLONG - PageCount)
        return STATUS_INTEGER_OVERFLOW;

    PayloadSize = ((ULONGLONG)HiberContext->ImageHeader.PayloadSizeHigh << 32) |
                  HiberContext->ImageHeader.PayloadSizeLow;
    ChunkPayloadSize = sizeof(ChunkHeader) + (ULONGLONG)StoredLength;
    if (PayloadSize > HIBER_MAX_ULONGLONG - ChunkPayloadSize ||
        PayloadSize + ChunkPayloadSize >
            0x7FFFFFFFFFFFFFFFULL - sizeof(HiberContext->ImageHeader))
    {
        return STATUS_INTEGER_OVERFLOW;
    }

    FileOffset = sizeof(HiberContext->ImageHeader) + PayloadSize;
    ChunkHeader.PhysicalAddress = (ULONGLONG)PhysicalAddress.QuadPart;
    ChunkHeader.Length = StoredLength |
                          (Compressed ? HIBER_CHUNK_COMPRESSED : 0);
    ChunkHeader.CheckSum =
        (ULONG)ChunkHeader.PhysicalAddress ^
        (ULONG)(ChunkHeader.PhysicalAddress >> 32) ^
        Length ^
        PopHiberCalculateMemoryChecksum(SerializationContext->Buffer, Length);

    Status = PopHiberWriteAt(HiberContext,
                             &ChunkHeader,
                             sizeof(ChunkHeader),
                             FileOffset);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = PopHiberWriteAt(HiberContext,
                             StoredBuffer,
                             StoredLength,
                             FileOffset + sizeof(ChunkHeader));
    if (!NT_SUCCESS(Status))
        return Status;

    PayloadSize += ChunkPayloadSize;
    HiberContext->ImageHeader.PayloadSizeLow = (ULONG)PayloadSize;
    HiberContext->ImageHeader.PayloadSizeHigh = (ULONG)(PayloadSize >> 32);
    HiberContext->PagesOut += PageCount;
    return STATUS_SUCCESS;
}

static
NTSTATUS
PopHiberBuildSerializableRanges(
    _Outptr_result_buffer_(*SerializableRangeCount) PPHYSICAL_MEMORY_RANGE *SerializableRanges,
    _Out_ PULONG SerializableRangeCount
)
{
    PPHYSICAL_MEMORY_RANGE PhysicalRanges;
    PPHYSICAL_MEMORY_RANGE FilteredRanges;
    PLIST_ENTRY Entry;
    ULONGLONG ExcludedCount = 0;
    ULONG PhysicalRangeCount = 0;
    ULONG FilteredRangeCount = 0;
    ULONG RangeIndex;
    ULONG Capacity;

    *SerializableRanges = NULL;
    *SerializableRangeCount = 0;

    PhysicalRanges = MmGetPhysicalMemoryRanges();
    if (PhysicalRanges == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    while (PhysicalRanges[PhysicalRangeCount].NumberOfBytes.QuadPart != 0)
    {
        if (PhysicalRangeCount == MAXULONG)
        {
            ExFreePool(PhysicalRanges);
            return STATUS_INTEGER_OVERFLOW;
        }
        PhysicalRangeCount++;
    }

    if (PhysicalRangeCount == 0)
    {
        ExFreePool(PhysicalRanges);
        return STATUS_DEVICE_DATA_ERROR;
    }

    if (KeLoaderBlock != NULL)
    {
        for (Entry = KeLoaderBlock->MemoryDescriptorListHead.Flink;
             Entry != &KeLoaderBlock->MemoryDescriptorListHead;
             Entry = Entry->Flink)
        {
            PMEMORY_ALLOCATION_DESCRIPTOR Descriptor =
                CONTAINING_RECORD(Entry, MEMORY_ALLOCATION_DESCRIPTOR, ListEntry);

            if (Descriptor->MemoryType == LoaderFirmwarePermanent)
                ExcludedCount++;
        }
    }

    if (ExcludedCount >= MAXULONG ||
        PhysicalRangeCount > MAXULONG / (ULONG)(ExcludedCount + 1))
    {
        ExFreePool(PhysicalRanges);
        return STATUS_INTEGER_OVERFLOW;
    }
    Capacity = PhysicalRangeCount * (ULONG)(ExcludedCount + 1);
    if (Capacity >= MAXULONG / sizeof(*FilteredRanges))
    {
        ExFreePool(PhysicalRanges);
        return STATUS_INTEGER_OVERFLOW;
    }

    FilteredRanges = ExAllocatePoolWithTag(NonPagedPool,
                                           (Capacity + 1) * sizeof(*FilteredRanges),
                                           HIBER_MAINTAG);
    if (FilteredRanges == NULL)
    {
        ExFreePool(PhysicalRanges);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    for (RangeIndex = 0; RangeIndex < PhysicalRangeCount; RangeIndex++)
    {
        ULONGLONG Start = (ULONGLONG)PhysicalRanges[RangeIndex].BaseAddress.QuadPart;
        ULONGLONG End;
        ULONGLONG Cursor;

        if (PhysicalRanges[RangeIndex].NumberOfBytes.QuadPart <= 0 ||
            (ULONGLONG)PhysicalRanges[RangeIndex].NumberOfBytes.QuadPart >
                HIBER_MAX_ULONGLONG - Start)
        {
            ExFreePool(FilteredRanges);
            ExFreePool(PhysicalRanges);
            return STATUS_INTEGER_OVERFLOW;
        }

        End = Start + (ULONGLONG)PhysicalRanges[RangeIndex].NumberOfBytes.QuadPart;
        Cursor = Start;

        if (KeLoaderBlock != NULL)
        {
            for (Entry = KeLoaderBlock->MemoryDescriptorListHead.Flink;
                 Entry != &KeLoaderBlock->MemoryDescriptorListHead;
                 Entry = Entry->Flink)
            {
                PMEMORY_ALLOCATION_DESCRIPTOR Descriptor =
                    CONTAINING_RECORD(Entry, MEMORY_ALLOCATION_DESCRIPTOR, ListEntry);
                ULONGLONG ExcludedStart;
                ULONGLONG ExcludedEnd;

                if (Descriptor->MemoryType != LoaderFirmwarePermanent)
                    continue;

                ExcludedStart = (ULONGLONG)Descriptor->BasePage << PAGE_SHIFT;
                ExcludedEnd = ExcludedStart +
                              ((ULONGLONG)Descriptor->PageCount << PAGE_SHIFT);
                if (ExcludedEnd <= Cursor || ExcludedStart >= End)
                    continue;

                if (ExcludedStart > Cursor)
                {
                    FilteredRanges[FilteredRangeCount].BaseAddress.QuadPart = Cursor;
                    FilteredRanges[FilteredRangeCount].NumberOfBytes.QuadPart =
                        ExcludedStart - Cursor;
                    FilteredRangeCount++;
                }

                if (ExcludedEnd > Cursor)
                    Cursor = ExcludedEnd;
                if (Cursor >= End)
                    break;
            }
        }

        if (Cursor < End)
        {
            FilteredRanges[FilteredRangeCount].BaseAddress.QuadPart = Cursor;
            FilteredRanges[FilteredRangeCount].NumberOfBytes.QuadPart = End - Cursor;
            FilteredRangeCount++;
        }
    }

    ExFreePool(PhysicalRanges);
    if (FilteredRangeCount == 0)
    {
        ExFreePool(FilteredRanges);
        return STATUS_DEVICE_DATA_ERROR;
    }

    RtlZeroMemory(&FilteredRanges[FilteredRangeCount],
                  sizeof(*FilteredRanges));
    *SerializableRanges = FilteredRanges;
    *SerializableRangeCount = FilteredRangeCount;
    return STATUS_SUCCESS;
}

static
NTSTATUS
PopHiberSerializeMemory(
    _Inout_ PPOP_HIBER_CONTEXT Context
)
{
    PPHYSICAL_MEMORY_RANGE Ranges;
    POP_HIBER_SERIALIZATION_CONTEXT SerializationContext;
    ULONGLONG PagesSerialized;
    ULONGLONG BytesSerialized;
    ULONG RangeCount;
    NTSTATUS Status;

    Status = PopHiberBuildSerializableRanges(&Ranges, &RangeCount);
    if (!NT_SUCCESS(Status))
        return Status;

    Context->NoRanges = RangeCount;
    Context->PagesOut = 0;
    SerializationContext.HiberContext = Context;
    SerializationContext.Buffer =
        ExAllocatePoolWithTag(NonPagedPool,
                              HIBER_MEMORY_CHUNK_SIZE,
                              HIBER_MAINTAG);
    if (SerializationContext.Buffer == NULL)
    {
        ExFreePool(Ranges);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

#if defined(HIBERFILE_SUPPORTS_LZ_CHUNKS)
    if (Context->CompressionWorkspace == NULL)
    {
        Context->CompressionWorkspace =
            ExAllocatePoolWithTag(NonPagedPool,
                                  HIBER_LZ_HASH_SIZE * sizeof(ULONG),
                                  HIBER_MAINTAG);
    }
    if (Context->CompressedWriteBuffer == NULL)
    {
        Context->CompressedWriteBuffer =
            ExAllocatePoolWithTag(NonPagedPool,
                                  HIBER_MEMORY_CHUNK_SIZE +
                                  (HIBER_MEMORY_CHUNK_SIZE / 8) +
                                  sizeof(ULONG) + 16,
                                  HIBER_MAINTAG);
    }
#endif /* HIBERFILE_SUPPORTS_LZ_CHUNKS */

    /* Compression allocation failure leaves raw chunk I/O valid. */
    Status = MiSerializePhysicalMemory(Ranges,
                                      RangeCount,
                                      HIBER_MEMORY_CHUNK_SIZE,
                                      PopHiberMapPhysicalChunk,
                                      PopHiberUnmapPhysicalChunk,
                                      PopHiberWriteMemoryChunk,
                                      &SerializationContext,
                                      &PagesSerialized,
                                      &BytesSerialized);
    ExFreePool(SerializationContext.Buffer);
    ExFreePool(Ranges);
    if (!NT_SUCCESS(Status))
        return Status;

    if (PagesSerialized == 0 || PagesSerialized != Context->PagesOut)
        return STATUS_DEVICE_DATA_ERROR;

    Context->ImageHeader.PageCountLow = (ULONG)PagesSerialized;
    Context->ImageHeader.PageCountHigh = (ULONG)(PagesSerialized >> 32);
    Context->ImageHeader.Flags |= HIBERFILE_FLAG_IMAGE_COMPLETE;
    return PopHiberWriteImageHeader(Context);
}

static
VOID
PopInitializeHiberContext(
    _Inout_ PPOP_HIBER_CONTEXT Context)
{
    RtlZeroMemory(Context, sizeof(*Context));
    Context->Lock = &PopHiberLock;
    Context->WriteToFile = TRUE;
    InitializeListHead(&Context->ClonedRanges);
}

/* Initialize hibernation state during phase-0 power-manager startup. */
NTSTATUS
NTAPI
PopHiberInitSystem(
    VOID
)
{
    if (PopHiberInitialized)
        return STATUS_SUCCESS;

    KeInitializeSpinLock(&PopHiberLock);
    PopHiberInitialized = TRUE;

    PopHiberContext = ExAllocatePoolWithTag(NonPagedPool,
                                            sizeof(POP_HIBER_CONTEXT),
                                            HIBER_MAINTAG);
    if (PopHiberContext == NULL)
    {
        PopHiberInitialized = FALSE;
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    PopInitializeHiberContext(PopHiberContext);
    PopCapabilities.HiberFilePresent = FALSE;

    return STATUS_SUCCESS;
}

#if defined(_M_IX86) || defined(_M_AMD64)
/*
 * Called on the resumed system, right after RtlCaptureContext returned for
 * the second time. The processor is in whatever state the resume stub left
 * it in. The state saved before hibernating is in the image header, which
 * comes back with the rest of memory. Put it back in the processor, including
 * the parts that are not in the saved context (MSRs, FPU/SSE state).
 */
static
NTSTATUS
PopHiberRestoreProcessorState(
    _Inout_ PPOP_HIBER_CONTEXT Context
)
{
    PO_HIBER_IMAGE_HEADER *Header = &Context->ImageHeader;
    DECLSPEC_ALIGN(16) UCHAR FxStateBuffer[512];
    BOOLEAN Interrupts;
    ULONG Cr0;

    if (!(Header->Flags & HIBERFILE_FLAG_HAS_CPU_STATE) ||
        Header->CpuStateVersion != HIBERFILE_CPU_STATE_VERSION ||
        Header->CpuStateSize != sizeof(*Context->WakeState) ||
        Context->WakeState == NULL)
    {
        return STATUS_DEVICE_DATA_ERROR;
    }

    if ((Header->Flags & HIBERFILE_FLAG_HAS_FX_STATE) &&
        (Header->FxStateSize != sizeof(FxStateBuffer) ||
         Header->FxStateOffset > HIBERFILE_CPU_STATE_MAX_SIZE ||
         Header->FxStateSize >
             HIBERFILE_CPU_STATE_MAX_SIZE - Header->FxStateOffset))
    {
        return STATUS_DEVICE_DATA_ERROR;
    }

    /* The header is not suitably aligned for a KPROCESSOR_STATE */
    RtlCopyMemory(Context->WakeState, Header->CpuState, sizeof(*Context->WakeState));

    Interrupts = KeDisableInterrupts();

    KiRestoreProcessorControlState(Context->WakeState);
    KiRestoreProcessorMsrState();

    if (Header->Flags & HIBERFILE_FLAG_HAS_FPU_STATE)
    {
        Cr0 = __readcr0();
        __writecr0(Cr0 & ~(CR0_MP | CR0_EM | CR0_TS));

        if (Header->Flags & HIBERFILE_FLAG_HAS_FX_STATE)
        {
            RtlCopyMemory(FxStateBuffer,
                          Header->CpuState + Header->FxStateOffset,
                          sizeof(FxStateBuffer));
#if defined(_M_AMD64)
            _fxrstor64(FxStateBuffer);
#else
            Ke386RestoreFpuState((PFX_SAVE_AREA)FxStateBuffer);
#endif
        }
#if defined(_M_IX86)
        else
        {
            /* Pre-FXSR CPUs: the FNSAVE image is in the saved context */
            Ke386RestoreFpuState(
                (PFX_SAVE_AREA)&Context->WakeState->ContextFrame.FloatSave);
        }
#endif

        __writecr0(Cr0);
    }

    KeRestoreInterrupts(Interrupts);
    return STATUS_SUCCESS;
}
#endif

NTSTATUS
NTAPI
PopHiberSystemHandler(
    _In_ PVOID SystemContext
)
{
#if !defined(_M_IX86) && !defined(_M_AMD64)
    UNREFERENCED_PARAMETER(SystemContext);
    return STATUS_NOT_SUPPORTED;
#else
    PPOP_HIBER_CONTEXT Context = SystemContext;
    BOOLEAN Interrupts;
    BOOLEAN UseDumpStack;
    KIRQL OldIrql;
    NTSTATUS Status, FlushStatus;

    if (Context == NULL)
        return STATUS_INVALID_PARAMETER;

    Context->Status = STATUS_SUCCESS;
    RtlZeroMemory(&Context->ImageHeader, sizeof(Context->ImageHeader));
    Context->PagesOut = 0;
    Context->NoRanges = 0;
    Context->HiberMetadataSaved = FALSE;
    Context->HiberStatusScreenInitialized = FALSE;
    Context->ResumeFlag = 0;
    if (KeNumberProcessors != 1)
        return STATUS_NOT_SUPPORTED;

    if (Context->WakeState == NULL)
    {
        Context->WakeState = ExAllocatePoolWithTag(NonPagedPool,
                                                   sizeof(KPROCESSOR_STATE),
                                                   HIBER_WAKE_TAG);
        if (Context->WakeState == NULL)
            return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Context->WakeState, sizeof(*Context->WakeState));

    /*
     * Try to get a dump stack for the hibernation file. With one, the whole
     * image is written at DISPATCH_LEVEL with interrupts off, so memory does
     * not change while it is saved. Without one (no legacy IDE disk, ...)
     * the image is written through the file system at PASSIVE_LEVEL, as
     * before. This has to be done now, it needs PASSIVE_LEVEL.
     */
    UseDumpStack = NT_SUCCESS(PopDumpStackInitialize(Context));

    /*
     * Capture the processor and FPU state at DISPATCH_LEVEL with interrupts
     * off: no thread switch, DPC or interrupt handler may change the
     * registers between the capture, the test of ResumeFlag below and the
     * copy into the image header. The IRQL is part of the captured state
     * (CR8 on amd64), so the resumed system comes back at DISPATCH_LEVEL too.
     *
     * With a dump stack IRQL and interrupts stay like this until the image
     * is written. Without one they are put back before, since ZwWriteFile
     * needs PASSIVE_LEVEL and working disk interrupts.
     *
     * The captured context has IF clear, so the resumed system also arrives
     * here with interrupts off and leaves at the same place.
     */
    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    Interrupts = KeDisableInterrupts();

    /*
     * The control state is saved once, before the context. The context is
     * captured here and not in a helper such as KeSaveStateForHibernate: the
     * resumed system continues right after the capture, and the frame of a
     * helper that has returned is long gone by the time the image is taken
     * (the calls below reuse that part of the stack), so returning from it
     * would use a clobbered return address. This frame is alive until the
     * function returns.
     */
    KiSaveProcessorControlState(Context->WakeState);
    RtlCaptureContext(&Context->WakeState->ContextFrame);

    /*
     * On the resumed system execution continues right here. Nothing below
     * may run again: it would overwrite the CPU state that was stored in the
     * image header before hibernating with the one of the resumed machine.
     */
    if (Context->ResumeFlag)
    {
        /*
         * The image was taken while the dump stack was active. On this
         * machine the disk is not in the state it expects, and the stack
         * (allocated before the image was taken) is no longer needed.
         */
        Context->DumpStackActive = FALSE;
        PopDumpStackFree(Context);

        Status = PopHiberRestoreProcessorState(Context);
        KeRestoreInterrupts(Interrupts);
        KeLowerIrql(OldIrql);
        return Status;
    }

#if defined(_M_IX86)
    if (Context->WakeState->SpecialRegisters.Cr4 & HIBER_CR4_PAE)
    {
        KeRestoreInterrupts(Interrupts);
        KeLowerIrql(OldIrql);
        PopDumpStackFree(Context);
        return STATUS_NOT_SUPPORTED;
    }
#endif

    Context->ImageHeader.CpuStateVersion = HIBERFILE_CPU_STATE_VERSION;
    Context->ImageHeader.CpuStateSize = sizeof(*Context->WakeState);
    Context->ImageHeader.ProcessorCount = 1;
    Context->ImageHeader.Flags |= HIBERFILE_FLAG_HAS_CPU_STATE |
                                  HIBERFILE_FLAG_UNIPROCESSOR;
#if defined(_M_AMD64)
    if (KeI386NpxPresent)
    {
        ULONG Cr0;
        PUCHAR FxState;
        DECLSPEC_ALIGN(16) UCHAR FxStateBuffer[512];

        Context->ImageHeader.Flags |= HIBERFILE_FLAG_HAS_FPU_STATE |
                                      HIBERFILE_FLAG_HAS_FX_STATE;
        FxState = Context->ImageHeader.CpuState +
                  sizeof(*Context->WakeState);
        Context->ImageHeader.FxStateOffset =
            (ULONG)(FxState - Context->ImageHeader.CpuState);
        Context->ImageHeader.FxStateSize = sizeof(FxStateBuffer);

        Cr0 = __readcr0();
        __writecr0(Cr0 & ~(CR0_MP | CR0_EM | CR0_TS));
        _fxsave64(FxStateBuffer);
        __writecr0(Cr0);
        RtlCopyMemory(FxState, FxStateBuffer, sizeof(FxStateBuffer));
    }
#else
    if (KeI386NpxPresent)
    {
        ULONG Cr0;

        Context->ImageHeader.Flags |= HIBERFILE_FLAG_HAS_FPU_STATE;
        Cr0 = __readcr0();
        __writecr0(Cr0 & ~(CR0_MP | CR0_EM | CR0_TS));

        if (KeI386FxsrPresent)
        {
            PUCHAR FxState;
            DECLSPEC_ALIGN(16) UCHAR FxStateBuffer[512];

            FxState = Context->ImageHeader.CpuState +
                      sizeof(*Context->WakeState);
            Context->ImageHeader.FxStateOffset =
                (ULONG)(FxState - Context->ImageHeader.CpuState);
            Context->ImageHeader.FxStateSize = 512;
            Context->ImageHeader.Flags |= HIBERFILE_FLAG_HAS_FX_STATE;
            Ke386SaveFpuState((PFX_SAVE_AREA)FxStateBuffer);
            RtlCopyMemory(FxState, FxStateBuffer, sizeof(FxStateBuffer));
        }
        else
        {
            Ke386SaveFpuState(
                (PFX_SAVE_AREA)&Context->WakeState->ContextFrame.FloatSave);
            Ke386RestoreFpuState(
                (PFX_SAVE_AREA)&Context->WakeState->ContextFrame.FloatSave);
        }

        __writecr0(Cr0);
    }
#endif

    Context->ImageHeader.ResumeFlagPhysicalAddress =
        MmGetPhysicalAddress((PVOID)&Context->ResumeFlag).QuadPart;
    RtlCopyMemory(Context->ImageHeader.CpuState,
                  Context->WakeState,
                  sizeof(*Context->WakeState));

    if (UseDumpStack)
    {
        /* Everything from here on is written by polling, at this IRQL */
        Context->DumpStackActive = TRUE;
    }
    else
    {
        /* The state is complete; the rest is file I/O at PASSIVE_LEVEL */
        KeRestoreInterrupts(Interrupts);
        KeLowerIrql(OldIrql);
    }

    Status = PopSaveHiberContextState(Context);
    if (NT_SUCCESS(Status))
        Status = PopHiberSerializeMemory(Context);

    if (UseDumpStack)
    {
        FlushStatus = PopDumpStackFlush(Context->DumpStack);
        Context->DumpStackActive = FALSE;
        KeRestoreInterrupts(Interrupts);
        KeLowerIrql(OldIrql);
        PopDumpStackFree(Context);

        if (NT_SUCCESS(Status))
            Status = FlushStatus;
    }

    if (NT_SUCCESS(Status))
        PopHiberUpdateComplete(100);

    return Status;
#endif
}

/*
 * Save the metadata required to identify and resume a hibernation image,
 * then initialize the progress display.  Commit the header before showing
 * the UI or starting memory serialization.
 */
NTSTATUS
NTAPI
PopSaveHiberContextState(
    _Inout_ PPOP_HIBER_CONTEXT Context
)
{
    TIME_FIELDS RtcTime;
    LARGE_INTEGER ValidatedTime;
    NTSTATUS Status;

    if (Context == NULL || Context->LinkFileHandle == NULL)
        return STATUS_INVALID_PARAMETER;

    if (!Context->HiberMetadataSaved)
    {
        RtlZeroMemory(&Context->ImageHeader.RtcTime,
                      sizeof(Context->ImageHeader.RtcTime));
        Context->ImageHeader.Flags &= ~HIBERFILE_FLAG_HAS_RTC_TIME;

        if (!HalQueryRealTimeClock(&RtcTime))
            return STATUS_UNSUCCESSFUL;

        if (!RtlTimeFieldsToTime(&RtcTime, &ValidatedTime))
            return STATUS_DEVICE_DATA_ERROR;

        Context->ImageHeader.RtcTime.Year = RtcTime.Year;
        Context->ImageHeader.RtcTime.Month = RtcTime.Month;
        Context->ImageHeader.RtcTime.Day = RtcTime.Day;
        Context->ImageHeader.RtcTime.Weekday = RtcTime.Weekday;
        Context->ImageHeader.RtcTime.Hour = RtcTime.Hour;
        Context->ImageHeader.RtcTime.Minute = RtcTime.Minute;
        Context->ImageHeader.RtcTime.Second = RtcTime.Second;
        Context->ImageHeader.RtcTime.Milliseconds = RtcTime.Milliseconds;
        Context->ImageHeader.Flags |= HIBERFILE_FLAG_HAS_RTC_TIME;

        Status = PopHiberWriteImageHeader(Context);
        if (!NT_SUCCESS(Status))
        {
            Context->ImageHeader.Flags &= ~HIBERFILE_FLAG_HAS_RTC_TIME;
            RtlZeroMemory(&Context->ImageHeader.RtcTime,
                          sizeof(Context->ImageHeader.RtcTime));
            return Status;
        }

        Context->HiberMetadataSaved = TRUE;
    }

    if (!Context->HiberStatusScreenInitialized)
    {
        DisplayHibernationScreen();
        Context->HiberStatusScreenInitialized = TRUE;
    }

    Context->HiberPercentComplete = 0;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
PopHiberWriteImageHeader(
    _Inout_ PPOP_HIBER_CONTEXT Context
)
{
    IO_STATUS_BLOCK IoStatusBlock;
    LARGE_INTEGER FileOffset;
    NTSTATUS Status;

    if (Context == NULL || Context->LinkFileHandle == NULL)
        return STATUS_INVALID_PARAMETER;

    Context->ImageHeader.Signature = HIBERFILE_SIGNATURE;
    Context->ImageHeader.Version = HIBERFILE_VERSION;
    Context->ImageHeader.HeaderSize = sizeof(Context->ImageHeader);
    Context->ImageHeader.MachineType = HIBERFILE_MACHINE_TYPE;
    Context->ImageHeader.PageSize = PAGE_SIZE;
    Context->ImageHeader.CheckSum = 0;
    Context->ImageHeader.CheckSum =
        PopHiberCalculateHeaderChecksum(&Context->ImageHeader);

    if (Context->DumpStackActive)
    {
        return PopDumpStackWrite(Context->DumpStack,
                                 0,
                                 &Context->ImageHeader,
                                 sizeof(Context->ImageHeader));
    }

    FileOffset.QuadPart = 0;
    Status = ZwWriteFile(Context->LinkFileHandle,
                         NULL,
                         NULL,
                         NULL,
                         &IoStatusBlock,
                         &Context->ImageHeader,
                         sizeof(Context->ImageHeader),
                         &FileOffset,
                         NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    if (IoStatusBlock.Information != sizeof(Context->ImageHeader))
        return STATUS_DEVICE_DATA_ERROR;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
PopHiberFinished(
    _In_ NTSTATUS Status
)
{
    if (PopHiberContext)
        PopHiberContext->Status = Status;

    if (Status == STATUS_DEVICE_DOES_NOT_EXIST)
    {
        DPRINT1("Hibernation failed: power-off device does not exist (0x%08lx)\n",
                Status);
        if (InbvBootDriverInstalled)
        {
            PUCHAR Logo1,Logo2;
            /* Get safe to shutdown bitmap */
            Logo1 = InbvGetResourceAddress(IDB_SHUTDOWN_MSG);
            if (SharedUserData->NtProductType == NtProductWinNt)
            {
                /* Professional */
                Logo2 = InbvGetResourceAddress(IDB_SHUTDOWN_LOGO_PRO);
            }
            else
            {
                /* Server */
                Logo2 = InbvGetResourceAddress(IDB_SHUTDOWN_LOGO_SERVER);
            }
        
            if ((Logo1) && (Logo2))
            {
                InbvBitBlt(Logo1, VID_SHUTDOWN_MSG_LEFT, VID_SHUTDOWN_MSG_TOP);
                InbvBitBlt(Logo2, VID_SHUTDOWN_LOGO_LEFT, VID_SHUTDOWN_LOGO_TOP);
            }
        }
        else
        {
            InbvDisplayString("REPUBLIC OF CATV 95\r\n");
            InbvDisplayString("State saved, power off the system\r\n");
        }

        /* Wait for user to power off */
        for (;;)
        {
        }
    }

    /* Continue with power-off operation */
    return Status;
}

VOID
NTAPI
PopHiberUpdateComplete(
    _In_ ULONG Percentage
)
{
    CHAR HiberBuf[16];

    if (PopHiberContext == NULL ||
        !PopHiberContext->HiberStatusScreenInitialized)
    {
        return;
    }

    if (Percentage > 100)
        Percentage = 100;

    PopHiberContext->HiberPercentComplete = Percentage;
    if (InbvBootDriverInstalled)
    {
        /* Update progress bar */
        InbvUpdateProgressBar(Percentage);
    }
    else
    {
        RtlStringCbPrintfA(HiberBuf,
                           sizeof(HiberBuf),
                           "REPUBLIC OF CATV 95\r\nPopSave: %lu%%\n",
                           (Percentage));
        InbvDisplayString(HiberBuf);
    }
}

NTSTATUS
NTAPI
PopHiberOpenFile(
    _In_ ULONG64 Size
)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    UNICODE_STRING FileName = RTL_CONSTANT_STRING(L"\\hiberfil.sys");
    FILE_END_OF_FILE_INFORMATION EndOfFile;
    NTSTATUS Status;
    HANDLE FileHandle;
    BOOLEAN NewContext = FALSE;

    if (PopHiberContext != NULL && PopHiberContext->LinkFileHandle != NULL)
    {
        PopCapabilities.HiberFilePresent = TRUE;
        PopCapabilities.SystemS4 = TRUE;
        return STATUS_SUCCESS;
    }

    if (PopHiberContext == NULL)
    {
        PopHiberContext = ExAllocatePoolWithTag(NonPagedPool,
                                                sizeof(POP_HIBER_CONTEXT),
                                                HIBER_MAINTAG);
        if (PopHiberContext == NULL)
            return STATUS_INSUFFICIENT_RESOURCES;

        if (!PopHiberInitialized)
        {
            KeInitializeSpinLock(&PopHiberLock);
            PopHiberInitialized = TRUE;
        }
        PopInitializeHiberContext(PopHiberContext);
        NewContext = TRUE;
    }

    InitializeObjectAttributes(&ObjectAttributes,
                               &FileName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    Status = ZwCreateFile(&FileHandle,
                          FILE_READ_DATA | FILE_WRITE_DATA | SYNCHRONIZE,
                          &ObjectAttributes,
                          &IoStatusBlock,
                          NULL,
                          FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM,
                          FILE_SHARE_READ,
                          FILE_SUPERSEDE,
                          FILE_SYNCHRONOUS_IO_NONALERT,
                          NULL,
                          0);
    if (!NT_SUCCESS(Status))
    {
        if (NewContext)
        {
            ExFreePoolWithTag(PopHiberContext, HIBER_MAINTAG);
            PopHiberContext = NULL;
        }
        return Status;
    }

    PopHiberContext->LinkFileHandle = FileHandle;

    if (Size != 0)
    {
        EndOfFile.EndOfFile.QuadPart = (LONGLONG)Size;
        Status = ZwSetInformationFile(FileHandle,
                                      &IoStatusBlock,
                                      &EndOfFile,
                                      sizeof(EndOfFile),
                                      FileEndOfFileInformation);
        if (!NT_SUCCESS(Status))
        {
            ZwClose(FileHandle);
            PopHiberContext->LinkFileHandle = NULL;
            if (NewContext)
            {
                ExFreePoolWithTag(PopHiberContext, HIBER_MAINTAG);
                PopHiberContext = NULL;
            }
            return Status;
        }
    }

    PopCapabilities.HiberFilePresent = TRUE;
    PopCapabilities.SystemS4 = TRUE;
    return STATUS_SUCCESS;
}

VOID
NTAPI
PopHiberCloseFile(
    VOID
)
{
    if (PopHiberContext == NULL)
    {
        PopCapabilities.HiberFilePresent = FALSE;
        PopCapabilities.SystemS4 = FALSE;
        return;
    }

    if (PopHiberContext->LinkFileHandle)
    {
        ZwClose(PopHiberContext->LinkFileHandle);
        PopHiberContext->LinkFileHandle = NULL;
    }

    PopCapabilities.HiberFilePresent = FALSE;
    PopCapabilities.SystemS4 = FALSE;
}

VOID
NTAPI
PopHiberReadPolicy(
    _Out_ PULONG Enabled,
    _Out_ PULONG Percent
)
{
    RTL_QUERY_REGISTRY_TABLE QueryTable[3];
    ULONG HiberEnabled = 1;
    ULONG HiberPercent = 40;

    RtlZeroMemory(QueryTable, sizeof(QueryTable));
    QueryTable[0].Name = L"HiberFileEnabled";
    QueryTable[0].EntryContext = &HiberEnabled;
    QueryTable[0].DefaultType = REG_DWORD;
    QueryTable[0].DefaultData = &HiberEnabled;
    QueryTable[0].DefaultLength = sizeof(HiberEnabled);
    QueryTable[1].Name = L"HiberFileSizePercent";
    QueryTable[1].EntryContext = &HiberPercent;
    QueryTable[1].DefaultType = REG_DWORD;
    QueryTable[1].DefaultData = &HiberPercent;
    QueryTable[1].DefaultLength = sizeof(HiberPercent);

    RtlQueryRegistryValues(RTL_REGISTRY_CONTROL,
                           L"Session Manager\\Power",
                           QueryTable,
                           NULL,
                           NULL);

    *Enabled = HiberEnabled ? 1 : 0;
    *Percent = HiberPercent > 100 ? 100 : HiberPercent;
}

VOID
NTAPI
PopHiberInvalidate(
    VOID
)
{
    if (PopHiberContext == NULL)
        return;

    if (PopHiberContext->MemoryImage)
        RtlZeroMemory(PopHiberContext->MemoryImage, sizeof(PO_MEMORY_IMAGE));

    if (PopHiberContext->CompressionWorkspace)
    {
        ExFreePool(PopHiberContext->CompressionWorkspace);
        PopHiberContext->CompressionWorkspace = NULL;
    }

    if (PopHiberContext->CompressedWriteBuffer)
    {
        ExFreePool(PopHiberContext->CompressedWriteBuffer);
        PopHiberContext->CompressedWriteBuffer = NULL;
    }

    PopHiberContext->Status = STATUS_UNSUCCESSFUL;
}

VOID
NTAPI
PopHiberRelease(
    VOID
)
{
    PLIST_ENTRY Entry;

    if (PopHiberContext == NULL)
        return;

    PopHiberInvalidate();

    while (!IsListEmpty(&PopHiberContext->ClonedRanges))
    {
        Entry = RemoveHeadList(&PopHiberContext->ClonedRanges);
        UNREFERENCED_PARAMETER(Entry);
    }

    if (PopHiberContext->MemoryImage)
    {
        ExFreePool(PopHiberContext->MemoryImage);
        PopHiberContext->MemoryImage = NULL;
    }

    if (PopHiberContext->WakeState)
    {
        ExFreePool(PopHiberContext->WakeState);
        PopHiberContext->WakeState = NULL;
    }

    PopHiberCloseFile();
    ExFreePoolWithTag(PopHiberContext, HIBER_MAINTAG);
    PopHiberContext = NULL;
}

