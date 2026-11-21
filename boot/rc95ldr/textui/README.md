# RC95LDR text-mode screens

This is the initial boot UI for RC95LDR. It deliberately uses a firmware-neutral
text-console contract and does not depend on BGFX, GOP, VBE, a framebuffer, or
boot graphics resources.

The platform adapter supplies a write callback and may supply a clear-screen
callback. `Rc95LdrTextUiClear` exposes clearing as an explicit operation; every
screen renderer also calls it before drawing. If the adapter omits `Clear`, the
operation succeeds without changing the display, preserving compatibility with
write-only consoles. BIOS adapters can target the conventional text display or
their firmware console; UEFI adapters can use Simple Text Output. The UI itself
owns screen content and message presentation, not firmware protocol discovery.

Available screens:
- Welcome / boot-manager title
- Boot selection (caller-provided entries and selected-entry marker)
- Progress / current operation
- Error / status code and explanatory message

The screens are intentionally synchronous and do not retain caller strings.
The boot manager remains responsible for input and selection policy. BGFX can
later replace the presentation layer without changing boot policy or error
codes. When no text console is attached, calls are harmless no-ops and the
loader continues to return structured status values.
