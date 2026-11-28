# RC95LDR text-mode screens

This is the initial boot UI for RC95LDR. It deliberately uses a firmware-neutral
text-console contract and does not depend on BGFX, GOP, VBE, a framebuffer, or
boot graphics resources.

The platform adapter supplies a write callback and may supply a clear-screen
callback. `Rc95LdrTextUiClear` exposes clearing as an explicit operation; every
screen renderer also calls it before drawing. If the adapter omits `Clear`,
the operation succeeds without changing the display, preserving compatibility
with write-only consoles. BIOS adapters can target the conventional text
display or their firmware console; UEFI adapters can use Simple Text Output.
The UI itself owns screen content and message presentation, not firmware
protocol discovery.

Available screens:
- Welcome / boot-manager title
- Boot selection (caller-provided entries and selected-entry marker)
- Progress / current operation, with a 50-character ASCII bar and percentage
- Error / status code and explanatory message

Progress rendering:
- `Rc95LdrTextUiShowProgress` draws the operation at 0%.
- `Rc95LdrTextUiUpdateProgress` redraws the operation and bar for a supplied
  percentage. The unsigned input is clamped to 100 before calculating the
  filled width, so the bar is always within the inclusive 0-100% range.
- The bar uses `#` for completed work and `-` for remaining work, and prints
  the clamped percentage beside it. No formatted-I/O runtime is required.

The screens are intentionally synchronous and do not retain caller strings.
The boot manager remains responsible for input and selection policy. BGFX can
later replace the presentation layer without changing boot policy or error
codes. When no text console is attached, calls are harmless no-ops and the
loader continues to return structured status values.


## 80x50 console, boot selection, and advanced startup

The text UI exposes an optional, size-versioned `RC95LDR_TEXTUI_EXT` adapter.
The platform binds `SetMode` to the firmware console's supported text-mode
operation and calls `Rc95LdrTextUiSetVideoMode(..., 80, 50)` before drawing
the boot manager. A firmware that cannot provide 80x50 should return an
explicit unsupported status; the caller may then retain its current mode.
The v1 `RC95LDR_TEXTUI` structure is unchanged.

`Rc95LdrTextUiRunBootMenu` implements wraparound UP/DOWN navigation, ENTER
selection, ESC cancellation, and F8 Advanced Boot Options. The advanced page
offers Safe Mode, Safe Mode with Networking, Disable automatic restart,
Enable boot logging, and Enable debugging. The platform's
`SelectAdvancedOption` callback maps the selected option into the selected
BCD entry's OS-specific load-options policy; the presentation layer does not
rewrite BCD or assume that every Windows generation supports every switch.
The caller still owns the BCD display order/default/timeout and starts the
selected entry only after this function returns success.

Key input is normalized by the platform adapter to the constants in
`rc95ldrtextui.h`. BIOS and UEFI adapters must translate their native
keyboard events to these values. The renderer remains console-stream based
and does not depend on GOP/BGFX.
