#pragma once

#include "bgfx_framebuffer.h"
#include "bgfx_font.h"

extern BOOLEAN BGFXGraphicsActive;

BOOLEAN
NTAPI
IsBootResGenuine(VOID);

/* TRUE when bootres.dll is absent or its RC95 ownership resource is invalid. */
BOOLEAN
NTAPI
IsBootResMissingOrCorrupt(VOID);

CODE_SEG("INIT")
VOID
NTAPI
DisplayGopBitmap(
    _In_ BOOLEAN InText
    );

CODE_SEG("INIT")
VOID
NTAPI
KillBGFXEngine(VOID);
