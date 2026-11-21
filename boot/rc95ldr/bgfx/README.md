# RC95LDR BGFX

Boot-application graphics belong here. This is distinct from the kernel BGFX
implementation under ntoskrnl/bgk/bgfx and from the UEFI GOP protocol driver.

The bootloader BGFX layer will consume the RC95LDR platform framebuffer
interface and own boot UI composition, boot-resource decoding, progress
presentation, and animation. The UEFI adapter supplies GOP framebuffer
metadata; the BIOS adapter may supply a VBE framebuffer or a text-mode fallback.

Do not duplicate GOP protocol discovery here and do not call UEFI boot
services after ExitBootServices. Keep a text/serial fallback for systems
without a usable graphics mode.
