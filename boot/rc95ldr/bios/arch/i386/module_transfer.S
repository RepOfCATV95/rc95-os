#include <asm.inc>

.code32
PUBLIC _Rc95LdrBiosCallModuleEntry
_Rc95LdrBiosCallModuleEntry:
    /* cdecl: [esp+4] entry address, [esp+8] handoff pointer. */
    mov eax, dword ptr [esp + 4]
    mov edx, dword ptr [esp + 8]
    test eax, eax
    jz BiosModuleBadEntry
    push edx
    call eax
    add esp, 4
    ret
BiosModuleBadEntry:
    mov eax, 1
    ret
END
