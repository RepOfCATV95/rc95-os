/*
 * PROJECT:   CATV 95 Kernel
 * LICENSE:   GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:   Sleep and Wakeup support
 * COPYRIGHT: Copyright 1997-2028 Republic of CATV 95 <catv95republic@proton.me>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/*
 * Perform the S1-S3 transition after PoSleepSystem has validated the
 * requested state and refreshed the platform capabilities.
 *
 * Set rollback flags before invoking each suspend callback: a callback can
 * fail after partially suspending its clients, so issue its matching resume
 * notification even when the suspend callback reports failure.
 */
NTSTATUS
NTAPI
PopEnterSleepState(
    _In_ SYSTEM_POWER_STATE SystemState
)
{
    WIN32_POWERSTATE_PARAMETERS Parameters;
    NTSTATUS Status;
    NTSTATUS ResumeStatus;
    BOOLEAN AppsSuspendAttempted = FALSE;
    BOOLEAN ServicesSuspendAttempted = FALSE;

    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return STATUS_INVALID_DEVICE_STATE;

    if (SystemState < PowerSystemSleeping1 ||
        SystemState > PowerSystemSleeping3)
    {
        return STATUS_INVALID_PARAMETER;
    }

    DPRINT("Entering system sleep state %u\n", SystemState);

    if (PopStateCallout != NULL)
    {
        RtlZeroMemory(&Parameters, sizeof(Parameters));
        Parameters.SystemAction = PowerActionSleep;
        Parameters.MinSystemState = SystemState;
        Parameters.Flags = PopAction.Flags;

        AppsSuspendAttempted = TRUE;
        Parameters.PowerStateTask = PowerState_SuspendApps;
        Status = PopStateCallout(&Parameters);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("Suspend-app notification failed: 0x%08lx\n", Status);
            goto Resume;
        }

        ServicesSuspendAttempted = TRUE;
        Parameters.PowerStateTask = PowerState_SuspendServices;
        Status = PopStateCallout(&Parameters);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("Suspend-service notification failed: 0x%08lx\n", Status);
            goto Resume;
        }
    }

    Status = PopSetSystemPowerState(SystemState, PowerActionSleep);

Resume:
    /* Resume in reverse order; preserve the original transition failure. */
    if (PopStateCallout != NULL)
    {
        if (ServicesSuspendAttempted)
        {
            Parameters.PowerStateTask = PowerState_ResumeServices;
            ResumeStatus = PopStateCallout(&Parameters);
            if (!NT_SUCCESS(ResumeStatus))
            {
                DPRINT1("Resume-service notification failed: 0x%08lx\n",
                        ResumeStatus);
                if (NT_SUCCESS(Status))
                    Status = ResumeStatus;
            }
        }

        if (AppsSuspendAttempted)
        {
            Parameters.PowerStateTask = PowerState_ResumeApps;
            ResumeStatus = PopStateCallout(&Parameters);
            if (!NT_SUCCESS(ResumeStatus))
            {
                DPRINT1("Resume-app notification failed: 0x%08lx\n",
                        ResumeStatus);
                if (NT_SUCCESS(Status))
                    Status = ResumeStatus;
            }
        }
    }

    return Status;
}
