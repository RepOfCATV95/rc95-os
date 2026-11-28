/* GNU as implementation of the AMD64 BIOS module call bridge. */
.intel_syntax noprefix
.code64
.global Rc95LdrBiosCallModuleEntry

Rc95LdrBiosCallModuleEntry:
    mov rax, rcx
    mov rcx, rdx
    test rax, rax
    jz .Lbad_entry
    sub rsp, 40
    call rax
    add rsp, 40
    ret
.Lbad_entry:
    mov eax, 1
    ret

