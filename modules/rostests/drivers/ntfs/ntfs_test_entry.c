/*
 * PROJECT:    RC95-OS NTFS regression tests
 * LICENSE:    GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:    Run NTFS-private tests without registering the filesystem.
 * COPYRIGHT:  Copyright 1997-2028 RepOfCATV95
 */

#include "ntfs.h"

#ifndef NTFS_TEST_BUILD
#error This source belongs only to the NTFS_TEST_BUILD target
#endif

#define NDEBUG
#include <debug.h>

NTSTATUS NtfsTestRunAll(PDRIVER_OBJECT DriverObject);

/* ntfs.c is deliberately excluded from this target. */
PNTFS_GLOBAL_DATA NtfsGlobalData = NULL;

CODE_SEG("INIT")
NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(RegistryPath);

    DPRINT1("NTFS_TEST_BUILD: starting private NTFS regression tests\n");
    return NtfsTestRunAll(DriverObject);
}
