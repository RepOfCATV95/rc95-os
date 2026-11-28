#pragma once

/* Built-in GOP-style 5x7 bitmap font for the BGFX framebuffer renderer. */
NTSTATUS NTAPI BgfxDrawText(_In_ ULONG Left, _In_ ULONG Top,
                             _In_z_ PCSTR Text, _In_ ULONG Foreground,
                             _In_ ULONG Background, _In_ ULONG Scale);
