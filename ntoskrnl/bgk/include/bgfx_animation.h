#pragma once

#define BGFX_ANIMATION_FRAME_COUNT 8
#define BGFX_ANIMATION_FRAME_INTERVAL_100NS (1000000ULL) /* 100 ms */

NTSTATUS NTAPI BgfxAnimationStart(VOID);
/*
 * Stop is a PASSIVE_LEVEL lifecycle operation. It cancels the periodic timer,
 * drains queued DPCs, waits for an in-flight progress tick, then frees frames.
 */
VOID NTAPI BgfxAnimationStop(VOID);
/*
 * Optional progress-path pump. The periodic timer is authoritative; this
 * function remains safe for legacy progress callbacks and returns TRUE only
 * when it actually presents a new frame.
 */
BOOLEAN NTAPI BgfxAnimationTick(VOID);
