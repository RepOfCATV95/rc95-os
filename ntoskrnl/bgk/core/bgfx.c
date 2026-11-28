/*
 * RC95-OS BGFX Controller
 * Controls the animation of the GOP boot screen using assets from bootres.dll.
 *
 * The bootres ownership record is a format/producer marker, not a cryptographic
 * signature. Secure authenticity must be supplied by the platform image trust
 * chain; this check prevents accidental use of foreign, missing, or malformed
 * boot resource DLLs.
 */

#include <ntoskrnl.h>
#include "../include/bgfx.h"
#include "../include/bgfx_bitmap.h"
#include "../include/bgfx_animation.h"
#include "../../inbv/logo.h"

#define NDEBUG
#include <debug.h>

BOOLEAN BGFXGraphicsActive;

#define RC95_BOOTRES_TYPE_RCDATA       MAKEINTRESOURCE(10)
#define RC95_BOOTRES_MARKER_RESOURCE   MAKEINTRESOURCE(0x95)
#define RC95_BOOTRES_SIGNATURE         0x35394352UL /* "RC95" */
#define RC95_BOOTRES_FORMAT_VERSION    1UL

typedef struct _RC95_BOOTRES_OWNER_MARKER
{
    ULONG Signature;
    ULONG Version;
    ULONG HeaderSize;
    ULONG TotalSize;
    UCHAR OwnerId[16];
    ULONG Checksum;
} RC95_BOOTRES_OWNER_MARKER, *PRC95_BOOTRES_OWNER_MARKER;

/* Stable RC95-OS product identifier embedded by the bootres resource build. */
static const UCHAR Rc95BootResOwnerId[16] =
{
    0x95, 0x43, 0x41, 0x54, 0x56, 0x2D, 0x52, 0x43,
    0x39, 0x35, 0x2D, 0x4F, 0x53, 0x00, 0x00, 0x01
};

static
BOOLEAN
NTAPI
BgfxQueryBootResOwnerMarker(
    _Out_ PRC95_BOOTRES_OWNER_MARKER *Marker)
{
    UNICODE_STRING BootResName = RTL_CONSTANT_STRING(L"bootres.dll");
    PLIST_ENTRY ListHead, NextEntry;
    PLDR_DATA_TABLE_ENTRY LdrEntry = NULL;
    PIMAGE_RESOURCE_DATA_ENTRY ResourceEntry;
    LDR_RESOURCE_INFO ResourceInfo;
    PVOID ResourceData = NULL;
    ULONG ResourceSize = 0;
    NTSTATUS Status;
    PRC95_BOOTRES_OWNER_MARKER OwnerMarker;

    *Marker = NULL;
    if (KeLoaderBlock == NULL)
        return FALSE;

    ListHead = &KeLoaderBlock->LoadOrderListHead;
    for (NextEntry = ListHead->Flink;
         NextEntry != ListHead;
         NextEntry = NextEntry->Flink)
    {
        PLDR_DATA_TABLE_ENTRY Entry =
            CONTAINING_RECORD(NextEntry, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);

        if (RtlEqualUnicodeString(&Entry->BaseDllName, &BootResName, TRUE))
        {
            LdrEntry = Entry;
            break;
        }
    }

    if (LdrEntry == NULL || LdrEntry->DllBase == NULL)
        return FALSE;

    ResourceInfo.Type = (ULONG_PTR)RC95_BOOTRES_TYPE_RCDATA;
    ResourceInfo.Name = (ULONG_PTR)RC95_BOOTRES_MARKER_RESOURCE;
    ResourceInfo.Language = MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL);

    Status = LdrFindResource_U(LdrEntry->DllBase,
                               &ResourceInfo,
                               RESOURCE_DATA_LEVEL,
                               &ResourceEntry);
    if (!NT_SUCCESS(Status))
        return FALSE;

    Status = LdrAccessResource(LdrEntry->DllBase,
                               ResourceEntry,
                               &ResourceData,
                               &ResourceSize);
    if (!NT_SUCCESS(Status) ||
        ResourceData == NULL ||
        ResourceSize != sizeof(RC95_BOOTRES_OWNER_MARKER))
    {
        return FALSE;
    }

    OwnerMarker = (PRC95_BOOTRES_OWNER_MARKER)ResourceData;
    if (OwnerMarker->Signature != RC95_BOOTRES_SIGNATURE ||
        OwnerMarker->Version != RC95_BOOTRES_FORMAT_VERSION ||
        OwnerMarker->HeaderSize != sizeof(RC95_BOOTRES_OWNER_MARKER) ||
        OwnerMarker->TotalSize != sizeof(RC95_BOOTRES_OWNER_MARKER) ||
        OwnerMarker->Checksum !=
            RtlComputeCrc32(0,
                            (PUCHAR)OwnerMarker,
                            FIELD_OFFSET(RC95_BOOTRES_OWNER_MARKER, Checksum)))
    {
        return FALSE;
    }

    *Marker = OwnerMarker;
    return TRUE;
}

BOOLEAN
NTAPI
IsBootResMissingOrCorrupt(VOID)
{
    PRC95_BOOTRES_OWNER_MARKER Marker;

    /*
     * TRUE means bootres.dll is not present in the loader module list, its
     * ownership resource cannot be read, or the resource fails format checks.
     * A well-formed resource from another producer is not "corrupt"; it is
     * rejected separately by IsBootResGenuine().
     */
    return !BgfxQueryBootResOwnerMarker(&Marker);
}

BOOLEAN
NTAPI
IsBootResGenuine(VOID)
{
    PRC95_BOOTRES_OWNER_MARKER Marker;

    if (!BgfxQueryBootResOwnerMarker(&Marker))
        return FALSE;

    return RtlCompareMemory(Marker->OwnerId,
                            Rc95BootResOwnerId,
                            sizeof(Rc95BootResOwnerId)) ==
           sizeof(Rc95BootResOwnerId);
}

CODE_SEG("INIT")
VOID
NTAPI
DisplayGopBitmap(
    _In_ BOOLEAN InText
    )
{
    BGFXGraphicsActive = TRUE;

    /* Preserve the legacy text-mode (/SOS) boot presentation. */
    if (InText)
    {
        BGFXGraphicsActive = FALSE;
        DisplayBootBitmap(TRUE);
        return;
    }

    if (IsBootResMissingOrCorrupt())
    {
        DPRINT1("BGFX: bootres.dll is missing or corrupt; falling back to INBV\n");
        BGFXGraphicsActive = FALSE;
        DisplayBootBitmap(InText);
        return;
    }

    if (!IsBootResGenuine())
    {
        DPRINT1("BGFX: bootres.dll is not an RC95-OS resource image; falling back to INBV\n");
        BGFXGraphicsActive = FALSE;
        DisplayBootBitmap(InText);
        return;
    }

    {
        BGFX_BITMAP Logo;
        NTSTATUS Status;

        Status = BgfxInitializeFramebuffer();
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("BGFX: framebuffer discovery/validation failed (0x%lx); falling back to INBV\n",
                    Status);
            DisplayBootBitmap(InText);
            return;
        }

        Status = BgfxLoadBitmapResource(L"RC95OSLOGO.BMP", &Logo);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("BGFX: RC95OSLOGO.BMP load failed (0x%lx); falling back to INBV\n",
                    Status);
            DisplayBootBitmap(InText);
            return;
        }

        Status = BgfxDrawBitmapCentered(&Logo);
        BgfxFreeBitmap(&Logo);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("BGFX: centered logo presentation failed (0x%lx); falling back to INBV\n",
                    Status);
            DisplayBootBitmap(InText);
            return;
        }

        Status = BgfxAnimationStart();
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("BGFX: animation unavailable (0x%lx); static logo remains visible\n",
                    Status);
        }
    }
}

CODE_SEG("INIT")
VOID
NTAPI
KillBGFXEngine(VOID)
{
    if ( !BGFXGraphicsActive )
        return;

    BgfxAnimationStop();
    BgfxResetFramebuffer();
    BGFXGraphicsActive = FALSE;
}
