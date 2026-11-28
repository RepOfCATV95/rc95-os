#include <asm.inc>

/*
 * RC95LDR native BIOS loader entry.
 *
 * Direct protected-mode ABI:
 *   EAX = RC95LDR_BIOS_ENTRY_MAGIC (0x52353942)
 *   EBX = flat pointer to caller-owned RC95LDR_BIOS_STARTUP
 *
 * Preconditions: CR0.PE=1, CR0.PG=0, A20 enabled, identity mapped memory,
 * CS=HEX(08) and flat writable DS/ES/SS=HEX(10). Interrupts are disabled on entry.
 * Returns RC95LDR_STATUS in EAX and restores the caller's stack.
 */

#define RC95LDR_BIOS_ENTRY_MAGIC HEX(52353942)

.code32
EXTERN _Rc95LdrBiosEntryMain:PROC

PUBLIC _Rc95LdrBiosI386Entry
_Rc95LdrBiosI386Entry:
    cli
    cld
    cmp eax, RC95LDR_BIOS_ENTRY_MAGIC
    jne BiosEntryBadProtocol

    mov edx, cr0
    test edx, 1
    jz BiosEntryBadEnvironment
    test edx, HEX(080000000)
    jnz BiosEntryBadEnvironment
    mov dx, cs
    cmp dx, HEX(008)
    jne BiosEntryBadEnvironment
    test ebx, ebx
    jz BiosEntryBadProtocol

    mov ax, HEX(010)
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    mov dword ptr ds:[_Rc95LdrBiosEntrySavedStack], esp
    mov esp, offset _Rc95LdrBiosEntryStackTop
    and esp, HEX(0FFFFFFF0)
    push ebx
    call _Rc95LdrBiosEntryMain
    add esp, 4
    mov esp, dword ptr ds:[_Rc95LdrBiosEntrySavedStack]
    ret

BiosEntryBadProtocol:
    mov eax, 1                   /* Rc95LdrInvalidParameter */
    ret
BiosEntryBadEnvironment:
    mov eax, 3                   /* Rc95LdrUnsupported */
    ret

/*
 * C-callable bridge used after the real-mode stage has populated Startup.
 * It deliberately creates the documented EAX/EBX register handoff rather
 * than bypassing the native entry point with a direct C call.
 */
PUBLIC _Rc95LdrBiosInvokeNativeEntry
_Rc95LdrBiosInvokeNativeEntry:
    push ebx
    mov ebx, dword ptr [esp + 8]
    mov eax, RC95LDR_BIOS_ENTRY_MAGIC
    call _Rc95LdrBiosI386Entry
    pop ebx
    ret

.data
ALIGN 4
_Rc95LdrBiosEntrySavedStack DWORD 0

.bss
ALIGN 16

_Rc95LdrBiosEntryStack BYTE 16384 DUP (?)

_Rc95LdrBiosEntryStackTop LABEL BYTE

END
