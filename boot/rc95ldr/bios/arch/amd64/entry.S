#include <asm.inc>

/*
 * RC95LDR AMD64 BIOS-provider entry bridge.
 *
 * Windows x64 ABI:
 *   RCX = RC95LDR_BIOS_STARTUP *
 *   EDX = RC95LDR_BIOS_ENTRY_MAGIC
 *
 * Preconditions: IA-32e long mode active, paging enabled, executable image
 * and data reachable, and the caller's stack writable. The provider must
 * supply a BIOS interrupt transition thunk; this routine never executes
 * legacy BIOS interrupts from long mode.
 */

.code64
EXTERN Rc95LdrBiosEntryMain:PROC

PUBLIC Rc95LdrBiosAmd64Entry
Rc95LdrBiosAmd64Entry:
    cli
    cld
    cmp edx, HEX(52353942)
    jne BiosAmd64BadProtocol

    push r12
    mov r12, rsp

    lea rsp, [Rc95LdrBiosAmd64EntryStackTop]
    and rsp, -16
    sub rsp, 32                 /* Windows x64 shadow space. */
    call Rc95LdrBiosEntryMain

    mov rsp, r12
    pop r12
    ret

BiosAmd64BadProtocol:
    mov eax, 1                  /* Rc95LdrInvalidParameter */
    ret

PUBLIC Rc95LdrBiosInvokeNativeEntry
Rc95LdrBiosInvokeNativeEntry:
    mov edx, HEX(52353942)
    jmp Rc95LdrBiosAmd64Entry

#ifdef _USE_ML
; MASM requires an uninitialized-data segment for DUP (?) storage.
.data?
ALIGN 16
Rc95LdrBiosAmd64EntryStack BYTE 32768 DUP (?)
Rc95LdrBiosAmd64EntryStackTop LABEL BYTE
#else
/* GAS keeps the stack in the uninitialized .bss section. */
.section .bss
.balign 16
Rc95LdrBiosAmd64EntryStack:
    .space 32768
Rc95LdrBiosAmd64EntryStackTop:
#endif
END
