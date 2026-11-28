#include "ntoskrnl.h"
#include "../include/bgfx_animation.h"
#include "../include/bgfx_bitmap.h"
#include "../include/bgfx_framebuffer.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

static ULONGLONG FakeTime;
static ULONG FakeScreenWidth=10, FakeScreenHeight=10;
static ULONG FakeLoadCount, FakeFreeCount, FakeDrawCount;
static ULONG FakeLastFrame, FakeLastLeft, FakeLastTop;
static LONG FakeFailFrame=-1, FakeMismatchFrame=-1;
static ULONG FakeTimerInitCount, FakeDpcInitCount, FakeTimerSetCount;
static ULONG FakeTimerCancelCount, FakeFlushCount;
static LARGE_INTEGER FakeDueTime;
static LONG FakeTimerPeriod;
static PKDPC FakeTimerDpc;

VOID KeInitializeTimer(PKTIMER Timer)
{
    Timer->Active=FALSE;
    ++FakeTimerInitCount;
}
VOID KeInitializeDpc(PKDPC Dpc,PKDEFERRED_ROUTINE Routine,PVOID Context)
{
    Dpc->DeferredRoutine=Routine;
    Dpc->DeferredContext=Context;
    ++FakeDpcInitCount;
}
BOOLEAN KeSetTimerEx(PKTIMER Timer,LARGE_INTEGER DueTime,LONG Period,PKDPC Dpc)
{
    Timer->Active=TRUE;
    FakeDueTime=DueTime;
    FakeTimerPeriod=Period;
    FakeTimerDpc=Dpc;
    ++FakeTimerSetCount;
    return FALSE;
}
BOOLEAN KeCancelTimer(PKTIMER Timer)
{
    BOOLEAN WasActive=Timer->Active;
    Timer->Active=FALSE;
    ++FakeTimerCancelCount;
    return WasActive;
}
VOID KeFlushQueuedDpcs(VOID) { ++FakeFlushCount; }
VOID KeStallExecutionProcessor(ULONG MicroSeconds) { (void)MicroSeconds; }
ULONGLONG KeQueryInterruptTime(VOID) { return FakeTime; }
ULONG KeGetCurrentIrql(VOID) { return PASSIVE_LEVEL; }
LONG InterlockedCompareExchange(volatile LONG *Destination,LONG Exchange,LONG Comparand)
{
    LONG Previous=*Destination;
    if(Previous==Comparand)*Destination=Exchange;
    return Previous;
}
LONG InterlockedExchange(volatile LONG *Destination,LONG Value)
{
    LONG Previous=*Destination;
    *Destination=Value;
    return Previous;
}

ULONG BgfxGetFramebufferWidth(VOID) { return FakeScreenWidth; }
ULONG BgfxGetFramebufferHeight(VOID) { return FakeScreenHeight; }
NTSTATUS BgfxDrawBitmapAt(const BGFX_BITMAP *Bitmap,ULONG Left,ULONG Top)
{
    if(!Bitmap||!Bitmap->Pixels)return STATUS_INVALID_PARAMETER;
    ++FakeDrawCount;
    FakeLastFrame=Bitmap->Pixels[0];
    FakeLastLeft=Left;
    FakeLastTop=Top;
    return STATUS_SUCCESS;
}
NTSTATUS BgfxLoadBitmapResource(PCWSTR ResourceName,PBGFX_BITMAP Bitmap)
{
    LONG Frame;
    if(!ResourceName||!Bitmap)return STATUS_INVALID_PARAMETER;
    Frame=(LONG)((ResourceName[10]-L'0')*10+(ResourceName[11]-L'0'));
    ++FakeLoadCount;
    if(Frame==FakeFailFrame)return STATUS_RESOURCE_DATA_NOT_FOUND;
    Bitmap->Width=(Frame==FakeMismatchFrame)?3:2;
    Bitmap->Height=1;
    Bitmap->Pixels=(PULONG)malloc(Bitmap->Width*sizeof(ULONG));
    if(!Bitmap->Pixels)return STATUS_INSUFFICIENT_RESOURCES;
    Bitmap->Pixels[0]=(ULONG)Frame;
    if(Bitmap->Width>1)Bitmap->Pixels[1]=(ULONG)Frame;
    return STATUS_SUCCESS;
}
VOID BgfxFreeBitmap(PBGFX_BITMAP Bitmap)
{
    if(Bitmap&&Bitmap->Pixels)
    {
        free(Bitmap->Pixels);
        ++FakeFreeCount;
    }
    if(Bitmap)memset(Bitmap,0,sizeof(*Bitmap));
}

static void ResetCounters(void)
{
    FakeLoadCount=FakeFreeCount=FakeDrawCount=0;
    FakeLastFrame=FakeLastLeft=FakeLastTop=0;
    FakeFailFrame=FakeMismatchFrame=-1;
    FakeTime=0;
}
static void FireTimerDpc(void)
{
    assert(FakeTimerDpc!=NULL);
    FakeTimerDpc->DeferredRoutine(FakeTimerDpc,FakeTimerDpc->DeferredContext,NULL,NULL);
}
static void TestIndependentTimerLifecycle(void)
{
    ULONG cancelBefore,flushBefore;
    ResetCounters();
    assert(BgfxAnimationStart()==STATUS_SUCCESS);
    assert(FakeLoadCount==BGFX_ANIMATION_FRAME_COUNT);
    assert(FakeDrawCount==1 && FakeLastFrame==0);
    assert(FakeLastLeft==4 && FakeLastTop==9);
    assert(FakeTimerInitCount==1 && FakeDpcInitCount==1);
    assert(FakeTimerSetCount==1 && FakeTimerPeriod==100);
    assert(FakeDueTime.QuadPart==-(int64_t)BGFX_ANIMATION_FRAME_INTERVAL_100NS);

    /* No progress callback is involved: the periodic DPC alone advances. */
    FakeTime=BGFX_ANIMATION_FRAME_INTERVAL_100NS-1;
    FireTimerDpc();
    assert(FakeDrawCount==1);
    FakeTime=BGFX_ANIMATION_FRAME_INTERVAL_100NS;
    FireTimerDpc();
    assert(FakeDrawCount==2 && FakeLastFrame==1);
    FakeTime=2*BGFX_ANIMATION_FRAME_INTERVAL_100NS;
    FireTimerDpc();
    assert(FakeDrawCount==3 && FakeLastFrame==2);

    /* A legacy progress tick and the timer share the same time gate. */
    FakeTime=3*BGFX_ANIMATION_FRAME_INTERVAL_100NS;
    assert(BgfxAnimationTick());
    assert(FakeLastFrame==3);
    FireTimerDpc();
    assert(FakeDrawCount==4);

    /* The eight-frame sequence wraps back to frame zero without reloading. */
    FakeTime=4*BGFX_ANIMATION_FRAME_INTERVAL_100NS; FireTimerDpc();
    assert(FakeLastFrame==4);
    FakeTime=5*BGFX_ANIMATION_FRAME_INTERVAL_100NS; FireTimerDpc();
    assert(FakeLastFrame==5);
    FakeTime=6*BGFX_ANIMATION_FRAME_INTERVAL_100NS; FireTimerDpc();
    assert(FakeLastFrame==6);
    FakeTime=7*BGFX_ANIMATION_FRAME_INTERVAL_100NS; FireTimerDpc();
    assert(FakeLastFrame==7);
    FakeTime=8*BGFX_ANIMATION_FRAME_INTERVAL_100NS; FireTimerDpc();
    assert(FakeLastFrame==0 && FakeDrawCount==9);
    assert(FakeLoadCount==BGFX_ANIMATION_FRAME_COUNT);

    cancelBefore=FakeTimerCancelCount;
    flushBefore=FakeFlushCount;
    BgfxAnimationStop();
    assert(FakeTimerCancelCount==cancelBefore+1);
    assert(FakeFlushCount==flushBefore+1);
    assert(FakeFreeCount==BGFX_ANIMATION_FRAME_COUNT);
    assert(!BgfxAnimationTick());
    BgfxAnimationStop();
    assert(FakeFreeCount==BGFX_ANIMATION_FRAME_COUNT);
}
static void TestPartialLoadFailureCleanup(void)
{
    ResetCounters();
    FakeFailFrame=4;
    assert(BgfxAnimationStart()==STATUS_RESOURCE_DATA_NOT_FOUND);
    assert(FakeLoadCount==5);
    assert(FakeFreeCount==4);
    assert(FakeDrawCount==0);
    assert(!BgfxAnimationTick());
}
static void TestMismatchedFrameCleanup(void)
{
    ResetCounters();
    FakeMismatchFrame=2;
    assert(BgfxAnimationStart()==STATUS_INVALID_IMAGE_FORMAT);
    assert(FakeLoadCount==3);
    assert(FakeFreeCount==3);
    assert(FakeDrawCount==0);
}
static void TestFramebufferTooSmallCleanup(void)
{
    ResetCounters();
    FakeScreenWidth=1;
    assert(BgfxAnimationStart()==STATUS_BUFFER_TOO_SMALL);
    assert(FakeLoadCount==1);
    assert(FakeFreeCount==1);
    assert(FakeDrawCount==0);
    FakeScreenWidth=10;
}
int main(void)
{
    TestIndependentTimerLifecycle();
    TestPartialLoadFailureCleanup();
    TestMismatchedFrameCleanup();
    TestFramebufferTooSmallCleanup();
    puts("BGFX animation lifecycle tests passed");
    return 0;
}
