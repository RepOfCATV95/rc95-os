/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS
 * FILE:            drivers/wdm/audio/backpln/portcls/irp.cpp
 * PURPOSE:         Port Class driver / IRP Handling
 * PROGRAMMER:      Andrew Greenwood
 *                  Johannes Anderwald
 * HISTORY:
 *                  27 Jan 07   Created
 */

#include "private.hpp"

#define NDEBUG
#include <debug.h>

NTSTATUS
NTAPI
PortClsCreate(
    IN  PDEVICE_OBJECT DeviceObject,
    IN  PIRP Irp)
{
    DPRINT("PortClsCreate called\n");

    return KsDispatchIrp(DeviceObject, Irp);
}

NTSTATUS
NTAPI
PortClsPnp(
    IN  PDEVICE_OBJECT DeviceObject,
    IN  PIRP Irp)
{
    NTSTATUS Status;
    PPCLASS_DEVICE_EXTENSION DeviceExt;
    PIO_STACK_LOCATION IoStack;
    POWER_STATE PowerState;
    IResourceList* resource_list = NULL;

    if (!DeviceObject || !Irp)
        return STATUS_INVALID_PARAMETER;

    if (!DeviceObject->DeviceExtension) {
        Irp->IoStatus.Status = STATUS_INVALID_DEVICE_STATE;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_INVALID_DEVICE_STATE;
    }

    DeviceExt = (PPCLASS_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
    IoStack = IoGetCurrentIrpStackLocation(Irp);

    DPRINT("PortClsPnp called %u\n", IoStack->MinorFunction);

    switch (IoStack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            if (DeviceExt->resources)
                return PcCompleteIrp(DeviceObject, Irp, STATUS_INVALID_DEVICE_STATE);

            Status = PcNewResourceList(
                        &resource_list,
                        NULL,
                        PagedPool,
                        IoStack->Parameters.StartDevice.AllocatedResourcesTranslated,
                        IoStack->Parameters.StartDevice.AllocatedResources);
            if (!NT_SUCCESS(Status) || !resource_list) {
                if (NT_SUCCESS(Status))
                    Status = STATUS_INSUFFICIENT_RESOURCES;
                Irp->IoStatus.Status = Status;
                Irp->IoStatus.Information = 0;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return Status;
            }

            Status = PcForwardIrpSynchronous(DeviceObject, Irp);
            if (!NT_SUCCESS(Status)) {
                resource_list->Release();
                Irp->IoStatus.Status = Status;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return Status;
            }

            if (!DeviceExt->StartDevice) {
                Status = STATUS_INVALID_DEVICE_STATE;
            } else {
                Status = DeviceExt->StartDevice(DeviceObject, Irp, resource_list);
            }

            if (!NT_SUCCESS(Status)) {
                resource_list->Release();
                Irp->IoStatus.Status = Status;
                Irp->IoStatus.Information = 0;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return Status;
            }

            DeviceExt->resources = resource_list;
            DeviceExt->DevicePowerState = PowerDeviceD0;
            DeviceExt->SystemPowerState = PowerSystemWorking;

            PowerState.DeviceState = PowerDeviceD0;
            PoSetPowerState(DeviceObject, DevicePowerState, PowerState);

            Irp->IoStatus.Status = STATUS_SUCCESS;
            Irp->IoStatus.Information = 0;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return STATUS_SUCCESS;

        case IRP_MN_REMOVE_DEVICE:
            DPRINT("IRP_MN_REMOVE_DEVICE\n");

            if (DeviceExt->TimerStarted) {
                IoStopTimer(DeviceObject);
                DeviceExt->TimerStarted = FALSE;
            }

            Status = PcForwardIrpSynchronous(DeviceObject, Irp);

            if (DeviceExt->resources) {
                DeviceExt->resources->Release();
                DeviceExt->resources = NULL;
            }

            if (DeviceExt->KsDeviceHeader) {
                KsFreeDeviceHeader(DeviceExt->KsDeviceHeader);
                DeviceExt->KsDeviceHeader = NULL;
            }

            if (DeviceExt->CreateItems) {
                FreeItem(DeviceExt->CreateItems, TAG_PORTCLASS);
                DeviceExt->CreateItems = NULL;
            }

            if (DeviceExt->AdapterPowerManagement) {
                IAdapterPowerManagement* power = DeviceExt->AdapterPowerManagement;
                DeviceExt->AdapterPowerManagement = NULL;
                power->Release();
            }

            IoUnregisterShutdownNotification(DeviceObject);

            if (DeviceExt->PrevDeviceObject) {
                IoDetachDevice(DeviceExt->PrevDeviceObject);
                DeviceExt->PrevDeviceObject = NULL;
            }

            Irp->IoStatus.Status = Status;
            Irp->IoStatus.Information = 0;
            IoDeleteDevice(DeviceObject);
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;

        case IRP_MN_QUERY_INTERFACE:
        case IRP_MN_QUERY_DEVICE_RELATIONS:
        case IRP_MN_FILTER_RESOURCE_REQUIREMENTS:
        case IRP_MN_QUERY_RESOURCE_REQUIREMENTS:
        case IRP_MN_READ_CONFIG:
            Status = PcForwardIrpSynchronous(DeviceObject, Irp);
            return PcCompleteIrp(DeviceObject, Irp, Status);

        default:
            /* Unhandled PnP requests still belong to the lower device stack. */
            Status = PcForwardIrpSynchronous(DeviceObject, Irp);
            return PcCompleteIrp(DeviceObject, Irp, Status);
    }
}


NTSTATUS
NTAPI
PortClsPower(
    IN  PDEVICE_OBJECT DeviceObject,
    IN  PIRP Irp)
{
    PIO_STACK_LOCATION IoStack;
    PPCLASS_DEVICE_EXTENSION DeviceExtension;
    POWER_STATE PowerState;
    NTSTATUS Status = STATUS_SUCCESS;

    if (!DeviceObject || !Irp || !DeviceObject->DeviceExtension) {
        if (Irp) {
            Irp->IoStatus.Status = STATUS_INVALID_DEVICE_STATE;
            Irp->IoStatus.Information = 0;
            PoStartNextPowerIrp(Irp);
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
        }
        return STATUS_INVALID_DEVICE_STATE;
    }

    IoStack = IoGetCurrentIrpStackLocation(Irp);
    DeviceExtension = (PPCLASS_DEVICE_EXTENSION)DeviceObject->DeviceExtension;

    if (!DeviceExtension->PrevDeviceObject) {
        Irp->IoStatus.Status = STATUS_DEVICE_NOT_CONNECTED;
        Irp->IoStatus.Information = 0;
        PoStartNextPowerIrp(Irp);
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_DEVICE_NOT_CONNECTED;
    }

    if (IoStack->MinorFunction != IRP_MN_SET_POWER &&
        IoStack->MinorFunction != IRP_MN_QUERY_POWER) {
        PoStartNextPowerIrp(Irp);
        IoSkipCurrentIrpStackLocation(Irp);
        return PoCallDriver(DeviceExtension->PrevDeviceObject, Irp);
    }

    if (IoStack->Parameters.Power.Type == DevicePowerState) {
        DEVICE_POWER_STATE requested_state = IoStack->Parameters.Power.State.DeviceState;

        if (requested_state < PowerDeviceD0 || requested_state > PowerDeviceD3) {
            Status = STATUS_INVALID_PARAMETER;
            goto complete_power_irp;
        }

        if (IoStack->MinorFunction == IRP_MN_QUERY_POWER) {
            if (DeviceExtension->DevicePowerState != requested_state &&
                DeviceExtension->AdapterPowerManagement) {
                PowerState = IoStack->Parameters.Power.State;
                Status = DeviceExtension->AdapterPowerManagement->QueryPowerChangeState(PowerState);
                if (!NT_SUCCESS(Status))
                    goto complete_power_irp;
            }

            PoStartNextPowerIrp(Irp);
            IoSkipCurrentIrpStackLocation(Irp);
            return PoCallDriver(DeviceExtension->PrevDeviceObject, Irp);
        }

        if (DeviceExtension->DevicePowerState != requested_state) {
            PowerState = IoStack->Parameters.Power.State;

            if (DeviceExtension->AdapterPowerManagement)
                DeviceExtension->AdapterPowerManagement->PowerChangeState(PowerState);

            DeviceExtension->DevicePowerState = requested_state;
            PoSetPowerState(DeviceObject, DevicePowerState, PowerState);
        }

        PoStartNextPowerIrp(Irp);
        IoSkipCurrentIrpStackLocation(Irp);
        return PoCallDriver(DeviceExtension->PrevDeviceObject, Irp);
    }

    if (IoStack->Parameters.Power.Type != SystemPowerState) {
        Status = STATUS_INVALID_PARAMETER;
        goto complete_power_irp;
    }

    if (!DeviceExtension->PhysicalDeviceObject) {
        Status = STATUS_DEVICE_NOT_CONNECTED;
        goto complete_power_irp;
    }

    if (IoStack->Parameters.Power.State.SystemState < PowerSystemWorking ||
        IoStack->Parameters.Power.State.SystemState > PowerSystemShutdown) {
        Status = STATUS_INVALID_PARAMETER;
        goto complete_power_irp;
    }

    /*
     * System power IRPs already travel down this device stack. Do not issue
     * PoRequestPowerIrp with a SYSTEM_POWER_STATE value here: that API
     * creates a device-power request, re-enters this stack as a
     * DEVICE_POWER_STATE request, and interprets the state value incorrectly.
     */
    if (IoStack->MinorFunction == IRP_MN_SET_POWER)
        DeviceExtension->SystemPowerState = IoStack->Parameters.Power.State.SystemState;

    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(DeviceExtension->PrevDeviceObject, Irp);

complete_power_irp:
    PoStartNextPowerIrp(Irp);
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

NTSTATUS
NTAPI
PortClsSysControl(
    IN  PDEVICE_OBJECT DeviceObject,
    IN  PIRP Irp)
{
    DPRINT("PortClsSysControl called\n");

    // TODO

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
PortClsShutdown(
    IN  PDEVICE_OBJECT DeviceObject,
    IN  PIRP Irp)
{
    PPCLASS_DEVICE_EXTENSION DeviceExtension;
    DPRINT("PortClsShutdown called\n");

    // get device extension
    DeviceExtension = (PPCLASS_DEVICE_EXTENSION)DeviceObject->DeviceExtension;

    if (DeviceExtension->AdapterPowerManagement)
    {
        IAdapterPowerManagement* power = DeviceExtension->AdapterPowerManagement;
        DeviceExtension->AdapterPowerManagement = NULL;
        DPRINT("Power %u\n", power->Release());
    }

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
PcDispatchIrp(
    IN  PDEVICE_OBJECT DeviceObject,
    IN  PIRP Irp)
{
    PIO_STACK_LOCATION IoStack;

    if (!DeviceObject || !Irp) {
        if (Irp) {
            Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
            Irp->IoStatus.Information = 0;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
        }
        return STATUS_INVALID_PARAMETER;
    }

    IoStack = IoGetCurrentIrpStackLocation(Irp);

    DPRINT("PcDispatchIrp called - handling IRP in PortCls MajorFunction %x MinorFunction %x\n", IoStack->MajorFunction, IoStack->MinorFunction);

    switch ( IoStack->MajorFunction )
    {
        // PortCls
        case IRP_MJ_CREATE :
            return PortClsCreate(DeviceObject, Irp);

        case IRP_MJ_PNP :
            return PortClsPnp(DeviceObject, Irp);

        case IRP_MJ_POWER :
            return PortClsPower(DeviceObject, Irp);

        case IRP_MJ_DEVICE_CONTROL:
            return KsDispatchIrp(DeviceObject, Irp);

        case IRP_MJ_CLOSE:
            return KsDispatchIrp(DeviceObject, Irp);

        case IRP_MJ_SYSTEM_CONTROL :
            return PortClsSysControl(DeviceObject, Irp);

        case IRP_MJ_SHUTDOWN:
            return PortClsShutdown(DeviceObject, Irp);

        default:
            DPRINT("Unhandled function %x\n", IoStack->MajorFunction);
            break;
    };

    // If we reach here, we just complete the IRP
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
PcCompleteIrp(
    IN  PDEVICE_OBJECT DeviceObject,
    IN  PIRP Irp,
    IN  NTSTATUS Status)
{
#if 0
    PC_ASSERT(DeviceObject);
    PC_ASSERT(Irp);
    PC_ASSERT(Status != STATUS_PENDING);
#endif

    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return Status;
}

NTSTATUS
NTAPI
CompletionRoutine(
    IN PDEVICE_OBJECT  DeviceObject,
    IN PIRP  Irp,
    IN PVOID  Context)
{
    if (Irp->PendingReturned != FALSE)
    {
        KeSetEvent ((PKEVENT) Context, IO_NO_INCREMENT, FALSE);
    }
    return STATUS_MORE_PROCESSING_REQUIRED;
}

#undef IoSetCompletionRoutine
#define IoSetCompletionRoutine(_Irp, \
                               _CompletionRoutine, \
                               _Context, \
                               _InvokeOnSuccess, \
                               _InvokeOnError, \
                               _InvokeOnCancel) \
{ \
  PIO_STACK_LOCATION _IrpSp; \
  _IrpSp = IoGetNextIrpStackLocation(_Irp); \
  _IrpSp->CompletionRoutine = (PIO_COMPLETION_ROUTINE)(_CompletionRoutine); \
  _IrpSp->Context = (_Context); \
  _IrpSp->Control = 0; \
  if (_InvokeOnSuccess) _IrpSp->Control = SL_INVOKE_ON_SUCCESS; \
  if (_InvokeOnError) _IrpSp->Control |= SL_INVOKE_ON_ERROR; \
  if (_InvokeOnCancel) _IrpSp->Control |= SL_INVOKE_ON_CANCEL; \
}

NTSTATUS
NTAPI
PcForwardIrpSynchronous(
    IN  PDEVICE_OBJECT DeviceObject,
    IN  PIRP Irp)
{
    KEVENT Event;
    PPCLASS_DEVICE_EXTENSION DeviceExt;
    NTSTATUS Status;

    if (!DeviceObject || !Irp || !DeviceObject->DeviceExtension)
        return STATUS_INVALID_PARAMETER;

    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return STATUS_INVALID_DEVICE_STATE;

    DeviceExt = (PPCLASS_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
    if (!DeviceExt->PrevDeviceObject)
        return STATUS_DEVICE_NOT_CONNECTED;

    // initialize the notification event
    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    // are there enough irp stack locations
    if (Irp->CurrentLocation < Irp->StackCount + 1)
    {
        IoCopyCurrentIrpStackLocationToNext(Irp);
    }

    IoSetCompletionRoutine(Irp, CompletionRoutine, (PVOID)&Event, TRUE, TRUE, TRUE);

    // now call the driver
    Status = IoCallDriver(DeviceExt->PrevDeviceObject, Irp);
    // did the request complete yet
    if (Status == STATUS_PENDING)
    {
        // not yet, lets wait a bit
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = Irp->IoStatus.Status;
    }
    return Status;
}
