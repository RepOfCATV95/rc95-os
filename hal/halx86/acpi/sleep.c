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

/* ENTERING SLEEP STATES ******************************************************/

#define PM1_WAKE_STATUS     0x8000      /* WAK_STS */
#define PM1_SLP_EN          0x2000
#define PM1_SLP_TYP_MASK    0x1C00
#define PM1_SLP_TYP_SHIFT   10

/*
 * Tell the firmware where to continue after S2/S3. This is the real-mode
 * waking vector in the FACS. Zero removes the vector.
 */
static
NTSTATUS
HalpWakeSetFirmwareVector(
    _In_ ULONG VectorPhysicalAddress)
{
    PHYSICAL_ADDRESS FacsAddress;
    PFACS Facs;

    FacsAddress.QuadPart = 0;
    if (HalpFixedAcpiDescTable.Header.Length >=
        FIELD_OFFSET(FADT, x_firmware_ctrl) + sizeof(PHYSICAL_ADDRESS))
    {
        FacsAddress = HalpFixedAcpiDescTable.x_firmware_ctrl;
    }
    if (FacsAddress.QuadPart == 0)
        FacsAddress.QuadPart = HalpFixedAcpiDescTable.facs;
    if (FacsAddress.QuadPart == 0)
        return STATUS_NOT_SUPPORTED;

    Facs = HalpMapPhysicalMemory64(FacsAddress, 1);
    if (Facs == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    if (Facs->Signature != FACS_SIGNATURE ||
        Facs->Length < FIELD_OFFSET(FACS, GlobalLock))
    {
        HalpUnmapVirtualAddress(Facs, 1);
        return STATUS_NOT_SUPPORTED;
    }

    Facs->pFirmwareWakingVector = VectorPhysicalAddress;

    /* The 64-bit vector would be used instead, and it is not for real mode */
    if (Facs->Length >= FIELD_OFFSET(FACS, Version))
        Facs->x_FirmwareWakingVector.QuadPart = 0;

    HalpUnmapVirtualAddress(Facs, 1);
    return STATUS_SUCCESS;
}

/*
 * Enter the sleep state whose SLP_TYP values were found by the caller in the
 * \_Sx object of the DSDT; the HAL can't evaluate AML.
 *
 * ContextLost is for S2/S3, where the processor is reset. The state of the
 * processor is saved, the stub that takes over after the wake up is set up and
 * the firmware is told about it. Then the function returns after the wake up
 * with *Resumed set to TRUE and interrupts still disabled; the interrupt
 * controller, the timers and the devices have to be set up again by the
 * caller. Without ContextLost (S1) the function returns when the machine
 * is awake again.
 *
 * For ContextLost HalpWakeAllocateStub has to be called first, at
 * PASSIVE_LEVEL: it needs memory below 1MB, that can't be allocated here.
 *
 * Has to be called with interrupts disabled, at DISPATCH_LEVEL or above, on a
 * uniprocessor machine for ContextLost. If the function returns with
 * *Resumed FALSE and a success status, the machine did not go to sleep.
 */
NTSTATUS
NTAPI
HalpAcpiSleep(
    _In_ UCHAR SlpTypA,
    _In_ UCHAR SlpTypB,
    _In_ BOOLEAN ContextLost,
    _Out_ PBOOLEAN Resumed)
{
    ULONG ControlA, ControlB, StatusA, StatusB;
    ULONG StubPhysicalAddress;
    USHORT Value;
    NTSTATUS Status;
    ULONG i;

    *Resumed = FALSE;

    if (HalpFixedAcpiDescTable.Header.Signature != FADT_SIGNATURE ||
        SlpTypA > 7 || SlpTypB > 7)
    {
        return STATUS_INVALID_PARAMETER;
    }

    ControlA = HalpGetPm1EventPort(HalpFixedAcpiDescTable.pm1a_ctrl_blk_io_port,
                                   &HalpFixedAcpiDescTable.x_pm1a_ctrl_blk,
                                   FIELD_OFFSET(FADT, x_pm1a_ctrl_blk));
    ControlB = HalpGetPm1EventPort(HalpFixedAcpiDescTable.pm1b_ctrl_blk_io_port,
                                   &HalpFixedAcpiDescTable.x_pm1b_ctrl_blk,
                                   FIELD_OFFSET(FADT, x_pm1b_ctrl_blk));
    StatusA = HalpGetPm1EventPort(HalpFixedAcpiDescTable.pm1a_evt_blk_io_port,
                                  &HalpFixedAcpiDescTable.x_pm1a_evt_blk,
                                  FIELD_OFFSET(FADT, x_pm1a_evt_blk));
    StatusB = HalpGetPm1EventPort(HalpFixedAcpiDescTable.pm1b_evt_blk_io_port,
                                  &HalpFixedAcpiDescTable.x_pm1b_evt_blk,
                                  FIELD_OFFSET(FADT, x_pm1b_evt_blk));
    if (ControlA == 0 || StatusA == 0)
        return STATUS_NOT_SUPPORTED;

    if (ContextLost)
    {
        /* The other processors would have to be started again */
        if (KeNumberProcessors != 1)
            return STATUS_NOT_SUPPORTED;

        Status = HalpWakeBuildStub(&StubPhysicalAddress);
        if (!NT_SUCCESS(Status))
            return Status;

        Status = HalpWakeSetFirmwareVector(StubPhysicalAddress);
        if (!NT_SUCCESS(Status))
            return Status;

        /* Returns TRUE here a second time after the wake up */
        if (HalpWakeSaveContext())
        {
            HalpWakeSetFirmwareVector(0);
            *Resumed = TRUE;
            return STATUS_SUCCESS;
        }
    }

    /* Forget an earlier wake up, WAK_STS is write-one-to-clear */
    WRITE_PORT_USHORT((PUSHORT)(ULONG_PTR)StatusA, PM1_WAKE_STATUS);
    if (StatusB != 0 && StatusB != StatusA)
        WRITE_PORT_USHORT((PUSHORT)(ULONG_PTR)StatusB, PM1_WAKE_STATUS);

    /* The caches have to be written back: memory is all that stays */
    __wbinvd();

    /* SLP_TYP first, then SLP_EN with it, as the specification wants it */
    Value = READ_PORT_USHORT((PUSHORT)(ULONG_PTR)ControlA);
    Value = (Value & ~(PM1_SLP_TYP_MASK | PM1_SLP_EN)) | (SlpTypA << PM1_SLP_TYP_SHIFT);
    WRITE_PORT_USHORT((PUSHORT)(ULONG_PTR)ControlA, Value);
    if (ControlB != 0 && ControlB != ControlA)
    {
        Value = READ_PORT_USHORT((PUSHORT)(ULONG_PTR)ControlB);
        Value = (Value & ~(PM1_SLP_TYP_MASK | PM1_SLP_EN)) | (SlpTypB << PM1_SLP_TYP_SHIFT);
        WRITE_PORT_USHORT((PUSHORT)(ULONG_PTR)ControlB, Value);
    }

    Value = READ_PORT_USHORT((PUSHORT)(ULONG_PTR)ControlA);
    WRITE_PORT_USHORT((PUSHORT)(ULONG_PTR)ControlA, Value | PM1_SLP_EN);
    if (ControlB != 0 && ControlB != ControlA)
    {
        Value = READ_PORT_USHORT((PUSHORT)(ULONG_PTR)ControlB);
        WRITE_PORT_USHORT((PUSHORT)(ULONG_PTR)ControlB, Value | PM1_SLP_EN);
    }

    /*
     * S2/S3: the processor stops here and the wake up goes through the stub.
     * S1: the processor continues when the machine is woken up, WAK_STS says
     * so. Wait for it for a while; if it does not come, nothing happened.
     */
    for (i = 0; i < 2000; i++)
    {
        if (READ_PORT_USHORT((PUSHORT)(ULONG_PTR)StatusA) & PM1_WAKE_STATUS)
            break;
        if (StatusB != 0 && StatusB != StatusA &&
            (READ_PORT_USHORT((PUSHORT)(ULONG_PTR)StatusB) & PM1_WAKE_STATUS))
        {
            break;
        }
        KeStallExecutionProcessor(1000);
    }

    if (ContextLost)
        HalpWakeSetFirmwareVector(0);

    if (i == 2000)
        return STATUS_UNSUCCESSFUL;

    *Resumed = TRUE;
    return STATUS_SUCCESS;
}
