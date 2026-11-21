/*
 * PROJECT:     ReactOS Kernel - Vista+ APIs
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Power request objects (Windows 7+)
 *
 * NOTES:       ReactOS has no policy manager that acts on system/display/
 *              away-mode requirements, so a power request object only does
 *              the bookkeeping: it owns a counter per request type that is
 *              raised by PoSetPowerRequest and lowered by PoClearPowerRequest.
 */

#include <ntdef.h>
#include <ntifs.h>
#include <debug.h>

#define TAG_PORQ 'qRoP'

#define PO_POWER_REQUEST_SIGNATURE 'qRoP'

typedef struct _PO_POWER_REQUEST
{
    ULONG Signature;
    PDEVICE_OBJECT DeviceObject;
    ULONG ContextFlags;
    volatile LONG Count[PowerRequestMaximum];
} PO_POWER_REQUEST, *PPO_POWER_REQUEST;

static
BOOLEAN
PorpIsValidRequest(
    _In_opt_ PVOID PowerRequest)
{
    PPO_POWER_REQUEST Request = PowerRequest;

    return (Request != NULL) && (Request->Signature == PO_POWER_REQUEST_SIGNATURE);
}

static
BOOLEAN
PorpIsValidReasonContext(
    _In_ PCOUNTED_REASON_CONTEXT Context)
{
    ULONG Flags = Context->Flags;

    if (Context->Version != POWER_REQUEST_CONTEXT_VERSION)
        return FALSE;

    /* Exactly one of the two string formats has to be selected */
    if (Flags != POWER_REQUEST_CONTEXT_SIMPLE_STRING &&
        Flags != POWER_REQUEST_CONTEXT_DETAILED_STRING)
    {
        return FALSE;
    }

    if (Flags == POWER_REQUEST_CONTEXT_DETAILED_STRING)
    {
        /* Detailed strings come with a resource file and a string array */
        if (Context->StringCount != 0 && Context->ReasonStrings == NULL)
            return FALSE;
    }

    return TRUE;
}

_IRQL_requires_max_(APC_LEVEL)
NTKRNLVISTAAPI
NTSTATUS
NTAPI
PoCreatePowerRequest(
    _Outptr_ PVOID *PowerRequest,
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_opt_ PCOUNTED_REASON_CONTEXT Context)
{
    PPO_POWER_REQUEST Request;

    PAGED_CODE();

    if (PowerRequest == NULL)
        return STATUS_INVALID_PARAMETER;

    *PowerRequest = NULL;

    if (DeviceObject == NULL)
        return STATUS_INVALID_PARAMETER;

    if (Context != NULL && !PorpIsValidReasonContext(Context))
        return STATUS_INVALID_PARAMETER;

    Request = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Request), TAG_PORQ);
    if (Request == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Request, sizeof(*Request));
    Request->Signature = PO_POWER_REQUEST_SIGNATURE;
    Request->ContextFlags = (Context != NULL) ? Context->Flags : 0;

    /* The request must not outlive the device it was made for */
    ObReferenceObject(DeviceObject);
    Request->DeviceObject = DeviceObject;

    *PowerRequest = Request;
    return STATUS_SUCCESS;
}

_IRQL_requires_max_(APC_LEVEL)
NTKRNLVISTAAPI
VOID
NTAPI
PoDeletePowerRequest(
    _Inout_ PVOID PowerRequest)
{
    PPO_POWER_REQUEST Request = PowerRequest;

    PAGED_CODE();

    if (!PorpIsValidRequest(Request))
    {
        DPRINT1("PoDeletePowerRequest: invalid power request %p\n", PowerRequest);
        return;
    }

    /* Make a double delete recognizable while the memory is not reused yet */
    Request->Signature = 0;

    ObDereferenceObject(Request->DeviceObject);
    ExFreePoolWithTag(Request, TAG_PORQ);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
NTKRNLVISTAAPI
NTSTATUS
NTAPI
PoSetPowerRequest(
    _Inout_ PVOID PowerRequest,
    _In_ POWER_REQUEST_TYPE Type)
{
    PPO_POWER_REQUEST Request = PowerRequest;
    LONG Old;

    if (!PorpIsValidRequest(Request))
        return STATUS_INVALID_PARAMETER;

    if ((ULONG)Type >= PowerRequestMaximum)
        return STATUS_INVALID_PARAMETER;

    /* Increment, but never wrap around */
    do
    {
        Old = Request->Count[Type];
        if (Old == MAXLONG)
            return STATUS_INTEGER_OVERFLOW;
    } while (InterlockedCompareExchange(&Request->Count[Type], Old + 1, Old) != Old);

    return STATUS_SUCCESS;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
NTKRNLVISTAAPI
NTSTATUS
NTAPI
PoClearPowerRequest(
    _Inout_ PVOID PowerRequest,
    _In_ POWER_REQUEST_TYPE Type)
{
    PPO_POWER_REQUEST Request = PowerRequest;
    LONG Old;

    if (!PorpIsValidRequest(Request))
        return STATUS_INVALID_PARAMETER;

    if ((ULONG)Type >= PowerRequestMaximum)
        return STATUS_INVALID_PARAMETER;

    /* Decrement, but never go below zero */
    do
    {
        Old = Request->Count[Type];
        if (Old == 0)
        {
            DPRINT1("PoClearPowerRequest: type %d is not set\n", (int)Type);
            return STATUS_SUCCESS;
        }
    } while (InterlockedCompareExchange(&Request->Count[Type], Old - 1, Old) != Old);

    return STATUS_SUCCESS;
}
