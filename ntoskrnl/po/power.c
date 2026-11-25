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
/* Retain each PnP notification registration so phase-1 failure can roll back. */
static PVOID PopPowerButtonNotification;
static PVOID PopLidNotification;
static PVOID PopBatteryNotification;

/*
 * NT 5.x execution-state and Vista power-setting registrations share one
 * phase-0 initialized registry and one handle-validation/lifetime domain.
 */
typedef enum _POP_POWER_POLICY_REGISTRATION_TYPE
{
    PopPolicyRegistrationSystemState,
    PopPolicyRegistrationPowerSetting
} POP_POWER_POLICY_REGISTRATION_TYPE;

typedef struct _POP_POWER_POLICY_REGISTRATION
{
    LIST_ENTRY ListEntry;
    POP_POWER_POLICY_REGISTRATION_TYPE Type;
    union
    {
        struct { EXECUTION_STATE Flags; } SystemState;
        struct
        {
            PDEVICE_OBJECT DeviceObject;
            GUID SettingGuid;
            PPOWER_SETTING_CALLBACK Callback;
            PVOID Context;
        } PowerSetting;
    } u;
} POP_POWER_POLICY_REGISTRATION, *PPOP_POWER_POLICY_REGISTRATION;

static KSPIN_LOCK PopPowerPolicyLock;
static LIST_ENTRY PopPowerPolicyList;
static KSPIN_LOCK PopDeviceIdleLock;
static LIST_ENTRY PopDeviceIdleList;
static BOOLEAN PopPhase0Initialized;

typedef struct _POP_DEVICE_IDLE_REGISTRATION
{
    LIST_ENTRY ListEntry;
    PDEVICE_OBJECT DeviceObject;
    KTIMER Timer;
    KDPC Dpc;
    WORK_QUEUE_ITEM WorkItem;
    KEVENT WorkComplete;
    volatile LONG WorkQueued;
    volatile LONG Removing;
    volatile ULONG IdleCount;
    ULONG LastIdleCount;
    ULONG ConservationIdleTime;
    ULONG PerformanceIdleTime;
    ULONG IdleThreshold;
    DEVICE_POWER_STATE TargetState;
    DEVICE_POWER_STATE CurrentState;
    ULONGLONG LastActivityTime;
} POP_DEVICE_IDLE_REGISTRATION, *PPOP_DEVICE_IDLE_REGISTRATION;

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
    PopCapabilities.SystemS4 = PopCapabilities.HiberFilePresent;
    PopCapabilities.SystemS5 = FALSE;
    PopCapabilities.FullWake = FALSE;
    PopCapabilities.AcOnLineWake = PowerSystemUnspecified;
    PopCapabilities.SoftLidWake = PowerSystemUnspecified;
    PopCapabilities.RtcWake = PowerSystemUnspecified;
    PopCapabilities.MinDeviceWakeState = PowerSystemUnspecified;
    PopCapabilities.DefaultLowLatencyWake = PowerSystemUnspecified;

    if (!PopAcpiPresent)
        return STATUS_SUCCESS;

    Status = IopGetSystemPowerDeviceObject(&DeviceObject);
    if (!NT_SUCCESS(Status))
        return STATUS_SUCCESS;

    TopDeviceObject = IoGetAttachedDeviceReference(DeviceObject);
    if (TopDeviceObject == DeviceObject)
    {
        ObDereferenceObject(TopDeviceObject);
        ObDereferenceObject(DeviceObject);
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
        ObDereferenceObject(DeviceObject);
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
    ObDereferenceObject(DeviceObject);

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

    /*
     * The ACPI capability IOCTL used by this NT 5.x implementation only
     * reports S1, S2 and S5.  Do not infer S3 or S4 from ACPI presence:
     * this kernel has no S3 capability bit, and hibernation initialization
     * alone does not mean that a hibernation file is configured.
     */
    PopCapabilities.SystemS3 = FALSE;
    PopCapabilities.SystemS4 = PopCapabilities.HiberFilePresent;

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

static VOID NTAPI PopDeviceIdleDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2);

static VOID NTAPI PopDeviceIdleWorker(_In_ PVOID Context);

static NTSTATUS
PopSendDevicePowerState(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ DEVICE_POWER_STATE PowerState)
{
    PDEVICE_OBJECT TopDeviceObject;
    KEVENT Event;
    IO_STATUS_BLOCK IoStatusBlock;
    PIRP Irp;
    PIO_STACK_LOCATION Stack;
    NTSTATUS Status;

    TopDeviceObject = IoGetAttachedDeviceReference(DeviceObject);
    if (TopDeviceObject == NULL)
        return STATUS_NO_SUCH_DEVICE;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildSynchronousFsdRequest(IRP_MJ_POWER,
                                       TopDeviceObject,
                                       NULL,
                                       0,
                                       NULL,
                                       &Event,
                                       &IoStatusBlock);
    if (Irp == NULL)
    {
        ObDereferenceObject(TopDeviceObject);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Stack = IoGetNextIrpStackLocation(Irp);
    Stack->MinorFunction = IRP_MN_SET_POWER;
    Stack->Parameters.Power.Type = DevicePowerState;
    Stack->Parameters.Power.State.DeviceState = PowerState;
    Stack->Parameters.Power.ShutdownType = PowerActionNone;

    Status = PoCallDriver(TopDeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatusBlock.Status;
    }

    ObDereferenceObject(TopDeviceObject);
    return Status;
}

static VOID NTAPI
PopDeviceIdleDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PPOP_DEVICE_IDLE_REGISTRATION Registration = DeferredContext;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    if (Registration == NULL || Registration->Removing)
        return;

    if (InterlockedCompareExchange(&Registration->WorkQueued, 1, 0) == 0)
    {
        KeClearEvent(&Registration->WorkComplete);
        ExQueueWorkItem(&Registration->WorkItem, DelayedWorkQueue);
    }
}

static VOID NTAPI
PopDeviceIdleWorker(_In_ PVOID Context)
{
    PPOP_DEVICE_IDLE_REGISTRATION Registration = Context;
    ULONG IdleCount;
    ULONGLONG Now;
    ULONGLONG IdleDuration;
    DEVICE_POWER_STATE RequestedState;
    KIRQL OldIrql;
    LARGE_INTEGER DueTime;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

    if (!Registration->Removing)
    {
        IdleCount = Registration->IdleCount;
        Now = KeQueryInterruptTime();

        if (IdleCount != Registration->LastIdleCount)
        {
            Registration->LastIdleCount = IdleCount;
            Registration->LastActivityTime = Now;
            RequestedState = PowerDeviceD0;
        }
        else
        {
            IdleDuration = Now - Registration->LastActivityTime;
            RequestedState =
                (IdleDuration >= ((ULONGLONG)Registration->IdleThreshold *
                                  10000000ULL)) ?
                Registration->TargetState : Registration->CurrentState;
        }

        if (!Registration->Removing &&
            RequestedState != Registration->CurrentState)
        {
            NTSTATUS Status = PopSendDevicePowerState(
                Registration->DeviceObject,
                RequestedState);

            if (NT_SUCCESS(Status))
                Registration->CurrentState = RequestedState;
            else
                DPRINT1("Power idle transition for %p to D%u failed: 0x%08lx\n",
                        Registration->DeviceObject,
                        RequestedState,
                        Status);
        }
    }

    /*
     * Rearm a one-shot timer only after this policy pass.  This prevents the
     * same WORK_QUEUE_ITEM from being queued while its previous invocation
     * is still returning.
     */
    InterlockedExchange(&Registration->WorkQueued, 0);
    KeAcquireSpinLock(&PopDeviceIdleLock, &OldIrql);
    if (!Registration->Removing)
    {
        DueTime.QuadPart = -10000000LL;
        KeSetTimerEx(&Registration->Timer, DueTime, 0, &Registration->Dpc);
    }
    KeReleaseSpinLock(&PopDeviceIdleLock, OldIrql);
    KeSetEvent(&Registration->WorkComplete, IO_NO_INCREMENT, FALSE);
}

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

    if ((UCHAR)(ULONG_PTR)Stack->Parameters.Others.Argument2 == IRP_MN_WAIT_WAKE)
        PowerState.SystemState = (ULONG_PTR)Stack->Parameters.Others.Argument3;
    else
        PowerState.DeviceState = (ULONG_PTR)Stack->Parameters.Others.Argument3;

    if (CompletionRoutine)
    {
        CompletionRoutine(Stack->Parameters.Others.Argument1,
                          (UCHAR)(ULONG_PTR)Stack->Parameters.Others.Argument2,
                          PowerState,
                          Stack->Parameters.Others.Argument4,
                          &Irp->IoStatus);
    }

    /*
     * PoRequestPowerIrp holds independent references on the original PDO
     * and the top of its stack.  The completion callback's DeviceObject is
     * the top-of-stack reference; Argument1 carries the original reference.
     */
    ObDereferenceObject(Stack->Parameters.Others.Argument1);
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
    PDEVICE_OBJECT DeviceObject = NULL;
    PDEVICE_OBJECT Fdo = NULL;
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
            ObDereferenceObject(DeviceObject);
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
            if (DeviceObject != NULL)
                ObDereferenceObject(DeviceObject);
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
        if (DeviceObject != NULL)
            ObDereferenceObject(DeviceObject);
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
        if (DeviceObject != NULL)
            ObDereferenceObject(DeviceObject);
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
    if (DeviceObject != NULL)
        ObDereferenceObject(DeviceObject);

    if (Fdo == NULL && Restore)
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

/*
 * Validate and dispatch a system sleep request.  Transition details,
 * including suspend/resume callouts and device-state rollback, live in
 * sleep.c so the Power Manager entry point remains small and stable.
 */
NTSTATUS
NTAPI
PoSleepSystem(
    _In_ SYSTEM_POWER_STATE SystemState
)
{
    NTSTATUS Status;

    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return STATUS_INVALID_DEVICE_STATE;

    if (SystemState < PowerSystemSleeping1 ||
        SystemState > PowerSystemSleeping3)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (!PopAcpiPresent)
        return STATUS_NOT_SUPPORTED;

    Status = PopQuerySystemPowerCapabilities();
    if (!NT_SUCCESS(Status))
        return Status;

    if ((SystemState == PowerSystemSleeping1 && !PopCapabilities.SystemS1) ||
        (SystemState == PowerSystemSleeping2 && !PopCapabilities.SystemS2) ||
        (SystemState == PowerSystemSleeping3 && !PopCapabilities.SystemS3))
    {
        return STATUS_NOT_SUPPORTED;
    }

    return PopEnterSleepState(SystemState);
}

CODE_SEG("INIT")
BOOLEAN
NTAPI
PoInitSystem(_In_ ULONG BootPhase)
{
    PCHAR CommandLine;
    PCHAR Option;
    BOOLEAN ForceAcpiDisable = FALSE;

    /* Check if this is phase 1 init */
    if (BootPhase == 1)
    {
        NTSTATUS Status;

        /* Phase 1 depends on all phase-0 locks, lists and hibernation state. */
        if (!PopPhase0Initialized)
            return FALSE;

        /* Phase 1 may be retried after a registration or query failure. */
        if (PopPowerButtonNotification != NULL &&
            PopLidNotification != NULL &&
            PopBatteryNotification != NULL)
        {
            Status = PopQuerySystemPowerCapabilities();
            if (!NT_SUCCESS(Status))
                DPRINT1("Power capability retry failed (0x%08lx)\n", Status);
            return TRUE;
        }

        /*
         * Recover from any stale partial registration set before trying
         * again.  Normal failure paths clear all three handles below.
         */
        if (PopBatteryNotification != NULL)
        {
            IoUnregisterPlugPlayNotification(PopBatteryNotification);
            PopBatteryNotification = NULL;
        }
        if (PopLidNotification != NULL)
        {
            IoUnregisterPlugPlayNotification(PopLidNotification);
            PopLidNotification = NULL;
        }
        if (PopPowerButtonNotification != NULL)
        {
            IoUnregisterPlugPlayNotification(PopPowerButtonNotification);
            PopPowerButtonNotification = NULL;
        }

        if (IopRootDeviceNode == NULL ||
            IopRootDeviceNode->PhysicalDeviceObject == NULL ||
            IopRootDeviceNode->PhysicalDeviceObject->DriverObject == NULL)
        {
            return FALSE;
        }

        /* Register power button notification. */
        Status = IoRegisterPlugPlayNotification(EventCategoryDeviceInterfaceChange,
                                                PNPNOTIFY_DEVICE_INTERFACE_INCLUDE_EXISTING_INTERFACES,
                                                (PVOID)&GUID_DEVICE_SYS_BUTTON,
                                                IopRootDeviceNode->PhysicalDeviceObject->DriverObject,
                                                PopAddRemoveSysCapsCallback,
                                                (PVOID)(ULONG_PTR)PolicyDeviceSystemButton,
                                                &PopPowerButtonNotification);
        if (!NT_SUCCESS(Status))
            goto Failure;

        /* Register lid notification. */
        Status = IoRegisterPlugPlayNotification(EventCategoryDeviceInterfaceChange,
                                                PNPNOTIFY_DEVICE_INTERFACE_INCLUDE_EXISTING_INTERFACES,
                                                (PVOID)&GUID_DEVICE_LID,
                                                IopRootDeviceNode->PhysicalDeviceObject->DriverObject,
                                                PopAddRemoveSysCapsCallback,
                                                (PVOID)(ULONG_PTR)PolicyDeviceSystemButton,
                                                &PopLidNotification);
        if (!NT_SUCCESS(Status))
            goto Failure;

        /* Register battery notification. */
        Status = IoRegisterPlugPlayNotification(EventCategoryDeviceInterfaceChange,
                                                PNPNOTIFY_DEVICE_INTERFACE_INCLUDE_EXISTING_INTERFACES,
                                                (PVOID)&GUID_DEVICE_BATTERY,
                                                IopRootDeviceNode->PhysicalDeviceObject->DriverObject,
                                                PopAddRemoveSysCapsCallback,
                                                (PVOID)(ULONG_PTR)PolicyDeviceBattery,
                                                &PopBatteryNotification);
        if (!NT_SUCCESS(Status))
            goto Failure;

        /*
         * Query only after the ACPI/PnP stack is available. Failure is
         * non-fatal: conservative zeroed capabilities remain in force.
         */
        Status = PopQuerySystemPowerCapabilities();
        if (!NT_SUCCESS(Status))
            DPRINT1("Power capability query failed (0x%08lx); using conservative defaults\n",
                    Status);

        return TRUE;

Failure:
        /* Never leave a partially registered notification set behind. */
        if (PopBatteryNotification != NULL)
        {
            IoUnregisterPlugPlayNotification(PopBatteryNotification);
            PopBatteryNotification = NULL;
        }
        if (PopLidNotification != NULL)
        {
            IoUnregisterPlugPlayNotification(PopLidNotification);
            PopLidNotification = NULL;
        }
        if (PopPowerButtonNotification != NULL)
        {
            IoUnregisterPlugPlayNotification(PopPowerButtonNotification);
            PopPowerButtonNotification = NULL;
        }

        DPRINT1("PoInitSystem(1): PnP notification registration failed (0x%08lx)\n",
                Status);
        return FALSE;
    }

    if (BootPhase != 0)
        return FALSE;

    if (PopPhase0Initialized)
        return TRUE;

    /* Initialize the power capabilities */
    RtlZeroMemory(&PopCapabilities, sizeof(SYSTEM_POWER_CAPABILITIES));

    /* Get the command line. Some boot paths provide no load-options string. */
    CommandLine = (KeLoaderBlock != NULL) ? KeLoaderBlock->LoadOptions : NULL;

    /* Upcase and inspect it only when the loader supplied one. */
    if (CommandLine != NULL)
    {
        /* Loader-owned load options are read-only from the kernel's point of view. */
        for (Option = CommandLine; *Option != '\0'; ++Option)
        {
            if (_strnicmp(Option, "NOACPI", sizeof("NOACPI") - 1) == 0)
            {
                ForceAcpiDisable = TRUE;
                break;
            }
        }
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

    /*
     * The unified policy registry is initialized exactly once in phase 0.
     * Phase 1 only verifies phase-0 completion and never resets this state.
     */
    KeInitializeSpinLock(&PopPowerPolicyLock);
    InitializeListHead(&PopPowerPolicyList);

    /* Initialize volume support */
    InitializeListHead(&PopVolumeDevices);
    KeInitializeGuardedMutex(&PopVolumeLock);

    /* Initialize support for device idle registrations and DOPE. */
    KeInitializeSpinLock(&PopDopeGlobalLock);
    KeInitializeSpinLock(&PopDeviceIdleLock);
    InitializeListHead(&PopDeviceIdleList);

    /* Initialize support for shutdown waits and work-items */
    PopInitShutdownList();

    /* Hibernation state must exist before power requests can enter S4. */
    {
        NTSTATUS HiberStatus = PopHiberInitSystem();
        if (!NT_SUCCESS(HiberStatus))
            DPRINT1("PopHiberInitSystem failed (0x%08lx); hibernation disabled\n",
                    HiberStatus);
    }

    PopPhase0Initialized = TRUE;
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
 * @implemented
 *
 * The idle counter is sampled once per second.  Any change records device
 * activity; after the shortest enabled idle policy interval the worker
 * requests the caller-selected low-power state.  Power IRPs are sent only
 * from the PASSIVE_LEVEL worker, never from the timer DPC.
 */
PULONG
NTAPI
PoRegisterDeviceForIdleDetection(_In_ PDEVICE_OBJECT DeviceObject,
                                 _In_ ULONG ConservationIdleTime,
                                 _In_ ULONG PerformanceIdleTime,
                                 _In_ DEVICE_POWER_STATE State)
{
    PPOP_DEVICE_IDLE_REGISTRATION Registration;
    PEXTENDED_DEVOBJ_EXTENSION DeviceExtension;
    ULONG IdleThreshold = MAXULONG;
    KIRQL OldIrql;
    PLIST_ENTRY Entry;
    LARGE_INTEGER DueTime;

    PAGED_CODE();

    if (!PopPhase0Initialized ||
        DeviceObject == NULL ||
        State <= PowerDeviceD0 ||
        State >= PowerDeviceMaximum)
        return NULL;

    if (ConservationIdleTime != 0 && ConservationIdleTime != MAXULONG)
        IdleThreshold = ConservationIdleTime;
    if (PerformanceIdleTime != 0 &&
        PerformanceIdleTime != MAXULONG &&
        PerformanceIdleTime < IdleThreshold)
        IdleThreshold = PerformanceIdleTime;

    /* Both policies disabled: there is no idle transition to register. */
    if (IdleThreshold == MAXULONG)
        return NULL;

    Registration = ExAllocatePoolWithTag(NonPagedPool,
                                         sizeof(*Registration),
                                         'iPoP');
    if (Registration == NULL)
        return NULL;

    RtlZeroMemory(Registration, sizeof(*Registration));
    Registration->DeviceObject = DeviceObject;
    Registration->ConservationIdleTime = ConservationIdleTime;
    Registration->PerformanceIdleTime = PerformanceIdleTime;
    Registration->IdleThreshold = IdleThreshold;
    Registration->TargetState = State;
    Registration->CurrentState = PowerDeviceD0;
    Registration->LastActivityTime = KeQueryInterruptTime();
    KeInitializeTimerEx(&Registration->Timer, NotificationTimer);
    KeInitializeDpc(&Registration->Dpc, PopDeviceIdleDpc, Registration);
    ExInitializeWorkItem(&Registration->WorkItem,
                         PopDeviceIdleWorker,
                         Registration);
    KeInitializeEvent(&Registration->WorkComplete, NotificationEvent, TRUE);

    ObReferenceObject(DeviceObject);
    DeviceExtension = IoGetDevObjExtension(DeviceObject);

    KeAcquireSpinLock(&PopDeviceIdleLock, &OldIrql);
    if (DeviceExtension->ExtensionFlags & DOE_DELETE_PENDING)
    {
        KeReleaseSpinLock(&PopDeviceIdleLock, OldIrql);
        ObDereferenceObject(DeviceObject);
        ExFreePoolWithTag(Registration, 'iPoP');
        return NULL;
    }

    /* One idle policy per device object; callers may not alias its counter. */
    for (Entry = PopDeviceIdleList.Flink;
         Entry != &PopDeviceIdleList;
         Entry = Entry->Flink)
    {
        PPOP_DEVICE_IDLE_REGISTRATION Existing =
            CONTAINING_RECORD(Entry, POP_DEVICE_IDLE_REGISTRATION, ListEntry);
        if (Existing->DeviceObject == DeviceObject)
        {
            KeReleaseSpinLock(&PopDeviceIdleLock, OldIrql);
            ObDereferenceObject(DeviceObject);
            ExFreePoolWithTag(Registration, 'iPoP');
            return NULL;
        }
    }

    InsertTailList(&PopDeviceIdleList, &Registration->ListEntry);

    /*
     * Start the one-shot timer before publishing the registration.  The
     * worker rearms it after each scan, avoiding concurrent reuse of the
     * embedded work item.
     */
    DueTime.QuadPart = -10000000LL;
    KeSetTimerEx(&Registration->Timer, DueTime, 0, &Registration->Dpc);
    KeReleaseSpinLock(&PopDeviceIdleLock, OldIrql);

    return (PULONG)&Registration->IdleCount;
}

/*
 * Called by IoDeleteDevice before the object is marked delete-pending.
 * Removing the registration from the list prevents new DPC/work references;
 * cancel + DPC flush + worker event wait drains anything already in flight.
 */
VOID
NTAPI
PoRemoveDevice(_In_ PDEVICE_OBJECT DeviceObject)
{
    PPOP_DEVICE_IDLE_REGISTRATION Registration = NULL;
    PEXTENDED_DEVOBJ_EXTENSION DeviceExtension;
    PLIST_ENTRY Entry;
    KIRQL OldIrql;

    PAGED_CODE();

    if (DeviceObject == NULL)
        return;

    DeviceExtension = IoGetDevObjExtension(DeviceObject);
    if (!PopPhase0Initialized)
    {
        DeviceExtension->ExtensionFlags |= DOE_DELETE_PENDING;
        return;
    }

    KeAcquireSpinLock(&PopDeviceIdleLock, &OldIrql);

    DeviceExtension->ExtensionFlags |= DOE_DELETE_PENDING;

    for (Entry = PopDeviceIdleList.Flink;
         Entry != &PopDeviceIdleList;
         Entry = Entry->Flink)
    {
        PPOP_DEVICE_IDLE_REGISTRATION Current =
            CONTAINING_RECORD(Entry, POP_DEVICE_IDLE_REGISTRATION, ListEntry);
        if (Current->DeviceObject == DeviceObject)
        {
            Registration = Current;
            InterlockedExchange(&Registration->Removing, 1);
            RemoveEntryList(&Registration->ListEntry);
            break;
        }
    }

    KeReleaseSpinLock(&PopDeviceIdleLock, OldIrql);

    if (Registration == NULL)
        return;

    KeCancelTimer(&Registration->Timer);
    KeFlushQueuedDpcs();
    KeWaitForSingleObject(&Registration->WorkComplete,
                          Executive,
                          KernelMode,
                          FALSE,
                          NULL);

    ObDereferenceObject(Registration->DeviceObject);
    ExFreePoolWithTag(Registration, 'iPoP');
}

/*
 * @implemented
 */
NTSTATUS
NTAPI
PopRegisterPowerSettingPolicy(
    _In_opt_ PDEVICE_OBJECT DeviceObject,
    _In_ LPCGUID SettingGuid,
    _In_ PPOWER_SETTING_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ PVOID *Handle)
{
    PPOP_POWER_POLICY_REGISTRATION Registration;
    KIRQL OldIrql;

    PAGED_CODE();
    if (Handle == NULL || SettingGuid == NULL || Callback == NULL)
        return STATUS_INVALID_PARAMETER;
    *Handle = NULL;
    if (!PopPhase0Initialized)
        return STATUS_DEVICE_NOT_READY;

    Registration = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Registration), 'sPoP');
    if (Registration == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(Registration, sizeof(*Registration));
    Registration->Type = PopPolicyRegistrationPowerSetting;
    Registration->u.PowerSetting.DeviceObject = DeviceObject;
    Registration->u.PowerSetting.SettingGuid = *SettingGuid;
    Registration->u.PowerSetting.Callback = Callback;
    Registration->u.PowerSetting.Context = Context;

    KeAcquireSpinLock(&PopPowerPolicyLock, &OldIrql);
    InsertTailList(&PopPowerPolicyList, &Registration->ListEntry);
    KeReleaseSpinLock(&PopPowerPolicyLock, OldIrql);
    *Handle = Registration;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
PopUnregisterPowerSettingPolicy(_In_ PVOID Handle)
{
    PLIST_ENTRY Entry;
    PPOP_POWER_POLICY_REGISTRATION Registration;
    KIRQL OldIrql;

    PAGED_CODE();
    if (Handle == NULL)
        return STATUS_INVALID_PARAMETER;
    if (!PopPhase0Initialized)
        return STATUS_DEVICE_NOT_READY;

    KeAcquireSpinLock(&PopPowerPolicyLock, &OldIrql);
    for (Entry = PopPowerPolicyList.Flink;
         Entry != &PopPowerPolicyList;
         Entry = Entry->Flink)
    {
        Registration = CONTAINING_RECORD(Entry, POP_POWER_POLICY_REGISTRATION, ListEntry);
        if ((PVOID)Registration == Handle &&
            Registration->Type == PopPolicyRegistrationPowerSetting)
        {
            RemoveEntryList(Entry);
            KeReleaseSpinLock(&PopPowerPolicyLock, OldIrql);
            ExFreePoolWithTag(Registration, 'sPoP');
            return STATUS_SUCCESS;
        }
    }
    KeReleaseSpinLock(&PopPowerPolicyLock, OldIrql);
    DPRINT1("PopUnregisterPowerSettingPolicy: invalid handle %p\n", Handle);
    return STATUS_INVALID_PARAMETER;
}

/*
 * @implemented
 */
PVOID
NTAPI
PoRegisterSystemState(_In_opt_ PVOID StateHandle,
                      _In_ EXECUTION_STATE Flags)
{
    const EXECUTION_STATE ValidFlags = ES_CONTINUOUS |
                                       ES_SYSTEM_REQUIRED |
                                       ES_DISPLAY_REQUIRED |
                                       ES_USER_PRESENT;
    PPOP_POWER_POLICY_REGISTRATION Registration;
    PLIST_ENTRY Entry;
    KIRQL OldIrql;

    PAGED_CODE();
    if ((Flags & ~ValidFlags) || !PopPhase0Initialized)
        return NULL;

    if (StateHandle != NULL)
    {
        KeAcquireSpinLock(&PopPowerPolicyLock, &OldIrql);
        for (Entry = PopPowerPolicyList.Flink;
             Entry != &PopPowerPolicyList;
             Entry = Entry->Flink)
        {
            Registration = CONTAINING_RECORD(Entry, POP_POWER_POLICY_REGISTRATION, ListEntry);
            if ((PVOID)Registration == StateHandle &&
                Registration->Type == PopPolicyRegistrationSystemState)
            {
                Registration->u.SystemState.Flags = Flags;
                KeReleaseSpinLock(&PopPowerPolicyLock, OldIrql);
                return StateHandle;
            }
        }
        KeReleaseSpinLock(&PopPowerPolicyLock, OldIrql);
        return NULL;
    }

    Registration = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Registration), 'sPoP');
    if (Registration == NULL)
        return NULL;
    RtlZeroMemory(Registration, sizeof(*Registration));
    Registration->Type = PopPolicyRegistrationSystemState;
    Registration->u.SystemState.Flags = Flags;

    KeAcquireSpinLock(&PopPowerPolicyLock, &OldIrql);
    InsertTailList(&PopPowerPolicyList, &Registration->ListEntry);
    KeReleaseSpinLock(&PopPowerPolicyLock, OldIrql);
    return Registration;
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

    if (pIrp != NULL)
        *pIrp = NULL;

    if (DeviceObject == NULL)
        return STATUS_INVALID_PARAMETER_1;

    if (MinorFunction != IRP_MN_QUERY_POWER &&
        MinorFunction != IRP_MN_SET_POWER &&
        MinorFunction != IRP_MN_WAIT_WAKE)
        return STATUS_INVALID_PARAMETER_2;

    /* Validate the union member before publishing an asynchronous request. */
    if (MinorFunction == IRP_MN_WAIT_WAKE)
    {
        /* A wait/wake request targets a sleep state, never shutdown. */
        if (PowerState.SystemState < PowerSystemSleeping1 ||
            PowerState.SystemState > PowerSystemHibernate)
            return STATUS_INVALID_PARAMETER_3;
    }
    else if (PowerState.DeviceState <= PowerDeviceUnspecified ||
             PowerState.DeviceState >= PowerDeviceMaximum)
    {
        return STATUS_INVALID_PARAMETER_3;
    }

    /*
     * Keep both objects alive until the completion routine runs.  The
     * original PDO is passed through Argument1; the attached top object is
     * the object used to send the IRP and is released by the completion.
     */
    ObReferenceObject(DeviceObject);
    TopDeviceObject = IoGetAttachedDeviceReference(DeviceObject);
    if (TopDeviceObject == NULL)
    {
        ObDereferenceObject(DeviceObject);
        return STATUS_NO_SUCH_DEVICE;
    }

    Irp = IoAllocateIrp(TopDeviceObject->StackSize + 2, FALSE);
    if (!Irp)
    {
        ObDereferenceObject(TopDeviceObject);
        ObDereferenceObject(DeviceObject);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    Irp->IoStatus.Information = 0;

    IoSetNextIrpStackLocation(Irp);

    Stack = IoGetNextIrpStackLocation(Irp);
    Stack->Parameters.Others.Argument1 = DeviceObject;
    Stack->Parameters.Others.Argument2 = (PVOID)(ULONG_PTR)MinorFunction;
    Stack->Parameters.Others.Argument3 =
        (PVOID)(ULONG_PTR)((MinorFunction == IRP_MN_WAIT_WAKE) ?
                           PowerState.SystemState : PowerState.DeviceState);
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
 * @implemented
 */
_IRQL_requires_max_(DISPATCH_LEVEL)
POWER_STATE
NTAPI
PoSetPowerState(_In_ PDEVICE_OBJECT DeviceObject,
                _In_ POWER_STATE_TYPE Type,
                _In_ POWER_STATE State)
{
    POWER_STATE PreviousState;
    KIRQL OldIrql;
    ULONG PowerFlags;

    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);

    if (DeviceObject == NULL)
    {
        PreviousState.SystemState = PowerSystemUnspecified;
        PreviousState.DeviceState = PowerDeviceUnspecified;
        return PreviousState;
    }

    /*
     * The low nibble stores the last reported system state and the next
     * nibble stores the device state. Preserve the remaining extension flags.
     * Serialize the read/modify/write because drivers may report state
     * changes concurrently.
     */
    KeAcquireSpinLock(&PopDopeGlobalLock, &OldIrql);
    PowerFlags = DeviceObject->DeviceObjectExtension->PowerFlags;
    PreviousState.SystemState = (SYSTEM_POWER_STATE)(PowerFlags & 0x0F);
    PreviousState.DeviceState = (DEVICE_POWER_STATE)((PowerFlags >> 4) & 0x0F);

    switch (Type)
    {
        case SystemPowerState:
            if (State.SystemState < PowerSystemMaximum)
            {
                PowerFlags = (PowerFlags & ~0x0FUL) |
                             ((ULONG)State.SystemState & 0x0FUL);
            }
            break;

        case DevicePowerState:
            if (State.DeviceState < PowerDeviceMaximum)
            {
                PowerFlags = (PowerFlags & ~0xF0UL) |
                             (((ULONG)State.DeviceState & 0x0FUL) << 4);
            }
            break;

        default:
            break;
    }

    DeviceObject->DeviceObjectExtension->PowerFlags = PowerFlags;
    KeReleaseSpinLock(&PopDopeGlobalLock, OldIrql);

    return PreviousState;
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
 * @implemented
 */
VOID
NTAPI
PoUnregisterSystemState(_In_ PVOID StateHandle)
{
    PLIST_ENTRY Entry;
    PPOP_POWER_POLICY_REGISTRATION Registration;
    KIRQL OldIrql;

    PAGED_CODE();
    if (StateHandle == NULL || !PopPhase0Initialized)
        return;

    KeAcquireSpinLock(&PopPowerPolicyLock, &OldIrql);
    for (Entry = PopPowerPolicyList.Flink;
         Entry != &PopPowerPolicyList;
         Entry = Entry->Flink)
    {
        Registration = CONTAINING_RECORD(Entry, POP_POWER_POLICY_REGISTRATION, ListEntry);
        if ((PVOID)Registration == StateHandle &&
            Registration->Type == PopPolicyRegistrationSystemState)
        {
            RemoveEntryList(Entry);
            KeReleaseSpinLock(&PopPowerPolicyLock, OldIrql);
            ExFreePoolWithTag(Registration, 'sPoP');
            return;
        }
    }
    KeReleaseSpinLock(&PopPowerPolicyLock, OldIrql);
    DPRINT1("PoUnregisterSystemState: invalid registration handle %p\n", StateHandle);
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
    PDEVICE_OBJECT DeviceObject;
    DEVICE_POWER_STATE CurrentState;
    KPROCESSOR_MODE PreviousMode = KeGetPreviousMode();
    KIRQL OldIrql;
    ULONG PowerFlags;
    NTSTATUS Status;

    PAGED_CODE();

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWrite(PowerState, sizeof(*PowerState), sizeof(UCHAR));
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    Status = ObReferenceObjectByHandle(Device,
                                       0,
                                       IoDeviceObjectType,
                                       PreviousMode,
                                       (PVOID *)&DeviceObject,
                                       NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    KeAcquireSpinLock(&PopDopeGlobalLock, &OldIrql);
    PowerFlags = DeviceObject->DeviceObjectExtension->PowerFlags;
    KeReleaseSpinLock(&PopDopeGlobalLock, OldIrql);

    CurrentState = (DEVICE_POWER_STATE)((PowerFlags >> 4) & 0x0F);
    if (CurrentState >= PowerDeviceMaximum)
        CurrentState = PowerDeviceUnspecified;

    _SEH2_TRY
    {
        *PowerState = CurrentState;
        Status = STATUS_SUCCESS;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    ObDereferenceObject(DeviceObject);
    return Status;
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
    if (esFlags & ~(ES_CONTINUOUS |
                    ES_SYSTEM_REQUIRED |
                    ES_DISPLAY_REQUIRED |
                    ES_USER_PRESENT))
    {
        /* Reject unsupported execution-state bits, but accept all NT 5.x bits. */
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
                BOOLEAN DeviceRestoreSucceeded;

                PopHiberContext->ResumeFlag = 0;
                PopResyncSystemTimeAfterResume();
                Status = PopSetSystemPowerState(PowerSystemWorking,
                                                PowerActionHibernate);
                DeviceRestoreSucceeded = NT_SUCCESS(Status);

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

                /*
                 * Relinquish boot-video ownership only after device power
                 * restoration and hibernation-file cleanup have completed.
                 * Keep VIDEOPRT's registered reset callback intact so the
                 * resumed WIN32K display stack can reclaim the adapter.
                 */
                if (DeviceRestoreSucceeded)
                    InbvSetDisplayOwnership(FALSE);

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
            Status = PoSleepSystem(PopAction.SystemState);
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
