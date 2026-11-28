#include <asm.inc>

.code64
PUBLIC Rc95LdrBiosCallModuleEntry
Rc95LdrBiosCallModuleEntry:
    /* Windows x64 ABI: RCX=entry, RDX=handoff. */
    mov rax, rcx
    mov rcx, rdx
    test rax, rax
    jz BiosModuleBadEntry
    sub rsp, HEX(28)
    call rax
    add rsp, HEX(28)
    ret
BiosModuleBadEntry:
    mov eax, 1
    ret
END
