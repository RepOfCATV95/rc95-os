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
#define HIBER_MAX_ULONGLONG ((ULONGLONG)~0ULL)
#define HIBER_CR4_PAE 0x00000020

C_ASSERT(sizeof(KPROCESSOR_STATE) + 15 + 512 <= HIBERFILE_CPU_STATE_MAX_SIZE);

PPOP_HIBER_CONTEXT PopHiberContext;
static KSPIN_LOCK PopHiberLock;
extern PLOADER_PARAMETER_BLOCK KeLoaderBlock;

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

    if (Offset > 0x7FFFFFFFFFFFFFFFULL)
        return STATUS_INTEGER_OVERFLOW;

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

    PageCount = Length >> PAGE_SHIFT;
    if (HiberContext->PagesOut > HIBER_MAX_ULONGLONG - PageCount)
        return STATUS_INTEGER_OVERFLOW;

    PayloadSize = ((ULONGLONG)HiberContext->ImageHeader.PayloadSizeHigh << 32) |
                  HiberContext->ImageHeader.PayloadSizeLow;
    ChunkPayloadSize = sizeof(ChunkHeader) + (ULONGLONG)Length;
    if (PayloadSize > HIBER_MAX_ULONGLONG - ChunkPayloadSize ||
        PayloadSize + ChunkPayloadSize >
            0x7FFFFFFFFFFFFFFFULL - sizeof(HiberContext->ImageHeader))
    {
        return STATUS_INTEGER_OVERFLOW;
    }

    FileOffset = sizeof(HiberContext->ImageHeader) + PayloadSize;
    ChunkHeader.PhysicalAddress = (ULONGLONG)PhysicalAddress.QuadPart;
    ChunkHeader.Length = Length;
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
                             SerializationContext->Buffer,
                             Length,
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
    NTSTATUS Status;

    if (Context == NULL)
        return STATUS_INVALID_PARAMETER;

    Context->Status = STATUS_SUCCESS;
    RtlZeroMemory(&Context->ImageHeader, sizeof(Context->ImageHeader));
    Context->PagesOut = 0;
    Context->NoRanges = 0;
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
    KeSaveStateForHibernate(Context->WakeState);

#if defined(_M_IX86)
    if (Context->WakeState->SpecialRegisters.Cr4 & HIBER_CR4_PAE)
        return STATUS_NOT_SUPPORTED;
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

    if (Context->ResumeFlag)
        return STATUS_SUCCESS;

    Status = PopSaveHiberContextStates(Context);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = PopHiberSerializeMemory(Context);
    if (NT_SUCCESS(Status))
        PopHiberUpdateComplete(100);

    return Status;
#endif
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
        return STATUS_SUCCESS;
    }

    if (PopHiberContext == NULL)
    {
        PopHiberContext = ExAllocatePoolWithTag(NonPagedPool,
                                                sizeof(POP_HIBER_CONTEXT),
                                                HIBER_MAINTAG);
        if (PopHiberContext == NULL)
            return STATUS_INSUFFICIENT_RESOURCES;

        KeInitializeSpinLock(&PopHiberLock);
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
        return;
    }

    if (PopHiberContext->LinkFileHandle)
    {
        ZwClose(PopHiberContext->LinkFileHandle);
        PopHiberContext->LinkFileHandle = NULL;
    }

    PopCapabilities.HiberFilePresent = FALSE;
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

