#include "precomp.h"

#define NDEBUG
#include <debug.h>

static
NTSTATUS
BusValidateSystemPowerState(
    SYSTEM_POWER_STATE SystemState)
{
    UINT8 AcpiState;
    UINT8 SleepTypeA, SleepTypeB;

    switch (SystemState)
    {
        case PowerSystemWorking:
            return STATUS_SUCCESS;

        case PowerSystemSleeping1:
            AcpiState = ACPI_STATE_S1;
            break;

        case PowerSystemSleeping2:
        case PowerSystemSleeping3:
            return STATUS_NOT_SUPPORTED;

        case PowerSystemShutdown:
            AcpiState = ACPI_STATE_S5;
            break;

        case PowerSystemHibernate:
            return STATUS_NOT_IMPLEMENTED;

        default:
            return STATUS_INVALID_PARAMETER;
    }

    return ACPI_SUCCESS(AcpiGetSleepTypeData(AcpiState,
                                              &SleepTypeA,
                                              &SleepTypeB)) ?
           STATUS_SUCCESS : STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
Bus_Power (
     PDEVICE_OBJECT DeviceObject,
     PIRP Irp
    )
{
    PIO_STACK_LOCATION  irpStack;
    NTSTATUS            status;
    PCOMMON_DEVICE_DATA commonData;

    status = STATUS_SUCCESS;
    irpStack = IoGetCurrentIrpStackLocation (Irp);
    ASSERT (IRP_MJ_POWER == irpStack->MajorFunction);

    commonData = (PCOMMON_DEVICE_DATA) DeviceObject->DeviceExtension;

    if (commonData->IsFDO) {

            DPRINT("FDO %s IRP:0x%p %s %s\n",
            PowerMinorFunctionString(irpStack->MinorFunction), Irp,
            DbgSystemPowerString(commonData->SystemPowerState),
            DbgDevicePowerString(commonData->DevicePowerState));


        status = Bus_FDO_Power ((PFDO_DEVICE_DATA)DeviceObject->DeviceExtension,
                                Irp);
    } else {

            DPRINT("PDO %s IRP:0x%p %s %s\n",
            PowerMinorFunctionString(irpStack->MinorFunction), Irp,
            DbgSystemPowerString(commonData->SystemPowerState),
            DbgDevicePowerString(commonData->DevicePowerState));

        status = Bus_PDO_Power ((PPDO_DEVICE_DATA)DeviceObject->DeviceExtension,
                                Irp);
    }

    return status;
}


NTSTATUS
Bus_FDO_Power (
    PFDO_DEVICE_DATA    Data,
    PIRP                Irp
    )
{
    NTSTATUS            status = STATUS_SUCCESS;
    POWER_STATE         powerState;
    POWER_STATE_TYPE    powerType;
    PIO_STACK_LOCATION  stack;
	ULONG AcpiState;
	ACPI_STATUS AcpiStatus;
    SYSTEM_POWER_STATE  oldPowerState;

    stack = IoGetCurrentIrpStackLocation (Irp);
    powerType = stack->Parameters.Power.Type;
    powerState = stack->Parameters.Power.State;


    if (stack->MinorFunction == IRP_MN_SET_POWER) {
        DPRINT("\tRequest to set %s state to %s\n",
               ((powerType == SystemPowerState) ?  "System" : "Device"),
               ((powerType == SystemPowerState) ? \
               DbgSystemPowerString(powerState.SystemState) :\
               DbgDevicePowerString(powerState.DeviceState)));

        if (powerType == SystemPowerState)
        {
            status = BusValidateSystemPowerState(powerState.SystemState);
            if (!NT_SUCCESS(status))
                goto CompletePowerIrp;

            switch (powerState.SystemState)
            {
                case PowerSystemWorking:
                    Data->Common.SystemPowerState = PowerSystemWorking;
                    break;

                case PowerSystemSleeping1:
                    AcpiState = ACPI_STATE_S1;
                    goto EnterSystemSleepState;

                case PowerSystemSleeping2:
                case PowerSystemSleeping3:
                    status = STATUS_NOT_SUPPORTED;
                    goto CompletePowerIrp;

                case PowerSystemHibernate:
                    status = STATUS_NOT_IMPLEMENTED;
                    goto CompletePowerIrp;

                case PowerSystemShutdown:
                    AcpiState = ACPI_STATE_S5;
                    goto EnterSystemSleepState;

                default:
                    status = STATUS_INVALID_PARAMETER;
                    goto CompletePowerIrp;
            }

EnterSystemSleepState:
            oldPowerState = Data->Common.SystemPowerState;
            Data->Common.SystemPowerState = powerState.SystemState;
            AcpiStatus = acpi_suspend(AcpiState);
            if (!ACPI_SUCCESS(AcpiStatus))
            {
                DPRINT1("Failed to enter sleep state %d (Status 0x%X)\n",
                        AcpiState,
                        AcpiStatus);
                Data->Common.SystemPowerState = oldPowerState;
                status = (AcpiStatus == AE_NOT_FOUND || AcpiStatus == AE_SUPPORT ||
                          AcpiStatus == AE_NOT_IMPLEMENTED) ?
                         STATUS_NOT_SUPPORTED : STATUS_UNSUCCESSFUL;
                goto CompletePowerIrp;
            }
        }
    }

    PoStartNextPowerIrp (Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    status = PoCallDriver (Data->NextLowerDriver, Irp);
    return status;

CompletePowerIrp:
    PoStartNextPowerIrp(Irp);
    Irp->IoStatus.Status = status;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return status;
}


NTSTATUS
Bus_PDO_Power (
    PPDO_DEVICE_DATA    PdoData,
    PIRP                Irp
    )
{
    NTSTATUS            status;
    PIO_STACK_LOCATION  stack;
    POWER_STATE         powerState;
    POWER_STATE_TYPE    powerType;
    ULONG               error;

    stack = IoGetCurrentIrpStackLocation (Irp);
    powerType = stack->Parameters.Power.Type;
    powerState = stack->Parameters.Power.State;

    switch (stack->MinorFunction) {
    case IRP_MN_SET_POWER:

        DPRINT("\tSetting %s power state to %s\n",
               ((powerType == SystemPowerState) ?  "System" : "Device"),
               ((powerType == SystemPowerState) ? \
               DbgSystemPowerString(powerState.SystemState) : \
               DbgDevicePowerString(powerState.DeviceState)));

        switch (powerType) {
            case DevicePowerState:
                if (powerState.DeviceState < PowerDeviceD0 ||
                    powerState.DeviceState > PowerDeviceD3)
                {
                    status = STATUS_INVALID_PARAMETER;
                    break;
                }

                if (!PdoData->AcpiHandle || !acpi_bus_power_manageable(PdoData->AcpiHandle))
                {
                    PoSetPowerState(PdoData->Common.Self, DevicePowerState, powerState);
                    PdoData->Common.DevicePowerState = powerState.DeviceState;
                    status = STATUS_SUCCESS;
                    break;
                }

                switch (powerState.DeviceState)
                {
                    case PowerDeviceD0:
                      error = acpi_bus_set_power(PdoData->AcpiHandle, ACPI_STATE_D0);
                      break;

                    case PowerDeviceD1:
                      error = acpi_bus_set_power(PdoData->AcpiHandle, ACPI_STATE_D1);
                      break;

                    case PowerDeviceD2:
                      error = acpi_bus_set_power(PdoData->AcpiHandle, ACPI_STATE_D2);
                      break;

                    case PowerDeviceD3:
                      error = acpi_bus_set_power(PdoData->AcpiHandle, ACPI_STATE_D3);
                      break;

                    default:
                      error = AE_BAD_PARAMETER;
                      break;
                }

                if (ACPI_SUCCESS(error))
                {
                    PoSetPowerState(PdoData->Common.Self, DevicePowerState, powerState);
                    PdoData->Common.DevicePowerState = powerState.DeviceState;
                    status = STATUS_SUCCESS;
                }
                else
                    status = STATUS_UNSUCCESSFUL;
                break;

            case SystemPowerState:
                status = BusValidateSystemPowerState(powerState.SystemState);
                if (!NT_SUCCESS(status))
                    break;
                PdoData->Common.SystemPowerState = powerState.SystemState;
                break;

            default:
                status = STATUS_INVALID_PARAMETER;
                break;
        }
        break;

    case IRP_MN_QUERY_POWER:
        if (powerType == SystemPowerState)
        {
            status = BusValidateSystemPowerState(powerState.SystemState);
        }
        else if (powerType == DevicePowerState &&
                 powerState.DeviceState >= PowerDeviceD0 &&
                 powerState.DeviceState <= PowerDeviceD3)
        {
            status = STATUS_SUCCESS;
        }
        else
        {
            status = STATUS_INVALID_PARAMETER;
        }
        break;

    case IRP_MN_WAIT_WAKE:
        //
        // We cannot support wait-wake because we are root-enumerated
        // driver, and our parent, the PnP manager, doesn't support wait-wake.
        //
    case IRP_MN_POWER_SEQUENCE:
    default:
        status = STATUS_NOT_SUPPORTED;
        break;
    }

    if (status != STATUS_NOT_SUPPORTED) {

        Irp->IoStatus.Status = status;
    }

    PoStartNextPowerIrp(Irp);
    status = Irp->IoStatus.Status;
    IoCompleteRequest (Irp, IO_NO_INCREMENT);

    return status;
}

#if !defined(NDEBUG) || defined(_MSC_VER)

PCHAR
PowerMinorFunctionString (
    UCHAR MinorFunction
)
{
    switch (MinorFunction)
    {
        case IRP_MN_SET_POWER:
            return "IRP_MN_SET_POWER";
        case IRP_MN_QUERY_POWER:
            return "IRP_MN_QUERY_POWER";
        case IRP_MN_POWER_SEQUENCE:
            return "IRP_MN_POWER_SEQUENCE";
        case IRP_MN_WAIT_WAKE:
            return "IRP_MN_WAIT_WAKE";

        default:
            return "unknown_power_irp";
    }
}

PCHAR
DbgSystemPowerString(
     SYSTEM_POWER_STATE Type
    )
{
    switch (Type)
    {
        case PowerSystemUnspecified:
            return "PowerSystemUnspecified";
        case PowerSystemWorking:
            return "PowerSystemWorking";
        case PowerSystemSleeping1:
            return "PowerSystemSleeping1";
        case PowerSystemSleeping2:
            return "PowerSystemSleeping2";
        case PowerSystemSleeping3:
            return "PowerSystemSleeping3";
        case PowerSystemHibernate:
            return "PowerSystemHibernate";
        case PowerSystemShutdown:
            return "PowerSystemShutdown";
        case PowerSystemMaximum:
            return "PowerSystemMaximum";
        default:
            return "UnKnown System Power State";
    }
 }

PCHAR
DbgDevicePowerString(
     DEVICE_POWER_STATE Type
    )
{
    switch (Type)
    {
        case PowerDeviceUnspecified:
            return "PowerDeviceUnspecified";
        case PowerDeviceD0:
            return "PowerDeviceD0";
        case PowerDeviceD1:
            return "PowerDeviceD1";
        case PowerDeviceD2:
            return "PowerDeviceD2";
        case PowerDeviceD3:
            return "PowerDeviceD3";
        case PowerDeviceMaximum:
            return "PowerDeviceMaximum";
        default:
            return "UnKnown Device Power State";
    }
}

#endif
