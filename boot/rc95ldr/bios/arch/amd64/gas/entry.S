/* GNU as implementation of the AMD64 BIOS-provider entry bridge. */
.intel_syntax noprefix
.code64
.extern Rc95LdrBiosEntryMain
.global Rc95LdrBiosAmd64Entry

Rc95LdrBiosAmd64Entry:
    cli
    cld
    cmp edx, 0x52353942
    jne .Lbad_protocol
    push r12
    mov r12, rsp
    lea rsp, [rip + Rc95LdrBiosAmd64EntryStackTop]
    and rsp, -16
    sub rsp, 32
    call Rc95LdrBiosEntryMain
    mov rsp, r12
    pop r12
    ret
.Lbad_protocol:
    mov eax, 1
    ret


.global Rc95LdrBiosInvokeNativeEntry

Rc95LdrBiosInvokeNativeEntry:
    mov edx, 0x52353942
    jmp Rc95LdrBiosAmd64Entry


.section .bss
.balign 16
Rc95LdrBiosAmd64EntryStack:
    .skip 32768
Rc95LdrBiosAmd64EntryStackTop:
