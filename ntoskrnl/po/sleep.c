/*
 * PROJECT:   CATV 95 Kernel
 * LICENSE:   GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:   Sleep and Wakeup support
 * COPYRIGHT: Copyright 1997-2028 Republic of CATV 95 <catv95republic@proton.me>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>
#include "inbv/logo.h"

NTSTATUS
NTAPI
PopEnterSleepState(_In_ SYSTEM_POWER_STATE SystemState)
{
    if ((SystemState < PowerSystemSleeping1) ||
        (SystemState > PowerSystemSleeping3))
    {
        return STATUS_INVALID_PARAMETER;
    }

    DPRINT("Entering system sleep state %u\n", SystemState);
    return PopSetSystemPowerState(SystemState, PowerActionSleep);
}

NTSTATUS
NTAPI
PopSaveHiberContextStates(
    _Inout_ PPOP_HIBER_CONTEXT Context
)
{
    TIME_FIELDS RtcTime;
    NTSTATUS Status;

    if (Context == NULL)
        return STATUS_INVALID_PARAMETER;

    if (Context->HiberStatusScreenInitialized)
        return STATUS_SUCCESS;

    if (!HalQueryRealTimeClock(&RtcTime))
        return STATUS_UNSUCCESSFUL;

    Context->ImageHeader.RtcTime.Year = RtcTime.Year;
    Context->ImageHeader.RtcTime.Month = RtcTime.Month;
    Context->ImageHeader.RtcTime.Day = RtcTime.Day;
    Context->ImageHeader.RtcTime.Weekday = RtcTime.Weekday;
    Context->ImageHeader.RtcTime.Hour = RtcTime.Hour;
    Context->ImageHeader.RtcTime.Minute = RtcTime.Minute;
    Context->ImageHeader.RtcTime.Second = RtcTime.Second;
    Context->ImageHeader.RtcTime.Milliseconds = RtcTime.Milliseconds;
    Context->ImageHeader.Flags |= HIBERFILE_FLAG_HAS_RTC_TIME;

    Status = PopHiberWriteImageHeader(Context);
    if (!NT_SUCCESS(Status))
        return Status;

    /* Put up a hibernating screen */
    if ( !Context->HiberStatusScreenInitialized )
    {
        DisplayHibernationScreen();
        Context->HiberStatusScreenInitialized = TRUE;
    }

    Context->HiberPercentComplete = 0;
    return STATUS_SUCCESS;
}