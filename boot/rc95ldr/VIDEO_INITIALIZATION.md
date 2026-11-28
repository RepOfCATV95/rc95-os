# RC95LDR native video initialization

RC95LDR owns its video path. The native implementation does not call
FreeLoader video routines, FreeLoader machine tables, or FreeLoader display
mode parsers.

## Shared renderer and policy

`bgfx` is the firmware-independent framebuffer renderer. Firmware adapters
populate `RC95LDR_BGFX_PLATFORM`; `Rc95LdrBgfxInitialize` applies the common
mode policy and validates the framebuffer before it can be rendered.

The preferred graphics mode is **1024x768**. The shared firmware-menu policy
uses that mode when available; otherwise it accepts a sane desktop mode from
640x400 through 4096x2160, with a supported pixel format and a conventional
aspect ratio, preferring the largest pixel area. The framebuffer contract
carries the physical pixel format, byte size, visible dimensions, and
pixels-per-scanline.

`graphicsmodedisabled` is a BIOS compatibility override only:

- BIOS with the option enabled switches to 80x50 text (720x400) and uses the
  native text UI.
- UEFI ignores that legacy BIOS option and continues to request 1024x768 GOP.
- Normal BIOS text screens are cleared with attribute 0x07 (black background).
  `Rc95LdrBiosShowErrorScreen` clears error screens with attribute 0x4F
  (white foreground, red background).
- The framebuffer renderer uses black for ordinary screens and red for its
  error-screen renderer, including unsupported-loader and boot-failure text.

The firmware-menu adapter exports the selected OS-loader's decoded BCD
Boolean through `RC95LDR_FIRMWARE_MENU_OPS.GraphicsModeDisabledOutput`.
Connect that output to `RC95LDR_BIOS_STARTUP.GraphicsModeDisabled` so the BIOS
startup stage applies the policy for the entry actually chosen by the user,
not merely the initially highlighted entry.

## Firmware adapters

### BIOS

`bios/video.c` uses the RC95LDR-owned INT 10h gateway. It queries the VBE 2.0
controller block, validates the far pointer to the terminated mode list, and
queries each listed mode's information block. It accepts VBE 2.x legacy pitch/mask fields and prefers VBE 3.0 linear
pitch/mask fields when present. Supported linear-framebuffer formats are
RGB565 and 32-bit RGB/BGR layouts, with validated dimensions, pitch, and
physical range. Invalid or unterminated mode lists are rejected. The shared selector prefers 1024x768 and can select a
suitable alternate VBE resolution; mode changes use the selected VBE mode ID
with the linear-framebuffer flag. Framebuffers are exposed through the
identity-mapped physical address contract of the native BIOS stage. Text
fallback is provided by the native INT 10h text-mode adapter; 80x50 is selected
by loading the VGA 8x8 font after setting mode 3.

### UEFI

`uefi/uefivideo.c` locates the Graphics Output Protocol, enumerates available
modes, and exposes supported RGBX8888, BGRX8888, or exact RGB565 framebuffer
formats. The shared firmware-menu policy prefers 1024x768 and can fall back
to a suitable native GOP resolution. Keyboard input is read from the UEFI
Simple Text Input protocol.

## Tests

The host-side `rc95ldr_bgfx_tests` exercise the BIOS text override, UEFI
graphics-mode policy, framebuffer validation, and red error rendering. The
BIOS service tests also cover VBE controller-list enumeration, malformed
far pointers, unterminated lists, and switching between 1024x768 RGB565 and
800x600 32-bit modes. VBE 3.0 linear pitch and pixel-mask decoding is tested
alongside the VBE 2.0 legacy-field path.
`rc95ldr_firmware_menu_tests` exercise preferred 1024x768 selection,
native-resolution fallback, and text fallback. `rc95ldr_uefi_video_tests`
exercise GOP pixel-format recognition, mode enumeration, framebuffer mapping,
and invalid-mode rejection.

The BIOS VBE adapter enumerates the VBE 2.0 controller mode list and supports
alternate-resolution switching. Legacy VBE implementations that do not expose
a valid VBE 2.0 mode list continue to use the text fallback; VBE 1.x controller support and runtime validation under multiple physical
BIOS implementations remain follow-up work.
