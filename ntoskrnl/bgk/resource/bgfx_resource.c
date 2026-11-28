/*
 * RC95-OS bootres resource access. BMPs are named RCDATA entries.
 */
#include <ntoskrnl.h>
#include "../include/bgfx_bitmap.h"

#define BGFX_BOOTRES_TYPE MAKEINTRESOURCE(10)

static PVOID BgfxFindBootResModule(VOID)
{
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"bootres.dll");
    PLIST_ENTRY Head, Link;
    if (!KeLoaderBlock) return NULL;
    Head = &KeLoaderBlock->LoadOrderListHead;
    for (Link = Head->Flink; Link != Head; Link = Link->Flink)
    {
        PLDR_DATA_TABLE_ENTRY Entry =
            CONTAINING_RECORD(Link, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
        if (RtlEqualUnicodeString(&Entry->BaseDllName, &Name, TRUE))
            return Entry->DllBase;
    }
    return NULL;
}

NTSTATUS NTAPI
BgfxLoadBitmapResource(PCWSTR ResourceName, PBGFX_BITMAP Bitmap)
{
    UNICODE_STRING Name;
    LDR_RESOURCE_INFO Info;
    PIMAGE_RESOURCE_DATA_ENTRY Entry;
    PVOID Module, Data;
    ULONG DataSize;
    NTSTATUS Status;

    if (!ResourceName || !Bitmap) return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(Bitmap, sizeof(*Bitmap));
    Module = BgfxFindBootResModule();
    if (!Module) return STATUS_RESOURCE_DATA_NOT_FOUND;
    RtlInitUnicodeString(&Name, ResourceName);
    Info.Type = (ULONG_PTR)BGFX_BOOTRES_TYPE;
    Info.Name = (ULONG_PTR)&Name;
    Info.Language = MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL);
    Status = LdrFindResource_U(Module, &Info, RESOURCE_DATA_LEVEL, &Entry);
    if (!NT_SUCCESS(Status)) return Status;
    Status = LdrAccessResource(Module, Entry, &Data, &DataSize);
    if (!NT_SUCCESS(Status)) return Status;
    return BgfxDecodeBmp((const UCHAR *)Data, DataSize, Bitmap);
}
