/*
 * RC95-OS BGFX Controller
 * Controls the animation of GOP boot screen which is represented by assets found in bootres.dll
 *
 * Created: 11-21-2026 (Saturday) at 10:38 a.m.
 * Author: Republic of CATV 95
 */

#include <ntoskrnl.h>
#include "bgfx.h"
#include "../inbv/logo.h"
#define NDEBUG
#include <debug.h>

BOOLEAN
NTAPI
IsBootResGenuine(VOID)
{
	// STUB
	return FALSE;
}

CODE_SEG("INIT")
VOID
NTAPI
DisplayGopBitmap(
    _In_ BOOLEAN InText
    )
{
    if (!IsBootResGenuine())
    {
        DisplayBootBitmap(InText);
        return;
    }
}