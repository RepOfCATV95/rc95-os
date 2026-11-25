#pragma once

BOOLEAN
NTAPI
IsBootResGenuine(VOID);

CODE_SEG("INIT")
VOID
NTAPI
DisplayGopBitmap(
    _In_ BOOLEAN InText
    );
