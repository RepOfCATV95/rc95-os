/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            ntoskrnl/po/power.c
 * PURPOSE:         Power Manager
 * PROGRAMMERS:     Casper S. Hornstrup (chorns@users.sourceforge.net)
 *                  Hervé Poussineau (hpoussin@reactos.com)
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#include <acpiioct.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/

typedef struct _POWER_STATE_TRAVERSE_CONTEXT
{
    SYSTEM_POWER_STATE SystemPowerState;
    POWER_ACTION PowerAction;
    PDEVICE_OBJECT PowerDevice;
} POWER_STATE_TRAVERSE_CONTEXT, *PPOWER_STATE_TRAVERSE_CONTEXT;

PDEVICE_NODE PopSystemPowerDeviceNode = NULL;
BOOLEAN PopAcpiPresent = FALSE;
POP_POWER_ACTION PopAction;
WORK_QUEUE_ITEM PopShutdownWorkItem;
SYSTEM_POWER_CAPABILITIES PopCapabilities;

/* PRIVATE FUNCTIONS *********************************************************/

static
NTSTATUS
PopQuerySystemPowerCapabilities(VOID)
{
    PDEVICE_OBJECT DeviceObject;
    PDEVICE_OBJECT TopDeviceObject;
    ULONG SupportedStates = 0;
    ULONG_PTR Information;
    IO_STATUS_BLOCK IoStatusBlock;
    KEVENT Event;
    PIRP Irp;
    NTSTATUS Status;

    PopCapabilities.SystemS1 = FALSE;
    PopCapabilities.SystemS2 = FALSE;
    PopCapabilities.SystemS3 = FALSE;
    PopCapabilities.SystemS4 = FALSE;
    PopCapabilities.SystemS5 = FALSE;

    if (!PopAcpiPresent)
        return STATUS_SUCCESS;

    Status = IopGetSystemPowerDeviceObject(&DeviceObject);
    if (!NT_SUCCESS(Status))
        return STATUS_SUCCESS;

    TopDeviceObject = IoGetAttachedDeviceReference(DeviceObject);
    if (TopDeviceObject == DeviceObject)
    {
        ObDereferenceObject(TopDeviceObject);
        return STATUS_SUCCESS;
    }

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(IOCTL_ACPI_QUERY_SYSTEM_POWER_CAPABILITIES,
                                        TopDeviceObject,
                                        NULL,
                                        0,
                                        &SupportedStates,
                                        sizeof(SupportedStates),
                                        FALSE,
                                        &Event,
                                        &IoStatusBlock);
    if (Irp == NULL)
    {
        ObDereferenceObject(TopDeviceObject);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = IoCallDriver(TopDeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event,
                              Executive,
                              KernelMode,
                              FALSE,
                              NULL);
        Status = IoStatusBlock.Status;
    }

    Information = IoStatusBlock.Information;
    ObDereferenceObject(TopDeviceObject);

    if (!NT_SUCCESS(Status))
        return Status;
    if (Information != sizeof(SupportedStates))
        return STATUS_DEVICE_DATA_ERROR;

    PopCapabilities.SystemS1 =
        BooleanFlagOn(SupportedStates, ACPI_SYSTEM_POWER_S1_SUPPORTED);
    PopCapabilities.SystemS2 =
        BooleanFlagOn(SupportedStates, ACPI_SYSTEM_POWER_S2_SUPPORTED);
    PopCapabilities.SystemS5 =
        BooleanFlagOn(SupportedStates, ACPI_SYSTEM_POWER_S5_SUPPORTED);
    PopCapabilities.SystemS4 = PopAcpiPresent;

    return STATUS_SUCCESS;
}

static WORKER_THREAD_ROUTINE PopPassivePowerCall;
_Use_decl_annotations_
static
VOID
NTAPI
PopPassivePowerCall(
    PVOID Parameter)
{
    PIRP Irp = Parameter;
    PIO_STACK_LOCATION IoStack;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

    _Analysis_assume_(Irp != NULL);
    IoStack = IoGetNextIrpStackLocation(Irp);

    (VOID)IoCallDriver(IoStack->DeviceObject, Irp);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_requires_same_
static
NTSTATUS
PopPresentIrp(
    _In_ PIO_STACK_LOCATION NextStack,
    _In_ PIRP Irp)
{
    NTSTATUS Status;
    BOOLEAN CallAtPassiveLevel;
    PDEVICE_OBJECT DeviceObject;
    PWORK_QUEUE_ITEM WorkQueueItem;

    ASSERT(NextStack->MajorFunction == IRP_MJ_POWER);

    DeviceObject = NextStack->DeviceObject;

    /* Determine whether the IRP must be handled at PASSIVE_LEVEL.
     * Only SET_POWER to working state can happen at raised IRQL. */
    CallAtPassiveLevel = TRUE;
    if ((NextStack->MinorFunction == IRP_MN_SET_POWER) &&
        !(DeviceObject->Flags & DO_POWER_PAGABLE))
    {
        if (NextStack->Parameters.Power.Type == DevicePowerState &&
            NextStack->Parameters.Power.State.DeviceState == PowerDeviceD0)
        {
            CallAtPassiveLevel = FALSE;
        }
        if (NextStack->Parameters.Power.Type == SystemPowerState &&
            NextStack->Parameters.Power.State.SystemState == PowerSystemWorking)
        {
            CallAtPassiveLevel = FALSE;
        }
    }

    if (CallAtPassiveLevel)
    {
        /* We need to fit a work item into the DriverContext below */
        C_ASSERT(sizeof(Irp->Tail.Overlay.DriverContext) >= sizeof(WORK_QUEUE_ITEM));

        if (KeGetCurrentIrql() == PASSIVE_LEVEL)
        {
            /* Already at passive, call next driver directly */
            return IoCallDriver(DeviceObject, Irp);
        }

        /* Need to schedule a work item and return pending */
        NextStack->Control |= SL_PENDING_RETURNED;

        WorkQueueItem = (PWORK_QUEUE_ITEM)&Irp->Tail.Overlay.DriverContext;
        ExInitializeWorkItem(WorkQueueItem,
                             PopPassivePowerCall,
                             Irp);
        ExQueueWorkItem(WorkQueueItem, DelayedWorkQueue);

        return STATUS_PENDING;
    }

    /* Direct call. Raise IRQL in debug to catch invalid paged memory access. */
#if DBG
    {
    KIRQL OldIrql;
    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
#endif

    Status = IoCallDriver(DeviceObject, Irp);

#if DBG
    KeLowerIrql(OldIrql);
    }
#endif

    return Status;
}

static IO_COMPLETION_ROUTINE PopRequestPowerIrpCompletion;

static
NTSTATUS
NTAPI
PopRequestPowerIrpCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_reads_opt_(_Inexpressible_("varies")) PVOID Context)
{
    PIO_STACK_LOCATION Stack;
    PREQUEST_POWER_COMPLETE CompletionRoutine;
    POWER_STATE PowerState;

    Stack = IoGetCurrentIrpStackLocation(Irp);
    CompletionRoutine = Context;

    PowerState.DeviceState = (ULONG_PTR)Stack->Parameters.Others.Argument3;

    if (CompletionRoutine)
    {
        CompletionRoutine(Stack->Parameters.Others.Argument1,
                          (UCHAR)(ULONG_PTR)Stack->Parameters.Others.Argument2,
                          PowerState,
                          Stack->Parameters.Others.Argument4,
                          &Irp->IoStatus);
    }

    IoSkipCurrentIrpStackLocation(Irp);
    IoFreeIrp(Irp);
    ObDereferenceObject(DeviceObject);

    return STATUS_MORE_PROCESSING_REQUIRED;
}

VOID
NTAPI
PopCleanupPowerState(_Inout_ PUCHAR PowerState)
{
    *PowerState = 0;
}

NTSTATUS
PopSendQuerySystemPowerState(PDEVICE_OBJECT DeviceObject, SYSTEM_POWER_STATE SystemState, POWER_ACTION PowerAction)
{
    KEVENT Event;
    IO_STATUS_BLOCK IoStatusBlock;
    PIO_STACK_LOCATION IrpSp;
    PIRP Irp;
    NTSTATUS Status;

    KeInitializeEvent(&Event,
                      NotificationEvent,
                      FALSE);

    Irp = IoBuildSynchronousFsdRequest(IRP_MJ_POWER,
                                       DeviceObject,
                                       NULL,
                                       0,
                                       NULL,
                                       &Event,
                                       &IoStatusBlock);
    if (!Irp) return STATUS_INSUFFICIENT_RESOURCES;

    IrpSp = IoGetNextIrpStackLocation(Irp);
    IrpSp->MinorFunction = IRP_MN_QUERY_POWER;
    IrpSp->Parameters.Power.Type = SystemPowerState;
    IrpSp->Parameters.Power.State.SystemState = SystemState;
    IrpSp->Parameters.Power.ShutdownType = PowerAction;

    Status = PoCallDriver(DeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event,
                              Executive,
                              KernelMode,
                              FALSE,
                              NULL);
        Status = IoStatusBlock.Status;
    }

    return Status;
}

NTSTATUS
PopSendSetSystemPowerState(PDEVICE_OBJECT DeviceObject, SYSTEM_POWER_STATE SystemState, POWER_ACTION PowerAction)
{
    KEVENT Event;
    IO_STATUS_BLOCK IoStatusBlock;
    PIO_STACK_LOCATION IrpSp;
    PIRP Irp;
    NTSTATUS Status;

    KeInitializeEvent(&Event,
                      NotificationEvent,
                      FALSE);

    Irp = IoBuildSynchronousFsdRequest(IRP_MJ_POWER,
                                       DeviceObject,
                                       NULL,
                                       0,
                                       NULL,
                                       &Event,
                                       &IoStatusBlock);
    if (!Irp) return STATUS_INSUFFICIENT_RESOURCES;

    IrpSp = IoGetNextIrpStackLocation(Irp);
    IrpSp->MinorFunction = IRP_MN_SET_POWER;
    IrpSp->Parameters.Power.Type = SystemPowerState;
    IrpSp->Parameters.Power.State.SystemState = SystemState;
    IrpSp->Parameters.Power.ShutdownType = PowerAction;

    Status = PoCallDriver(DeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event,
                              Executive,
                              KernelMode,
                              FALSE,
                              NULL);
        Status = IoStatusBlock.Status;
    }

    return Status;
}

NTSTATUS
PopQuerySystemPowerStateTraverse(PDEVICE_NODE DeviceNode,
                                 PVOID Context)
{
    PPOWER_STATE_TRAVERSE_CONTEXT PowerStateContext = Context;
    PDEVICE_OBJECT TopDeviceObject;
    NTSTATUS Status;

    DPRINT("PopQuerySystemPowerStateTraverse(%p, %p)\n", DeviceNode, Context);

    if (DeviceNode == IopRootDeviceNode)
        return STATUS_SUCCESS;

    if (DeviceNode->Flags & DNF_LEGACY_DRIVER)
        return STATUS_SUCCESS;

    TopDeviceObject = IoGetAttachedDeviceReference(DeviceNode->PhysicalDeviceObject);

    Status = PopSendQuerySystemPowerState(TopDeviceObject,
                                          PowerStateContext->SystemPowerState,
                                          PowerStateContext->PowerAction);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Device '%wZ' failed IRP_MN_QUERY_POWER\n", &DeviceNode->InstancePath);
    }
    ObDereferenceObject(TopDeviceObject);

    return Status;
}

NTSTATUS
PopSetSystemPowerStateTraverse(PDEVICE_NODE DeviceNode,
                               PVOID Context)
{
    PPOWER_STATE_TRAVERSE_CONTEXT PowerStateContext = Context;
    PDEVICE_OBJECT TopDeviceObject;
    NTSTATUS Status;

    DPRINT("PopSetSystemPowerStateTraverse(%p, %p)\n", DeviceNode, Context);

    if (DeviceNode == IopRootDeviceNode)
        return STATUS_SUCCESS;

    if (DeviceNode->PhysicalDeviceObject == PowerStateContext->PowerDevice)
        return STATUS_SUCCESS;

    if (DeviceNode->Flags & DNF_LEGACY_DRIVER)
        return STATUS_SUCCESS;

    TopDeviceObject = IoGetAttachedDeviceReference(DeviceNode->PhysicalDeviceObject);
    if (TopDeviceObject == PowerStateContext->PowerDevice)
    {
        ObDereferenceObject(TopDeviceObject);
        return STATUS_SUCCESS;
    }

    Status = PopSendSetSystemPowerState(TopDeviceObject,
                                        PowerStateContext->SystemPowerState,
                                        PowerStateContext->PowerAction);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Device '%wZ' failed IRP_MN_SET_POWER\n", &DeviceNode->InstancePath);
    }

    ObDereferenceObject(TopDeviceObject);

    return Status;
}

static
NTSTATUS
PopTraverseSystemPowerState(
    _In_ SYSTEM_POWER_STATE SystemPowerState,
    _In_ POWER_ACTION PowerAction,
    _In_opt_ PDEVICE_OBJECT PowerDevice)
{
    DEVICETREE_TRAVERSE_CONTEXT Context;
    POWER_STATE_TRAVERSE_CONTEXT PowerContext;

    PowerContext.PowerAction = PowerAction;
    PowerContext.SystemPowerState = SystemPowerState;
    PowerContext.PowerDevice = PowerDevice;

    IopInitDeviceTreeTraverseContext(&Context,
                                    IopRootDeviceNode,
                                    PopSetSystemPowerStateTraverse,
                                    &PowerContext);

    return IopTraverseDeviceTree(&Context);
}

static
NTSTATUS
PopRestoreWorkingPowerState(
    _In_ POWER_ACTION PowerAction,
    _In_opt_ PDEVICE_OBJECT PowerDevice)
{
    NTSTATUS Status, DeviceStatus;

    Status = STATUS_SUCCESS;
    if (PowerDevice != NULL)
    {
        Status = PopSendSetSystemPowerState(PowerDevice,
                                            PowerSystemWorking,
                                            PowerAction);
    }

    DeviceStatus = PopTraverseSystemPowerState(PowerSystemWorking,
                                               PowerAction,
                                               PowerDevice);
    if (NT_SUCCESS(Status))
        Status = DeviceStatus;

    return Status;
}

/*
 * Entering a sleeping state (S1-S3 for sleep, S4 for hibernation) moves all
 * devices to a low power state. When it does not work out, or when the system
 * comes back, the devices have to be put back to D0 here.
 */
static
BOOLEAN
PopMustRestoreWorkingPowerState(
    _In_ SYSTEM_POWER_STATE PowerState,
    _In_ POWER_ACTION PowerAction)
{
    if (PowerAction == PowerActionSleep)
        return TRUE;

    return (PowerAction == PowerActionHibernate) &&
           (PowerState != PowerSystemWorking);
}

NTSTATUS
NTAPI
PopSetSystemPowerState(
    _In_ SYSTEM_POWER_STATE PowerState,
    _In_ POWER_ACTION PowerAction)
{
    PDEVICE_OBJECT DeviceObject;
    PDEVICE_OBJECT Fdo;
    NTSTATUS Status;
    DEVICETREE_TRAVERSE_CONTEXT Context;
    POWER_STATE_TRAVERSE_CONTEXT PowerContext;
    NTSTATUS RestoreStatus;
    BOOLEAN Restore = PopMustRestoreWorkingPowerState(PowerState, PowerAction);

    if (PowerAction == PowerActionSleep && !PopAcpiPresent)
    {
        DPRINT1("Cannot enter a sleep state without ACPI support\n");
        return STATUS_NOT_SUPPORTED;
    }

    Status = IopGetSystemPowerDeviceObject(&DeviceObject);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("No system power driver available\n");
        if (PowerAction == PowerActionSleep)
            return Status;

        Fdo = NULL;
    }
    else
    {
        Fdo = IoGetAttachedDeviceReference(DeviceObject);
        if (Fdo == DeviceObject)
        {
            DPRINT("An FDO was not attached\n");
            ObDereferenceObject(Fdo);
            return STATUS_UNSUCCESSFUL;
        }
    }

    /* Set up context */
    PowerContext.PowerAction = PowerAction;
    PowerContext.SystemPowerState = PowerState;
    PowerContext.PowerDevice = Fdo;

    /* Query for system power change */
    IopInitDeviceTreeTraverseContext(&Context,
                                     IopRootDeviceNode,
                                     PopQuerySystemPowerStateTraverse,
                                     &PowerContext);

    Status = IopTraverseDeviceTree(&Context);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Query system power state failed: 0x%lx\n", Status);
        if (PowerAction == PowerActionSleep)
        {
            if (Fdo != NULL)
                ObDereferenceObject(Fdo);
            return Status;
        }
    }

    /* Set system power change */
    Status = PopTraverseSystemPowerState(PowerState, PowerAction, Fdo);
    if (!NT_SUCCESS(Status) && Restore)
    {
        DPRINT1("Setting device power state failed: 0x%lx\n", Status);
        RestoreStatus = PopTraverseSystemPowerState(PowerSystemWorking,
                                                    PowerAction,
                                                    Fdo);
        if (!NT_SUCCESS(RestoreStatus))
            DPRINT1("Restoring device power states failed: 0x%lx\n",
                    RestoreStatus);
        if (Fdo != NULL)
            ObDereferenceObject(Fdo);
        return Status;
    }

    if (!PopAcpiPresent)
    {
        /* Without ACPI nobody can enter the state: the devices stay in D0 */
        if (Restore)
        {
            RestoreStatus = PopTraverseSystemPowerState(PowerSystemWorking,
                                                        PowerAction,
                                                        Fdo);
            if (!NT_SUCCESS(RestoreStatus))
                DPRINT1("Restoring device power states failed: 0x%lx\n",
                        RestoreStatus);
        }
        if (Fdo != NULL)
            ObDereferenceObject(Fdo);
        return STATUS_NOT_IMPLEMENTED;
    }

    if (Fdo != NULL)
    {
        if (PowerAction != PowerActionShutdownReset)
        {
            Status = PopSendSetSystemPowerState(Fdo, PowerState, PowerAction);
            if (!NT_SUCCESS(Status) && !Restore)
                DPRINT1("Setting system power state failed: 0x%lx\n", Status);
        }

        /*
         * For sleep this is the wake-up. For hibernation the machine normally
         * powers off here and never returns; if it does, entering S4 failed.
         */
        if (Restore)
        {
            if (!NT_SUCCESS(Status))
                DPRINT1("Entering system power state failed: 0x%lx\n", Status);

            RestoreStatus = PopRestoreWorkingPowerState(PowerAction, Fdo);
            if (!NT_SUCCESS(RestoreStatus))
                DPRINT1("Restoring working power state failed: 0x%lx\n",
                        RestoreStatus);
            else if (NT_SUCCESS(Status))
                Status = RestoreStatus;
        }

        ObDereferenceObject(Fdo);
    }
    else if (Restore)
    {
        /* No system power driver to talk to, but the devices were lowered */
        RestoreStatus = PopRestoreWorkingPowerState(PowerAction, NULL);
        if (!NT_SUCCESS(RestoreStatus))
            DPRINT1("Restoring working power state failed: 0x%lx\n",
                    RestoreStatus);
        if (NT_SUCCESS(Status))
            Status = RestoreStatus;
    }

    return Status;
}

CODE_SEG("INIT")
BOOLEAN
NTAPI
PoInitSystem(_In_ ULONG BootPhase)
{
    PVOID NotificationEntry;
    PCHAR CommandLine;
    BOOLEAN ForceAcpiDisable = FALSE;

    /* Check if this is phase 1 init */
    if (BootPhase == 1)
    {
        NTSTATUS Status;
        /* Register power button notification */
        Status = IoRegisterPlugPlayNotification(EventCategoryDeviceInterfaceChange,
                                                PNPNOTIFY_DEVICE_INTERFACE_INCLUDE_EXISTING_INTERFACES,
                                                (PVOID)&GUID_DEVICE_SYS_BUTTON,
                                                IopRootDeviceNode->PhysicalDeviceObject->DriverObject,
                                                PopAddRemoveSysCapsCallback,
                                                (PVOID)(ULONG_PTR)PolicyDeviceSystemButton,
                                                &NotificationEntry);
        if (!NT_SUCCESS(Status))
            return FALSE;

        /* Register lid notification */
        Status = IoRegisterPlugPlayNotification(EventCategoryDeviceInterfaceChange,
                                                PNPNOTIFY_DEVICE_INTERFACE_INCLUDE_EXISTING_INTERFACES,
                                                (PVOID)&GUID_DEVICE_LID,
                                                IopRootDeviceNode->PhysicalDeviceObject->DriverObject,
                                                PopAddRemoveSysCapsCallback,
                                                (PVOID)(ULONG_PTR)PolicyDeviceSystemButton,
                                                &NotificationEntry);
        if (!NT_SUCCESS(Status))
            return FALSE;

        /* Register battery notification */
        Status = IoRegisterPlugPlayNotification(EventCategoryDeviceInterfaceChange,
                                                PNPNOTIFY_DEVICE_INTERFACE_INCLUDE_EXISTING_INTERFACES,
                                                (PVOID)&GUID_DEVICE_BATTERY,
                                                IopRootDeviceNode->PhysicalDeviceObject->DriverObject,
                                                PopAddRemoveSysCapsCallback,
                                                (PVOID)(ULONG_PTR)PolicyDeviceBattery,
                                                &NotificationEntry);

        return NT_SUCCESS(Status);
    }

    /* Initialize the power capabilities */
    RtlZeroMemory(&PopCapabilities, sizeof(SYSTEM_POWER_CAPABILITIES));

    /* Get the command line. Some boot paths provide no load-options string. */
    CommandLine = (KeLoaderBlock != NULL) ? KeLoaderBlock->LoadOptions : NULL;

    /* Upcase and inspect it only when the loader supplied one. */
    if (CommandLine != NULL)
    {
        _strupr(CommandLine);
        if (strstr(CommandLine, "NOACPI")) ForceAcpiDisable = TRUE;
    }

    if (ForceAcpiDisable)
    {
        /* Set the ACPI State to False if it's been forced that way */
        PopAcpiPresent = FALSE;
    }
    else
    {
        /* Otherwise check if the LoaderBlock has a ACPI Table  */
        PopAcpiPresent = (KeLoaderBlock != NULL &&
                         KeLoaderBlock->Extension != NULL &&
                         KeLoaderBlock->Extension->AcpiTable != NULL);
    }

    /* Enable shutdown by power button */
    if (PopAcpiPresent)
        PopCapabilities.SystemS5 = TRUE;

    /* Initialize volume support */
    InitializeListHead(&PopVolumeDevices);
    KeInitializeGuardedMutex(&PopVolumeLock);

    /* Initialize support for dope */
    KeInitializeSpinLock(&PopDopeGlobalLock);

    /* Initialize support for shutdown waits and work-items */
    PopInitShutdownList();

    /* Hibernation state must exist before power requests can enter S4. */
    {
        NTSTATUS HiberStatus = PopHiberInitSystem();
        if (!NT_SUCCESS(HiberStatus))
            DPRINT1("PopHiberInitSystem failed (0x%08lx); hibernation disabled\n",
                    HiberStatus);
    }

    return TRUE;
}

VOID
NTAPI
PopPerfIdle(_In_ PPROCESSOR_POWER_STATE PowerState)
{
    DPRINT1("PerfIdle function: %p\n", PowerState);
}

VOID
NTAPI
PopPerfIdleDpc(_In_ PKDPC Dpc,
               _In_ PVOID DeferredContext,
               _In_ PVOID SystemArgument1,
               _In_ PVOID SystemArgument2)
{
    /* Call the Perf Idle function */
    PopPerfIdle(&((PKPRCB)DeferredContext)->PowerState);
}

VOID
FASTCALL
PopIdle0(_In_ PPROCESSOR_POWER_STATE PowerState)
{
    /* FIXME: Extremly naive implementation */
    HalProcessorIdle();
}

CODE_SEG("INIT")
VOID
NTAPI
PoInitializePrcb(_In_ PKPRCB Prcb)
{
    /* Initialize the Power State */
    RtlZeroMemory(&Prcb->PowerState, sizeof(Prcb->PowerState));
    Prcb->PowerState.Idle0KernelTimeLimit = 0xFFFFFFFF;
    Prcb->PowerState.CurrentThrottle = 100;
    Prcb->PowerState.CurrentThrottleIndex = 0;
    Prcb->PowerState.IdleFunction = PopIdle0;

    /* Initialize the Perf DPC and Timer */
    KeInitializeDpc(&Prcb->PowerState.PerfDpc, PopPerfIdleDpc, Prcb);
    KeSetTargetProcessorDpc(&Prcb->PowerState.PerfDpc, Prcb->Number);
    KeInitializeTimerEx(&Prcb->PowerState.PerfTimer, SynchronizationTimer);
}

/* PUBLIC FUNCTIONS **********************************************************/

/*
 * @unimplemented
 */
NTSTATUS
NTAPI
PoCancelDeviceNotify(_In_ PVOID NotifyBlock)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

/*
 * @unimplemented
 */
NTSTATUS
NTAPI
PoRegisterDeviceNotify(_Out_ PVOID Unknown0,
                       _In_ ULONG Unknown1,
                       _In_ ULONG Unknown2,
                       _In_ ULONG Unknown3,
                       _In_ PVOID Unknown4,
                       _In_ PVOID Unknown5)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

/*
 * @unimplemented
 */
VOID
NTAPI
PoShutdownBugCheck(_In_ BOOLEAN LogError,
                   _In_ ULONG BugCheckCode,
                   _In_ ULONG_PTR BugCheckParameter1,
                   _In_ ULONG_PTR BugCheckParameter2,
                   _In_ ULONG_PTR BugCheckParameter3,
                   _In_ ULONG_PTR BugCheckParameter4)
{
    DPRINT1("PoShutdownBugCheck called\n");

    /* FIXME: Log error if requested */
    /* FIXME: Initiate a shutdown */

    /* Bugcheck the system */
    KeBugCheckEx(BugCheckCode,
                 BugCheckParameter1,
                 BugCheckParameter2,
                 BugCheckParameter3,
                 BugCheckParameter4);
}

/*
 * @unimplemented
 */
VOID
NTAPI
PoSetHiberRange(IN PVOID HiberContext,
                IN ULONG Flags,
                IN OUT PVOID StartPage,
                IN ULONG Length,
                IN ULONG PageTag)
{
    PPOP_HIBER_CONTEXT Context = HiberContext;
    PPO_MEMORY_RANGE_ARRAY RangeArray;

    UNREFERENCED_PARAMETER(PageTag);

    if (Context == NULL)
    {
        Context = PopHiberContext;
    }

    if (Context == NULL || Length == 0 || StartPage == NULL)
        return;

    RangeArray = ExAllocatePoolWithTag(NonPagedPool,
                                       sizeof(*RangeArray),
                                       'RbiH');
    if (RangeArray == NULL)
        return;

    RangeArray->Range.PageNo = (PFN_NUMBER)(ULONG_PTR)StartPage;
    RangeArray->Range.StartPage = RangeArray->Range.PageNo;
    RangeArray->Range.EndPage = RangeArray->Range.PageNo + Length;
    RangeArray->Range.CheckSum = Flags ^ PageTag ^ Length;
    Context->NoRanges++;

    DPRINT("PoSetHiberRange: range %I64x-%I64x flags %lx\n",
           (ULONGLONG)RangeArray->Range.StartPage,
           (ULONGLONG)RangeArray->Range.EndPage,
           Flags);

    ExFreePool(RangeArray);
    return;
}

/*
 * @implemented
 */
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS
NTAPI
PoCallDriver(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ __drv_aliasesMem PIRP Irp)
{
    PIO_STACK_LOCATION NextStack;

    ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);

    ASSERT(DeviceObject);
    ASSERT(Irp);

    NextStack = IoGetNextIrpStackLocation(Irp);
    ASSERT(NextStack->MajorFunction == IRP_MJ_POWER);

    /* Set DeviceObject for PopPresentIrp */
    NextStack->DeviceObject = DeviceObject;

    /* Only QUERY_POWER and SET_POWER use special handling */
    if (NextStack->MinorFunction != IRP_MN_SET_POWER &&
        NextStack->MinorFunction != IRP_MN_QUERY_POWER)
    {
        return IoCallDriver(DeviceObject, Irp);
    }

    /* Call the next driver, either directly or at PASSIVE_LEVEL */
    return PopPresentIrp(NextStack, Irp);
}

/*
 * @unimplemented
 */
PULONG
NTAPI
PoRegisterDeviceForIdleDetection(_In_ PDEVICE_OBJECT DeviceObject,
                                 _In_ ULONG ConservationIdleTime,
                                 _In_ ULONG PerformanceIdleTime,
                                 _In_ DEVICE_POWER_STATE State)
{
    UNIMPLEMENTED;
    return NULL;
}

/*
 * @unimplemented
 */
PVOID
NTAPI
PoRegisterSystemState(_In_opt_ PVOID StateHandle,
                      _In_ EXECUTION_STATE Flags)
{
    UNIMPLEMENTED;
    return NULL;
}

/*
 * @implemented
 */
NTSTATUS
NTAPI
PoRequestPowerIrp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ UCHAR MinorFunction,
    _In_ POWER_STATE PowerState,
    _In_opt_ PREQUEST_POWER_COMPLETE CompletionFunction,
    _In_opt_ __drv_aliasesMem PVOID Context,
    _Outptr_opt_ PIRP *pIrp)
{
    PDEVICE_OBJECT TopDeviceObject;
    PIO_STACK_LOCATION Stack;
    PIRP Irp;

    if (MinorFunction != IRP_MN_QUERY_POWER
        && MinorFunction != IRP_MN_SET_POWER
        && MinorFunction != IRP_MN_WAIT_WAKE)
        return STATUS_INVALID_PARAMETER_2;

    /* Always call the top of the device stack */
    TopDeviceObject = IoGetAttachedDeviceReference(DeviceObject);

    Irp = IoAllocateIrp(TopDeviceObject->StackSize + 2, FALSE);
    if (!Irp)
    {
        ObDereferenceObject(TopDeviceObject);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    Irp->IoStatus.Information = 0;

    IoSetNextIrpStackLocation(Irp);

    Stack = IoGetNextIrpStackLocation(Irp);
    Stack->Parameters.Others.Argument1 = DeviceObject;
    Stack->Parameters.Others.Argument2 = (PVOID)(ULONG_PTR)MinorFunction;
    Stack->Parameters.Others.Argument3 = (PVOID)(ULONG_PTR)PowerState.DeviceState;
    Stack->Parameters.Others.Argument4 = Context;
    Stack->DeviceObject = TopDeviceObject;
    IoSetNextIrpStackLocation(Irp);

    Stack = IoGetNextIrpStackLocation(Irp);
    Stack->MajorFunction = IRP_MJ_POWER;
    Stack->MinorFunction = MinorFunction;
    if (MinorFunction == IRP_MN_WAIT_WAKE)
    {
        Stack->Parameters.WaitWake.PowerState = PowerState.SystemState;
    }
    else
    {
        Stack->Parameters.Power.Type = DevicePowerState;
        Stack->Parameters.Power.State = PowerState;
    }

    if (pIrp != NULL)
        *pIrp = Irp;

    IoSetCompletionRoutine(Irp, PopRequestPowerIrpCompletion, CompletionFunction, TRUE, TRUE, TRUE);
    PoCallDriver(TopDeviceObject, Irp);

    /* Always return STATUS_PENDING. The completion routine
     * will call CompletionFunction and complete the Irp.
     */
    return STATUS_PENDING;
}

/*
 * @unimplemented
 */
POWER_STATE
NTAPI
PoSetPowerState(_In_ PDEVICE_OBJECT DeviceObject,
                _In_ POWER_STATE_TYPE Type,
                _In_ POWER_STATE State)
{
    POWER_STATE ps;

    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);

    ps.SystemState = PowerSystemWorking;  // Fully on
    ps.DeviceState = PowerDeviceD0;       // Fully on

    return ps;
}

/*
 * @unimplemented
 */
VOID
NTAPI
PoSetSystemState(_In_ EXECUTION_STATE Flags)
{
    UNIMPLEMENTED;
}

/*
 * @unimplemented
 */
VOID
NTAPI
PoStartNextPowerIrp(_In_ PIRP Irp)
{
    UNIMPLEMENTED_ONCE;
}

/*
 * @unimplemented
 */
VOID
NTAPI
PoUnregisterSystemState(_In_ PVOID StateHandle)
{
    UNIMPLEMENTED;
}

/*
 * @unimplemented
 */
NTSTATUS
NTAPI
NtInitiatePowerAction(_In_ POWER_ACTION SystemAction,
                      _In_ SYSTEM_POWER_STATE MinSystemState,
                      _In_ ULONG Flags,
                      _In_ BOOLEAN Asynchronous)
{
    UNREFERENCED_PARAMETER(Asynchronous);
    return NtSetSystemPowerState(SystemAction, MinSystemState, Flags);
}

static
NTSTATUS
PopQueryBatteryState(
    _Out_ PSYSTEM_BATTERY_STATE BatteryState)
{
    static const UNICODE_STRING CompBattDeviceName =
        RTL_CONSTANT_STRING(L"\\Device\\CompositeBattery");
    OBJECT_ATTRIBUTES ObjectAttributes = RTL_CONSTANT_OBJECT_ATTRIBUTES(
        &CompBattDeviceName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE);
    HANDLE CompBattHandle;
    IO_STATUS_BLOCK IoStatusBlock;
    BATTERY_QUERY_INFORMATION BatteryQueryInfo = {0};
    BATTERY_INFORMATION BatteryInfo = { 0 };
    BATTERY_STATUS BatteryStatus;
    NTSTATUS Status;

    /* Open the CompositeBattery device */
    Status = ZwOpenFile(&CompBattHandle,
                        GENERIC_READ | SYNCHRONIZE,
                        &ObjectAttributes,
                        &IoStatusBlock,
                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                        FILE_SYNCHRONOUS_IO_NONALERT);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to open CompositeBattery device: Status: 0x%08lX\n", Status);
        return Status;
    }

    /* Query the battery tag */
    ULONG Wait = 0;
    Status = ZwDeviceIoControlFile(CompBattHandle,
                                   NULL,
                                   NULL,
                                   NULL,
                                   &IoStatusBlock,
                                   IOCTL_BATTERY_QUERY_TAG,
                                   &Wait,
                                   sizeof(Wait),
                                   &BatteryQueryInfo.BatteryTag,
                                   sizeof(BatteryQueryInfo.BatteryTag));
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to query battery tag: Status: 0x%08lX\n", Status);
        goto Exit;
    }

    /* Query the battery information */
    BatteryQueryInfo.InformationLevel = BatteryInformation;
    Status = ZwDeviceIoControlFile(CompBattHandle,
                                   NULL,
                                   NULL,
                                   NULL,
                                   &IoStatusBlock,
                                   IOCTL_BATTERY_QUERY_INFORMATION,
                                   &BatteryQueryInfo,
                                   sizeof(BatteryQueryInfo),
                                   &BatteryInfo,
                                   sizeof(BatteryInfo));
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to query battery information: Status: 0x%08lX\n", Status);
    }

    /* Query the battery status */
    BATTERY_WAIT_STATUS BatteryWait = {0};
    BatteryWait.BatteryTag = BatteryQueryInfo.BatteryTag;
    BatteryWait.PowerState = 0xF ; // Wait for all power states
    BatteryWait.HighCapacity = MAXULONG;
    Status = ZwDeviceIoControlFile(CompBattHandle,
                                   NULL,
                                   NULL,
                                   NULL,
                                   &IoStatusBlock,
                                   IOCTL_BATTERY_QUERY_STATUS,
                                   &BatteryWait,
                                   sizeof(BatteryWait),
                                   &BatteryStatus,
                                   sizeof(BatteryStatus));
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to query battery status: Status: 0x%08lX\n", Status);
        goto Exit;
    }

    RtlZeroMemory(BatteryState, sizeof(*BatteryState));
    BatteryState->BatteryPresent = TRUE;
    BatteryState->AcOnLine = BooleanFlagOn(BatteryStatus.PowerState, BATTERY_POWER_ON_LINE);
    BatteryState->Charging = BooleanFlagOn(BatteryStatus.PowerState, BATTERY_CHARGING);
    BatteryState->Discharging = BooleanFlagOn(BatteryStatus.PowerState, BATTERY_DISCHARGING);
    BatteryState->MaxCapacity = BatteryInfo.FullChargedCapacity;
    BatteryState->RemainingCapacity = BatteryStatus.Capacity;
    BatteryState->Rate = BatteryStatus.Rate;
    if (BatteryState->Discharging && BatteryStatus.Rate < 0)
    {
        BatteryState->EstimatedTime =
            3600 * BatteryInfo.FullChargedCapacity / -BatteryStatus.Rate;
    }
    else
    {
        BatteryState->EstimatedTime = 0;
    }

Exit:
    ZwClose(CompBattHandle);
    return Status;
}


/*
 * @unimplemented
 */
NTSTATUS
NTAPI
NtPowerInformation(_In_ POWER_INFORMATION_LEVEL PowerInformationLevel,
                   _In_reads_bytes_opt_(InputBufferLength) PVOID InputBuffer,
                   _In_ ULONG InputBufferLength,
                   _Out_writes_bytes_opt_(OutputBufferLength) PVOID OutputBuffer,
                   _In_ ULONG OutputBufferLength)
{
    NTSTATUS Status;
    KPROCESSOR_MODE PreviousMode = KeGetPreviousMode();

    PAGED_CODE();

    DPRINT("NtPowerInformation(PowerInformationLevel 0x%x, InputBuffer 0x%p, "
           "InputBufferLength 0x%x, OutputBuffer 0x%p, OutputBufferLength 0x%x)\n",
           PowerInformationLevel,
           InputBuffer, InputBufferLength,
           OutputBuffer, OutputBufferLength);

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForRead(InputBuffer, InputBufferLength, 1);
            ProbeForWrite(OutputBuffer, OutputBufferLength, sizeof(ULONG));
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    switch (PowerInformationLevel)
    {
        case SystemBatteryState:
        {
            SYSTEM_BATTERY_STATE BatteryState;

            if (InputBuffer != NULL)
                return STATUS_INVALID_PARAMETER;
            if (OutputBufferLength < sizeof(SYSTEM_BATTERY_STATE))
                return STATUS_BUFFER_TOO_SMALL;

            if (PopCapabilities.SystemBatteriesPresent)
            {
                /* Open the battery driver and query the battery state */
                Status = PopQueryBatteryState(&BatteryState);
                if (!NT_SUCCESS(Status))
                {
                    DPRINT1("Failed to query battery state: Status: 0x%08X\n", Status);
                    return Status;
                }
            }
            else
            {
                /* No batteries present, set defaults */
                RtlZeroMemory(&BatteryState, sizeof(BatteryState));
                BatteryState.AcOnLine = TRUE;
            }

            _SEH2_TRY
            {
                /* Copy the battery state to the output buffer */
                RtlCopyMemory(OutputBuffer, &BatteryState, sizeof(BatteryState));
                Status = STATUS_SUCCESS;
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                Status = _SEH2_GetExceptionCode();
            }
            _SEH2_END;

            break;
        }

        case SystemPowerCapabilities:
        {
            PSYSTEM_POWER_CAPABILITIES PowerCapabilities = (PSYSTEM_POWER_CAPABILITIES)OutputBuffer;

            if (InputBuffer != NULL)
                return STATUS_INVALID_PARAMETER;
            if (OutputBufferLength < sizeof(SYSTEM_POWER_CAPABILITIES))
                return STATUS_BUFFER_TOO_SMALL;

            Status = PopQuerySystemPowerCapabilities();
            if (!NT_SUCCESS(Status))
                return Status;

            _SEH2_TRY
            {
                RtlCopyMemory(PowerCapabilities,
                              &PopCapabilities,
                              sizeof(SYSTEM_POWER_CAPABILITIES));

                Status = STATUS_SUCCESS;
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                Status = _SEH2_GetExceptionCode();
            }
            _SEH2_END;

            break;
        }

        case ProcessorInformation:
        {
            PPROCESSOR_POWER_INFORMATION PowerInformation = (PPROCESSOR_POWER_INFORMATION)OutputBuffer;

            if (InputBuffer != NULL)
                return STATUS_INVALID_PARAMETER;
            if (OutputBufferLength < sizeof(PROCESSOR_POWER_INFORMATION))
                return STATUS_BUFFER_TOO_SMALL;

            /* FIXME: return structures for all processors */

            _SEH2_TRY
            {
                /* FIXME: some values are hardcoded */
                PowerInformation->Number = 0;
                PowerInformation->MaxMhz = 1000;
                PowerInformation->CurrentMhz = KeGetCurrentPrcb()->MHz;
                PowerInformation->MhzLimit = 1000;
                PowerInformation->MaxIdleState = 0;
                PowerInformation->CurrentIdleState = 0;

                Status = STATUS_SUCCESS;
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                Status = _SEH2_GetExceptionCode();
            }
            _SEH2_END;

            break;
        }

        default:
            Status = STATUS_NOT_IMPLEMENTED;
            DPRINT1("PowerInformationLevel 0x%x is UNIMPLEMENTED! Have a nice day.\n",
                    PowerInformationLevel);
            break;
    }

    return Status;
}

NTSTATUS
NTAPI
NtGetDevicePowerState(_In_ HANDLE Device,
                      _Out_ PDEVICE_POWER_STATE PowerState)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

BOOLEAN
NTAPI
NtIsSystemResumeAutomatic(VOID)
{
    UNIMPLEMENTED;
    return FALSE;
}

NTSTATUS
NTAPI
NtRequestWakeupLatency(_In_ LATENCY_TIME Latency)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
NtSetThreadExecutionState(_In_ EXECUTION_STATE esFlags,
                          _Out_ EXECUTION_STATE *PreviousFlags)
{
    PKTHREAD Thread = KeGetCurrentThread();
    KPROCESSOR_MODE PreviousMode = KeGetPreviousMode();
    EXECUTION_STATE PreviousState;
    PAGED_CODE();

    /* Validate flags */
    if (esFlags & ~(ES_CONTINUOUS | ES_USER_PRESENT))
    {
        /* Fail the request */
        return STATUS_INVALID_PARAMETER;
    }

    /* Check for user parameters */
    if (PreviousMode != KernelMode)
    {
        /* Protect the probes */
        _SEH2_TRY
        {
            /* Check if the pointer is valid */
            ProbeForWriteUlong(PreviousFlags);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            /* It isn't -- fail */
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    /* Save the previous state, always masking in the continous flag */
    PreviousState = Thread->PowerState | ES_CONTINUOUS;

    /* Check if we need to update the power state */
    if (esFlags & ES_CONTINUOUS) Thread->PowerState = (UCHAR)esFlags;

    /* Protect the write back to user mode */
    _SEH2_TRY
    {
        /* Return the previous flags */
        *PreviousFlags = PreviousState;
    }
    _SEH2_EXCEPT(ExSystemExceptionFilter())
    {
        /* Something's wrong, fail */
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    /* All is good */
    return STATUS_SUCCESS;
}

/*
 * The system time stood still while the machine was off. Take the time of the
 * RTC, the same way as boot does.
 */
static
VOID
PopResyncSystemTimeAfterResume(VOID)
{
    TIME_FIELDS TimeFields;
    LARGE_INTEGER LocalTime, SystemTime, OldTime;

    if (!HalQueryRealTimeClock(&TimeFields) ||
        !RtlTimeFieldsToTime(&TimeFields, &LocalTime))
    {
        DPRINT1("Could not read the RTC after resume\n");
        return;
    }

    ExLocalTimeToSystemTime(&LocalTime, &SystemTime);
    KeSetSystemTime(&SystemTime, &OldTime, FALSE, NULL);
    PoNotifySystemTimeSet();
}

NTSTATUS
NTAPI
NtSetSystemPowerState(_In_ POWER_ACTION SystemAction,
                      _In_ SYSTEM_POWER_STATE MinSystemState,
                      _In_ ULONG Flags)
{
    KPROCESSOR_MODE PreviousMode = KeGetPreviousMode();
    POP_POWER_ACTION Action = {0};
    NTSTATUS Status;
    ULONG Dummy;

    /* Check for invalid parameter combinations */
    if ((MinSystemState >= PowerSystemMaximum) ||
        (MinSystemState <= PowerSystemUnspecified) ||
        (SystemAction > PowerActionWarmEject) ||
        (SystemAction < PowerActionReserved) ||
        (Flags & ~(POWER_ACTION_QUERY_ALLOWED  |
                   POWER_ACTION_UI_ALLOWED     |
                   POWER_ACTION_OVERRIDE_APPS  |
                   POWER_ACTION_LIGHTEST_FIRST |
                   POWER_ACTION_LOCK_CONSOLE   |
                   POWER_ACTION_DISABLE_WAKES  |
                   POWER_ACTION_CRITICAL)))
    {
        DPRINT1("NtSetSystemPowerState: Bad parameters!\n");
        DPRINT1("                       SystemAction: 0x%x\n", SystemAction);
        DPRINT1("                       MinSystemState: 0x%x\n", MinSystemState);
        DPRINT1("                       Flags: 0x%x\n", Flags);
        return STATUS_INVALID_PARAMETER;
    }

    if (SystemAction == PowerActionSleep &&
        (MinSystemState < PowerSystemSleeping1 ||
         MinSystemState > PowerSystemSleeping3))
    {
        return STATUS_INVALID_PARAMETER;
    }

    /* Check for user caller */
    if (PreviousMode != KernelMode)
    {
        /* Check for shutdown permission */
        if (!SeSinglePrivilegeCheck(SeShutdownPrivilege, PreviousMode))
        {
            /* Not granted */
            DPRINT1("ERROR: Privilege not held for shutdown\n");
            return STATUS_PRIVILEGE_NOT_HELD;
        }

        /* Do it as a kernel-mode caller for consistency with system state */
        return ZwSetSystemPowerState(SystemAction, MinSystemState, Flags);
    }

    if (SystemAction == PowerActionSleep)
    {
        Status = PopQuerySystemPowerCapabilities();
        if (!NT_SUCCESS(Status))
            return Status;

        if ((MinSystemState == PowerSystemSleeping1 &&
             !PopCapabilities.SystemS1) ||
            (MinSystemState == PowerSystemSleeping2 &&
             !PopCapabilities.SystemS2) ||
            (MinSystemState == PowerSystemSleeping3 &&
             !PopCapabilities.SystemS3))
        {
            return STATUS_NOT_SUPPORTED;
        }
    }

    if (SystemAction == PowerActionSleep && PopStateCallout != NULL)
    {
        WIN32_POWERSTATE_PARAMETERS Parameters = {0};

        Parameters.SystemAction = SystemAction;
        Parameters.MinSystemState = MinSystemState;
        Parameters.Flags = Flags;
        Parameters.PowerStateTask = PowerState_QueryApps;
        Status = PopStateCallout(&Parameters);
        if (!NT_SUCCESS(Status))
        {
            Parameters.PowerStateTask = PowerState_QueryAppsFailed;
            (VOID)PopStateCallout(&Parameters);
            return Status;
        }

        Parameters.PowerStateTask = PowerState_QueryServices;
        Status = PopStateCallout(&Parameters);
        if (!NT_SUCCESS(Status))
        {
            Parameters.PowerStateTask = PowerState_QueryAppsFailed;
            (VOID)PopStateCallout(&Parameters);
            Parameters.PowerStateTask = PowerState_QueryServicesFailed;
            (VOID)PopStateCallout(&Parameters);
            return Status;
        }
    }

    /* Read policy settings (partial shutdown vs. full shutdown) */
    if (SystemAction == PowerActionShutdown) PopReadShutdownPolicy();

    /* Disable lazy flushing of registry */
    DPRINT("Stopping lazy flush\n");
    CmSetLazyFlushState(FALSE);

    /* Setup the power action */
    Action.Action = SystemAction;
    Action.Flags = Flags;
    Action.SystemState = MinSystemState;

    /* Notify callbacks */
    DPRINT("Notifying callbacks\n");
    ExNotifyCallback(PowerStateCallback, (PVOID)3, NULL);

    /* Swap in any worker thread stacks */
    DPRINT("Swapping worker threads\n");
    ExSwapinWorkerThreads(FALSE);

    /* Make our action global */
    PopAction = Action;

    /* Start power loop */
    Status = STATUS_CANCELLED;
    while (TRUE)
    {
        /* Break out if there's nothing to do */
        if (Action.Action == PowerActionNone) break;

        /* Check for first-pass or restart */
        if (Status == STATUS_CANCELLED)
        {
            /* Check for shutdown action */
            if ((PopAction.Action == PowerActionShutdown) ||
                (PopAction.Action == PowerActionShutdownReset) ||
                (PopAction.Action == PowerActionShutdownOff))
            {
                /* Set the action */
                PopAction.Shutdown = TRUE;
            }

            /* Now we are good to go */
            Status = STATUS_SUCCESS;
        }

        /* Check if we're still in an invalid status */
        if (!NT_SUCCESS(Status)) break;

        /* Flush all volumes and the registry */
        DPRINT("Flushing volumes\n");
        PopFlushVolumes(PopAction.Shutdown);

#ifndef NEWCC
        /* Flush dirty cache pages */
        /* XXX: Is that still mandatory? As now we'll wait on lazy writer to complete? */
        CcRosFlushDirtyPages(MAXULONG, &Dummy, TRUE, FALSE);
        DPRINT("Cache flushed %lu pages\n", Dummy);
#else
        Dummy = 0;
#endif

        /* Set IRP for drivers */
        PopAction.IrpMinor = IRP_MN_SET_POWER;
        if (PopAction.Action == PowerActionHibernate)
        {
            Status = PopHiberOpenFile(0);
            if (!NT_SUCCESS(Status))
            {
                DPRINT1("Could not open the hibernation file: 0x%lx\n", Status);
                break;
            }

            Status = PopHiberSystemHandler(PopHiberContext);
            if (!NT_SUCCESS(Status))
            {
                PopHiberCloseFile();
                break;
            }

            if (PopHiberContext->ResumeFlag)
            {
                NTSTATUS InvalidateStatus;

                PopHiberContext->ResumeFlag = 0;
                PopResyncSystemTimeAfterResume();
                Status = PopSetSystemPowerState(PowerSystemWorking,
                                                PowerActionHibernate);

                PopHiberContext->ImageHeader.Flags &=
                    ~HIBERFILE_FLAG_IMAGE_COMPLETE;
                InvalidateStatus =
                    PopHiberWriteImageHeader(PopHiberContext);
                if (!NT_SUCCESS(InvalidateStatus))
                {
                    DPRINT1("Could not invalidate the resumed hibernation image: 0x%lx\n",
                            InvalidateStatus);
                    if (NT_SUCCESS(Status))
                        Status = InvalidateStatus;
                }

                PopHiberCloseFile();
                break;
            }

            if (!(PopHiberContext->ImageHeader.Flags &
                  HIBERFILE_FLAG_IMAGE_COMPLETE))
            {
                DPRINT1("Hibernation image is incomplete; refusing to power off\n");
                Status = STATUS_NOT_SUPPORTED;
                PopHiberCloseFile();
                break;
            }

            DPRINT("Entering system hibernation state\n");
            Status = PopSetSystemPowerState(PowerSystemHibernate,
                                            PowerActionHibernate);

            /* Reaching this point means the system did not remain in S4. */
            PopHiberContext->ImageHeader.Flags &=
                ~HIBERFILE_FLAG_IMAGE_COMPLETE;
            {
                NTSTATUS InvalidateStatus;

                InvalidateStatus = PopHiberWriteImageHeader(PopHiberContext);
                if (!NT_SUCCESS(InvalidateStatus))
                {
                    DPRINT1("Could not invalidate the hibernation image: 0x%lx\n",
                            InvalidateStatus);
                    if (NT_SUCCESS(Status))
                        Status = InvalidateStatus;
                }
            }

            Status = PopHiberFinished(Status);
            PopHiberCloseFile();
            break;
        }

        if (PopAction.Action == PowerActionSleep)
        {
            if ((PopAction.SystemState < PowerSystemSleeping1) ||
                (PopAction.SystemState > PowerSystemSleeping3))
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            DPRINT("Entering system sleep state %u\n", PopAction.SystemState);
            if (PopStateCallout != NULL)
            {
                WIN32_POWERSTATE_PARAMETERS Parameters = {0};

                Parameters.SystemAction = PopAction.Action;
                Parameters.MinSystemState = PopAction.SystemState;
                Parameters.Flags = PopAction.Flags;
                Parameters.PowerStateTask = PowerState_SuspendApps;
                Status = PopStateCallout(&Parameters);
                if (!NT_SUCCESS(Status))
                    DPRINT1("Suspend-app notification failed: 0x%lx\n", Status);

                Parameters.PowerStateTask = PowerState_SuspendServices;
                Status = PopStateCallout(&Parameters);
                if (!NT_SUCCESS(Status))
                    DPRINT1("Suspend-service notification failed: 0x%lx\n", Status);
            }

            Status = PopEnterSleepState(PopAction.SystemState);
            if (PopStateCallout != NULL)
            {
                WIN32_POWERSTATE_PARAMETERS Parameters = {0};

                Parameters.SystemAction = PopAction.Action;
                Parameters.MinSystemState = PopAction.SystemState;
                Parameters.Flags = PopAction.Flags;
                Parameters.PowerStateTask = PowerState_ResumeServices;
                (VOID)PopStateCallout(&Parameters);
                Parameters.PowerStateTask = PowerState_ResumeApps;
                (VOID)PopStateCallout(&Parameters);
            }
            break;
        }

        if (PopAction.Shutdown)
        {
            DPRINT("Queueing shutdown thread\n");
            /* Check if we are running in the system context */
            if (PsGetCurrentProcess() != PsInitialSystemProcess)
            {
                /* We're not, so use a worker thread for shutdown */
                ExInitializeWorkItem(&PopShutdownWorkItem,
                                     &PopGracefulShutdown,
                                     NULL);

                ExQueueWorkItem(&PopShutdownWorkItem, CriticalWorkQueue);

                /* Spend us -- when we wake up, the system is good to go down */
                KeSuspendThread(KeGetCurrentThread());
                Status = STATUS_SYSTEM_SHUTDOWN;
                goto Exit;

            }
            else
            {
                /* Do the shutdown inline */
                PopGracefulShutdown(NULL);
            }
        }

        /* You should not have made it this far */
        // ASSERTMSG("System is still up and running?!\n", FALSE);
        DPRINT1("System is still up and running, you may not have chosen a yet supported power option: %u\n", PopAction.Action);
        break;
    }

Exit:
    if (SystemAction == PowerActionSleep)
    {
        /* Sleeping temporarily disables lazy registry flushing. */
        CmSetLazyFlushState(TRUE);
    }

    /* We're done, return */
    return Status;
}
