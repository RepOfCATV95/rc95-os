#include <ntdef.h>
#include <ntifs.h>
#include <debug.h>

#define TAG_POFX 'xFoP'

typedef struct _POFX_COMPONENT_STATE
{
    ULONG IdleStateCount;
    ULONG DeepestWakeableIdleState;
    ULONG ActiveCount;
    ULONG RequestedIdleState;
    ULONG CurrentIdleState;
    BOOLEAN IdleConditionPending;
    BOOLEAN IdleStatePending;
    BOOLEAN InIdleState;
} POFX_COMPONENT_STATE, *PPOFX_COMPONENT_STATE;

typedef struct _POFX_DEVICE_CONTEXT
{
    struct _POFX_DEVICE_CONTEXT *Next;
    PDEVICE_OBJECT Pdo;
    ULONG ComponentCount;
    ULONG ReferenceCount;
    BOOLEAN Unregistering;
    BOOLEAN Started;
    BOOLEAN PoweredOn;
    BOOLEAN PowerNotRequired;
    BOOLEAN PowerNotRequiredPending;
    ULONGLONG IdleTimeout;
    PPO_FX_COMPONENT_ACTIVE_CONDITION_CALLBACK ComponentActiveConditionCallback;
    PPO_FX_COMPONENT_IDLE_CONDITION_CALLBACK ComponentIdleConditionCallback;
    PPO_FX_COMPONENT_IDLE_STATE_CALLBACK ComponentIdleStateCallback;
    PPO_FX_DEVICE_POWER_REQUIRED_CALLBACK DevicePowerRequiredCallback;
    PPO_FX_DEVICE_POWER_NOT_REQUIRED_CALLBACK DevicePowerNotRequiredCallback;
    PPO_FX_POWER_CONTROL_CALLBACK PowerControlCallback;
    PVOID DeviceContext;
    POFX_COMPONENT_STATE Components[ANYSIZE_ARRAY];
} POFX_DEVICE_CONTEXT, *PPOFX_DEVICE_CONTEXT;

static KSPIN_LOCK PofxLock = 0;
static PPOFX_DEVICE_CONTEXT PofxDevices;

static
PPOFX_DEVICE_CONTEXT
PofxpReferenceDevice(
    _In_ POHANDLE Handle)
{
    KIRQL OldIrql;
    PPOFX_DEVICE_CONTEXT Device;

    if (Handle == NULL)
        return NULL;

    KeAcquireSpinLock(&PofxLock, &OldIrql);
    for (Device = PofxDevices; Device != NULL; Device = Device->Next)
    {
        if ((PVOID)Device == (PVOID)Handle && !Device->Unregistering)
        {
            ++Device->ReferenceCount;
            KeReleaseSpinLock(&PofxLock, OldIrql);
            return Device;
        }
    }

    KeReleaseSpinLock(&PofxLock, OldIrql);
    return NULL;
}

static
VOID
PofxpDereferenceDevice(
    _In_ PPOFX_DEVICE_CONTEXT Device)
{
    KIRQL OldIrql;
    BOOLEAN FreeContext = FALSE;

    KeAcquireSpinLock(&PofxLock, &OldIrql);
    ASSERT(Device->ReferenceCount != 0);
    if (--Device->ReferenceCount == 0 && Device->Unregistering)
        FreeContext = TRUE;
    KeReleaseSpinLock(&PofxLock, OldIrql);

    if (FreeContext)
        ExFreePoolWithTag(Device, TAG_POFX);
}

static
VOID
PofxpCompleteIdleConditionLocked(
    _Inout_ PPOFX_DEVICE_CONTEXT Device,
    _In_ ULONG Component,
    _Out_ PPO_FX_COMPONENT_IDLE_STATE_CALLBACK *StateCallback,
    _Out_ PULONG State)
{
    PPOFX_COMPONENT_STATE ComponentState = &Device->Components[Component];

    ComponentState->IdleConditionPending = FALSE;
    ComponentState->RequestedIdleState =
        ComponentState->DeepestWakeableIdleState;
    ComponentState->IdleStatePending = TRUE;
    *StateCallback = Device->ComponentIdleStateCallback;
    *State = ComponentState->RequestedIdleState;

    if (*StateCallback == NULL)
    {
        ComponentState->CurrentIdleState = *State;
        ComponentState->IdleStatePending = FALSE;
        ComponentState->InIdleState = TRUE;
    }
}

static
VOID
PofxpNotifyDeviceIdle(
    _Inout_ PPOFX_DEVICE_CONTEXT Device)
{
    KIRQL OldIrql;
    ULONG i;
    BOOLEAN AllIdle = TRUE;
    PPO_FX_DEVICE_POWER_NOT_REQUIRED_CALLBACK Callback = NULL;

    KeAcquireSpinLock(&PofxLock, &OldIrql);
    for (i = 0; i < Device->ComponentCount; ++i)
    {
        if (Device->Components[i].ActiveCount != 0 ||
            !Device->Components[i].InIdleState)
        {
            AllIdle = FALSE;
            break;
        }
    }

    if (AllIdle && Device->Started && Device->PoweredOn &&
        !Device->PowerNotRequired &&
        !Device->PowerNotRequiredPending)
    {
        Callback = Device->DevicePowerNotRequiredCallback;
        if (Callback != NULL)
            Device->PowerNotRequiredPending = TRUE;
        else
            Device->PowerNotRequired = TRUE;
    }
    KeReleaseSpinLock(&PofxLock, OldIrql);

    if (Callback != NULL)
        Callback(Device->DeviceContext);
}

NTKRNLVISTAAPI
NTSTATUS
NTAPI
PoFxRegisterDevice(
    _In_ PDEVICE_OBJECT Pdo,
    _In_ PPO_FX_DEVICE Device,
    _Out_ POHANDLE *Handle)
{
    SIZE_T AllocationSize;
    KIRQL OldIrql;
    ULONG i;
    PPOFX_DEVICE_CONTEXT Context;

    if (Pdo == NULL || Device == NULL || Handle == NULL ||
        Device->Version != PO_FX_VERSION_V1 ||
        Device->ComponentCount == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    *Handle = NULL;

    if (Device->ComponentCount >
        (MAXULONG_PTR - FIELD_OFFSET(POFX_DEVICE_CONTEXT, Components)) /
            sizeof(POFX_COMPONENT_STATE))
    {
        return STATUS_INVALID_PARAMETER;
    }

    for (i = 0; i < Device->ComponentCount; ++i)
    {
        if (Device->Components[i].IdleStateCount == 0 ||
            Device->Components[i].IdleStates == NULL ||
            Device->Components[i].DeepestWakeableIdleState >=
                Device->Components[i].IdleStateCount)
        {
            return STATUS_INVALID_PARAMETER;
        }
    }

    AllocationSize = FIELD_OFFSET(POFX_DEVICE_CONTEXT, Components) +
                    Device->ComponentCount * sizeof(POFX_COMPONENT_STATE);
    Context = ExAllocatePoolWithTag(NonPagedPool, AllocationSize, TAG_POFX);
    if (Context == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Context, AllocationSize);
    Context->Pdo = Pdo;
    Context->ComponentCount = Device->ComponentCount;
    Context->ComponentActiveConditionCallback =
        Device->ComponentActiveConditionCallback;
    Context->ComponentIdleConditionCallback =
        Device->ComponentIdleConditionCallback;
    Context->ComponentIdleStateCallback = Device->ComponentIdleStateCallback;
    Context->DevicePowerRequiredCallback = Device->DevicePowerRequiredCallback;
    Context->DevicePowerNotRequiredCallback =
        Device->DevicePowerNotRequiredCallback;
    Context->PowerControlCallback = Device->PowerControlCallback;
    Context->DeviceContext = Device->DeviceContext;

    for (i = 0; i < Device->ComponentCount; ++i)
    {
        Context->Components[i].IdleStateCount =
            Device->Components[i].IdleStateCount;
        Context->Components[i].DeepestWakeableIdleState =
            Device->Components[i].DeepestWakeableIdleState;
    }

    KeAcquireSpinLock(&PofxLock, &OldIrql);
    Context->Next = PofxDevices;
    PofxDevices = Context;
    KeReleaseSpinLock(&PofxLock, OldIrql);

    *Handle = (POHANDLE)Context;
    return STATUS_SUCCESS;
}

NTKRNLVISTAAPI
VOID
NTAPI
PoFxUnregisterDevice(
    _In_ POHANDLE Handle)
{
    KIRQL OldIrql;
    PPOFX_DEVICE_CONTEXT *Link;
    PPOFX_DEVICE_CONTEXT Context;
    BOOLEAN FreeContext;

    KeAcquireSpinLock(&PofxLock, &OldIrql);
    for (Link = &PofxDevices; *Link != NULL; Link = &(*Link)->Next)
    {
        if ((PVOID)*Link == (PVOID)Handle)
            break;
    }

    Context = *Link;
    if (Context == NULL || Context->Unregistering)
    {
        KeReleaseSpinLock(&PofxLock, OldIrql);
        DPRINT1("PoFxUnregisterDevice: invalid handle %p\n", Handle);
        return;
    }

    *Link = Context->Next;
    Context->Unregistering = TRUE;
    FreeContext = (Context->ReferenceCount == 0);
    KeReleaseSpinLock(&PofxLock, OldIrql);

    if (FreeContext)
        ExFreePoolWithTag(Context, TAG_POFX);
}

NTKRNLVISTAAPI
VOID
NTAPI
PoFxStartDevicePowerManagement(
    _In_ POHANDLE Handle)
{
    KIRQL OldIrql;
    PPOFX_DEVICE_CONTEXT Context = PofxpReferenceDevice(Handle);
    PPO_FX_DEVICE_POWER_REQUIRED_CALLBACK Callback = NULL;

    if (Context == NULL)
    {
        DPRINT1("PoFxStartDevicePowerManagement: invalid handle %p\n", Handle);
        return;
    }

    KeAcquireSpinLock(&PofxLock, &OldIrql);
    if (!Context->Started)
    {
        Context->Started = TRUE;
        Callback = Context->DevicePowerRequiredCallback;
        if (Callback == NULL)
            Context->PoweredOn = TRUE;
    }
    else
    {
        DPRINT1("PoFxStartDevicePowerManagement: device already started\n");
    }
    KeReleaseSpinLock(&PofxLock, OldIrql);

    if (Callback != NULL)
        Callback(Context->DeviceContext);

    PofxpNotifyDeviceIdle(Context);
    PofxpDereferenceDevice(Context);
}

NTKRNLVISTAAPI
VOID
NTAPI
PoFxActivateComponent(
    _In_ POHANDLE Handle,
    _In_ ULONG Component,
    _In_ ULONG Flags)
{
    KIRQL OldIrql;
    PPOFX_DEVICE_CONTEXT Context = PofxpReferenceDevice(Handle);
    PPO_FX_COMPONENT_ACTIVE_CONDITION_CALLBACK Callback = NULL;
    PPO_FX_DEVICE_POWER_REQUIRED_CALLBACK PowerRequiredCallback = NULL;
    PPOFX_COMPONENT_STATE ComponentState;

    UNREFERENCED_PARAMETER(Flags);

    if (Context == NULL)
    {
        DPRINT1("PoFxActivateComponent: invalid handle %p\n", Handle);
        return;
    }

    KeAcquireSpinLock(&PofxLock, &OldIrql);
    if (Component >= Context->ComponentCount)
    {
        KeReleaseSpinLock(&PofxLock, OldIrql);
        DPRINT1("PoFxActivateComponent: invalid component %lu\n", Component);
        PofxpDereferenceDevice(Context);
        return;
    }

    ComponentState = &Context->Components[Component];
    if (ComponentState->ActiveCount == MAXULONG)
    {
        KeReleaseSpinLock(&PofxLock, OldIrql);
        DPRINT1("PoFxActivateComponent: activation count overflow\n");
        PofxpDereferenceDevice(Context);
        return;
    }

    if (ComponentState->ActiveCount++ == 0)
    {
        ComponentState->IdleConditionPending = FALSE;
        ComponentState->IdleStatePending = FALSE;
        ComponentState->InIdleState = FALSE;
        Callback = Context->ComponentActiveConditionCallback;
        if (Context->PowerNotRequired || Context->PowerNotRequiredPending)
        {
            Context->PowerNotRequired = FALSE;
            Context->PowerNotRequiredPending = FALSE;
            Context->PoweredOn = FALSE;
            PowerRequiredCallback = Context->DevicePowerRequiredCallback;
            if (PowerRequiredCallback == NULL)
                Context->PoweredOn = TRUE;
        }
    }
    KeReleaseSpinLock(&PofxLock, OldIrql);

    if (PowerRequiredCallback != NULL)
        PowerRequiredCallback(Context->DeviceContext);
    if (Callback != NULL)
        Callback(Context->DeviceContext, Component);

    PofxpDereferenceDevice(Context);
}

NTKRNLVISTAAPI
VOID
NTAPI
PoFxCompleteDevicePowerNotRequired(
    _In_ POHANDLE Handle)
{
    KIRQL OldIrql;
    BOOLEAN InvalidCompletion = FALSE;
    PPOFX_DEVICE_CONTEXT Context = PofxpReferenceDevice(Handle);

    if (Context == NULL)
    {
        DPRINT1("PoFxCompleteDevicePowerNotRequired: invalid handle %p\n", Handle);
        return;
    }

    KeAcquireSpinLock(&PofxLock, &OldIrql);
    if (Context->PowerNotRequiredPending)
    {
        Context->PowerNotRequiredPending = FALSE;
        Context->PowerNotRequired = TRUE;
        Context->PoweredOn = FALSE;
    }
    else
    {
        InvalidCompletion = TRUE;
    }
    KeReleaseSpinLock(&PofxLock, OldIrql);

    if (InvalidCompletion)
        DPRINT1("PoFxCompleteDevicePowerNotRequired: no request is pending\n");

    PofxpDereferenceDevice(Context);
}

NTKRNLVISTAAPI
VOID
NTAPI
PoFxIdleComponent(
    _In_ POHANDLE Handle,
    _In_ ULONG Component,
    _In_ ULONG Flags)
{
    KIRQL OldIrql;
    PPOFX_DEVICE_CONTEXT Context = PofxpReferenceDevice(Handle);
    PPO_FX_COMPONENT_IDLE_CONDITION_CALLBACK IdleCallback = NULL;
    PPO_FX_COMPONENT_IDLE_STATE_CALLBACK StateCallback = NULL;
    PPOFX_COMPONENT_STATE ComponentState;
    ULONG State = 0;
    BOOLEAN CompleteIdleCondition = FALSE;

    UNREFERENCED_PARAMETER(Flags);

    if (Context == NULL)
    {
        DPRINT1("PoFxIdleComponent: invalid handle %p\n", Handle);
        return;
    }

    KeAcquireSpinLock(&PofxLock, &OldIrql);
    if (Component >= Context->ComponentCount)
    {
        KeReleaseSpinLock(&PofxLock, OldIrql);
        DPRINT1("PoFxIdleComponent: invalid component %lu\n", Component);
        PofxpDereferenceDevice(Context);
        return;
    }

    ComponentState = &Context->Components[Component];
    if (ComponentState->ActiveCount == 0)
    {
        KeReleaseSpinLock(&PofxLock, OldIrql);
        DPRINT1("PoFxIdleComponent: component %lu is not active\n", Component);
        PofxpDereferenceDevice(Context);
        return;
    }

    if (--ComponentState->ActiveCount == 0)
    {
        ComponentState->IdleConditionPending = TRUE;
        ComponentState->InIdleState = FALSE;
        IdleCallback = Context->ComponentIdleConditionCallback;
        if (IdleCallback == NULL)
        {
            PofxpCompleteIdleConditionLocked(Context,
                                              Component,
                                              &StateCallback,
                                              &State);
            CompleteIdleCondition = TRUE;
        }
    }
    KeReleaseSpinLock(&PofxLock, OldIrql);

    if (IdleCallback != NULL)
        IdleCallback(Context->DeviceContext, Component);
    else if (CompleteIdleCondition && StateCallback != NULL)
        StateCallback(Context->DeviceContext, Component, State);

    PofxpNotifyDeviceIdle(Context);
    PofxpDereferenceDevice(Context);
}

NTKRNLVISTAAPI
VOID
NTAPI
PoFxCompleteIdleCondition(
    _In_ POHANDLE Handle,
    _In_ ULONG Component)
{
    KIRQL OldIrql;
    PPOFX_DEVICE_CONTEXT Context = PofxpReferenceDevice(Handle);
    PPO_FX_COMPONENT_IDLE_STATE_CALLBACK Callback = NULL;
    ULONG State = 0;

    if (Context == NULL)
    {
        DPRINT1("PoFxCompleteIdleCondition: invalid handle %p\n", Handle);
        return;
    }

    KeAcquireSpinLock(&PofxLock, &OldIrql);
    if (Component >= Context->ComponentCount)
    {
        KeReleaseSpinLock(&PofxLock, OldIrql);
        DPRINT1("PoFxCompleteIdleCondition: invalid component %lu\n", Component);
        PofxpDereferenceDevice(Context);
        return;
    }

    if (!Context->Components[Component].IdleConditionPending ||
        Context->Components[Component].ActiveCount != 0)
    {
        KeReleaseSpinLock(&PofxLock, OldIrql);
        DPRINT1("PoFxCompleteIdleCondition: component %lu is not waiting\n",
                Component);
        PofxpDereferenceDevice(Context);
        return;
    }

    PofxpCompleteIdleConditionLocked(Context, Component, &Callback, &State);
    KeReleaseSpinLock(&PofxLock, OldIrql);

    if (Callback != NULL)
        Callback(Context->DeviceContext, Component, State);

    PofxpNotifyDeviceIdle(Context);
    PofxpDereferenceDevice(Context);
}

NTKRNLVISTAAPI
VOID
NTAPI
PoFxCompleteIdleState(
    _In_ POHANDLE Handle,
    _In_ ULONG Component)
{
    KIRQL OldIrql;
    PPOFX_DEVICE_CONTEXT Context = PofxpReferenceDevice(Handle);

    if (Context == NULL)
    {
        DPRINT1("PoFxCompleteIdleState: invalid handle %p\n", Handle);
        return;
    }

    KeAcquireSpinLock(&PofxLock, &OldIrql);
    if (Component >= Context->ComponentCount)
    {
        KeReleaseSpinLock(&PofxLock, OldIrql);
        DPRINT1("PoFxCompleteIdleState: invalid component %lu\n", Component);
        PofxpDereferenceDevice(Context);
        return;
    }

    if (!Context->Components[Component].IdleStatePending ||
        Context->Components[Component].ActiveCount != 0)
    {
        KeReleaseSpinLock(&PofxLock, OldIrql);
        DPRINT1("PoFxCompleteIdleState: component %lu is not waiting\n",
                Component);
        PofxpDereferenceDevice(Context);
        return;
    }

    Context->Components[Component].CurrentIdleState =
        Context->Components[Component].RequestedIdleState;
    Context->Components[Component].IdleStatePending = FALSE;
    Context->Components[Component].InIdleState = TRUE;
    KeReleaseSpinLock(&PofxLock, OldIrql);

    PofxpNotifyDeviceIdle(Context);
    PofxpDereferenceDevice(Context);
}

NTKRNLVISTAAPI
VOID
NTAPI
PoFxSetDeviceIdleTimeout(
    _In_ POHANDLE Handle,
    _In_ ULONGLONG IdleTimeout)
{
    KIRQL OldIrql;
    PPOFX_DEVICE_CONTEXT Context = PofxpReferenceDevice(Handle);

    if (Context == NULL)
    {
        DPRINT1("PoFxSetDeviceIdleTimeout: invalid handle %p\n", Handle);
        return;
    }

    KeAcquireSpinLock(&PofxLock, &OldIrql);
    Context->IdleTimeout = IdleTimeout;
    KeReleaseSpinLock(&PofxLock, OldIrql);
    PofxpDereferenceDevice(Context);
}

NTKRNLVISTAAPI
VOID
NTAPI
PoFxReportDevicePoweredOn(
    _In_ POHANDLE Handle)
{
    KIRQL OldIrql;
    PPOFX_DEVICE_CONTEXT Context = PofxpReferenceDevice(Handle);

    if (Context == NULL)
    {
        DPRINT1("PoFxReportDevicePoweredOn: invalid handle %p\n", Handle);
        return;
    }

    KeAcquireSpinLock(&PofxLock, &OldIrql);
    Context->PoweredOn = TRUE;
    KeReleaseSpinLock(&PofxLock, OldIrql);
    PofxpNotifyDeviceIdle(Context);
    PofxpDereferenceDevice(Context);
}
