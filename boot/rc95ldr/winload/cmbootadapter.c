/*
 * RC95LDR adapter for the Configuration Manager's shared boot-hive resolver.
 *
 * This file is compiled into the FreeLoader native-loader targets, where
 * cmboot.c is already built with _BLDR_. Keep control-set selection,
 * Start-type filtering, group/tag
 * ordering, and dependency-cycle handling in that shared implementation.
 */
#include <ntoskrnl.h>
#include <freeldr.h>
#include <cmlib.h>
#include "internal/cmboot.h"
#include "rc95ldrcmboot.h"

#define RC95LDR_CM_TAG 'h59R'

static PVOID NTAPI
Rc95LdrCmAllocate(SIZE_T Size, BOOLEAN Paged, ULONG Tag)
{
    UNREFERENCED_PARAMETER(Paged);
    UNREFERENCED_PARAMETER(Tag);
    return FrLdrHeapAlloc(Size, RC95LDR_CM_TAG);
}

static VOID NTAPI
Rc95LdrCmFree(PVOID Ptr, ULONG Quota)
{
    UNREFERENCED_PARAMETER(Quota);
    if (Ptr) FrLdrHeapFree(Ptr, 0);
}

static RC95LDR_STATUS
Rc95LdrCmCopyUnicode(const UNICODE_STRING *Value, char *Buffer, SIZE_T Capacity)
{
    USHORT i, Count;
    if (!Value || !Buffer || !Capacity ||
        (Value->Length & (sizeof(WCHAR) - 1)) ||
        Value->Length > Value->MaximumLength ||
        (Value->Length != 0 && !Value->Buffer))
        return Rc95LdrCorruptData;
    Count = Value->Length / sizeof(WCHAR);
    if ((SIZE_T)Count >= Capacity) return Rc95LdrOutOfBounds;
    for (i = 0; i < Count; ++i)
    {
        WCHAR Ch = Value->Buffer[i];
        /* UNICODE_STRING is length-delimited, not necessarily terminated.
         * Reject embedded NULs and control characters instead of allowing
         * a truncated or otherwise ambiguous service/image path downstream. */
        if (Ch == UNICODE_NULL || Ch < 0x20 || Ch == 0x7f)
            return Rc95LdrCorruptData;
        if (Ch > 0x7f) return Rc95LdrUnsupported;
        Buffer[i] = (char)Ch;
    }
    Buffer[Count] = '\0';
    return Rc95LdrSuccess;
}

static BOOLEAN
Rc95LdrCmGetServiceType(
    PHHIVE Hive,
    HCELL_INDEX ControlSet,
    PBOOT_DRIVER_NODE Driver,
    PULONG ServiceType)
{
    PCM_KEY_NODE ControlNode, ServicesNode, ServiceNode;
    HCELL_INDEX ServicesCell, ServiceCell, ValueCell;
    UNICODE_STRING Name;
    PCM_KEY_VALUE Value;
    PULONG Data;
    ULONG Length;

    if (!ServiceType) return FALSE;
    *ServiceType = 0;
    ControlNode = (PCM_KEY_NODE)HvGetCell(Hive, ControlSet);
    if (!ControlNode) return FALSE;
    RtlInitUnicodeString(&Name, L"Services");
    ServicesCell = CmpFindSubKeyByName(Hive, ControlNode, &Name);
    if (ServicesCell == HCELL_NIL) return FALSE;
    ServicesNode = (PCM_KEY_NODE)HvGetCell(Hive, ServicesCell);
    if (!ServicesNode) return FALSE;
    ServiceCell = CmpFindSubKeyByName(Hive, ServicesNode, &Driver->Name);
    if (ServiceCell == HCELL_NIL) return FALSE;
    ServiceNode = (PCM_KEY_NODE)HvGetCell(Hive, ServiceCell);
    if (!ServiceNode) return FALSE;
    RtlInitUnicodeString(&Name, L"Type");
    ValueCell = CmpFindValueByName(Hive, ServiceNode, &Name);
    if (ValueCell == HCELL_NIL) return FALSE;
    Value = (PCM_KEY_VALUE)HvGetCell(Hive, ValueCell);
    if (!Value || Value->Type != REG_DWORD) return FALSE;
    Data = (PULONG)CmpValueToData(Hive, Value, &Length);
    if (!Data || Length != sizeof(ULONG)) return FALSE;
    *ServiceType = *Data;
    return TRUE;
}

RC95LDR_STATUS
Rc95LdrCmEnumerateBootDrivers(
    const void *SystemHiveBytes,
    size_t SystemHiveSize,
    RC95LDR_HIVE_BOOT_DRIVER_CALLBACK Callback,
    void *Context)
{
    HHIVE Hive;
    LIST_ENTRY DriverList;
    PLIST_ENTRY Entry;
    UNICODE_STRING CurrentName;
    HCELL_INDEX ControlSet;
    BOOLEAN AutoSelect = FALSE;
    NTSTATUS NtStatus;
    RC95LDR_STATUS Status = Rc95LdrSuccess;
    PVOID WritableHive;

    if (!SystemHiveBytes || !SystemHiveSize || SystemHiveSize > MAXULONG || !Callback)
        return Rc95LdrInvalidParameter;

    /* CmpFindControlSet updates Select\Current. Work on a private writable
     * copy so the loader's original SYSTEM hive remains unchanged. */
    WritableHive = FrLdrHeapAlloc(SystemHiveSize, RC95LDR_CM_TAG);
    if (!WritableHive) return Rc95LdrOutOfMemory;
    RtlCopyMemory(WritableHive, SystemHiveBytes, SystemHiveSize);
    RtlZeroMemory(&Hive, sizeof(Hive));

    NtStatus = HvInitialize(&Hive,
                            HINIT_FLAT,
                            0,
                            HFILE_TYPE_PRIMARY,
                            WritableHive,
                            Rc95LdrCmAllocate,
                            Rc95LdrCmFree,
                            NULL,
                            NULL,
                            NULL,
                            NULL,
                            1,
                            NULL);
    if (!NT_SUCCESS(NtStatus))
    {
        FrLdrHeapFree(WritableHive, 0);
        return Rc95LdrCorruptData;
    }

    InitializeListHead(&DriverList);
    RtlInitUnicodeString(&CurrentName, L"Current");
    ControlSet = CmpFindControlSet(&Hive,
                                   Hive.BaseBlock->RootCell,
                                   &CurrentName,
                                   &AutoSelect);
    if (ControlSet == HCELL_NIL)
    {
        Status = Rc95LdrNotFound;
        goto Cleanup;
    }

    /* BootLoad is SERVICE_BOOT_START (Start=0). Start=1 services are loaded
     * later by the kernel and are deliberately not mapped by winload. */
    if (!CmpFindDrivers(&Hive, ControlSet, BootLoad, NULL, &DriverList) ||
        !CmpSortDriverList(&Hive, ControlSet, &DriverList) ||
        !CmpResolveDriverDependencies(&DriverList))
    {
        Status = Rc95LdrCorruptData;
        goto Cleanup;
    }

    for (Entry = DriverList.Flink; Entry != &DriverList; Entry = Entry->Flink)
    {
        PBOOT_DRIVER_NODE Node;
        RC95LDR_HIVE_BOOT_DRIVER Driver;
        ULONG ServiceType;

        Node = CONTAINING_RECORD(Entry, BOOT_DRIVER_NODE, ListEntry.Link);
        /* CmpFindDrivers selected this service for BootLoad. A missing,
         * malformed, or unreadable Type value must not silently drop it:
         * doing so could omit a boot-critical image while still publishing
         * an apparently complete loader block. */
        if (!Rc95LdrCmGetServiceType(&Hive, ControlSet, Node, &ServiceType))
        {
            Status = Rc95LdrCorruptData;
            break;
        }
        if (ServiceType != SERVICE_KERNEL_DRIVER &&
            ServiceType != SERVICE_FILE_SYSTEM_DRIVER)
            continue;

        RtlZeroMemory(&Driver, sizeof(Driver));
        Status = Rc95LdrCmCopyUnicode(&Node->Name, Driver.ServiceName,
                                      sizeof(Driver.ServiceName));
        if (Status != Rc95LdrSuccess) break;
        Status = Rc95LdrCmCopyUnicode(&Node->ListEntry.FilePath, Driver.ImagePath,
                                      sizeof(Driver.ImagePath));
        if (Status != Rc95LdrSuccess) break;
        if (Node->Group.Buffer)
        {
            Status = Rc95LdrCmCopyUnicode(&Node->Group, Driver.Group,
                                          sizeof(Driver.Group));
            if (Status != Rc95LdrSuccess) break;
        }
        Driver.Start = SERVICE_BOOT_START;
        Driver.Type = ServiceType;
        Driver.ErrorControl = Node->ErrorControl;
        Driver.Tag = Node->Tag;
        Status = Callback(Context, &Driver);
        if (Status != Rc95LdrSuccess) break;
    }

Cleanup:
    CmpFreeDriverList(&Hive, &DriverList);
    HvFree(&Hive);
    FrLdrHeapFree(WritableHive, 0);
    return Status;
}
