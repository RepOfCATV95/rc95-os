/*
 * PROJECT:     CATV 95 Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     i386 multiprocessor freeze/thaw support
 */

/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/*
 * The i386 PRCB's IpiFrozen member is an IPI request bitmap. Keep the
 * debugger freeze protocol in a separate per-processor state array.
 */
#define KI_I386_FREEZE_RUNNING       0
#define KI_I386_FREEZE_TARGET        1
#define KI_I386_FREEZE_FROZEN        2
#define KI_I386_FREEZE_THAW          3
#define KI_I386_FREEZE_OWNER         4
#define KI_I386_FREEZE_ACTIVE     0x20

static volatile LONG KiI386FreezeState[MAXIMUM_PROCESSORS];
static PKPRCB volatile KiI386FreezeOwner;

/* FUNCTIONS ******************************************************************/

BOOLEAN
NTAPI
KiI386ProcessorFreezeHandler(VOID)
{
    PKPRCB Prcb = KeGetCurrentPrcb();
    ULONG ProcessorNumber = KeGetCurrentProcessorNumber();
    KCONTINUE_STATUS ContinueStatus;

    if (ProcessorNumber >= MAXIMUM_PROCESSORS ||
        InterlockedCompareExchange(&KiI386FreezeState[ProcessorNumber],
                                   KI_I386_FREEZE_TARGET,
                                   KI_I386_FREEZE_TARGET) != KI_I386_FREEZE_TARGET)
    {
        return FALSE;
    }

    /*
     * Keep the interrupted execution on this CPU's IPI stack while parked.
     * The saved context is the debugger's view of this processor.
     */
    RtlCaptureContext(&Prcb->ProcessorState.ContextFrame);
    Prcb->ProcessorState.ContextFrame.ContextFlags =
        CONTEXT_FULL | CONTEXT_DEBUG_REGISTERS;
    KiSaveProcessorControlState(&Prcb->ProcessorState);

    InterlockedExchange(&KiI386FreezeState[ProcessorNumber],
                        KI_I386_FREEZE_FROZEN);
    KeMemoryBarrier();

    for (;;)
    {
        LONG State = InterlockedCompareExchange(
            &KiI386FreezeState[ProcessorNumber], 0, 0);

        if (State == KI_I386_FREEZE_THAW)
            break;

        if (State & KI_I386_FREEZE_ACTIVE)
        {
            ContinueStatus = KdReportProcessorChange();

            InterlockedExchange(&KiI386FreezeState[ProcessorNumber],
                                KI_I386_FREEZE_FROZEN);
            KeMemoryBarrier();

            if (ContinueStatus == ContinueSuccess)
            {
                PKPRCB Owner = KiI386FreezeOwner;
                if (Owner != NULL && Owner->Number < MAXIMUM_PROCESSORS)
                {
                    InterlockedExchange(&KiI386FreezeState[Owner->Number],
                                        KI_I386_FREEZE_THAW);
                }
            }
        }

        YieldProcessor();
        KeMemoryBarrier();
    }

    KiRestoreProcessorControlState(&Prcb->ProcessorState);
    InterlockedExchange(&KiI386FreezeState[ProcessorNumber],
                        KI_I386_FREEZE_RUNNING);
    KeMemoryBarrier();
    return TRUE;
}

VOID
NTAPI
KxFreezeExecution(VOID)
{
    PKPRCB CurrentPrcb = KeGetCurrentPrcb();
    ULONG CurrentProcessor = KeGetCurrentProcessorNumber();
    KAFFINITY TargetSet;
    ULONG Index;

    if (CurrentProcessor >= MAXIMUM_PROCESSORS)
    {
        ASSERT(FALSE);
        return;
    }

    /* A recursive freeze by the owner must not wait on itself. */
    if (KiI386FreezeOwner == CurrentPrcb)
        return;

    while (InterlockedCompareExchangePointer(
               (PVOID volatile *)&KiI386FreezeOwner,
               CurrentPrcb,
               NULL) != NULL)
    {
        while (KiI386FreezeOwner != NULL)
        {
            YieldProcessor();
            KeMemoryBarrier();
        }
    }

    InterlockedExchange(&KiI386FreezeState[CurrentProcessor],
                        KI_I386_FREEZE_OWNER | KI_I386_FREEZE_ACTIVE);
    TargetSet = KeActiveProcessors & ~CurrentPrcb->SetMember;

    /* Publish all target states before delivering the broadcast IPI. */
    for (Index = 0; Index < KeNumberProcessors; ++Index)
    {
        PKPRCB TargetPrcb = KiProcessorBlock[Index];

        if (TargetPrcb != NULL && (TargetSet & TargetPrcb->SetMember))
        {
            ASSERT(InterlockedCompareExchange(&KiI386FreezeState[Index],
                                               0,
                                               0) == KI_I386_FREEZE_RUNNING);
            InterlockedExchange(&KiI386FreezeState[Index],
                                KI_I386_FREEZE_TARGET);
        }
    }
    KeMemoryBarrier();

    if (TargetSet != 0)
        KiIpiSend(TargetSet, IPI_FREEZE);

    /* Do not return until every active target has reached its freeze loop. */
    for (Index = 0; Index < KeNumberProcessors; ++Index)
    {
        PKPRCB TargetPrcb = KiProcessorBlock[Index];

        if (TargetPrcb != NULL && (TargetSet & TargetPrcb->SetMember))
        {
            while (InterlockedCompareExchange(&KiI386FreezeState[Index],
                                               0,
                                               0) != KI_I386_FREEZE_FROZEN)
            {
                YieldProcessor();
                KeMemoryBarrier();
            }
        }
    }
}

VOID
NTAPI
KxThawExecution(VOID)
{
    PKPRCB CurrentPrcb = KeGetCurrentPrcb();
    ULONG CurrentProcessor = KeGetCurrentProcessorNumber();
    KAFFINITY TargetSet = KeActiveProcessors & ~CurrentPrcb->SetMember;
    ULONG Index;

    ASSERT(CurrentProcessor < MAXIMUM_PROCESSORS);
    ASSERT(KiI386FreezeOwner == CurrentPrcb);

    for (Index = 0; Index < KeNumberProcessors; ++Index)
    {
        PKPRCB TargetPrcb = KiProcessorBlock[Index];

        if (TargetPrcb != NULL && (TargetSet & TargetPrcb->SetMember))
        {
            ASSERT(InterlockedCompareExchange(&KiI386FreezeState[Index],
                                               0,
                                               0) == KI_I386_FREEZE_FROZEN);
            InterlockedExchange(&KiI386FreezeState[Index],
                                KI_I386_FREEZE_THAW);
        }
    }
    KeMemoryBarrier();

    for (Index = 0; Index < KeNumberProcessors; ++Index)
    {
        PKPRCB TargetPrcb = KiProcessorBlock[Index];

        if (TargetPrcb != NULL && (TargetSet & TargetPrcb->SetMember))
        {
            while (InterlockedCompareExchange(&KiI386FreezeState[Index],
                                               0,
                                               0) != KI_I386_FREEZE_RUNNING)
            {
                YieldProcessor();
                KeMemoryBarrier();
            }
        }
    }

    InterlockedExchange(&KiI386FreezeState[CurrentProcessor],
                        KI_I386_FREEZE_RUNNING);
    KeMemoryBarrier();
    InterlockedExchangePointer((PVOID volatile *)&KiI386FreezeOwner, NULL);
}

KCONTINUE_STATUS
NTAPI
KxSwitchKdProcessor(_In_ ULONG ProcessorIndex)
{
    PKPRCB CurrentPrcb = KeGetCurrentPrcb();
    ULONG CurrentProcessor = KeGetCurrentProcessorNumber();
    PKPRCB TargetPrcb;

    if (ProcessorIndex >= KeNumberProcessors ||
        ProcessorIndex >= MAXIMUM_PROCESSORS ||
        ProcessorIndex == CurrentProcessor)
    {
        return ContinueError;
    }

    TargetPrcb = KiProcessorBlock[ProcessorIndex];
    if (TargetPrcb == NULL ||
        InterlockedCompareExchange(&KiI386FreezeState[ProcessorIndex],
                                   0,
                                   0) != KI_I386_FREEZE_FROZEN)
    {
        return ContinueError;
    }

    /* Hand the active debugger turn to the selected parked processor. */
    InterlockedAnd(&KiI386FreezeState[CurrentProcessor],
                   ~KI_I386_FREEZE_ACTIVE);
    InterlockedOr(&KiI386FreezeState[ProcessorIndex],
                  KI_I386_FREEZE_ACTIVE);
    KeMemoryBarrier();

    if (KiI386FreezeOwner != CurrentPrcb)
        return ContinueNextProcessor;

    for (;;)
    {
        LONG State = InterlockedCompareExchange(
            &KiI386FreezeState[CurrentProcessor], 0, 0);

        if (State == KI_I386_FREEZE_THAW)
        {
            InterlockedExchange(&KiI386FreezeState[CurrentProcessor],
                                KI_I386_FREEZE_OWNER | KI_I386_FREEZE_ACTIVE);
            return ContinueSuccess;
        }

        if (State == (KI_I386_FREEZE_OWNER | KI_I386_FREEZE_ACTIVE))
            return ContinueProcessorReselected;

        YieldProcessor();
        KeMemoryBarrier();
    }
}
