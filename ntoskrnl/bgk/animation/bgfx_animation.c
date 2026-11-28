/*
 * RC95-OS indeterminate boot animation.
 *
 * Decode the complete eight-frame resource set once during BGK startup.
 * A periodic kernel timer owns frame cadence, so animation remains active
 * when no boot-progress callback is being issued. Progress callbacks may
 * still call BgfxAnimationTick; the interlocked gate makes both paths safe.
 */
#include <ntoskrnl.h>
#include "../include/bgfx_animation.h"
#include "../include/bgfx_bitmap.h"
#include "../include/bgfx_framebuffer.h"

#define NDEBUG
#include <debug.h>

static ULONG BgfxAnimationFrame;
static ULONGLONG BgfxAnimationLastFrame;
static volatile LONG BgfxAnimationRunning;
static volatile LONG BgfxAnimationTickBusy;
static BGFX_BITMAP BgfxAnimationBitmaps[BGFX_ANIMATION_FRAME_COUNT];
static ULONG BgfxAnimationLoadedFrames;
static KTIMER BgfxAnimationTimer;
static KDPC BgfxAnimationDpc;
static BOOLEAN BgfxAnimationTimerInitialized;

static const PCWSTR BgfxAnimationResources[BGFX_ANIMATION_FRAME_COUNT] =
{
    L"RC95OSANIM00.BMP", L"RC95OSANIM01.BMP",
    L"RC95OSANIM02.BMP", L"RC95OSANIM03.BMP",
    L"RC95OSANIM04.BMP", L"RC95OSANIM05.BMP",
    L"RC95OSANIM06.BMP", L"RC95OSANIM07.BMP"
};

static VOID NTAPI
BgfxAnimationTimerDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(DeferredContext);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    if (!InterlockedCompareExchange(&BgfxAnimationRunning, 0, 0))
    {
        KeCancelTimer(&BgfxAnimationTimer);
        return;
    }

    (VOID)BgfxAnimationTick();
}

VOID NTAPI
BgfxAnimationStop(VOID)
{
    ULONG Index;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

    /*
     * This routine is a PASSIVE_LEVEL lifecycle operation. Cancel and drain
     * the timer DPC before freeing its cached nonpaged bitmap buffers.
     */
    InterlockedExchange(&BgfxAnimationRunning, 0);
    if (BgfxAnimationTimerInitialized)
    {
        KeCancelTimer(&BgfxAnimationTimer);
        KeFlushQueuedDpcs();
    }

    while (InterlockedCompareExchange(&BgfxAnimationTickBusy, 0, 0) != 0)
        KeStallExecutionProcessor(50);

    BgfxAnimationFrame = 0;
    BgfxAnimationLastFrame = 0;
    for (Index = 0; Index < BgfxAnimationLoadedFrames; ++Index)
        BgfxFreeBitmap(&BgfxAnimationBitmaps[Index]);

    BgfxAnimationLoadedFrames = 0;
    RtlZeroMemory(BgfxAnimationBitmaps, sizeof(BgfxAnimationBitmaps));
}

static NTSTATUS
BgfxLoadAnimationFrames(VOID)
{
    ULONG Index;
    ULONG Width = 0, Height = 0;
    ULONG ScreenWidth, ScreenHeight;
    NTSTATUS Status;

    ScreenWidth = BgfxGetFramebufferWidth();
    ScreenHeight = BgfxGetFramebufferHeight();
    if (!ScreenWidth || !ScreenHeight)
        return STATUS_DEVICE_NOT_READY;

    for (Index = 0; Index < BGFX_ANIMATION_FRAME_COUNT; ++Index)
    {
        BGFX_BITMAP *Bitmap = &BgfxAnimationBitmaps[Index];

        Status = BgfxLoadBitmapResource(BgfxAnimationResources[Index], Bitmap);
        if (!NT_SUCCESS(Status))
            goto Failure;

        ++BgfxAnimationLoadedFrames;
        if (!Index)
        {
            Width = Bitmap->Width;
            Height = Bitmap->Height;
        }
        else if (Bitmap->Width != Width || Bitmap->Height != Height)
        {
            /* Fixed-size frames prevent trails when the animation advances. */
            Status = STATUS_INVALID_IMAGE_FORMAT;
            goto Failure;
        }

        if (!Bitmap->Width || !Bitmap->Height ||
            Bitmap->Width > ScreenWidth || Bitmap->Height > ScreenHeight)
        {
            Status = STATUS_BUFFER_TOO_SMALL;
            goto Failure;
        }
    }

    return STATUS_SUCCESS;

Failure:
    DPRINT1("BGFX: animation resource %lu failed validation (0x%lx)\n",
            Index, Status);
    BgfxAnimationStop();
    return Status;
}

static NTSTATUS
BgfxPresentAnimationFrame(ULONG Frame)
{
    ULONG ScreenWidth, ScreenHeight, Left, Top;
    BGFX_BITMAP *Bitmap;

    if (Frame >= BGFX_ANIMATION_FRAME_COUNT ||
        Frame >= BgfxAnimationLoadedFrames)
        return STATUS_INVALID_PARAMETER;

    Bitmap = &BgfxAnimationBitmaps[Frame];
    ScreenWidth = BgfxGetFramebufferWidth();
    ScreenHeight = BgfxGetFramebufferHeight();
    if (Bitmap->Width > ScreenWidth || Bitmap->Height > ScreenHeight)
        return STATUS_BUFFER_TOO_SMALL;

    Left = (ScreenWidth - Bitmap->Width) / 2;
    Top = ScreenHeight - Bitmap->Height;
    if (Top > 24) Top -= 24;
    return BgfxDrawBitmapAt(Bitmap, Left, Top);
}

NTSTATUS NTAPI
BgfxAnimationStart(VOID)
{
    LARGE_INTEGER DueTime;
    NTSTATUS Status;

    BgfxAnimationStop();
    if (!BgfxAnimationTimerInitialized)
    {
        KeInitializeTimer(&BgfxAnimationTimer);
        KeInitializeDpc(&BgfxAnimationDpc, BgfxAnimationTimerDpc, NULL);
        BgfxAnimationTimerInitialized = TRUE;
    }

    Status = BgfxLoadAnimationFrames();
    if (!NT_SUCCESS(Status))
        return Status;

    Status = BgfxPresentAnimationFrame(0);
    if (!NT_SUCCESS(Status))
    {
        BgfxAnimationStop();
        return Status;
    }

    BgfxAnimationFrame = 0;
    BgfxAnimationLastFrame = KeQueryInterruptTime();
    InterlockedExchange(&BgfxAnimationRunning, 1);

    DueTime.QuadPart = -(LONGLONG)BGFX_ANIMATION_FRAME_INTERVAL_100NS;
    KeSetTimerEx(&BgfxAnimationTimer,
                 DueTime,
                 100, /* periodic interval in milliseconds */
                 &BgfxAnimationDpc);
    return STATUS_SUCCESS;
}

BOOLEAN NTAPI
BgfxAnimationTick(VOID)
{
    ULONGLONG Now;
    NTSTATUS Status;

    if (!InterlockedCompareExchange(&BgfxAnimationRunning, 0, 0))
        return FALSE;
    if (InterlockedCompareExchange(&BgfxAnimationTickBusy, 1, 0) != 0)
        return FALSE;

    if (!InterlockedCompareExchange(&BgfxAnimationRunning, 0, 0))
    {
        InterlockedExchange(&BgfxAnimationTickBusy, 0);
        return FALSE;
    }

    Now = KeQueryInterruptTime();
    if (Now - BgfxAnimationLastFrame < BGFX_ANIMATION_FRAME_INTERVAL_100NS)
    {
        InterlockedExchange(&BgfxAnimationTickBusy, 0);
        return FALSE;
    }

    BgfxAnimationLastFrame = Now;
    BgfxAnimationFrame = (BgfxAnimationFrame + 1) % BGFX_ANIMATION_FRAME_COUNT;
    Status = BgfxPresentAnimationFrame(BgfxAnimationFrame);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("BGFX: cached animation frame %lu presentation failed: 0x%lx\n",
                BgfxAnimationFrame, Status);
        InterlockedExchange(&BgfxAnimationRunning, 0);
        KeCancelTimer(&BgfxAnimationTimer);
        InterlockedExchange(&BgfxAnimationTickBusy, 0);
        return FALSE;
    }

    InterlockedExchange(&BgfxAnimationTickBusy, 0);
    return TRUE;
}
