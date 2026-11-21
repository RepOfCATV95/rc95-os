/*
 * PROJECT:         ReactOS HAL
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * PURPOSE:         ACPI wake alarm support
 */

#include <hal.h>

#define RTC_SECONDS_ALARM 0x01
#define RTC_MINUTES_ALARM 0x03
#define RTC_HOURS_ALARM   0x05
#define RTC_AIE           0x20
#define RTC_BINARY_MODE   0x04
#define RTC_24_HOUR_MODE  0x02

#define PM1_RTC_WAKE      0x0400
#define ACPI_SYSTEM_IO    0x01

static
ULONG
HalpGetPm1EventPort(
    _In_ ULONG LegacyPort,
    _In_ PGEN_ADDR ExtendedPort,
    _In_ ULONG ExtendedPortOffset)
{
    if ((HalpFixedAcpiDescTable.Header.Length >=
         ExtendedPortOffset + sizeof(*ExtendedPort)) &&
        (ExtendedPort->AddressSpaceID == ACPI_SYSTEM_IO) &&
        (ExtendedPort->BitWidth >= sizeof(USHORT) * 8) &&
        (ExtendedPort->Address.QuadPart <= MAXUSHORT))
    {
        return (ULONG)ExtendedPort->Address.QuadPart;
    }

    return LegacyPort;
}

static
VOID
HalpSetPm1RtcWake(
    _In_ ULONG EventPort,
    _In_ BOOLEAN Enable)
{
    ULONG EnablePort;
    USHORT Value;
    ULONG EventLength = HalpFixedAcpiDescTable.pm1_evt_len;

    if ((EventPort == 0) ||
        (EventPort > MAXUSHORT) ||
        (EventLength < 2 * sizeof(USHORT)) ||
        (EventLength & 1))
    {
        return;
    }

    EnablePort = EventPort + EventLength / 2;
    if (EnablePort > MAXUSHORT - sizeof(USHORT) + 1)
    {
        return;
    }

    /* RTC_STS is write-one-to-clear. Drop a stale event before enabling... */
    if (Enable)
    {
        WRITE_PORT_USHORT((PUSHORT)(ULONG_PTR)EventPort, PM1_RTC_WAKE);
    }

    Value = READ_PORT_USHORT((PUSHORT)(ULONG_PTR)EnablePort);
    if (Enable)
        Value |= PM1_RTC_WAKE;
    else
        Value &= ~PM1_RTC_WAKE;
    WRITE_PORT_USHORT((PUSHORT)(ULONG_PTR)EnablePort, Value);

    /* ... and the one that is left after disabling, so it can't show up later */
    if (!Enable)
    {
        WRITE_PORT_USHORT((PUSHORT)(ULONG_PTR)EventPort, PM1_RTC_WAKE);
    }
}

/* Switch the RTC alarm interrupt off and drop an alarm that already fired */
static
VOID
HalpCancelWakeAlarm(VOID)
{
    UCHAR RegisterB;

    HalpAcquireCmosSpinLock();
    RegisterB = HalpReadCmos(RTC_REGISTER_B);
    HalpWriteCmos(RTC_REGISTER_B, RegisterB & ~RTC_AIE);
    (VOID)HalpReadCmos(RTC_REGISTER_C);
    HalpReleaseCmosSpinLock();
}

static
UCHAR
HalpEncodeRtcValue(
    _In_ CSHORT Value,
    _In_ UCHAR RegisterB)
{
    if (RegisterB & RTC_BINARY_MODE)
        return (UCHAR)Value;

    return INT_BCD(Value);
}

static
BOOLEAN
HalpGetAlarmTimeFields(
    _In_ ULONGLONG AlarmTime,
    _In_opt_ PTIME_FIELDS TimeFields,
    _Out_ PTIME_FIELDS AlarmFields)
{
    LARGE_INTEGER Time;

    if (TimeFields)
    {
        *AlarmFields = *TimeFields;
    }
    else
    {
        if (AlarmTime == 0)
            return FALSE;

        Time.QuadPart = AlarmTime;
        RtlTimeToTimeFields(&Time, AlarmFields);
    }

    return (AlarmFields->Second >= 0 && AlarmFields->Second <= 59 &&
            AlarmFields->Minute >= 0 && AlarmFields->Minute <= 59 &&
            AlarmFields->Hour >= 0 && AlarmFields->Hour <= 23 &&
            AlarmFields->Day >= 1 && AlarmFields->Day <= 31 &&
            AlarmFields->Month >= 1 && AlarmFields->Month <= 12);
}

VOID
NTAPI
HalpSetWakeEnable(
    IN BOOLEAN Enable)
{
    ULONG EventPort;
    ULONG EventPortOffset;
    ULONG PrimaryEventPort;
    UCHAR RegisterB;

    if (HalpFixedAcpiDescTable.Header.Signature != FADT_SIGNATURE)
        return;

    EventPortOffset = FIELD_OFFSET(FADT, x_pm1a_evt_blk);
    EventPort = HalpGetPm1EventPort(
        HalpFixedAcpiDescTable.pm1a_evt_blk_io_port,
        &HalpFixedAcpiDescTable.x_pm1a_evt_blk,
        EventPortOffset);
    PrimaryEventPort = EventPort;
    HalpSetPm1RtcWake(EventPort, Enable);

    EventPortOffset = FIELD_OFFSET(FADT, x_pm1b_evt_blk);
    EventPort = HalpGetPm1EventPort(
        HalpFixedAcpiDescTable.pm1b_evt_blk_io_port,
        &HalpFixedAcpiDescTable.x_pm1b_evt_blk,
        EventPortOffset);
    if (EventPort != PrimaryEventPort)
    {
        HalpSetPm1RtcWake(EventPort, Enable);
    }

    HalpAcquireCmosSpinLock();
    RegisterB = HalpReadCmos(RTC_REGISTER_B);
    if (Enable)
        RegisterB |= RTC_AIE;
    else
        RegisterB &= ~RTC_AIE;

    /* A flag latched in register C would hide the next alarm; reading clears it */
    (VOID)HalpReadCmos(RTC_REGISTER_C);
    HalpWriteCmos(RTC_REGISTER_B, RegisterB);
    HalpReleaseCmosSpinLock();
}

#if (NTDDI_VERSION >= NTDDI_WIN8)
VOID
NTAPI
HalpSetWakeAlarm(
    IN ULONGLONG AlarmTime,
    IN ULONGLONG DcWakeTime)
{
    TIME_FIELDS AlarmFields;
    UCHAR RegisterB;
    UCHAR Hour;

    UNREFERENCED_PARAMETER(DcWakeTime);
    if (HalpFixedAcpiDescTable.Header.Signature != FADT_SIGNATURE)
        return;

    /* Zero means "no alarm": remove the one that is set */
    if (AlarmTime == 0)
    {
        HalpCancelWakeAlarm();
        return;
    }

    if (!HalpGetAlarmTimeFields(AlarmTime, NULL, &AlarmFields))
        return;
#else
VOID
NTAPI
HalpSetWakeAlarm(
    IN ULONGLONG AlarmTime,
    IN PTIME_FIELDS TimeFields)
{
    TIME_FIELDS AlarmFields;
    UCHAR RegisterB;
    UCHAR Hour;

    if (HalpFixedAcpiDescTable.Header.Signature != FADT_SIGNATURE)
        return;

    /* Zero means "no alarm": remove the one that is set */
    if (AlarmTime == 0)
    {
        HalpCancelWakeAlarm();
        return;
    }

    if (!HalpGetAlarmTimeFields(AlarmTime, TimeFields, &AlarmFields))
        return;
#endif

    HalpAcquireCmosSpinLock();
    RegisterB = HalpReadCmos(RTC_REGISTER_B);
    HalpWriteCmos(RTC_REGISTER_B, RegisterB & ~RTC_AIE);

    HalpWriteCmos(RTC_SECONDS_ALARM,
                  HalpEncodeRtcValue(AlarmFields.Second, RegisterB));
    HalpWriteCmos(RTC_MINUTES_ALARM,
                  HalpEncodeRtcValue(AlarmFields.Minute, RegisterB));

    Hour = (UCHAR)AlarmFields.Hour;
    if (!(RegisterB & RTC_24_HOUR_MODE))
    {
        if (Hour >= 12)
        {
            Hour -= 12;
            Hour |= 0x80;
        }
        if ((Hour & 0x7F) == 0)
            Hour = (Hour & 0x80) | 12;
    }
    if (!(RegisterB & RTC_BINARY_MODE))
        Hour = (Hour & 0x80) | INT_BCD(Hour & 0x7F);
    HalpWriteCmos(RTC_HOURS_ALARM, Hour);

    if (HalpFixedAcpiDescTable.day_alarm_index > 0 &&
        HalpFixedAcpiDescTable.day_alarm_index < 0x80)
    {
        HalpWriteCmos(HalpFixedAcpiDescTable.day_alarm_index,
                      HalpEncodeRtcValue(AlarmFields.Day, RegisterB));
    }
    if (HalpFixedAcpiDescTable.month_alarm_index > 0 &&
        HalpFixedAcpiDescTable.month_alarm_index < 0x80)
    {
        HalpWriteCmos(HalpFixedAcpiDescTable.month_alarm_index,
                      HalpEncodeRtcValue(AlarmFields.Month, RegisterB));
    }

    /* Drop a stale alarm flag before the interrupt can be enabled again */
    (VOID)HalpReadCmos(RTC_REGISTER_C);
    HalpWriteCmos(RTC_REGISTER_B, RegisterB);
    HalpReleaseCmosSpinLock();
}