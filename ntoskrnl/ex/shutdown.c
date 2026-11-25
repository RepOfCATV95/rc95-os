/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         RC95-OS kernel
 * FILE:            ntoskrnl/ex/shutdown.c
 * PURPOSE:         Power management
 *
 * PROGRAMMERS:     David Welch (welch@cwcom.net)
 */

/* INCLUDES *****************************************************************/

#include <ntoskrnl.h>

#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS *********************************************************/

VOID
NTAPI
ExShutdownSystem(VOID)
{
    PVOID ErrorPort;
    PEPROCESS ErrorPortProcess;

    /*
     * Detach the global references before dropping them. This makes the
     * shutdown operation idempotent and ensures the globals no longer retain
     * references to objects that have already entered teardown.
     */
    ErrorPort = ExpDefaultErrorPort;
    ErrorPortProcess = ExpDefaultErrorPortProcess;
    ExpDefaultErrorPort = NULL;
    ExpDefaultErrorPortProcess = NULL;

    if (ErrorPort) ObDereferenceObject(ErrorPort);
    if (ErrorPortProcess) ObDereferenceObject(ErrorPortProcess);
}

/* FUNCTIONS *****************************************************************/

/*
 * @implemented
 */
NTSTATUS
NTAPI
NtShutdownSystem(IN SHUTDOWN_ACTION Action)
{
    POWER_ACTION PowerAction;

    /* Convert to power action */
    if (Action == ShutdownNoReboot)
    {
        PowerAction = PowerActionShutdown;
    }
    else if (Action == ShutdownReboot)
    {
        PowerAction = PowerActionShutdownReset;
    }
    else if (Action == ShutdownPowerOff)
    {
        PowerAction = PowerActionShutdownOff;
    }
    else
    {
        return STATUS_INVALID_PARAMETER;
    }

    /* Now call the power manager */
    DPRINT("Setting state to: %lx\n", PowerAction);
    return NtSetSystemPowerState(PowerAction,
                                 PowerSystemSleeping3,
                                 POWER_ACTION_OVERRIDE_APPS |
                                 POWER_ACTION_DISABLE_WAKES |
                                 POWER_ACTION_CRITICAL);
}

/* EOF */
