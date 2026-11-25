/*******************************************************************************
*                                                                              *
* ACPI Component Architecture Operating System Layer (OSL) for ReactOS         *
*                                                                              *
*******************************************************************************/

#include "precomp.h"

#include <arc/arc.h>
#include <reactos/drivers/acpi/acpi.h>
#include <pseh/pseh2.h>

#define NDEBUG
#include <debug.h>

#define TAG_ACPI_LOADER_RSDP_TABLE   'RpcA'

static PKINTERRUPT AcpiInterrupt;
static BOOLEAN AcpiInterruptHandlerRegistered = FALSE;
static ACPI_OSD_HANDLER AcpiIrqHandler = NULL;
static PVOID AcpiIrqContext = NULL;
static ULONG AcpiIrqNumber = 0;
/* TODO: Replace these local declarations with <ndk/kefuncs.h> once the
 * acpi.sys build context can consume the required NDK dependencies cleanly.
 */
extern NTSYSAPI PLOADER_PARAMETER_BLOCK KeLoaderBlock;
extern NTSYSAPI
PCONFIGURATION_COMPONENT_DATA
NTAPI
KeFindConfigurationNextEntry(
    _In_ PCONFIGURATION_COMPONENT_DATA Child,
    _In_ CONFIGURATION_CLASS Class,
    _In_ CONFIGURATION_TYPE Type,
    _In_opt_ PULONG ComponentKey,
    _Inout_ PCONFIGURATION_COMPONENT_DATA *NextLink);

static ACPI_TABLE_RSDP *AcpiLoaderRsdp = NULL;

static
UCHAR
AcpiChecksumBuffer(
    _In_reads_bytes_(Length) const UCHAR *Buffer,
    _In_ ULONG Length)
{
    ULONG Index;
    UCHAR Sum;

    Sum = 0;
    for (Index = 0; Index < Length; ++Index)
        Sum = (UCHAR)(Sum + Buffer[Index]);

    return (UCHAR)(0 - Sum);
}

static
PACPI_BIOS_MULTI_NODE
AcpiGetLoaderAcpiBiosNode(VOID)
{
    PCONFIGURATION_COMPONENT_DATA ComponentEntry = NULL;
    PCONFIGURATION_COMPONENT_DATA Next = NULL;
    PCM_PARTIAL_RESOURCE_LIST ResourceList;

    if (!KeLoaderBlock || !KeLoaderBlock->ConfigurationRoot)
        return NULL;

    ComponentEntry = KeFindConfigurationNextEntry(KeLoaderBlock->ConfigurationRoot,
                                                  AdapterClass,
                                                  MultiFunctionAdapter,
                                                  NULL,
                                                  &Next);
    while (ComponentEntry)
    {
        if ((ComponentEntry->ComponentEntry.Identifier != NULL) &&
            !_stricmp(ComponentEntry->ComponentEntry.Identifier, "ACPI BIOS"))
        {
            break;
        }

        Next = ComponentEntry;
        ComponentEntry = KeFindConfigurationNextEntry(KeLoaderBlock->ConfigurationRoot,
                                                      AdapterClass,
                                                      MultiFunctionAdapter,
                                                      NULL,
                                                      &Next);
    }

    if (!ComponentEntry)
        return NULL;

    ResourceList = ComponentEntry->ConfigurationData;
    if (!ResourceList ||
        (ComponentEntry->ComponentEntry.ConfigurationDataLength <
         FIELD_OFFSET(CM_PARTIAL_RESOURCE_LIST, PartialDescriptors[1]) +
         FIELD_OFFSET(ACPI_BIOS_MULTI_NODE, E820Entry)) ||
        (ResourceList->Count < 1) ||
        (ResourceList->PartialDescriptors[0].Type != CmResourceTypeDeviceSpecific))
    {
        DPRINT1("Loader ACPI BIOS node is missing valid device-specific data\n");
        return NULL;
    }

    return (PACPI_BIOS_MULTI_NODE)(ResourceList + 1);
}

static
ACPI_PHYSICAL_ADDRESS
AcpiBuildLoaderRootPointer(VOID)
{
    PACPI_BIOS_MULTI_NODE NodeData;
    ACPI_TABLE_HEADER *RootTable = NULL;
    PHYSICAL_ADDRESS PhysicalAddress;

    if (AcpiLoaderRsdp != NULL)
    {
        PhysicalAddress = MmGetPhysicalAddress(AcpiLoaderRsdp);
        return (ACPI_PHYSICAL_ADDRESS)PhysicalAddress.QuadPart;
    }

    NodeData = AcpiGetLoaderAcpiBiosNode();
    if (!NodeData || (NodeData->RsdtAddress.QuadPart == 0))
        goto Failure;

    RootTable = MmMapIoSpace(NodeData->RsdtAddress, sizeof(*RootTable), MmNonCached);
    if (!RootTable)
    {
        DPRINT1("Unable to map loader ACPI root table at 0x%I64x\n",
                NodeData->RsdtAddress.QuadPart);
        goto Failure;
    }

    AcpiLoaderRsdp = ExAllocatePoolZero(NonPagedPool,
                                        sizeof(*AcpiLoaderRsdp),
                                        TAG_ACPI_LOADER_RSDP_TABLE);
    if (!AcpiLoaderRsdp)
    {
        DPRINT1("Unable to allocate synthetic RSDP for loader ACPI tables\n");
        goto Failure;
    }

    RtlCopyMemory(AcpiLoaderRsdp->Signature,
                  ACPI_SIG_RSDP,
                  sizeof(AcpiLoaderRsdp->Signature));
    RtlCopyMemory(AcpiLoaderRsdp->OemId, "ROS   ", sizeof(AcpiLoaderRsdp->OemId));

    if (RtlEqualMemory(RootTable->Signature, ACPI_SIG_XSDT, ACPI_NAMESEG_SIZE))
    {
        AcpiLoaderRsdp->Revision = 2;
        AcpiLoaderRsdp->Length = sizeof(*AcpiLoaderRsdp);
        AcpiLoaderRsdp->XsdtPhysicalAddress = NodeData->RsdtAddress.QuadPart;
    }
    else if (RtlEqualMemory(RootTable->Signature, ACPI_SIG_RSDT, ACPI_NAMESEG_SIZE))
    {
        if (NodeData->RsdtAddress.QuadPart > MAXULONG)
        {
            DPRINT1("Loader RSDT address does not fit in 32 bits: 0x%I64x\n",
                    NodeData->RsdtAddress.QuadPart);
            goto Failure;
        }

        AcpiLoaderRsdp->Revision = 0;
        AcpiLoaderRsdp->RsdtPhysicalAddress = NodeData->RsdtAddress.LowPart;
    }
    else
    {
        DPRINT1("Loader ACPI root table has unexpected signature '%.4s'\n",
                RootTable->Signature);
        goto Failure;
    }

    AcpiLoaderRsdp->Checksum = AcpiChecksumBuffer((const UCHAR *)AcpiLoaderRsdp,
                                                  ACPI_RSDP_CHECKSUM_LENGTH);
    if (AcpiLoaderRsdp->Revision >= 2)
    {
        AcpiLoaderRsdp->ExtendedChecksum =
            AcpiChecksumBuffer((const UCHAR *)AcpiLoaderRsdp,
                               sizeof(*AcpiLoaderRsdp));
    }

    MmUnmapIoSpace(RootTable, sizeof(*RootTable));
    PhysicalAddress = MmGetPhysicalAddress(AcpiLoaderRsdp);
    return (ACPI_PHYSICAL_ADDRESS)PhysicalAddress.QuadPart;

Failure:
    if (AcpiLoaderRsdp)
    {
        ExFreePoolWithTag(AcpiLoaderRsdp, TAG_ACPI_LOADER_RSDP_TABLE);
        AcpiLoaderRsdp = NULL;
    }
    if (RootTable)
        MmUnmapIoSpace(RootTable, sizeof(*RootTable));
    return 0;
}

ACPI_STATUS
AcpiOsInitialize (void)
{
    DPRINT("AcpiOsInitialize called\n");

#ifndef NDEBUG
    /* Verboseness level of the acpica core */
    AcpiDbgLevel = 0x00FFFFFF;
    AcpiDbgLayer = 0xFFFFFFFF;
#endif

    (VOID)AcpiBuildLoaderRootPointer();

    return AE_OK;
}

ACPI_STATUS
AcpiOsTerminate(void)
{
    DPRINT("AcpiOsTerminate() called\n");

    /* Release the synthetic RSDP built from the loader-provided ACPI tables.
     * ACPICA has already torn down its table mappings (AcpiUtSubsystemShutdown)
     * by the time it calls us, so nothing references this buffer anymore. */
    if (AcpiLoaderRsdp != NULL)
    {
        ExFreePoolWithTag(AcpiLoaderRsdp, TAG_ACPI_LOADER_RSDP_TABLE);
        AcpiLoaderRsdp = NULL;
    }

    return AE_OK;
}

ACPI_PHYSICAL_ADDRESS
AcpiOsGetRootPointer (
    void)
{
    ACPI_PHYSICAL_ADDRESS pa = 0;
    PHYSICAL_ADDRESS PhysicalAddress;

    DPRINT("AcpiOsGetRootPointer\n");

    if (AcpiLoaderRsdp != NULL)
    {
        PhysicalAddress = MmGetPhysicalAddress(AcpiLoaderRsdp);
        return (ACPI_PHYSICAL_ADDRESS)PhysicalAddress.QuadPart;
    }

    AcpiFindRootPointer(&pa);
    return pa;
}

ACPI_STATUS
AcpiOsPredefinedOverride(
    const ACPI_PREDEFINED_NAMES *PredefinedObject,
    ACPI_STRING                 *NewValue)
{
    if (!PredefinedObject || !NewValue)
    {
        DPRINT1("Invalid parameter\n");
        return AE_BAD_PARAMETER;
    }

    /* No override */
    *NewValue = NULL;

    return AE_OK;
}

ACPI_STATUS
AcpiOsTableOverride(
    ACPI_TABLE_HEADER *ExistingTable,
    ACPI_TABLE_HEADER **NewTable)
{
    if (!ExistingTable || !NewTable)
    {
        DPRINT1("Invalid parameter\n");
        return AE_BAD_PARAMETER;
    }

    /* No override */
    *NewTable = NULL;

    return AE_OK;
}

ACPI_STATUS
AcpiOsPhysicalTableOverride(
    ACPI_TABLE_HEADER       *ExistingTable,
    ACPI_PHYSICAL_ADDRESS   *NewAddress,
    UINT32                  *NewTableLength)
{
    if (!ExistingTable || !NewAddress || !NewTableLength)
    {
        DPRINT1("Invalid parameter\n");
        return AE_BAD_PARAMETER;
    }

    /* No override */
    *NewAddress     = 0;
    *NewTableLength = 0;

    return AE_OK;
}

void *
AcpiOsMapMemory (
    ACPI_PHYSICAL_ADDRESS   phys,
    ACPI_SIZE               length)
{
    PHYSICAL_ADDRESS Address;
    PVOID Ptr;

    if (length == 0 || length > MAXULONG)
        return NULL;

    DPRINT("AcpiOsMapMemory(phys 0x%p  size 0x%X)\n", phys, length);

    Address.QuadPart = (ULONGLONG)phys;
    Ptr = MmMapIoSpace(Address, length, MmNonCached);
    if (!Ptr)
    {
        DPRINT1("Mapping failed\n");
    }

    return Ptr;
}

void
AcpiOsUnmapMemory (
    void                    *virt,
    ACPI_SIZE               length)
{
    if (!virt || length == 0)
        return;

    DPRINT("AcpiOsUnmapMemory(virt %p)\n", virt);
    MmUnmapIoSpace(virt, length);
}

ACPI_STATUS
AcpiOsGetPhysicalAddress(
    void *LogicalAddress,
    ACPI_PHYSICAL_ADDRESS *PhysicalAddress)
{
    PHYSICAL_ADDRESS PhysAddr;

    if (!LogicalAddress || !PhysicalAddress)
    {
        DPRINT1("Bad parameter\n");
        return AE_BAD_PARAMETER;
    }

    PhysAddr = MmGetPhysicalAddress(LogicalAddress);

    *PhysicalAddress = (ACPI_PHYSICAL_ADDRESS)PhysAddr.QuadPart;

    return AE_OK;
}

void *
AcpiOsAllocate (ACPI_SIZE size)
{
    DPRINT("AcpiOsAllocate size %d\n",size);
    return ExAllocatePoolWithTag(NonPagedPool, size, 'ipcA');
}

void
AcpiOsFree(void *ptr)
{
    if (!ptr)
        return;

    ExFreePoolWithTag(ptr, 'ipcA');
}

BOOLEAN
AcpiOsReadable(
    void *Memory,
    ACPI_SIZE Length)
{
    BOOLEAN Ret = FALSE;

    _SEH2_TRY
    {
        ProbeForRead(Memory, Length, sizeof(UCHAR));
        Ret = TRUE;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Ret = FALSE;
    }
    _SEH2_END;

    return Ret;
}

BOOLEAN
AcpiOsWritable(
    void *Memory,
    ACPI_SIZE Length)
{
    BOOLEAN Ret = FALSE;

    _SEH2_TRY
    {
        ProbeForWrite(Memory, Length, sizeof(UCHAR));
        Ret = TRUE;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Ret = FALSE;
    }
    _SEH2_END;

    return Ret;
}

ACPI_THREAD_ID
AcpiOsGetThreadId (void)
{
    /* Thread ID must be non-zero */
    return (ULONG_PTR)PsGetCurrentThreadId() + 1;
}

ACPI_STATUS
AcpiOsExecute (
    ACPI_EXECUTE_TYPE       Type,
    ACPI_OSD_EXEC_CALLBACK  Function,
    void                    *Context)
{
    HANDLE ThreadHandle;
    OBJECT_ATTRIBUTES ObjectAttributes;
    NTSTATUS Status;

    DPRINT("AcpiOsExecute\n");

    InitializeObjectAttributes(&ObjectAttributes,
                               NULL,
                               OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    Status = PsCreateSystemThread(&ThreadHandle,
                                  THREAD_ALL_ACCESS,
                                  &ObjectAttributes,
                                  NULL,
                                  NULL,
                                  (PKSTART_ROUTINE)Function,
                                  Context);
    if (!NT_SUCCESS(Status))
        return AE_ERROR;

    ZwClose(ThreadHandle);

    return AE_OK;
}

void
AcpiOsSleep (UINT64 milliseconds)
{
    LARGE_INTEGER Interval;

    if (KeGetCurrentIrql() > APC_LEVEL)
    {
        DPRINT1("AcpiOsSleep called above APC_LEVEL\n");
        return;
    }

    if (milliseconds > ((ULONGLONG)MAXLONGLONG / 10000))
        milliseconds = (ULONGLONG)MAXLONGLONG / 10000;

    Interval.QuadPart = -(LONGLONG)(milliseconds * 10000);
    if (milliseconds != 0)
        KeDelayExecutionThread(KernelMode, FALSE, &Interval);
}

void
AcpiOsStall (UINT32 microseconds)
{
    DPRINT("AcpiOsStall %d\n",microseconds);
    KeStallExecutionProcessor(microseconds);
}

ACPI_STATUS
AcpiOsCreateMutex(
    ACPI_MUTEX *OutHandle)
{
    PFAST_MUTEX Mutex;

    if (!OutHandle)
        return AE_BAD_PARAMETER;
    *OutHandle = NULL;

    Mutex = ExAllocatePoolWithTag(NonPagedPool, sizeof(FAST_MUTEX), 'LpcA');
    if (!Mutex) return AE_NO_MEMORY;

    ExInitializeFastMutex(Mutex);

    *OutHandle = (ACPI_MUTEX)Mutex;

    return AE_OK;
}

void
AcpiOsDeleteMutex(
    ACPI_MUTEX Handle)
{
    if (!Handle)
    {
        DPRINT1("Bad parameter\n");
        return;
    }

    ExFreePoolWithTag(Handle, 'LpcA');
}

ACPI_STATUS
AcpiOsAcquireMutex(
    ACPI_MUTEX Handle,
    UINT16 Timeout)
{
    if (!Handle)
    {
        DPRINT1("Bad parameter\n");
        return AE_BAD_PARAMETER;
    }

    /* Check what the caller wants us to do */
    if (Timeout == ACPI_DO_NOT_WAIT)
    {
        /* Try to acquire without waiting */
        if (!ExTryToAcquireFastMutex((PFAST_MUTEX)Handle))
            return AE_TIME;
    }
    else
    {
        /* Block until we get it */
        ExAcquireFastMutex((PFAST_MUTEX)Handle);
    }

    return AE_OK;
}

void
AcpiOsReleaseMutex(
    ACPI_MUTEX Handle)
{
    if (!Handle)
    {
        DPRINT1("Bad parameter\n");
        return;
    }

    ExReleaseFastMutex((PFAST_MUTEX)Handle);
}

typedef struct _ACPI_SEM {
    UINT32 CurrentUnits;
    UINT32 MaxUnits;
    KEVENT Event;
    KSPIN_LOCK Lock;
} ACPI_SEM, *PACPI_SEM;

ACPI_STATUS
AcpiOsCreateSemaphore(
    UINT32 MaxUnits,
    UINT32 InitialUnits,
    ACPI_SEMAPHORE *OutHandle)
{
    PACPI_SEM Sem;

    if (!OutHandle || MaxUnits == 0 || InitialUnits > MaxUnits)
    {
        DPRINT1("Invalid semaphore parameters\n");
        return AE_BAD_PARAMETER;
    }

    *OutHandle = NULL;
    Sem = ExAllocatePoolWithTag(NonPagedPool, sizeof(ACPI_SEM), 'LpcA');
    if (!Sem) return AE_NO_MEMORY;

    Sem->CurrentUnits = InitialUnits;
    Sem->MaxUnits = MaxUnits;
    KeInitializeEvent(&Sem->Event, SynchronizationEvent, Sem->CurrentUnits != 0);
    KeInitializeSpinLock(&Sem->Lock);

    *OutHandle = (ACPI_SEMAPHORE)Sem;

    return AE_OK;
}

ACPI_STATUS
AcpiOsDeleteSemaphore(
    ACPI_SEMAPHORE Handle)
{
    if (!Handle)
    {
        DPRINT1("Bad parameter\n");
        return AE_BAD_PARAMETER;
    }

    ExFreePoolWithTag(Handle, 'LpcA');

    return AE_OK;
}

ACPI_STATUS
AcpiOsWaitSemaphore(
    ACPI_SEMAPHORE Handle,
    UINT32 Units,
    UINT16 Timeout)
{
    PACPI_SEM Sem = Handle;
    KIRQL OldIrql;
    LARGE_INTEGER RemainingTime;
    ULONGLONG Deadline = 0;
    NTSTATUS WaitStatus;

    if (!Handle || Units > Sem->MaxUnits)
        return AE_BAD_PARAMETER;
    if (Units == 0)
        return AE_OK;

    if (Timeout != ACPI_WAIT_FOREVER && Timeout != ACPI_DO_NOT_WAIT)
        Deadline = KeQueryInterruptTime() + ((ULONGLONG)Timeout * 10000);

    KeAcquireSpinLock(&Sem->Lock, &OldIrql);

    /* Make sure we can wait if we have fewer units than we need */
    if ((Timeout == ACPI_DO_NOT_WAIT) && (Sem->CurrentUnits < Units))
    {
        /* We can't so we must bail now */
        KeReleaseSpinLock(&Sem->Lock, OldIrql);
        return AE_TIME;
    }

    /* Time to block until we get enough units */
    while (Sem->CurrentUnits < Units)
    {
        KeReleaseSpinLock(&Sem->Lock, OldIrql);

        if (Timeout == ACPI_DO_NOT_WAIT)
            return AE_TIME;
        if (Timeout == ACPI_WAIT_FOREVER)
        {
            WaitStatus = KeWaitForSingleObject(&Sem->Event, Executive,
                                               KernelMode, FALSE, NULL);
        }
        else
        {
            ULONGLONG Now = KeQueryInterruptTime();
            if (Now >= Deadline)
                return AE_TIME;
            RemainingTime.QuadPart = -(LONGLONG)(Deadline - Now);
            WaitStatus = KeWaitForSingleObject(&Sem->Event, Executive,
                                               KernelMode, FALSE, &RemainingTime);
            if (WaitStatus == STATUS_TIMEOUT)
                return AE_TIME;
        }
        if (!NT_SUCCESS(WaitStatus))
            return AE_ERROR;

        KeAcquireSpinLock(&Sem->Lock, &OldIrql);
    }

    Sem->CurrentUnits -= Units;

    if (Sem->CurrentUnits != 0) KeSetEvent(&Sem->Event, IO_NO_INCREMENT, FALSE);

    KeReleaseSpinLock(&Sem->Lock, OldIrql);

    return AE_OK;
}

ACPI_STATUS
AcpiOsSignalSemaphore(
    ACPI_SEMAPHORE Handle,
    UINT32 Units)
{
    PACPI_SEM Sem = Handle;
    KIRQL OldIrql;

    if (!Handle)
    {
        DPRINT1("Bad parameter\n");
        return AE_BAD_PARAMETER;
    }
    if (Units == 0)
        return AE_OK;

    KeAcquireSpinLock(&Sem->Lock, &OldIrql);

    if (Units > (Sem->MaxUnits - Sem->CurrentUnits))
    {
        KeReleaseSpinLock(&Sem->Lock, OldIrql);
        return AE_LIMIT;
    }

    Sem->CurrentUnits += Units;
    KeSetEvent(&Sem->Event, IO_NO_INCREMENT, FALSE);

    KeReleaseSpinLock(&Sem->Lock, OldIrql);

    return AE_OK;
}

ACPI_STATUS
AcpiOsCreateLock(
    ACPI_SPINLOCK *OutHandle)
{
    PKSPIN_LOCK SpinLock;

    if (!OutHandle)
        return AE_BAD_PARAMETER;
    *OutHandle = NULL;

    SpinLock = ExAllocatePoolWithTag(NonPagedPool, sizeof(KSPIN_LOCK), 'LpcA');
    if (!SpinLock) return AE_NO_MEMORY;

    KeInitializeSpinLock(SpinLock);

    *OutHandle = (ACPI_SPINLOCK)SpinLock;

    return AE_OK;
}

void
AcpiOsDeleteLock(
    ACPI_SPINLOCK Handle)
{
    if (!Handle)
    {
        DPRINT1("Bad parameter\n");
        return;
    }

    ExFreePoolWithTag(Handle, 'LpcA');
}

ACPI_CPU_FLAGS
AcpiOsAcquireLock(
    ACPI_SPINLOCK Handle)
{
    KIRQL OldIrql;

    if (!Handle)
        return 0;

    if ((OldIrql = KeGetCurrentIrql()) >= DISPATCH_LEVEL)
    {
        KeAcquireSpinLockAtDpcLevel((PKSPIN_LOCK)Handle);
    }
    else
    {
        KeAcquireSpinLock((PKSPIN_LOCK)Handle, &OldIrql);
    }

    return (ACPI_CPU_FLAGS)OldIrql;
}

void
AcpiOsReleaseLock(
    ACPI_SPINLOCK Handle,
    ACPI_CPU_FLAGS Flags)
{
    KIRQL OldIrql = (KIRQL)Flags;

    if (!Handle)
        return;

    if (OldIrql >= DISPATCH_LEVEL)
    {
        KeReleaseSpinLockFromDpcLevel((PKSPIN_LOCK)Handle);
    }
    else
    {
        KeReleaseSpinLock((PKSPIN_LOCK)Handle, OldIrql);
    }
}

BOOLEAN NTAPI
OslIsrStub(
  PKINTERRUPT Interrupt,
  PVOID ServiceContext)
{
  INT32 Status;

  if (!AcpiIrqHandler)
      return FALSE;

  Status = (*AcpiIrqHandler)(AcpiIrqContext);

  if (Status == ACPI_INTERRUPT_HANDLED)
    return TRUE;
  else
    return FALSE;
}

UINT32
AcpiOsInstallInterruptHandler (
    UINT32                  InterruptNumber,
    ACPI_OSD_HANDLER        ServiceRoutine,
    void                    *Context)
{
    ULONG Vector;
    KIRQL DIrql;
    KAFFINITY Affinity;
    NTSTATUS Status;

    if (AcpiInterruptHandlerRegistered)
    {
        DPRINT1("Reregister interrupt attempt failed\n");
        return AE_ALREADY_EXISTS;
    }

    if (!ServiceRoutine)
    {
        DPRINT1("Bad parameter\n");
        return AE_BAD_PARAMETER;
    }

    DPRINT("AcpiOsInstallInterruptHandler()\n");
    Vector = HalGetInterruptVector(
        Internal,
        0,
        InterruptNumber,
        InterruptNumber,
        &DIrql,
        &Affinity);

    AcpiIrqNumber = InterruptNumber;
    AcpiIrqHandler = ServiceRoutine;
    AcpiIrqContext = Context;

    Status = IoConnectInterrupt(
        &AcpiInterrupt,
        OslIsrStub,
        NULL,
        NULL,
        Vector,
        DIrql,
        DIrql,
        LevelSensitive,
        TRUE,
        Affinity,
        FALSE);

    if (!NT_SUCCESS(Status))
    {
        AcpiInterrupt = NULL;
        AcpiIrqNumber = 0;
        AcpiIrqHandler = NULL;
        AcpiIrqContext = NULL;
        AcpiInterruptHandlerRegistered = FALSE;
        DPRINT("Could not connect to interrupt %d\n", Vector);
        return AE_ERROR;
    }

    AcpiIrqNumber = InterruptNumber;
    AcpiIrqHandler = ServiceRoutine;
    AcpiIrqContext = Context;
    AcpiInterruptHandlerRegistered = TRUE;
    return AE_OK;
}

ACPI_STATUS
AcpiOsRemoveInterruptHandler (
    UINT32                  InterruptNumber,
    ACPI_OSD_HANDLER        ServiceRoutine)
{
    DPRINT("AcpiOsRemoveInterruptHandler()\n");

    if (!ServiceRoutine)
    {
        DPRINT1("Bad parameter\n");
        return AE_BAD_PARAMETER;
    }

    if (AcpiInterruptHandlerRegistered)
    {
        if (InterruptNumber != AcpiIrqNumber || ServiceRoutine != AcpiIrqHandler)
            return AE_BAD_PARAMETER;

        IoDisconnectInterrupt(AcpiInterrupt);
        AcpiInterrupt = NULL;
        AcpiIrqNumber = 0;
        AcpiIrqHandler = NULL;
        AcpiIrqContext = NULL;
        AcpiInterruptHandlerRegistered = FALSE;
    }
    else
    {
        DPRINT1("Trying to remove non-existing interrupt handler\n");
        return AE_NOT_EXIST;
    }

    return AE_OK;
}

ACPI_STATUS
AcpiOsReadMemory (
    ACPI_PHYSICAL_ADDRESS   Address,
    UINT64                  *Value,
    UINT32                  Width)
{
    PHYSICAL_ADDRESS PhysicalAddress;
    PVOID Mapping;
    ULONG Length;

    if (!Value || (Width != 8 && Width != 16 && Width != 32 && Width != 64))
        return AE_BAD_PARAMETER;

    Length = Width >> 3;
    PhysicalAddress.QuadPart = Address;
    Mapping = MmMapIoSpace(PhysicalAddress, Length, MmNonCached);
    if (!Mapping)
        return AE_NO_MEMORY;

    switch (Width)
    {
    case 8:  *Value = READ_REGISTER_UCHAR((volatile UCHAR *)Mapping); break;
    case 16: *Value = READ_REGISTER_USHORT((volatile USHORT *)Mapping); break;
    case 32: *Value = READ_REGISTER_ULONG((volatile ULONG *)Mapping); break;
    default:
        *Value = (UINT64)READ_REGISTER_ULONG((volatile ULONG *)Mapping);
        *Value |= ((UINT64)READ_REGISTER_ULONG((volatile ULONG *)((PUCHAR)Mapping + sizeof(ULONG))) << 32);
        break;
    }

    MmUnmapIoSpace(Mapping, Length);
    return AE_OK;
}

ACPI_STATUS
AcpiOsWriteMemory (
    ACPI_PHYSICAL_ADDRESS   Address,
    UINT64                  Value,
    UINT32                  Width)
{
    PHYSICAL_ADDRESS PhysicalAddress;
    PVOID Mapping;
    ULONG Length;

    if (Width != 8 && Width != 16 && Width != 32 && Width != 64)
        return AE_BAD_PARAMETER;

    Length = Width >> 3;
    PhysicalAddress.QuadPart = Address;
    Mapping = MmMapIoSpace(PhysicalAddress, Length, MmNonCached);
    if (!Mapping)
        return AE_NO_MEMORY;

    switch (Width)
    {
    case 8:  WRITE_REGISTER_UCHAR((volatile UCHAR *)Mapping, (UCHAR)Value); break;
    case 16: WRITE_REGISTER_USHORT((volatile USHORT *)Mapping, (USHORT)Value); break;
    case 32: WRITE_REGISTER_ULONG((volatile ULONG *)Mapping, (ULONG)Value); break;
    default:
        WRITE_REGISTER_ULONG((volatile ULONG *)Mapping, (ULONG)Value);
        WRITE_REGISTER_ULONG((volatile ULONG *)((PUCHAR)Mapping + sizeof(ULONG)), (ULONG)(Value >> 32));
        break;
    }

    MmUnmapIoSpace(Mapping, Length);
    return AE_OK;
}

ACPI_STATUS
AcpiOsReadPort (
    ACPI_IO_ADDRESS         Address,
    UINT32                  *Value,
    UINT32                  Width)
{
    if (!Value)
        return AE_BAD_PARAMETER;

    DPRINT("AcpiOsReadPort %p, width %d\n",Address,Width);

    switch (Width)
    {
    case 8:
        *Value = READ_PORT_UCHAR((PUCHAR)(ULONG_PTR)Address);
        break;

    case 16:
        *Value = READ_PORT_USHORT((PUSHORT)(ULONG_PTR)Address);
        break;

    case 32:
        *Value = READ_PORT_ULONG((PULONG)(ULONG_PTR)Address);
        break;

    default:
        DPRINT1("AcpiOsReadPort got bad width: %d\n",Width);
        return (AE_BAD_PARAMETER);
        break;
    }
    return (AE_OK);
}

ACPI_STATUS
AcpiOsWritePort (
    ACPI_IO_ADDRESS         Address,
    UINT32                  Value,
    UINT32                  Width)
{
    if (Width != 8 && Width != 16 && Width != 32)
        return AE_BAD_PARAMETER;

    DPRINT("AcpiOsWritePort %p, width %d\n",Address,Width);
    switch (Width)
    {
    case 8:
        WRITE_PORT_UCHAR((PUCHAR)(ULONG_PTR)Address, Value);
        break;

    case 16:
        WRITE_PORT_USHORT((PUSHORT)(ULONG_PTR)Address, Value);
        break;

    case 32:
        WRITE_PORT_ULONG((PULONG)(ULONG_PTR)Address, Value);
        break;

    default:
        DPRINT1("AcpiOsWritePort got bad width: %d\n",Width);
        return (AE_BAD_PARAMETER);
        break;
    }
    return (AE_OK);
}

BOOLEAN
OslIsPciDevicePresent(ULONG BusNumber, ULONG SlotNumber)
{
    UINT32 ReadLength;
    PCI_COMMON_CONFIG PciConfig;

    /* Detect device presence by reading the PCI configuration space */

    ReadLength = HalGetBusDataByOffset(PCIConfiguration,
                                       BusNumber,
                                       SlotNumber,
                                       &PciConfig,
                                       0,
                                       sizeof(PciConfig));
    if (ReadLength == 0)
    {
        DPRINT("PCI device is not present\n");
        return FALSE;
    }

    if (ReadLength < sizeof(PciConfig.VendorID))
        return FALSE;

    if (PciConfig.VendorID == PCI_INVALID_VENDORID)
    {
        DPRINT("Invalid vendor ID in PCI configuration space\n");
        return FALSE;
    }

    DPRINT("PCI device is present\n");

    return TRUE;
}

ACPI_STATUS
AcpiOsReadPciConfiguration (
    ACPI_PCI_ID             *PciId,
    UINT32                  Reg,
    UINT64                  *Value,
    UINT32                  Width)
{
    PCI_SLOT_NUMBER slot;
    ULONG Length;

    if (!PciId || !Value || (Width != 8 && Width != 16 && Width != 32))
        return AE_BAD_PARAMETER;
    Length = Width >> 3;
    if (Reg > 256 - Length)
        return AE_BAD_PARAMETER;

    *Value = 0;
    slot.u.AsULONG = 0;
    slot.u.bits.DeviceNumber = PciId->Device;
    slot.u.bits.FunctionNumber = PciId->Function;

    DPRINT("AcpiOsReadPciConfiguration, slot=0x%X, func=0x%X\n", slot.u.AsULONG, Reg);

    if (!OslIsPciDevicePresent(PciId->Bus, slot.u.AsULONG))
        return AE_NOT_FOUND;

    /* Width is in BITS */
    if (HalGetBusDataByOffset(PCIConfiguration,
                              PciId->Bus,
                              slot.u.AsULONG,
                              Value,
                              Reg,
                              Length) != Length)
        return AE_ERROR;

    return AE_OK;
}

ACPI_STATUS
AcpiOsWritePciConfiguration (
    ACPI_PCI_ID              *PciId,
    UINT32                   Reg,
    UINT64                   Value,
    UINT32                   Width)
{
    UINT64 buf = Value;
    PCI_SLOT_NUMBER slot;
    ULONG Length;

    if (!PciId || (Width != 8 && Width != 16 && Width != 32))
        return AE_BAD_PARAMETER;
    Length = Width >> 3;
    if (Reg > 256 - Length)
        return AE_BAD_PARAMETER;

    slot.u.AsULONG = 0;
    slot.u.bits.DeviceNumber = PciId->Device;
    slot.u.bits.FunctionNumber = PciId->Function;

    DPRINT("AcpiOsWritePciConfiguration, slot=0x%x\n", slot.u.AsULONG);
    if (!OslIsPciDevicePresent(PciId->Bus, slot.u.AsULONG))
        return AE_NOT_FOUND;

    /* Width is in BITS */
    if (HalSetBusDataByOffset(PCIConfiguration,
                              PciId->Bus,
                              slot.u.AsULONG,
                              &buf,
                              Reg,
                              Length) != Length)
        return AE_ERROR;

    return AE_OK;
}

void ACPI_INTERNAL_VAR_XFACE
AcpiOsPrintf (
    const char              *Fmt,
    ...)
{
    va_list                 Args;
    va_start (Args, Fmt);

    AcpiOsVprintf (Fmt, Args);

    va_end (Args);
    return;
}

void
AcpiOsVprintf (
    const char              *Fmt,
    va_list                 Args)
{
#ifndef NDEBUG
    vDbgPrintEx (-1, DPFLTR_ERROR_LEVEL, Fmt, Args);
#endif
    return;
}

void
AcpiOsRedirectOutput(
    void *Destination)
{
    /* No-op */
    DPRINT1("Output redirection not supported\n");
}

UINT64
AcpiOsGetTimer(
    void)
{
    /* ACPICA timeouts are relative to boot, not wall-clock time. */
    return KeQueryInterruptTime();
}

void
AcpiOsWaitEventsComplete(void)
{
    /*
     * Wait for all asynchronous events to complete.
     * This implementation does nothing.
     */
    return;
}

ACPI_STATUS
AcpiOsSignal (
    UINT32                  Function,
    void                    *Info)
{
    ACPI_SIGNAL_FATAL_INFO *FatalInfo = Info;

    switch (Function)
    {
    case ACPI_SIGNAL_FATAL:
        if (Info)
            DPRINT1 ("AcpiOsBreakpoint: %d %d %d ****\n", FatalInfo->Type, FatalInfo->Code, FatalInfo->Argument);
        else
            DPRINT1 ("AcpiOsBreakpoint ****\n");
        break;
    case ACPI_SIGNAL_BREAKPOINT:
        if (Info)
            DPRINT1 ("AcpiOsBreakpoint: %s ****\n", Info);
        else
            DPRINT1 ("AcpiOsBreakpoint ****\n");
        break;
    }

    ASSERT(FALSE);

    return (AE_OK);
}

ACPI_STATUS
AcpiOsEnterSleep(
    UINT8 SleepState,
    UINT32 RegaValue,
    UINT32 RegbValue)
{
    DPRINT1("Entering sleep state S%u.\n", SleepState);
    return AE_OK;
}

ACPI_STATUS
AcpiOsGetLine(
    char *Buffer,
    UINT32 BufferLength,
    UINT32 *BytesRead)
{
    DPRINT1("File reading not supported\n");
    return AE_ERROR;
}
