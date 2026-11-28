# RC95LDR BGFX boot presentation

RC95LDR BGFX is the native boot-application framebuffer renderer. It is
independent of the kernel BGFX subsystem under `ntoskrnl/bgk`. The common
renderer validates framebuffer dimensions, pitch, pixel format and mapped
extent; supports RGBX8888, BGRX8888 and RGB565; and provides fill, rectangle,
built-in 5x7 text, a Vista/7-inspired boot selection panel, selection
highlighting, and an F8 advanced-startup page. Long boot-entry lists use a
scrolling viewport so the selected row remains visible without overwriting the
footer or drawing menu rows outside the panel.

## Firmware integration

The shared firmware-menu coordinator in `firmware/firmwaremenu.c` selects a
supported display mode, builds an `RC95LDR_BGFX_PLATFORM`, invokes the common
renderer, and falls back to the text UI when graphics discovery, mode setting,
framebuffer mapping, or validation fails.

- **BIOS/VBE:** `bios/video.c` enumerates VBE 2.0+ linear-framebuffer modes,
  validates supported 16-bit RGB565 and 32-bit RGBX/BGRX layouts, and exposes
  mode-setting and framebuffer-mapping callbacks. BIOS startup binds these
  operations through `Rc95LdrBiosBindFirmwareMenuOps` and
  `Rc95LdrBiosRunCandidateMenu`.
- **UEFI/GOP:** `uefi/uefivideo.c` discovers GOP, enumerates QueryMode
  results, rejects BLT-only and unsupported bitmask formats, sets the selected
  mode, and exposes its framebuffer. UEFI native boot binds the firmware menu
  before candidate selection and calls `Rc95LdrRunUefiCandidateMenu`.
- **Mode selection:** The coordinator prefers 1024x768 when available;
  otherwise it selects the largest supported mode within the validated
  desktop-size and aspect-ratio limits. The common renderer does not depend on
  EFI or BIOS ABI structures.
- **Input:** Firmware adapters normalize arrow, Enter, Escape and F8 input to
  `RC95LDR_TEXTUI_KEY_*` values.

UEFI GOP and Boot Services pointers are only used while boot services remain
active. They must not be used after `ExitBootServices`.

## BCD graphics policy and fallback

The BCD Boolean element `graphicsmodedisabled` is decoded by the firmware-menu
policy. A true value disables graphics for the selected OS-loader candidate
on both BIOS and UEFI; it does not attempt GOP/VBE enumeration, mode setting,
or framebuffer mapping. BIOS selects its existing 80x50 text mode, while UEFI
retains firmware text-console ownership. The selected candidate's exact
policy value is returned to the caller for subsequent boot dispatch.

Any graphics initialization failure falls back to
`Rc95LdrTextUiRunBootMenu`. Rendering never mutates BCD. Advanced-startup
actions are delegated to the platform policy callback; the renderer itself
does not silently change BCD settings.

## Host regression coverage

The standalone host suite includes:

- `rc95ldr_bgfx_tests`: framebuffer bounds and pitch, RGBX/BGRX/RGB565
  packing, clipped text, error rendering, graphics-disabled behavior, and
  scrolling boot/advanced menus with guard-byte checks.
- `rc95ldr_firmware_menu_tests`: BIOS/UEFI mode selection, non-XGA fallback,
  text fallback, candidate-specific BCD graphics policy, and restart action.
- `rc95ldr_uefi_video_tests`: GOP discovery, mode enumeration, supported
  pixel formats, framebuffer extent validation, mode transitions, and text
  input adapter behavior.

The CI workflow `.github/workflows/bgfx-host-tests.yml` builds and runs the
BGFX, firmware-menu, UEFI-services and UEFI-video regression targets with GCC
and Clang.
