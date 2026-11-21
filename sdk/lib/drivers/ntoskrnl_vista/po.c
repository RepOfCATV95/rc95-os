/*
 * PROJECT:     ReactOS Kernel - Vista+ APIs
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Po functions of Vista+
 * COPYRIGHT:   2020 Victor Perevertkin (victor.perevertkin@reactos.org)
 */

#include <ntdef.h>
#include <ntifs.h>
#include <debug.h>

#define TAG_POPS 'sPoP'

/*
 * Power setting callbacks.
 *
 * ReactOS has no power setting store that could be notified, so a registration
 * is only recorded. This lets drivers register and unregister (and have the
 * handle validated), but no callback is ever invoked.
 */
typedef struct _PO_POWER_SETTING_REGISTRATION
{
    LIST_ENTRY Link;
    PDEVICE_OBJECT DeviceObject;
    GUID SettingGuid;
    PPOWER_SETTING_CALLBACK Callback;
    PVOID Context;
} PO_POWER_SETTING_REGISTRATION, *PPO_POWER_SETTING_REGISTRATION;

static KSPIN_LOCK PopSettingLock = 0;
static LIST_ENTRY PopSettingList = { &PopSettingList, &PopSettingList };

_IRQL_requires_max_(APC_LEVEL)
NTKRNLVISTAAPI
NTSTATUS
NTAPI
PoRegisterPowerSettingCallback(
    _In_opt_ PDEVICE_OBJECT DeviceObject,
    _In_ LPCGUID SettingGuid,
    _In_ PPOWER_SETTING_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Outptr_opt_ PVOID *Handle)
{
    PPO_POWER_SETTING_REGISTRATION Registration;
    KIRQL OldIrql;

    PAGED_CODE();

    if (Handle != NULL)
        *Handle = NULL;

    if (SettingGuid == NULL || Callback == NULL)
        return STATUS_INVALID_PARAMETER;

    /* Nothing can ever be delivered, and without a handle nobody could undo it */
    if (Handle == NULL)
        return STATUS_SUCCESS;

    Registration = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Registration), TAG_POPS);
    if (Registration == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Registration->DeviceObject = DeviceObject;
    Registration->SettingGuid = *SettingGuid;
    Registration->Callback = Callback;
    Registration->Context = Context;

    KeAcquireSpinLock(&PopSettingLock, &OldIrql);
    InsertTailList(&PopSettingList, &Registration->Link);
    KeReleaseSpinLock(&PopSettingLock, OldIrql);

    *Handle = Registration;
    return STATUS_SUCCESS;
}

_IRQL_requires_max_(APC_LEVEL)
NTKRNLVISTAAPI
NTSTATUS
NTAPI
PoUnregisterPowerSettingCallback(
    _Inout_ PVOID Handle)
{
    PLIST_ENTRY Entry;
    KIRQL OldIrql;

    PAGED_CODE();

    if (Handle == NULL)
        return STATUS_INVALID_PARAMETER;

    /* Only unlink what is really on the list, the handle comes from the caller */
    KeAcquireSpinLock(&PopSettingLock, &OldIrql);
    for (Entry = PopSettingList.Flink; Entry != &PopSettingList; Entry = Entry->Flink)
    {
        /* Link is the first member, so the entry address is the handle */
        if ((PVOID)Entry == Handle)
        {
            RemoveEntryList(Entry);
            KeReleaseSpinLock(&PopSettingLock, OldIrql);

            /* No callback is ever in flight, so the record can go immediately */
            ExFreePoolWithTag(Handle, TAG_POPS);
            return STATUS_SUCCESS;
        }
    }
    KeReleaseSpinLock(&PopSettingLock, OldIrql);

    DPRINT1("PoUnregisterPowerSettingCallback: invalid handle %p\n", Handle);
    return STATUS_INVALID_PARAMETER;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
NTKRNLVISTAAPI
BOOLEAN
NTAPI
PoQueryWatchdogTime(
    _In_ PDEVICE_OBJECT Pdo,
    _Out_ PULONG SecondsRemaining)
{
    UNREFERENCED_PARAMETER(Pdo);

    /*
     * The power manager of ReactOS does not arm a watchdog for outstanding
     * device power IRPs, so there is never a time to report.
     */
    *SecondsRemaining = 0;
    return FALSE;
}

/*
 * System wake flag of an IRP.
 *
 * The flag cannot be stored in the IRP itself, so the IRPs that were marked
 * are remembered in a small ring. Only wait/wake IRPs are marked and only a
 * few of them are pending at a time; when the ring is full the oldest mark is
 * dropped. A mark is not removed when the IRP completes, so an IRP that
 * reuses the address of an old marked one inherits the mark until it is
 * overwritten.
 */
#define PO_SYSTEM_WAKE_SLOTS 32

static KSPIN_LOCK PopSystemWakeLock = 0;
static PIRP PopSystemWakeIrps[PO_SYSTEM_WAKE_SLOTS];
static ULONG PopSystemWakeNext;

_IRQL_requires_max_(DISPATCH_LEVEL)
NTKRNLVISTAAPI
VOID
NTAPI
PoSetSystemWake(
    _Inout_ struct _IRP *Irp)
{
    KIRQL OldIrql;
    ULONG i;

    if (Irp == NULL)
        return;

    KeAcquireSpinLock(&PopSystemWakeLock, &OldIrql);
    for (i = 0; i < PO_SYSTEM_WAKE_SLOTS; ++i)
    {
        if (PopSystemWakeIrps[i] == Irp)
            goto Done;
    }

    PopSystemWakeIrps[PopSystemWakeNext] = Irp;
    PopSystemWakeNext = (PopSystemWakeNext + 1) % PO_SYSTEM_WAKE_SLOTS;

Done:
    KeReleaseSpinLock(&PopSystemWakeLock, OldIrql);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
NTKRNLVISTAAPI
BOOLEAN
NTAPI
PoGetSystemWake(
    _In_ struct _IRP *Irp)
{
    BOOLEAN Found = FALSE;
    KIRQL OldIrql;
    ULONG i;

    if (Irp == NULL)
        return FALSE;

    KeAcquireSpinLock(&PopSystemWakeLock, &OldIrql);
    for (i = 0; i < PO_SYSTEM_WAKE_SLOTS; ++i)
    {
        if (PopSystemWakeIrps[i] == Irp)
        {
            Found = TRUE;
            break;
        }
    }
    KeReleaseSpinLock(&PopSystemWakeLock, OldIrql);

    return Found;
}

/*
 * Device busy notification.
 *
 * The idle counter is the one handed out by PoRegisterDeviceForIdleDetection.
 * ReactOS has no idle detection (that function returns NULL), so there is no
 * idle timer that counts the value up and nothing to suppress. The functions
 * keep the contract of the Windows ones - the counter is reset to zero - and
 * tolerate the NULL pointer that drivers get from the stubbed registration.
 */

_IRQL_requires_max_(DISPATCH_LEVEL)
NTKRNLVISTAAPI
VOID
NTAPI
PoSetDeviceBusyEx(
    _Inout_ PULONG IdlePointer)
{
    if (IdlePointer != NULL)
        InterlockedExchange((volatile LONG *)IdlePointer, 0);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
NTKRNLVISTAAPI
VOID
NTAPI
PoStartDeviceBusy(
    _Inout_ PULONG IdlePointer)
{
    /* Without an idle timer "busy" and "just used" are the same thing */
    PoSetDeviceBusyEx(IdlePointer);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
NTKRNLVISTAAPI
VOID
NTAPI
PoEndDeviceBusy(
    _Inout_ PULONG IdlePointer)
{
    /* The idle period starts counting from here */
    PoSetDeviceBusyEx(IdlePointer);
}
