/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Core source file for SMP management
 * COPYRIGHT:   Copyright 2021 Victor Perevertkin <victor.perevertkin@reactos.org>
 *              Copyright 2021-2023 Justin Miller <justin.miller@reactos.org>
 */

/* INCLUDES ******************************************************************/

#include <hal.h>
#include <smp.h>

#define NDEBUG
#include <debug.h>

/* Shared processor state and real-mode AP trampoline storage. */
PROCESSOR_IDENTITY HalpProcessorIdentity[MAXIMUM_PROCESSORS];
PHYSICAL_ADDRESS HalpLowStubPhysicalAddress;
PVOID HalpLowStub;

BOOLEAN
NTAPI
HalpInitializeLowStub(_In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    if (HalpLowStub && HalpLowStubPhysicalAddress.QuadPart)
        return TRUE;

    HalpLowStubPhysicalAddress.QuadPart = HalpAllocPhysicalMemory(
        LoaderBlock, 0x100000, HALP_LOW_STUB_SIZE_IN_PAGES, FALSE);
    if (!HalpLowStubPhysicalAddress.QuadPart)
        return FALSE;

    HalpLowStub = HalpMapPhysicalMemory64(HalpLowStubPhysicalAddress,
                                          HALP_LOW_STUB_SIZE_IN_PAGES);
    if (!HalpLowStub)
    {
        HalpLowStubPhysicalAddress.QuadPart = 0;
        return FALSE;
    }

    RtlZeroMemory(HalpLowStub, HALP_LOW_STUB_SIZE_IN_PAGES * PAGE_SIZE);
    return TRUE;
}

/* FUNCTIONS *****************************************************************/

VOID
HalpSetupProcessorsTable(
    _In_ UINT32 NTProcessorNumber)
{
    PKPRCB CurrentPrcb;

    /*
     * Link the Prcb of the current CPU to
     * the current CPUs entry in the global ProcessorIdentity
     */
    CurrentPrcb = KeGetCurrentPrcb();
    HalpProcessorIdentity[NTProcessorNumber].ProcessorPrcb = CurrentPrcb;
    HalpProcessorIdentity[NTProcessorNumber].ProcessorStarted = TRUE;
}

VOID
FASTCALL
HalpBroadcastClockIpi(
    _In_ UCHAR Vector)
{
    /* Send a clock IPI to all processors */
    HalpBroadcastIpiSpecifyVector(Vector, FALSE);
}
